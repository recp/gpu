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

#include <math.h>

/* texel reads: where the device takes them (GPU_TEXEL_READS=1 forces them), the shader's read-only arrays reflect
 * the views' formats (GPUBufferBindingLayout.texelView) and plain storage buffers, one at an offset, are read
 * through those views; a layout made without the views runs the same entry with buffer reads. Both read what the
 * buffers hold. */

enum {
  READS_COUNT = 128u
};

static float
reads_half(uint16_t h) {
  uint32_t e = (h >> 10) & 31u, m = h & 1023u;
  float    v = e ? ldexpf((float)(1024u + m), (int)e - 25) : ldexpf((float)m, -24);

  return (h & 0x8000u) ? -v : v;
}

static GPUBuffer *
reads_buffer(GPUDevice *device, uint64_t size, GPUBufferUsageFlags usage) {
  GPUBufferCreateInfo info   = {0};
  GPUBuffer          *buffer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = size;
  info.usage            = usage;

  return GPUCreateBuffer(device, &info, &buffer) == GPU_OK ? buffer : NULL;
}

/* one dispatch over the five bindings with this layout; the outputs cleared first */
static int
reads_dispatch(GPUDevice                *device,
               GPUQueue                 *queue,
               GPUShaderLibrary         *library,
               GPUPipelineLayout        *layout,
               GPUBindGroupLayout       *groupLayout,
               GPUBuffer *const         *buffers,
               const uint64_t           *offsets,
               const uint64_t           *sizes,
               const char               *what) {
  GPUComputePipelineCreateInfo info      = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submit    = {0};
  GPUBindGroupEntry            entries[5];
  GPUCommandBuffer            *commands[1];
  GPUComputePipeline          *pipeline = NULL;
  GPUBindGroup                *group    = NULL;
  GPUFence                    *fence    = NULL;
  GPUComputePassEncoder       *pass;
  GPUCommandBuffer            *cmdb;
  static const uint8_t         zeros[READS_COUNT * 16u] = {0};
  uint32_t                     i;
  int                          ok = 0;

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.library          = library;
  info.layout           = layout;
  info.entryPoint       = "texel_reads";

  memset(entries, 0, sizeof(entries));

  for (i = 0u; i < 5u; i++) {
    entries[i].binding       = i;
    entries[i].bindingType   = i < 3u ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER : GPU_BINDING_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.offset = offsets[i];
    entries[i].buffer.size   = sizes[i];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = groupLayout;
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 5u;

  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline
      || GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group
      || GPUQueueWriteBuffer(queue, buffers[3], 0u, zeros, sizes[3]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[4], 0u, zeros, sizes[4]) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence
      || GPUAcquireCommandBuffer(queue, "texel-reads", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "texel-reads: %s: pipeline, bind group or submission failed\n", what);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "texel-reads");
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, READS_COUNT / 64u, 1u, 1u);
  GPUEndComputePass(pass);

  commands[0]               = cmdb;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = commands;
  submit.fence              = fence;

  if (GPUQueueSubmit(queue, &submit) != GPU_OK || GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "texel-reads: %s: dispatch failed\n", what);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (fence)
    GPUDestroyFence(fence);
  if (group)
    GPUDestroyBindGroup(group);
  if (pipeline)
    GPUDestroyComputePipeline(pipeline);

  return ok;
}

static int
reads_check(GPUQueue        *queue,
            GPUBuffer *const *buffers,
            const uint32_t  *words,
            const uint16_t  *halves,
            const float     *floats,
            const char      *what) {
  static uint32_t wout[READS_COUNT * 4u];
  static float    fout[READS_COUNT * 4u];
  uint32_t        i;

  if (GPUQueueReadBuffer(queue, buffers[3], 0u, wout, sizeof(wout)) != GPU_OK
      || GPUQueueReadBuffer(queue, buffers[4], 0u, fout, sizeof(fout)) != GPU_OK) {
    fprintf(stderr, "texel-reads: %s: readback failed\n", what);
    return 0;
  }

  for (i = 0u; i < READS_COUNT * 4u; i++) {
    /* exact: the halves and the floats share one 22-bit window */
    float want = reads_half(halves[i]) + floats[i / 4u];

    if (wout[i] != words[i] || fout[i] != want) {
      fprintf(stderr, "texel-reads: %s: %u: %08x %a, %08x %a wanted\n", what, i, wout[i], fout[i], words[i], want);
      return 0;
    }
  }

  return 1;
}

