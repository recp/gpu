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

/* texel reads: where the device takes them (GPU_TEXEL_READS=1 forces them), the shader's read-only arrays reflect
 * the views' formats (GPUBufferBindingLayout.texelView) and plain storage buffers, one at an offset, are read
 * through those views; a layout made without the views runs the same entry with buffer reads; and a group whose
 * ranges a view can't hold (GPU_TEXEL_READS_MAX_ELEMENTS lowers the limit here) runs the plain module, back and
 * forth with one that can in a pass. All read what the buffers hold. A texel attempt that finds nothing (an
 * artifact packed without the atom; GPU_TEXEL_READS_FAIL=1 here) gives the plain library, and the app hears nothing. */

enum {
  READS_COUNT    = 128u,
  READS_BINDINGS = 6u
};

static float
reads_half(uint16_t h) {
  uint32_t e = (h >> 10) & 31u, m = h & 1023u;
  float    v = e ? ldexpf((float)(1024u + m), (int)e - 25) : ldexpf((float)m, -24);

  return (h & 0x8000u) ? -v : v;
}

/* a normal half's bits for a value one exactly */
static uint16_t
reads_half_bits(float v) {
  uint16_t sign = v < 0.0f ? 0x8000u : 0u;
  int      e;
  float    m;

  if (v == 0.0f)
    return sign;

  m = frexpf(fabsf(v), &e); /* [0.5, 1) */

  return (uint16_t)(sign | (uint16_t)((e + 14) << 10) | (uint16_t)((uint32_t)ldexpf(m, 11) & 1023u));
}

