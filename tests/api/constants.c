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

#include "test.h"
#include "../../src/api/device_internal.h"

#include <math.h>

static const GPUConstant overrides[2][4] = {
  {
    {{.boolean = true}, 0u, GPU_CONSTANT_BOOL},
    {{.i32 = -7}, 1u, GPU_CONSTANT_I32},
    {{.u32 = 9u}, 2u, GPU_CONSTANT_U32},
    {{.f32 = 1.5f}, 3u, GPU_CONSTANT_F32}
  },
  {
    {{.boolean = false}, 0u, GPU_CONSTANT_BOOL},
    {{.i32 = 17}, 1u, GPU_CONSTANT_I32},
    {{.u32 = 0u}, 2u, GPU_CONSTANT_U32},
    {{.f32 = -2.0f}, 3u, GPU_CONSTANT_F32}
  }
};

static const float expected[7][4] = {
  {0.0f, -3.0f, 5.0f, 0.25f},
  {1.0f, -7.0f, 9.0f, 1.5f},
  {0.0f, 17.0f, 0.0f, -2.0f},
  {0.0f, -3.0f, 5.0f, 0.25f},
  {0.0f, -3.0f, 5.0f, 0.75f},
  {0.0f, -3.0f, 5.0f, 0.0f},
  {0.0f, -3.0f, 5.0f, -0.0f}
};

static int
check_invalid_constants(GPUDevice *device, const GPUComputePipelineCreateInfo *base) {
  GPUComputePipelineCreateInfo info;
  GPUPipelineConstants        constants = {0};
  GPUPipelineConstants        duplicate;
  GPUMeshPipelineEXT          mesh = {0};
  GPUConstant                 values[4];
  GPUComputePipeline         *pipeline;
  GPUResult                   result;
  uint32_t                    i;

  for (i = 0u; i < 11u; i++) {
    memcpy(values, overrides[0], sizeof(values));
    constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
    constants.chain.structSize = sizeof(constants);
    constants.chain.pNext      = NULL;
    constants.pConstants       = values;
    constants.constantCount    = 4u;
    info                       = *base;
    info.chain.pNext           = &constants;

    switch (i) {
      case 0u: constants.pConstants = NULL; break;
      case 1u: constants.chain.structSize--; break;
      case 2u: values[0].type = (GPUConstantType)0; break;
      case 3u: values[0].type = GPU_CONSTANT_U32; break;
      case 4u: values[0].id = UINT32_MAX; break;
      case 5u: values[1] = values[0]; break;
      case 6u: values[3].value.f32 = NAN; break;
      case 7u: values[3].value.f32 = INFINITY; break;
      case 8u: constants.chain.pNext = &constants; break;
      case 9u:
        mesh.chain.sType      = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT;
        mesh.chain.structSize = sizeof(mesh);
        constants.chain.pNext = &mesh;
        break;
      case 10u:
        duplicate             = constants;
        constants.chain.pNext = &duplicate;
        break;
    }

    pipeline = (GPUComputePipeline *)(uintptr_t)1u;
    result   = GPUCreateComputePipeline(device, &info, &pipeline);

    if (result != GPU_ERROR_INVALID_ARGUMENT || pipeline) {
      fprintf(stderr, "constants invalid case %u: %d\n", i, result);
      return 0;
    }
  }

  return 1;
}

