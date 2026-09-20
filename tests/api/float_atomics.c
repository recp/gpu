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
#include "../../src/backend/api/gpudef.h"

#include <us/compiler.h>

static uint32_t captures;

static GPUShaderLibrary *
capture_binary(GPUDevice *device, const void *data, uint64_t size) {
  const uint32_t *words = data;
  size_t         count = (size_t)(size / sizeof(*words));

  (void)device;
  if (count < 5u || size % sizeof(*words) || words[0] != 0x07230203u) return NULL;

  for (size_t i = 5u; i < count;) {
    uint32_t length = words[i] >> 16u;

    if (!length || length > count - i) return NULL;
    if ((words[i] & 0xffffu) == 6035u) captures++; // OpAtomicFAddEXT
    i += length;
  }
  return NULL;
}

static int
check_masks(const GPUShaderLibraryCreateInfo *info, uint32_t domain) {
  GPUApi    api    = {0};
  GPUDevice device = {0};

  api.backend                     = GPU_BACKEND_VULKAN;
  api.library.newLibraryWithBinary = capture_binary;
  device._api                     = &api;
  device.uslTargetProfile          = USL_TARGET_PROFILE_VULKAN_1_3;

  // Repeat in one process to exercise capability-sensitive payload caching.
  for (uint32_t round = 0u; round < 2u; round++) {
    for (uint32_t mask = 0u; mask < 4u; mask++) {
      GPUShaderLibrary *library = NULL;
      GPUResult         result;

      device.uslFloatAtomicAdd = (uint8_t)mask;
      captures                = 0u;
      result = GPUCreateShaderLibrary(&device, info, &library);
      if (result == GPU_OK || library ||
          captures != ((mask & domain) ? (domain == 1u ? 2u : 1u) : 0u)) {
        fprintf(stderr, "float atomic mask %u domain %u: result=%d captures=%u\n",
                mask, domain, result, captures);
        return 0;
      }
    }
  }
  return 1;
}

static GPUShaderLibrary *
capture_source(GPUDevice *device, const char *source, uint64_t size, uint32_t flags) {
  const char *cursor = source;

  (void)device;
  (void)flags;
  if (!size) return NULL;

  while ((cursor = strstr(cursor, "atomic_fetch_add_explicit("))) {
    captures++;
    cursor++;
  }
  return NULL;
}

static int
check_metal_masks(const GPUShaderLibraryCreateInfo *info, uint32_t domain) {
  GPUApi    api    = {0};
  GPUDevice device = {0};

  api.backend                     = GPU_BACKEND_METAL;
  api.library.newLibraryWithSource = capture_source;
  device._api                     = &api;

  for (uint32_t round = 0u; round < 2u; round++) {
    for (uint32_t mask = 0u; mask < 4u; mask++) {
      GPUShaderLibrary *library = NULL;
      GPUResult         result;

      device.uslTargetVersion  = 401u;
      device.uslFloatAtomicAdd = (uint8_t)mask;
      captures                = 0u;
      result = GPUCreateShaderLibrary(&device, info, &library);
      if (result == GPU_OK || library ||
          captures != ((mask & domain) ? (domain == 1u ? 2u : 1u) : 0u)) return 0;
    }
  }
  if (domain == 2u) {
    GPUShaderLibrary *library = NULL;

    // An enabled device bit cannot substitute for the MSL 4.1 language gate.
    device.uslTargetVersion = 0u;
    captures               = 0u;
    if (GPUCreateShaderLibrary(&device, info, &library) == GPU_OK || library || captures) return 0;
  }
  return 1;
}

