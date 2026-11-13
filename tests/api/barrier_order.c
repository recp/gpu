/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * barriers order work across command buffers on one queue, on any backend:
 * a slow kernel writes a storage texture, a fast one reads it, ordered by
 *   chain:    the writer's command buffer ends SHADER_WRITE -> TRANSFER_WRITE,
 *             the reader's begins TRANSFER_WRITE -> SHADER_READ (a layer's life)
 *   direct:   the reader's begins SHADER_WRITE -> SHADER_READ
 *   same:     one command buffer, SHADER_WRITE -> SHADER_READ between
 * each submitted without a CPU wait between. any texel the reader saw before
 * the writer wrote it reads 0. 'unordered' (no barrier) is reported, not
 * checked: it shows whether this device would race at all.
 *   barrier_order <write.us> <read.us> [rounds]
 */

#include "test.h"

enum {
  BO_SIZE  = 256u,
  BO_WORK  = 4096u,
  BO_TEXELS = BO_SIZE * BO_SIZE
};

typedef enum BOOrder {
  BO_UNORDERED,
  BO_CHAIN,
  BO_DIRECT,
  BO_SAME
} BOOrder;

static const char *const bo_names[] = {"unordered", "chain", "direct", "same"};

typedef struct BOTest {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUShaderLibrary   *libraries[2];
  GPUShaderLayout    *layouts[2];
  GPUComputePipeline *write, *read;
  GPUBuffer          *work, *out;
} BOTest;

static bool
bo_pipeline(BOTest *t, uint32_t index, const char *path, const char *entry, GPUComputePipeline **out) {
  GPUComputePipelineCreateInfo info = {0};
  void                        *bytecode;
  uint64_t                     size;
  bool                         ok;

  if (!(bytecode = gpu_test_read_file(path, &size)))
    return false;

  ok = GPUCreateShaderLibraryFromUSL(t->device, bytecode, size, &t->libraries[index]) == GPU_OK
       && GPUCreateShaderLayout(t->device, t->libraries[index], &t->layouts[index]) == GPU_OK;
  free(bytecode);

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = ok ? t->layouts[index]->pipelineLayout : NULL;
  info.library          = t->libraries[index];
  info.entryPoint       = entry;

  return ok && GPUCreateComputePipeline(t->device, &info, out) == GPU_OK;
}

static GPUBindGroup*
bo_group(BOTest *t, uint32_t index, GPUBindingType type, GPUTextureView *view, GPUBindingType bufferType,
         GPUBuffer *buffer, uint64_t size) {
  GPUBindGroupCreateInfo info = {0};
  GPUBindGroupEntry      entries[2];
  GPUBindGroup          *group = NULL;

  memset(entries, 0, sizeof(entries));
  entries[0].binding       = 0u;
  entries[0].bindingType   = type;
  entries[0].textureView   = view;
  entries[1].binding       = 1u;
  entries[1].bindingType   = bufferType;
  entries[1].buffer.buffer = buffer;
  entries[1].buffer.size   = size;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = t->layouts[index]->bindGroupLayouts[0];
  info.entryCount       = 2u;
  info.pEntries         = entries;

  return GPUCreateBindGroup(t->device, &info, &group) == GPU_OK ? group : NULL;
}

static void
bo_dispatch(GPUCommandBuffer *cmdb, GPUComputePipeline *pipeline, GPUBindGroup *group) {
  GPUComputePassEncoder *pass;

  if (!(pass = GPUBeginComputePass(cmdb, "barrier-order")))
    return;

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, BO_SIZE / 8u, BO_SIZE / 8u, 1u);
  GPUEndComputePass(pass);
}

static GPUResult
bo_submit(BOTest *t, GPUCommandBuffer *cmdb, GPUFence *fence) {
  GPUQueueSubmitInfo submit = {0};

  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &cmdb;
  submit.fence              = fence;

  return GPUQueueSubmit(t->queue, &submit);
}

/* texels the reader saw before the writer wrote them; UINT32_MAX on failure */