static int
check_render_constants(GPUDevice *device, GPUShaderLibrary *library) {
  GPURenderPipelineCreateInfo  info        = {0};
  GPUPipelineLayoutCreateInfo  layoutInfo  = {0};
  GPUPipelineCacheCreateInfo   cacheInfo   = {0};
  GPUPipelineConstants         constants   = {0};
  GPUColorTargetState          target      = {0};
  GPUTextureCreateInfo         textureInfo = {0};
  GPUTextureViewCreateInfo     viewInfo    = {0};
  GPUBufferCreateInfo          bufferInfo  = {0};
  GPURenderPassColorAttachment color       = {0};
  GPURenderPassCreateInfo      passInfo    = {0};
  GPUTextureBarrier            barrier     = {0};
  GPUBarrierBatch              batch       = {0};
  GPUBufferTextureCopyRegion   region      = {0};
  GPUQueueSubmitInfo           submit      = {0};
  GPUPipelineCompileHandle     handle      = {0};
  uint8_t                      pixels[256] = {0};
  GPUCommandBuffer            *commands[1];
  GPURenderPipeline           *pipeline    = NULL;
  GPUPipelineLayout           *layout      = NULL;
  GPUPipelineCache            *cache       = NULL;
  GPUTexture                  *texture     = NULL;
  GPUTextureView              *view        = NULL;
  GPUBuffer                   *buffer      = NULL;
  GPUFence                    *fence       = NULL;
  GPUConstant                 *values      = NULL;
  GPURenderPassEncoder        *pass;
  GPUTransferPassEncoder      *copy;
  GPUCommandBuffer            *cmdb;
  GPUQueue                    *queue;
  uint64_t                     deadline;
  GPUPipelineCompileStatus     status;
  uint32_t                     i;
  int                          ok          = 0;

  queue                       = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);

  if (!queue || GPUCreatePipelineLayout(device, &layoutInfo, &layout) != GPU_OK
      || GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK) {
    goto cleanup;
  }

  target.format         = GPU_FORMAT_RGBA8_UNORM;
  info.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library          = library;
  info.layout           = layout;
  info.cache            = cache;
  info.vertexEntry      = "constants_vs";
  info.fragmentEntry    = "constants_fs";
  info.colorTargetCount = 1u;
  info.pColorTargets    = &target;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.format              = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(pixels);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;

  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK
      || GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK) {
    goto cleanup;
  }

  color.view                    = view;
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;

  barrier.texture           = texture;
  barrier.srcAccess         = GPU_ACCESS_COLOR_WRITE;
  barrier.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  barrier.mipCount          = 1u;
  barrier.layerCount        = 1u;
  batch.srcStages           = GPU_STAGE_FRAGMENT;
  batch.dstStages           = GPU_STAGE_TRANSFER;
  batch.pTextureBarriers    = &barrier;
  batch.textureBarrierCount = 1u;

  region.bytesPerRow        = sizeof(pixels);
  region.rowsPerImage       = 1u;
  region.texture.width      = 1u;
  region.texture.height     = 1u;
  region.texture.depth      = 1u;
  region.texture.layerCount = 1u;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = commands;
  submit.commandBufferCount = 1u;
  submit.fence              = fence;

  for (i = 0u; i < 2u; i++) {
    if (i == 0u) {
      if (GPUCreateRenderPipeline(device, &info, &pipeline) != GPU_OK) {
        goto cleanup;
      }
    } else {
      if (!(values = calloc(2u, sizeof(*values)))) {
        goto cleanup;
      }

      values[0]                  = (GPUConstant){.value.f32 = 0.75f, .id = 3u, .type = GPU_CONSTANT_F32};
      values[1]                  = (GPUConstant){.value.boolean = true, .id = 0u, .type = GPU_CONSTANT_BOOL};
      constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
      constants.chain.structSize = sizeof(constants);
      constants.pConstants       = values;
      constants.constantCount    = 2u;
      info.chain.pNext           = &constants;

      if (GPUCompileRenderPipelineAsync(device, cache, &info, &handle) != GPU_OK) {
        goto cleanup;
      }

      memset(values, 0, 2u * sizeof(*values));
      free(values);
      values               = NULL;
      constants.pConstants = NULL;
      deadline             = gpu_test_now_ns() + UINT64_C(10000000000);

      do {
        if (GPUPollRenderPipelineCompile(device, handle, &status, &pipeline) != GPU_OK) {
          goto cleanup;
        }
      } while (status == GPU_PIPELINE_COMPILE_PENDING && gpu_test_now_ns() < deadline);

      if (status != GPU_PIPELINE_COMPILE_READY || !pipeline) {
        goto cleanup;
      }

      handle.id = 0u;
    }

    if (GPUAcquireCommandBuffer(queue, "constants-render", &cmdb) != GPU_OK
        || !(pass = GPUBeginRenderPass(cmdb, &passInfo))) {
      goto cleanup;
    }

    GPUBindRenderPipeline(pass, pipeline);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
    GPUEndRenderPass(pass);
    GPUEncodeBarriers(cmdb, &batch);

    if (!(copy = GPUBeginTransferPass(cmdb, "constants-readback"))) {
      goto cleanup;
    }

    GPUCopyTextureToBuffer(copy, texture, buffer, &region);
    GPUEndTransferPass(copy);
    commands[0] = cmdb;

    if (GPUQueueSubmit(queue, &submit) != GPU_OK
        || GPUWaitFence(fence, UINT64_MAX) != GPU_OK
        || GPUQueueReadBuffer(queue, buffer, 0u, pixels, sizeof(pixels)) != GPU_OK
        || abs((int)pixels[0] - (i ? 191 : 64)) > 1
        || pixels[1] != (i ? 255u : 0u) || pixels[2] != 0u || pixels[3] != 255u) {
      fprintf(stderr, "constants render %u readback failed: %u %u %u %u\n",
              i, pixels[0], pixels[1], pixels[2], pixels[3]);
      goto cleanup;
    }

    GPUDestroyRenderPipeline(pipeline);
    pipeline = NULL;
  }

  ok = 1;

