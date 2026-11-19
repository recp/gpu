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

/* texture_buffer<format> through GPU_BINDING_TEXEL_BUFFER: the formats reflected into the layout, views of
 * plain buffers (one at an offset) read whole on the device, and the entries a device refuses */

enum {
  TEXEL_COUNT = 64u
};

static float
texel_half_to_float(uint16_t h) {
  uint32_t e = (h >> 10) & 31u, m = h & 1023u;
  float    v = e ? ldexpf((float)(1024u + m), (int)e - 25) : ldexpf((float)m, -24);

  return (h & 0x8000u) ? -v : v;
}

static GPUBuffer *
texel_buffer(GPUDevice *device, uint64_t size, GPUBufferUsageFlags usage) {
  GPUBufferCreateInfo info   = {0};
  GPUBuffer          *buffer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = size;
  info.usage            = usage;

  return GPUCreateBuffer(device, &info, &buffer) == GPU_OK ? buffer : NULL;
}

/* unorm8: c / 255, within one ulp (WGSL's unpack and some texture units round it so) */
static int
texel_unorm_close(float got, uint32_t c) {
  float want = (float)c / 255.0f;

  return got == want || got == nextafterf(want, 2.0f) || got == nextafterf(want, -2.0f);
}

int
gpu_test_texel_buffer(GPUDevice *device, const char *bytecodePath) {
  GPUDeviceCapabilities             caps       = {0};
  GPUFormatCapabilities             formatCaps = {0};
  GPUShaderReflection               reflection = {0};
  GPUComputePipelineCreateInfo      info       = {0};
  GPUBindGroupCreateInfo            groupInfo  = {0};
  GPUBindGroupEntry                 entries[6];
  GPUBindGroupEntry                 bad;
  GPUQueueSubmitInfo                submit     = {0};
  static const GPUFormat            formats[4] = {GPU_FORMAT_RGBA32_UINT, GPU_FORMAT_RGBA16_FLOAT,
                                                  GPU_FORMAT_R16_FLOAT, GPU_FORMAT_RGBA8_UNORM};
  static const uint64_t             sizes[4]   = {TEXEL_COUNT * 16u, TEXEL_COUNT * 8u, TEXEL_COUNT * 2u,
                                                  TEXEL_COUNT * 4u};
  uint32_t                          words[TEXEL_COUNT * 4u], wout[TEXEL_COUNT * 4u];
  uint16_t                          halves[TEXEL_COUNT * 4u], shorts[TEXEL_COUNT];
  uint8_t                           bytes[TEXEL_COUNT * 4u];
  float                             fout[TEXEL_COUNT * 12u];
  uint8_t                           prefix[256];
  GPUCommandBuffer                 *commands[1];
  GPUBuffer                        *buffers[6] = {0};
  GPUBuffer                        *plain      = NULL;
  GPUShaderLibrary                 *library    = NULL;
  GPUShaderLayout                  *layout     = NULL;
  GPUComputePipeline               *pipeline   = NULL;
  GPUBindGroup                     *group      = NULL;
  GPUBindGroup                     *refused    = NULL;
  GPUFence                         *fence      = NULL;
  GPUComputePassEncoder            *pass;
  GPUCommandBuffer                 *cmdb;
  GPUQueue                         *queue;
  void                             *bytecode;
  uint64_t                          bytecodeSize;
  uint64_t                          offset;
  uint32_t                          i;
  uint32_t                          found;
  GPUResult                         rc;
  int                               ok = 0;

  if (GPUGetDeviceCapabilities(device, &caps) != GPU_OK)
    return 0;

  if (caps.limits.maxTexelBufferElements == 0u) {
    printf("texel-buffer: the device has no texel buffers; skipped\n");
    return 1;
  }

  for (i = 0u; i < 4u; i++) {
    if (GPUGetFormatCapabilities(device->adapter, formats[i], &formatCaps) != GPU_OK || !formatCaps.texelBuffer) {
      printf("texel-buffer: format %u has no texel buffer views here; skipped\n", (unsigned)formats[i]);
      return 1;
    }
  }

  /* the bound words start one alignment in: views at an offset */
  offset = caps.limits.minTexelBufferOffsetAlignment > 16u ? caps.limits.minTexelBufferOffsetAlignment : 16u;

  if (offset > sizeof(prefix))
    offset = sizeof(prefix);

  if (offset % caps.limits.minTexelBufferOffsetAlignment != 0u) {
    printf("texel-buffer: offset alignment %llu past the test's prefix; skipped\n",
           (unsigned long long)caps.limits.minTexelBufferOffsetAlignment);
    return 1;
  }

  for (i = 0u; i < TEXEL_COUNT * 4u; i++) {
    words[i]  = (i + 1u) * 0x9e3779b9u ^ (i << 7);
    /* normal halves of both signs, and zeros */
    halves[i] = i % 17u == 0u ? (uint16_t)((i & 1u) << 15)
                              : (uint16_t)(((i & 1u) << 15) | ((1u + i % 29u) << 10) | ((i * 37u) & 1023u));
    bytes[i]  = (uint8_t)(i * 13u + 7u);
  }

  for (i = 0u; i < TEXEL_COUNT; i++)
    shorts[i] = (uint16_t)(((i & 2u) << 14) | ((2u + i % 27u) << 10) | ((i * 101u) & 1023u));

  for (i = 0u; i < sizeof(prefix); i++)
    prefix[i] = 0xa5u;

  if (!(bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize)))
    return 0;

  if ((rc = GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library)) != GPU_OK || !library) {
    fprintf(stderr, "texel-buffer: the library failed (%d)\n", (int)rc);
    free(bytecode);
    goto cleanup;
  }

  free(bytecode);

  /* the shader's formats reach the reflection */
  if (GPUGetShaderReflection(library, &reflection) != GPU_OK) {
    fprintf(stderr, "texel-buffer: no reflection\n");
    goto cleanup;
  }

  for (i = 0u, found = 0u; i < reflection.resourceCount; i++) {
    const GPUShaderResourceReflection *resource = &reflection.pResources[i];

    if (resource->binding < 4u
        && (resource->bindingType != GPU_BINDING_TEXEL_BUFFER
            || resource->texelBuffer.format != formats[resource->binding])) {
      fprintf(stderr, "texel-buffer: binding %u reflected as type %d, format %d\n", resource->binding,
              (int)resource->bindingType, (int)resource->texelBuffer.format);
      GPUFreeShaderReflection(&reflection);
      goto cleanup;
    }

    found += resource->binding < 4u;
  }

  GPUFreeShaderReflection(&reflection);

  if (found != 4u) {
    fprintf(stderr, "texel-buffer: %u texel bindings reflected, 4 wanted\n", found);
    goto cleanup;
  }

  if ((rc = GPUCreateShaderLayout(device, library, &layout)) != GPU_OK || !layout) {
    fprintf(stderr, "texel-buffer: the layout failed (%d)\n", (int)rc);
    goto cleanup;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library          = library;
  info.layout           = layout->pipelineLayout;
  info.entryPoint       = "texel_read";

  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "texel-buffer: pipeline failed\n");
    goto cleanup;
  }

  for (i = 0u; i < 4u; i++) {
    buffers[i] = texel_buffer(device, (i == 0u ? offset : 0u) + sizes[i],
                              GPU_BUFFER_USAGE_TEXEL | GPU_BUFFER_USAGE_COPY_DST);
  }

  buffers[4] = texel_buffer(device, sizeof(wout), GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC);
  buffers[5] = texel_buffer(device, sizeof(fout), GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC);
  plain      = texel_buffer(device, sizes[0], GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST);
  queue      = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);

  if (!buffers[0] || !buffers[1] || !buffers[2] || !buffers[3] || !buffers[4] || !buffers[5] || !plain || !queue
      || GPUQueueWriteBuffer(queue, buffers[0], 0u, prefix, offset) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[0], offset, words, sizes[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[1], 0u, halves, sizes[1]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[2], 0u, shorts, sizes[2]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[3], 0u, bytes, sizes[3]) != GPU_OK) {
    fprintf(stderr, "texel-buffer: buffers failed\n");
    goto cleanup;
  }

  memset(entries, 0, sizeof(entries));

  for (i = 0u; i < 6u; i++) {
    entries[i].binding       = i;
    entries[i].bindingType   = i < 4u ? GPU_BINDING_TEXEL_BUFFER : GPU_BINDING_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.offset = i == 0u ? offset : 0u;
    entries[i].buffer.size   = i < 4u ? sizes[i] : i == 4u ? sizeof(wout) : sizeof(fout);
  }

  entries[1].texelFormat     = GPU_FORMAT_RGBA16_FLOAT; /* the layout's, named again */
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 6u;

  /* refused: another format than the shader's, a buffer without texel usage, an offset inside a texel */
  bad                   = entries[0];
  bad.texelFormat       = GPU_FORMAT_R32_UINT;
  entries[0]            = bad;
  if (GPUCreateBindGroup(device, &groupInfo, &refused) != GPU_ERROR_INVALID_ARGUMENT || refused) {
    fprintf(stderr, "texel-buffer: a view of another format was accepted\n");
    goto cleanup;
  }

  entries[0].texelFormat   = GPU_FORMAT_UNDEFINED;
  entries[0].buffer.buffer = plain;
  entries[0].buffer.offset = 0u;
  if (GPUCreateBindGroup(device, &groupInfo, &refused) != GPU_ERROR_INVALID_ARGUMENT || refused) {
    fprintf(stderr, "texel-buffer: a buffer without texel usage was accepted\n");
    goto cleanup;
  }

  entries[0].buffer.buffer = buffers[0];
  entries[0].buffer.offset = offset - 8u; /* not a whole rgba32uint texel */
  if (GPUCreateBindGroup(device, &groupInfo, &refused) != GPU_ERROR_INVALID_ARGUMENT || refused) {
    fprintf(stderr, "texel-buffer: an offset inside a texel was accepted\n");
    goto cleanup;
  }

  if (refused) {
    GPUDestroyBindGroup(refused);
    refused = NULL;
  }

  entries[0].buffer.offset = offset;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group
      || GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence
      || GPUAcquireCommandBuffer(queue, "texel-buffer", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "texel-buffer: bind group or submission failed\n");
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "texel-buffer");
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, TEXEL_COUNT / 64u, 1u, 1u);
  GPUEndComputePass(pass);

  commands[0]               = cmdb;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = commands;
  submit.fence              = fence;

  if (GPUQueueSubmit(queue, &submit) != GPU_OK || GPUWaitFence(fence, UINT64_MAX) != GPU_OK
      || GPUQueueReadBuffer(queue, buffers[4], 0u, wout, sizeof(wout)) != GPU_OK
      || GPUQueueReadBuffer(queue, buffers[5], 0u, fout, sizeof(fout)) != GPU_OK) {
    fprintf(stderr, "texel-buffer: dispatch or readback failed\n");
    goto cleanup;
  }

  for (i = 0u; i < TEXEL_COUNT * 4u; i++) {
    if (wout[i] != words[i]) {
      fprintf(stderr, "texel-buffer: rgba32uint word %u: %08x, %08x wanted\n", i, wout[i], words[i]);
      goto cleanup;
    }
  }

  for (i = 0u; i < TEXEL_COUNT; i++) {
    const float *h = &fout[i * 12u], *s = h + 4u, *b = h + 8u;
    uint32_t     c;

    for (c = 0u; c < 4u; c++) {
      if (h[c] != texel_half_to_float(halves[i * 4u + c])) {
        fprintf(stderr, "texel-buffer: rgba16float %u.%u: %a, %a wanted\n", i, c, h[c],
                texel_half_to_float(halves[i * 4u + c]));
        goto cleanup;
      }

      if (!texel_unorm_close(b[c], bytes[i * 4u + c])) {
        fprintf(stderr, "texel-buffer: rgba8unorm %u.%u: %a, %u / 255 wanted\n", i, c, b[c], bytes[i * 4u + c]);
        goto cleanup;
      }
    }

    if (s[0] != texel_half_to_float(shorts[i]) || s[1] != 0.0f || s[2] != 0.0f || s[3] != 1.0f) {
      fprintf(stderr, "texel-buffer: r16float %u: (%a %a %a %a)\n", i, s[0], s[1], s[2], s[3]);
      goto cleanup;
    }
  }

  ok = 1;

cleanup:
  if (refused)
    GPUDestroyBindGroup(refused);
  if (group)
    GPUDestroyBindGroup(group);
  if (fence)
    GPUDestroyFence(fence);
  for (i = 0u; i < 6u; i++)
    if (buffers[i])
      GPUDestroyBuffer(buffers[i]);
  if (plain)
    GPUDestroyBuffer(plain);
  if (pipeline)
    GPUDestroyComputePipeline(pipeline);
  if (layout)
    GPUDestroyShaderLayout(layout);
  if (library)
    GPUDestroyShaderLibrary(library);

  return ok;
}