int
gpu_test_texel_reads(GPUDevice *device, const char *bytecodePath) {
  GPUDeviceCapabilities          caps        = {0};
  GPUBindGroupLayoutCreateInfo   groupInfo   = {0};
  GPUPipelineLayoutCreateInfo    layoutInfo  = {0};
  GPUBindGroupLayoutEntry        plainEntries[8];
  static const GPUFormat         views[3]    = {GPU_FORMAT_RGBA32_UINT, GPU_FORMAT_RGBA16_FLOAT,
                                                GPU_FORMAT_R32_FLOAT};
  static uint32_t                words[READS_COUNT * 4u];
  static uint16_t                halves[READS_COUNT * 4u];
  static float                   floats[READS_COUNT];
  uint8_t                        prefix[256];
  const GPUBindGroupLayoutEntry *entries;
  GPUBuffer                     *buffers[5] = {0};
  GPUShaderLibrary              *library    = NULL;
  GPUShaderLayout               *layout     = NULL;
  GPUBindGroupLayout            *plainGroup = NULL;
  GPUPipelineLayout             *plain      = NULL;
  GPUQueue                      *queue;
  void                          *bytecode;
  uint64_t                       bytecodeSize;
  uint64_t                       offsets[5] = {0};
  uint64_t                       sizes[5];
  uint32_t                       entryCount;
  uint32_t                       i;
  uint32_t                       viewed;
  GPUResult                      rc;
  int                            ok = 0;

  if (GPUGetDeviceCapabilities(device, &caps) != GPU_OK)
    return 0;

  /* the words start one storage alignment in: views at an offset */
  offsets[0] = caps.limits.minStorageBufferOffsetAlignment > 16u ? caps.limits.minStorageBufferOffsetAlignment : 16u;

  if (offsets[0] > sizeof(prefix)) {
    printf("texel-reads: storage alignment %llu past the test's prefix; skipped\n", (unsigned long long)offsets[0]);
    return 1;
  }

  for (i = 0u; i < READS_COUNT * 4u; i++) {
    words[i]  = (i + 1u) * 0x9e3779b9u ^ (i << 9);
    /* 2^-5..2^5 of both signs, and zeros */
    halves[i] = i % 19u == 0u ? (uint16_t)((i & 1u) << 15)
                              : (uint16_t)(((i & 1u) << 15) | ((10u + i % 11u) << 10) | ((i * 37u) & 1023u));
  }

  for (i = 0u; i < READS_COUNT; i++)
    floats[i] = (float)i * 0.25f - 8.0f;

  for (i = 0u; i < sizeof(prefix); i++)
    prefix[i] = 0xa5u;

  sizes[0] = sizeof(words);
  sizes[1] = sizeof(halves);
  sizes[2] = sizeof(floats);
  sizes[3] = sizeof(words);
  sizes[4] = READS_COUNT * 16u;

  if (!(bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize)))
    return 0;

  if ((rc = GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library)) != GPU_OK || !library
      || (rc = GPUCreateShaderLayout(device, library, &layout)) != GPU_OK || !layout || layout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "texel-reads: %s failed (%d)\n", library ? "the layout" : "the library", (int)rc);
    free(bytecode);
    goto cleanup;
  }

  free(bytecode);

  /* the views the layout names: all three or none */
  entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0], &entryCount);

  if (!entries || entryCount != 5u || entryCount > sizeof(plainEntries) / sizeof(plainEntries[0])) {
    fprintf(stderr, "texel-reads: %u layout entries, 5 wanted\n", entries ? entryCount : 0u);
    goto cleanup;
  }

  for (i = 0u, viewed = 0u; i < entryCount; i++) {
    plainEntries[i] = entries[i];

    if (entries[i].binding < 3u
        && (entries[i].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER
            || (entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED
                && entries[i].buffer.texelView != views[entries[i].binding]))) {
      fprintf(stderr, "texel-reads: binding %u: type %d, view %d\n", entries[i].binding,
              (int)entries[i].bindingType, (int)entries[i].buffer.texelView);
      goto cleanup;
    }

    if (entries[i].binding >= 3u && entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED) {
      fprintf(stderr, "texel-reads: output %u named a view\n", entries[i].binding);
      goto cleanup;
    }

    viewed                            += entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED;
    plainEntries[i].buffer.texelView  = GPU_FORMAT_UNDEFINED;
  }

  if (viewed != 0u && viewed != 3u) {
    fprintf(stderr, "texel-reads: %u of 3 arrays through views\n", viewed);
    goto cleanup;
  }

  printf("texel-reads: %s\n", viewed ? "read through texel views" : "buffer reads (the device takes no texel reads)");

  /* the same group without the views: the entry runs with buffer reads */
  groupInfo.chain.sType           = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  groupInfo.chain.structSize      = sizeof(groupInfo);
  groupInfo.entryCount            = entryCount;
  groupInfo.pEntries              = plainEntries;
  layoutInfo.chain.sType          = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize     = sizeof(layoutInfo);
  layoutInfo.bindGroupLayoutCount = 1u;
  layoutInfo.ppBindGroupLayouts   = &plainGroup;

  if (GPUCreateBindGroupLayout(device, &groupInfo, &plainGroup) != GPU_OK || !plainGroup
      || GPUCreatePipelineLayout(device, &layoutInfo, &plain) != GPU_OK || !plain) {
    fprintf(stderr, "texel-reads: the layout without views failed\n");
    goto cleanup;
  }

  for (i = 0u; i < 5u; i++) {
    buffers[i] = reads_buffer(device, offsets[i] + sizes[i],
                              GPU_BUFFER_USAGE_STORAGE | (i < 3u ? GPU_BUFFER_USAGE_COPY_DST
                                                                 : GPU_BUFFER_USAGE_COPY_DST
                                                                     | GPU_BUFFER_USAGE_COPY_SRC));
  }

  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);

  if (!buffers[0] || !buffers[1] || !buffers[2] || !buffers[3] || !buffers[4] || !queue
      || GPUQueueWriteBuffer(queue, buffers[0], 0u, prefix, offsets[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[0], offsets[0], words, sizes[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[1], 0u, halves, sizes[1]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[2], 0u, floats, sizes[2]) != GPU_OK) {
    fprintf(stderr, "texel-reads: buffers failed\n");
    goto cleanup;
  }

  if (!reads_dispatch(device, queue, library, layout->pipelineLayout, layout->bindGroupLayouts[0], buffers,
                      offsets, sizes, "the shader's layout")
      || !reads_check(queue, buffers, words, halves, floats, "the shader's layout")
      || !reads_dispatch(device, queue, library, plain, plainGroup, buffers, offsets, sizes, "a layout without views")
      || !reads_check(queue, buffers, words, halves, floats, "a layout without views"))
    goto cleanup;

  ok = 1;

cleanup:
  for (i = 0u; i < 5u; i++)
    if (buffers[i])
      GPUDestroyBuffer(buffers[i]);
  if (plain)
    GPUDestroyPipelineLayout(plain);
  if (plainGroup)
    GPUDestroyBindGroupLayout(plainGroup);
  if (layout)
    GPUDestroyShaderLayout(layout);
  if (library)
    GPUDestroyShaderLibrary(library);

  return ok;
}