cleanup:
  free(values);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyPipelineCache(cache);
  GPUDestroyPipelineLayout(layout);

  return ok;
}

static int
check_constants_library(GPUDevice *device, GPUShaderLibrary *library) {
  GPUComputePipelineCreateInfo info       = {0};
  GPUPipelineCacheCreateInfo   cacheInfo  = {0};
  GPUPipelineConstants         constants  = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupCreateInfo       groupInfo  = {0};
  GPUBindGroupEntry            binding    = {0};
  GPUQueueSubmitInfo           submit     = {0};
  GPUBufferBarrier             barrier    = {0};
  GPUBarrierBatch              batch      = {0};
  GPUConstant                  reversed[4];
  GPUConstant                  unused     = {.value.f32 = 0.0f, .id = 4u, .type = GPU_CONSTANT_F32};
  GPUConstant                  partial    = {.value.f32 = 0.75f, .id = 3u, .type = GPU_CONSTANT_F32};
  GPUCommandBuffer            *commands[1];
  float                        output[4];
  GPUComputePipeline          *pipeline   = NULL;
  GPUComputePipeline          *same       = NULL;
  GPUComputePipeline          *previous   = NULL;
  GPUShaderLayout             *layout     = NULL;
  GPUPipelineCache            *cache      = NULL;
  GPUComputePassEncoder       *pass;
  GPUCommandBuffer            *cmdb;
  GPUBindGroup                *group      = NULL;
  GPUBuffer                   *buffer     = NULL;
  GPUFence                    *fence      = NULL;
  GPUQueue                    *queue;
  uint64_t                     createdAt;
  uint64_t                     creationTime;
  uint64_t                     reuseTime;
  uint32_t                     i;
  uint32_t                     j;
  int                          ok         = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);

  if (!queue || GPUCreateShaderLayout(device, library, &layout) != GPU_OK) {
    goto cleanup;
  }

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);

  if (GPUCreatePipelineCache(device, &cacheInfo, &cache) != GPU_OK) {
    goto cleanup;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library          = library;
  info.layout           = layout->pipelineLayout;
  info.cache            = cache;
  info.entryPoint       = "constants_cs";

  if (!check_invalid_constants(device, &info)) {
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(output);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK) {
    goto cleanup;
  }

  binding.bindingType        = GPU_BINDING_STORAGE_BUFFER;
  binding.buffer.buffer      = buffer;
  binding.buffer.size        = sizeof(output);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.pEntries         = &binding;
  groupInfo.entryCount       = 1u;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK) {
    goto cleanup;
  }

  barrier.buffer           = buffer;
  barrier.srcAccess        = GPU_ACCESS_SHADER_WRITE;
  barrier.dstAccess        = GPU_ACCESS_TRANSFER_READ;
  barrier.sizeBytes        = sizeof(output);
  batch.srcStages          = GPU_STAGE_COMPUTE;
  batch.dstStages          = GPU_STAGE_TRANSFER;
  batch.pBufferBarriers    = &barrier;
  batch.bufferBarrierCount = 1u;

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.constantCount    = 4u;
  submit.chain.sType         = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize    = sizeof(submit);
  submit.ppCommandBuffers    = commands;
  submit.commandBufferCount  = 1u;
  submit.fence               = fence;

  for (i = 0u; i < 7u; i++) {
    info.chain.pNext        = i ? &constants : NULL;
    constants.constantCount = i > 2u ? 1u : 4u;

    if (i == 3u) {
      constants.pConstants = &unused;
    } else if (i > 3u) {
      constants.pConstants = &partial;
    } else {
      constants.pConstants = i ? overrides[i - 1u] : NULL;
    }

    if (i >= 5u) {
      partial.value.f32 = i == 5u ? 0.0f : -0.0f;
    }

    createdAt = gpu_test_now_ns();

    if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || pipeline == previous) {
      fprintf(stderr, "constants pipeline variant %u failed\n", i);
      goto cleanup;
    }

    creationTime = gpu_test_now_ns() - createdAt;
    reuseTime    = 0u;

    if (i == 0u) {
      constants.constantCount = 0u;
      constants.pConstants    = (const GPUConstant *)(uintptr_t)1u;
      info.chain.pNext        = &constants;

      if (GPUCreateComputePipeline(device, &info, &same) != GPU_OK || same != pipeline) {
        fprintf(stderr, "constants empty list cache key failed\n");
        goto cleanup;
      }

      GPUDestroyComputePipeline(same);
      same = NULL;
    }

    if (i == 1u || i == 2u) {
      for (j = 0u; j < 4u; j++) {
        reversed[j] = constants.pConstants[3u - j];
      }

      constants.pConstants = reversed;

      createdAt = gpu_test_now_ns();

      if (GPUCreateComputePipeline(device, &info, &same) != GPU_OK || same != pipeline) {
        fprintf(stderr, "constants reordered cache key failed\n");
        goto cleanup;
      }

      reuseTime = gpu_test_now_ns() - createdAt;
      GPUDestroyComputePipeline(same);
      same = NULL;
    }

    if (GPUAcquireCommandBuffer(queue, "constants", &cmdb) != GPU_OK
        || !(pass = GPUBeginComputePass(cmdb, "constants"))) {
      goto cleanup;
    }

    GPUBindComputePipeline(pass, pipeline);
    GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
    GPUDispatch(pass, 1u, 1u, 1u);
    GPUEndComputePass(pass);
    GPUEncodeBarriers(cmdb, &batch);
    commands[0] = cmdb;

    if (GPUQueueSubmit(queue, &submit) != GPU_OK
        || GPUWaitFence(fence, UINT64_MAX) != GPU_OK
        || GPUQueueReadBuffer(queue, buffer, 0u, output, sizeof(output)) != GPU_OK) {
      goto cleanup;
    }

    /* WGSL 15.7.2 permits ignoring the sign of zero; cache keys still differ. */

    for (j = 0u; j < 4u; j++) {
      if (!isfinite(output[j]) || output[j] != expected[i][j]
          || (i >= 5u && j == 3u && device->_api->backend != GPU_BACKEND_WEBGPU
              && !!signbit(output[j]) != (i == 6u))) {
        fprintf(stderr, "constants variant %u component %u: %g != %g\n",
                i, j, output[j], expected[i][j]);
        goto cleanup;
      }
    }

    if (getenv("GPU_API_TIMINGS")) {
      printf("constants: variant %u creation %.3f ms, reordered reuse %.3f ms\n",
             i, (double)creationTime / 1000000.0, (double)reuseTime / 1000000.0);
    }

    previous = pipeline;
    GPUDestroyComputePipeline(pipeline);
    pipeline = NULL;
  }

  if (!check_render_constants(device, library)) {
    fprintf(stderr, "constants render/async failed\n");
    goto cleanup;
  }

  printf("constants: native defaults/variants, cache order, validation and render async lifetime passed\n");
  ok = 1;