/* a test switch GPU-C reads (NULL: gone) */
static void
reads_env(const char *name, const char *value) {
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

static void
reads_count_error(GPUDevice *device, const GPUDeviceErrorInfo *error, void *userData) {
  (void)device;
  ++*(uint32_t *)userData;
  fprintf(stderr, "texel-reads: device error: %s\n", error && error->message ? error->message : "unknown");
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

/* the six bindings: the inputs, and this group's outputs */
static GPUBindGroup *
reads_group(GPUDevice          *device,
            GPUBindGroupLayout *layout,
            GPUBuffer *const   *buffers,
            const uint64_t     *offsets,
            const uint64_t     *sizes) {
  GPUBindGroupCreateInfo info = {0};
  GPUBindGroupEntry      entries[READS_BINDINGS];
  GPUBindGroup          *group = NULL;
  uint32_t               i;

  memset(entries, 0, sizeof(entries));

  for (i = 0u; i < READS_BINDINGS; i++) {
    entries[i].binding       = i;
    entries[i].bindingType   = i == 3u || i == 4u ? GPU_BINDING_STORAGE_BUFFER : GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.offset = offsets[i];
    entries[i].buffer.size   = sizes[i];
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = layout;
  info.pEntries         = entries;
  info.entryCount       = READS_BINDINGS;

  return GPUCreateBindGroup(device, &info, &group) == GPU_OK ? group : NULL;
}

/* one pass: the pipeline, then each group bound and dispatched in turn */
static int
reads_run(GPUDevice *device, GPUQueue *queue, GPUComputePipeline *pipeline, GPUBindGroup *const *groups,
          uint32_t count, const char *what) {
  GPUQueueSubmitInfo     submit = {0};
  GPUCommandBuffer      *commands[1];
  GPUComputePassEncoder *pass;
  GPUCommandBuffer      *cmdb;
  GPUFence              *fence = NULL;
  uint32_t               i;
  int                    ok = 0;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence
      || GPUAcquireCommandBuffer(queue, "texel-reads", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "texel-reads: %s: submission failed\n", what);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "texel-reads");
  GPUBindComputePipeline(pass, pipeline);

  for (i = 0u; i < count; i++) {
    GPUBindComputeGroup(pass, 0u, groups[i], 0u, NULL);
    GPUDispatch(pass, READS_COUNT / 64u, 1u, 1u);
  }

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

  return ok;
}

static int
reads_clear(GPUQueue *queue, GPUBuffer *wout, GPUBuffer *fout) {
  static const uint8_t zeros[READS_COUNT * 16u] = {0};

  return GPUQueueWriteBuffer(queue, wout, 0u, zeros, sizeof(zeros)) == GPU_OK
         && GPUQueueWriteBuffer(queue, fout, 0u, zeros, sizeof(zeros)) == GPU_OK;
}

static int
reads_check(GPUQueue        *queue,
            GPUBuffer       *woutBuffer,
            GPUBuffer       *foutBuffer,
            const uint32_t  *words,
            const uint16_t  *halves,
            const float     *floats,
            const uint16_t  *singles,
            const char      *what) {
  static uint32_t wout[READS_COUNT * 4u];
  static float    fout[READS_COUNT * 4u];
  uint32_t        i;

  if (GPUQueueReadBuffer(queue, woutBuffer, 0u, wout, sizeof(wout)) != GPU_OK
      || GPUQueueReadBuffer(queue, foutBuffer, 0u, fout, sizeof(fout)) != GPU_OK) {
    fprintf(stderr, "texel-reads: %s: readback failed\n", what);
    return 0;
  }

  for (i = 0u; i < READS_COUNT * 4u; i++) {
    /* exact: the halves and the floats share one 22-bit window */
    float want = reads_half(halves[i]) + floats[i / 4u] + reads_half(singles[i / 4u]);

    if (wout[i] != words[i] || fout[i] != want) {
      fprintf(stderr, "texel-reads: %s: %u: %08x %a, %08x %a wanted\n", what, i, wout[i], fout[i], words[i], want);
      return 0;
    }
  }

  return 1;
}

int
gpu_test_texel_reads(GPUDevice *device, const char *bytecodePath) {
  GPUDeviceCapabilities          caps         = {0};
  GPUBindGroupLayoutCreateInfo   groupInfo    = {0};
  GPUPipelineLayoutCreateInfo    layoutInfo   = {0};
  GPUComputePipelineCreateInfo   pipelineInfo = {0};
  GPUBindGroupLayoutEntry        plainEntries[8];
  static const GPUFormat         views[READS_BINDINGS] = {GPU_FORMAT_RGBA32_UINT, GPU_FORMAT_RGBA16_FLOAT,
                                                          GPU_FORMAT_R32_FLOAT, GPU_FORMAT_UNDEFINED,
                                                          GPU_FORMAT_UNDEFINED, GPU_FORMAT_R16_FLOAT};
  static uint32_t                words[READS_COUNT * 4u];
  static uint16_t                halves[READS_COUNT * 4u];
  static float                   floats[READS_COUNT];
  static uint16_t                singles[READS_COUNT];
  uint8_t                        prefix[256];
  const GPUBindGroupLayoutEntry *entries;
  GPUBuffer                     *buffers[READS_BINDINGS] = {0};
  GPUBuffer                     *outputs[2]              = {0}; /* the over-range group's wout, fout */
  GPUBuffer                     *ranged[READS_BINDINGS];
  GPUBindGroup                  *groups[3]               = {0};
  GPUShaderLibrary              *library                 = NULL;
  GPUShaderLayout               *layout                  = NULL;
  GPUBindGroupLayout            *plainGroup              = NULL;
  GPUPipelineLayout             *plain                   = NULL;
  GPUComputePipeline            *pipelines[2]            = {0};
  GPUQueue                      *queue;
  void                          *bytecode = NULL;
  uint64_t                       bytecodeSize;
  uint64_t                       offsets[READS_BINDINGS] = {0};
  uint64_t                       sizes[READS_BINDINGS];
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

  for (i = 0u; i < READS_COUNT; i++) {
    floats[i]  = (float)i * 0.25f - 8.0f;
    singles[i] = reads_half_bits((float)(i % 16u) * 0.5f - 4.0f);
  }

  for (i = 0u; i < sizeof(prefix); i++)
    prefix[i] = 0xa5u;

  sizes[0] = sizeof(words);
  sizes[1] = sizeof(halves);
  sizes[2] = sizeof(floats);
  sizes[3] = sizeof(words);
  sizes[4] = READS_COUNT * 16u;
  sizes[5] = sizeof(singles);

  if (!(bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize)))
    return 0;

  if ((rc = GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library)) != GPU_OK || !library
      || (rc = GPUCreateShaderLayout(device, library, &layout)) != GPU_OK || !layout || layout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "texel-reads: %s failed (%d)\n", library ? "the layout" : "the library", (int)rc);
    goto cleanup;
  }

  /* the views the layout names: all four or none */
  entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0], &entryCount);

  if (!entries || entryCount != READS_BINDINGS || entryCount > sizeof(plainEntries) / sizeof(plainEntries[0])) {
    fprintf(stderr, "texel-reads: %u layout entries, %u wanted\n", entries ? entryCount : 0u, READS_BINDINGS);
    goto cleanup;
  }

  for (i = 0u, viewed = 0u; i < entryCount; i++) {
    uint32_t b = entries[i].binding;

    plainEntries[i] = entries[i];

    if (b >= READS_BINDINGS
        || (views[b] != GPU_FORMAT_UNDEFINED
            && (entries[i].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                || (entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED && entries[i].buffer.texelView != views[b])))
        || (views[b] == GPU_FORMAT_UNDEFINED && entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED)) {
      fprintf(stderr, "texel-reads: binding %u: type %d, view %d\n", b, (int)entries[i].bindingType,
              (int)entries[i].buffer.texelView);
      goto cleanup;
    }

    viewed                           += entries[i].buffer.texelView != GPU_FORMAT_UNDEFINED;
    plainEntries[i].buffer.texelView  = GPU_FORMAT_UNDEFINED;
  }

  if (viewed != 0u && viewed != 4u) {
    fprintf(stderr, "texel-reads: %u of 4 arrays through views\n", viewed);
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

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "texel_reads";
  pipelineInfo.layout           = layout->pipelineLayout;

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipelines[0]) != GPU_OK || !pipelines[0]) {
    fprintf(stderr, "texel-reads: the pipeline failed\n");
    goto cleanup;
  }

  pipelineInfo.layout = plain;

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipelines[1]) != GPU_OK || !pipelines[1]) {
    fprintf(stderr, "texel-reads: the pipeline without views failed\n");
    goto cleanup;
  }

  for (i = 0u; i < READS_BINDINGS; i++) {
    buffers[i] = reads_buffer(device, offsets[i] + sizes[i],
                              GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST
                                | (i == 3u || i == 4u ? GPU_BUFFER_USAGE_COPY_SRC : 0u));
  }

  outputs[0] = reads_buffer(device, sizes[3], GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST
                                                | GPU_BUFFER_USAGE_COPY_SRC);
  outputs[1] = reads_buffer(device, sizes[4], GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST
                                                | GPU_BUFFER_USAGE_COPY_SRC);
  queue      = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);

  if (!buffers[0] || !buffers[1] || !buffers[2] || !buffers[3] || !buffers[4] || !buffers[5] || !outputs[0]
      || !outputs[1] || !queue
      || GPUQueueWriteBuffer(queue, buffers[0], 0u, prefix, offsets[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[0], offsets[0], words, sizes[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[1], 0u, halves, sizes[1]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[2], 0u, floats, sizes[2]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[5], 0u, singles, sizes[5]) != GPU_OK) {
    fprintf(stderr, "texel-reads: buffers failed\n");
    goto cleanup;
  }

  /* the shader's layout, then a layout without views */
  if (!(groups[0] = reads_group(device, layout->bindGroupLayouts[0], buffers, offsets, sizes))
      || !(groups[1] = reads_group(device, plainGroup, buffers, offsets, sizes))) {
    fprintf(stderr, "texel-reads: bind groups failed\n");
    goto cleanup;
  }

  if (!reads_clear(queue, buffers[3], buffers[4])
      || !reads_run(device, queue, pipelines[0], &groups[0], 1u, "the shader's layout")
      || !reads_check(queue, buffers[3], buffers[4], words, halves, floats, singles, "the shader's layout")
      || !reads_clear(queue, buffers[3], buffers[4])
      || !reads_run(device, queue, pipelines[1], &groups[1], 1u, "a layout without views")
      || !reads_check(queue, buffers[3], buffers[4], words, halves, floats, singles, "a layout without views"))
    goto cleanup;

  if (!viewed) {
    ok = 1;
    goto cleanup;
  }

  /* ranges a view can't hold (the limit lowered for this group): the plain module, back and forth in one pass */
  for (i = 0u; i < READS_BINDINGS; i++)
    ranged[i] = buffers[i];

  ranged[3] = outputs[0];
  ranged[4] = outputs[1];
  reads_env("GPU_TEXEL_READS_MAX_ELEMENTS", "64");
  groups[2] = reads_group(device, layout->bindGroupLayouts[0], ranged, offsets, sizes);
  reads_env("GPU_TEXEL_READS_MAX_ELEMENTS", NULL);

  {
    GPUBindGroup *order[3] = {groups[2], groups[0], groups[2]};

    if (!groups[2] || !reads_clear(queue, buffers[3], buffers[4]) || !reads_clear(queue, outputs[0], outputs[1])
        || !reads_run(device, queue, pipelines[0], order, 3u, "ranges past the views")
        || !reads_check(queue, outputs[0], outputs[1], words, halves, floats, singles, "ranges past the views")
        || !reads_check(queue, buffers[3], buffers[4], words, halves, floats, singles, "back through the views"))
      goto cleanup;
  }

  printf("texel-reads: ranges past the views ran on the plain module\n");

  /* a texel attempt that finds nothing: the plain library, and no error reaches the app */
  {
    GPUDeviceErrorCallback savedCallback = device->errorCallback;
    void                  *savedUserData = device->errorUserData;
    GPUShaderReflection    reflection    = {0};
    GPUShaderLibrary      *fallback      = NULL;
    uint32_t               errors        = 0u;
    uint32_t               r;
    int                    plainOk;

    GPUSetDeviceErrorCallback(device, reads_count_error, &errors);
    reads_env("GPU_TEXEL_READS_FAIL", "1");
    rc = GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &fallback);
    reads_env("GPU_TEXEL_READS_FAIL", NULL);
    GPUSetDeviceErrorCallback(device, savedCallback, savedUserData);

    plainOk = rc == GPU_OK && fallback && GPUGetShaderReflection(fallback, &reflection) == GPU_OK;

    for (r = 0u; plainOk && r < reflection.resourceCount; r++)
      plainOk = reflection.pResources[r].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                || reflection.pResources[r].buffer.texelView == GPU_FORMAT_UNDEFINED;

    if (plainOk)
      GPUFreeShaderReflection(&reflection);
    if (fallback)
      GPUDestroyShaderLibrary(fallback);

    if (!plainOk || errors) {
      fprintf(stderr, "texel-reads: a texel attempt that found nothing: %d, %u errors reported\n", (int)rc, errors);
      goto cleanup;
    }
  }

  printf("texel-reads: a texel attempt that found nothing gave the plain library quietly\n");
  ok = 1;

cleanup:
  free(bytecode);
  for (i = 0u; i < 3u; i++)
    if (groups[i])
      GPUDestroyBindGroup(groups[i]);
  for (i = 0u; i < READS_BINDINGS; i++)
    if (buffers[i])
      GPUDestroyBuffer(buffers[i]);
  for (i = 0u; i < 2u; i++) {
    if (outputs[i])
      GPUDestroyBuffer(outputs[i]);
    if (pipelines[i])
      GPUDestroyComputePipeline(pipelines[i]);
  }
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