static int
dispatch(GPUDevice *device, GPUShaderLibrary *library, uint32_t domain) {
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo   = {0};
  GPUBindGroupCreateInfo       groupInfo    = {0};
  GPUQueueSubmitInfo           submitInfo   = {0};
  GPUBindGroupEntry            entries[2]   = {0};
  GPUShaderLayout            *layout       = NULL;
  GPUComputePipeline         *pipeline     = NULL;
  GPUBindGroup               *group        = NULL;
  GPUFence                   *fence        = NULL;
  GPUCommandBuffer           *command      = NULL;
  GPUComputePassEncoder      *pass         = NULL;
  GPUBuffer                  *buffers[2]   = {NULL, NULL};
  GPUQueue                   *queue;
  float                       values[2][2048];
  bool                        seen[1024]   = {0};
  int                         ok           = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!queue || GPUCreateShaderLayout(device, library, &layout) != GPU_OK ||
      !layout || layout->bindGroupLayoutCount != 1u) goto cleanup;

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = layout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "float_atomic";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK) goto cleanup;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(values[0]);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC |
                               GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t b = 0u; b < 2u; b++) {
    for (uint32_t i = 0u; i < 2048u; i++) values[b][i] = -12345.0f;
    if (b == 0u && domain == 1u) values[b][64] = values[b][65] = 0.0f;

    if (GPUCreateBuffer(device, &bufferInfo, &buffers[b]) != GPU_OK ||
        GPUQueueWriteBuffer(queue, buffers[b], 0u, values[b], sizeof(values[b])) != GPU_OK) goto cleanup;

    entries[b].binding       = b;
    entries[b].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[b].buffer.buffer = buffers[b];
    entries[b].buffer.offset = 256u;
    entries[b].buffer.size   = 4096u;
  }
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 2u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK ||
      GPUCreateFence(device, NULL, &fence) != GPU_OK ||
      GPUAcquireCommandBuffer(queue, "float-atomics", &command) != GPU_OK ||
      !(pass = GPUBeginComputePass(command, "float-atomics"))) goto cleanup;

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 16u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &command;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) goto cleanup;

  for (uint32_t b = 0u; b < 2u; b++) {
    uint32_t written = b == 1u ? 1024u : domain == 1u ? 2u : 16u;

    if (GPUQueueReadBuffer(queue, buffers[b], 0u, values[b], sizeof(values[b])) != GPU_OK) goto cleanup;
    for (uint32_t i = 0u; i < 2048u; i++) {
      if ((i < 64u || i >= 64u + written) && values[b][i] != -12345.0f) goto cleanup;
    }
  }
  for (uint32_t i = 0u; i < (domain == 1u ? 2u : 16u); i++) {
    float expected = domain == 1u ? (i == 0u ? 1024.0f : 512.0f) : 64.0f;

    if (values[0][64u + i] != expected) goto cleanup;
  }
  for (uint32_t i = 0u; i < 1024u; i++) {
    float    value = values[1][64u + i];
    uint32_t limit = domain == 1u ? 1024u : 64u;
    uint32_t index;

    if (!(value >= 0.0f && value < (float)limit)) goto cleanup;
    index = (uint32_t)value;
    if (value != (float)index) goto cleanup;
    if (domain == 2u) index += (i / 64u) * 64u;
    if (seen[index]) goto cleanup;
    seen[index] = true;
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  for (uint32_t i = 0u; i < 2u; i++) GPUDestroyBuffer(buffers[i]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(layout);
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo      instanceInfo = {0};
  GPUDeviceCreateInfo        deviceInfo   = {0};
  GPUShaderLibraryCreateInfo libraryInfo  = {0};
  GPUInstance              *instance     = NULL;
  GPUAdapter               *adapter      = NULL;
  GPUDevice                *device       = NULL;
  GPUShaderLibrary         *library      = NULL;
  void                     *artifact;
  uint64_t                  size;
  uint32_t                  domain;
  int                       result = 1;

  if (argc != 4) return 1;
  artifact = gpu_test_read_file(argv[2], &size);
  if (!artifact) return 1;
  domain = strcmp(argv[3], "buffer") == 0 ? 1u : 2u;
  libraryInfo.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  libraryInfo.chain.structSize = sizeof(libraryInfo);
  libraryInfo.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
  libraryInfo.sourceData       = artifact;
  libraryInfo.sourceSize       = size;
  libraryInfo.disableDiskCache = true;
  if (strcmp(argv[1], "capture") == 0) {
    result = check_masks(&libraryInfo, domain) && check_metal_masks(&libraryInfo, domain) ? 0 : 1;
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = strcmp(argv[1], "metal") == 0 ? GPU_BACKEND_METAL : GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  deviceInfo.chain.sType        = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize   = sizeof(deviceInfo);
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK ||
      gpu_test_request_adapter(instance, &adapter) != GPU_OK ||
      gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK) goto cleanup;

  printf("enabled float atomic mask=%u, target version=%u\n",
         device->uslFloatAtomicAdd, device->uslTargetVersion);
  if (!(device->uslFloatAtomicAdd & domain)) {
    result = GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK && !library ? 77 : 1;
    goto cleanup;
  }
  if (GPUCreateShaderLibrary(device, &libraryInfo, &library) != GPU_OK ||
      !library || !dispatch(device, library, domain)) goto cleanup;

  puts("float atomic readback passed: 1024 tickets, 16384 guarded bytes");
  result = 0;

cleanup:
  if (result == 1) fprintf(stderr, "float atomic %s %s failed\n", argv[1], argv[3]);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  return result;
}