static uint32_t
bo_run(BOTest *t, BOOrder order) {
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTexture          *texture = NULL;
  GPUTextureView      *view    = NULL;
  GPUBindGroup        *writeGroup = NULL, *readGroup = NULL;
  GPUCommandBuffer    *writer = NULL, *reader = NULL;
  GPUFence            *fence = NULL, *written = NULL;
  static uint32_t      texels[BO_TEXELS];
  const char          *step;
  uint32_t             i, missing;
  bool                 ok;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_R32_UINT;
  textureInfo.width            = BO_SIZE;
  textureInfo.height           = BO_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_STORAGE | GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_R32_UINT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  memset(texels, 0, sizeof(texels));

  step = "texture";
  ok   = GPUCreateTexture(t->device, &textureInfo, &texture) == GPU_OK
         && GPUCreateTextureView(texture, &viewInfo, &view) == GPU_OK;
  step = ok ? "groups" : step;
  ok   = ok && (writeGroup = bo_group(t, 0u, GPU_BINDING_STORAGE_TEXTURE, view, GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                                       t->work, 16u))
         && (readGroup = bo_group(t, 1u, GPU_BINDING_SAMPLED_TEXTURE, view, GPU_BINDING_STORAGE_BUFFER, t->out,
                                      sizeof(texels)));
  step = ok ? "command buffer" : step;
  ok   = ok && GPUQueueWriteBuffer(t->queue, t->out, 0u, texels, sizeof(texels)) == GPU_OK
         && GPUCreateFence(t->device, NULL, &fence) == GPU_OK && GPUCreateFence(t->device, NULL, &written) == GPU_OK
         && GPUAcquireCommandBuffer(t->queue, "barrier-order-write", &writer) == GPU_OK && writer;
  step = ok ? "submit" : step;

  if (ok) {
    GPUTransitionTexture(writer, texture, GPU_ACCESS_NONE, GPU_ACCESS_SHADER_WRITE);
    bo_dispatch(writer, t->write, writeGroup);

    if (order == BO_CHAIN)
      GPUTransitionTexture(writer, texture, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_WRITE);

    if (order == BO_SAME) {
      GPUTransitionTexture(writer, texture, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_SHADER_READ);
      bo_dispatch(writer, t->read, readGroup);
      ok = bo_submit(t, writer, fence) == GPU_OK;
    } else {
      ok = bo_submit(t, writer, written) == GPU_OK
           && GPUAcquireCommandBuffer(t->queue, "barrier-order-read", &reader) == GPU_OK && reader;

      if (ok) {
        if (order == BO_CHAIN)
          GPUTransitionTexture(reader, texture, GPU_ACCESS_TRANSFER_WRITE, GPU_ACCESS_SHADER_READ);
        else if (order == BO_DIRECT)
          GPUTransitionTexture(reader, texture, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_SHADER_READ);

        bo_dispatch(reader, t->read, readGroup);
        ok = bo_submit(t, reader, fence) == GPU_OK;
      }
    }
  }

  step = ok ? "wait or read" : step;
  ok   = ok && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
         && GPUQueueReadBuffer(t->queue, t->out, 0u, texels, sizeof(texels)) == GPU_OK;

  if (!ok)
    fprintf(stderr, "barrier order: %s failed at %s\n", bo_names[order], step);

  /* a reader that raced ahead: the writer done before its texture goes */

  if (order != BO_SAME && written)
    (void)GPUWaitFence(written, UINT64_MAX);

  for (i = 0u, missing = 0u; ok && i < BO_TEXELS; i++)
    missing += texels[i] != 1u;

  GPUDestroyFence(written);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(readGroup);
  GPUDestroyBindGroup(writeGroup);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);

  return ok ? missing : UINT32_MAX;
}

int
main(int argc, char **argv) {
  GPUBufferCreateInfo bufferInfo = {0};
  BOTest              t = {0};
  GPUInstance        *instance = NULL;
  GPUAdapter         *adapter  = NULL;
  uint32_t            work[4] = {BO_WORK, 0u, 0u, 0u};
  uint32_t            rounds, round, order, missing, raced[4] = {0};
  bool                ok;

  if (argc < 3) {
    fprintf(stderr, "usage: %s <barrier_order_write.us> <barrier_order_read.us> [rounds]\n", argv[0]);
    return 2;
  }

  rounds = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 8u;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || gpu_test_request_adapter(instance, &adapter) != GPU_OK
      || !adapter || gpu_test_create_device(adapter, NULL, &t.device) != GPU_OK || !t.device) {
    GPUDestroyInstance(instance);
    puts("barrier order: no device");
    return 77;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;

  t.queue = GPUGetQueue(t.device, GPU_QUEUE_GRAPHICS, 0u);
  ok      = t.queue && bo_pipeline(&t, 0u, argv[1], "barrier_order_write_cs", &t.write)
            && bo_pipeline(&t, 1u, argv[2], "barrier_order_read_cs", &t.read);
  bufferInfo.sizeBytes = 16u;
  ok = ok && GPUCreateBuffer(t.device, &bufferInfo, &t.work) == GPU_OK
       && GPUQueueWriteBuffer(t.queue, t.work, 0u, work, sizeof(work)) == GPU_OK;
  bufferInfo.sizeBytes = BO_TEXELS * sizeof(uint32_t);
  ok = ok && GPUCreateBuffer(t.device, &bufferInfo, &t.out) == GPU_OK;

  if (!ok) {
    fprintf(stderr, "barrier order: setup failed\n");
  } else {
    for (round = 0u; ok && round < rounds; round++) {
      for (order = BO_UNORDERED; ok && order <= BO_SAME; order++) {
        if ((missing = bo_run(&t, (BOOrder)order)) == UINT32_MAX) {
          fprintf(stderr, "barrier order: %s: run failed\n", bo_names[order]);
          ok = false;
        } else if (missing) {
          raced[order]++;

          if (order != BO_UNORDERED)
            fprintf(stderr, "barrier order: %s round %u: %u of %u texels read before written\n", bo_names[order],
                    round, missing, BO_TEXELS);
        }
      }
    }

    for (order = BO_CHAIN; order <= BO_SAME; order++)
      ok = ok && raced[order] == 0u;
  }

  printf("barrier order (%u rounds; unordered raced %u, chain %u, direct %u, same %u): %s\n", rounds,
         raced[BO_UNORDERED], raced[BO_CHAIN], raced[BO_DIRECT], raced[BO_SAME], ok ? "ok" : "FAILED");

  GPUDestroyBuffer(t.out);
  GPUDestroyBuffer(t.work);
  GPUDestroyComputePipeline(t.read);
  GPUDestroyComputePipeline(t.write);

  for (round = 0u; round < 2u; round++) {
    GPUDestroyShaderLayout(t.layouts[round]);
    GPUDestroyShaderLibrary(t.libraries[round]);
  }

  GPUDestroyDevice(t.device);
  GPUDestroyInstance(instance);

  return ok ? 0 : 1;
}