cleanup:
  GPUDestroyComputePipeline(same);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyBindGroup(group);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyPipelineCache(cache);
  GPUDestroyShaderLayout(layout);

  return ok;
}

int
gpu_test_constants(GPUDevice *device, const char *bytecodePath) {
  GPUShaderLibraryCreateInfo info       = {0};
  GPUAdapterProperties       properties = {0};
  GPUShaderLibrary          *library    = NULL;
  void                      *data;
  uint64_t                   size;
  GPUResult                  result;
  int                        ok;

  if (GPUGetAdapterProperties(device->adapter, &properties) != GPU_OK) {
    return 0;
  }

  if (properties.backend != GPU_BACKEND_METAL && properties.backend != GPU_BACKEND_VULKAN
      && properties.backend != GPU_BACKEND_WEBGPU) {
    puts("constants: caller overrides unavailable on this backend");
    return 1;
  }

  data = gpu_test_read_file(bytecodePath, &size);

  if (!data) {
    return 0;
  }

  ok = GPUCreateShaderLibraryFromUSL(device, data, size, &library) == GPU_OK
       && check_constants_library(device, library);
  GPUDestroyShaderLibrary(library);
  library = NULL;

  if (ok) {
    info.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
    info.sourceData       = data;
    info.sourceSize       = size;
    ok                    = GPUCreateShaderLibrary(device, &info, &library) == GPU_OK
         && check_constants_library(device, library);
    GPUDestroyShaderLibrary(library);
    library = NULL;
  }

  /* the reserved source-text mode must not acquire new parsing semantics. */

  info.sourceKind = GPU_SHADER_SOURCE_USL_TEXT;
  result          = GPUCreateShaderLibrary(device, &info, &library);

  if (result != GPU_ERROR_INVALID_ARGUMENT || library) {
    ok = 0;
  }

  GPUDestroyShaderLibrary(library);
  free(data);

  return ok;
}
