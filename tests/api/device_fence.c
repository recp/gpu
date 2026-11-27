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
 * GPU_FEATURE_DEVICE_FENCE: the adapter's answer against the backend's rule
 * (stated here apart from USL's), the device's (on without asking), and
 * device_fence.usl's kernel through GPU-C. The fence artifact (made for
 * Vulkan, so #available(device_fence) took the fence) sums every group's
 * values in one dispatch where the feature is on and is refused elsewhere;
 * there the split artifact (made for WebGPU, the other arm) runs and leaves
 * the partials for a later step.
 */

/* sysctl's headers need Darwin's types, which _POSIX_C_SOURCE hides */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE 1
#endif

#include "test.h"

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#  include <sys/sysctl.h>
#endif

enum {
  GPU_DEVICE_FENCE_GROUPS = 64u,
  GPU_DEVICE_FENCE_VALUES = GPU_DEVICE_FENCE_GROUPS * 64u
};

#define GPU_DEVICE_FENCE_UNTOUCHED 0xffffffffu

/* Metal's fence is MSL 3.2's (macOS 15, iOS 18), WGSL has none, the other
 * targets always have one */
static bool
gpu_deviceFenceExpected(GPUBackend backend) {
#if defined(__APPLE__)
  char     version[64];
  size_t   size;
  unsigned major;
#endif

  switch (backend) {
    case GPU_BACKEND_METAL:
#if defined(__APPLE__)
      memset(version, 0, sizeof(version));
      size = sizeof(version);

      if (sysctlbyname("kern.osproductversion", version, &size, NULL, 0) != 0) {
        return false;
      }

      major = (unsigned)strtoul(version, NULL, 10);
#  if defined(TARGET_OS_VISION) && TARGET_OS_VISION
      return major >= 2u;
#  elif TARGET_OS_IPHONE || TARGET_OS_TV
      return major >= 18u;
#  else
      return major >= 15u;
#  endif
#else
      return false;
#endif
    case GPU_BACKEND_WEBGPU:
      return false;
    default:
      return true;
  }
}

static bool
gpu_deviceFenceListed(const GPUFeatureSet *set) {
  uint32_t i;

  for (i = 0u; i < set->featureCount; i++) {
    if (set->pFeatures[i] == GPU_FEATURE_DEVICE_FENCE) {
      return true;
    }
  }

  return false;
}

static GPUBuffer*
gpu_deviceFenceBuffer(GPUDevice *device, uint64_t size) {
  GPUBufferCreateInfo info   = {0};
  GPUBuffer          *buffer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = size;
  info.usage            = GPU_BUFFER_USAGE_STORAGE
                          | GPU_BUFFER_USAGE_COPY_SRC
                          | GPU_BUFFER_USAGE_COPY_DST;

  return GPUCreateBuffer(device, &info, &buffer) == GPU_OK ? buffer : NULL;
}

/* one dispatch of fence_total; the partials, counters and total read back */
static int
gpu_deviceFenceRun(GPUDevice        *device,
                   GPUShaderLibrary *library,
                   uint32_t         *partials,
                   uint32_t          counters[2],
                   uint32_t          total[4]) {
  GPUComputePipelineCreateInfo pipelineInfo  = {0};
  GPUBindGroupCreateInfo       groupInfo     = {0};
  GPUQueueSubmitInfo           submitInfo    = {0};
  GPUBindGroupEntry            entries[4]    = {0};
  GPUCommandBuffer            *submitList[1] = {0};
  GPUBuffer                   *buffers[4]    = {0};
  uint64_t                     sizes[4];
  uint32_t                    *values        = NULL;
  GPUShaderLayout             *layout        = NULL;
  GPUComputePipeline          *pipeline      = NULL;
  GPUBindGroup                *group         = NULL;
  GPUCommandBuffer            *cmdb          = NULL;
  GPUComputePassEncoder       *pass          = NULL;
  GPUFence                    *fence         = NULL;
  GPUQueue                    *queue;
  uint32_t                     i;
  int                          ok = 0;

  sizes[0] = GPU_DEVICE_FENCE_VALUES * sizeof(uint32_t);
  sizes[1] = GPU_DEVICE_FENCE_VALUES * sizeof(uint32_t);
  sizes[2] = 2u * sizeof(uint32_t);
  sizes[3] = 4u * sizeof(uint32_t);

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))
      || !(values = malloc(sizes[0]))) {
    fprintf(stderr, "device fence setup failed\n");
    goto cleanup;
  }

  for (i = 0u; i < 4u; i++) {
    if (!(buffers[i] = gpu_deviceFenceBuffer(device, sizes[i]))) {
      fprintf(stderr, "device fence buffer %u failed\n", (unsigned)i);
      goto cleanup;
    }
  }

  for (i = 0u; i < GPU_DEVICE_FENCE_VALUES; i++) {
    values[i]   = i;
    partials[i] = 0u;
  }

  counters[0] = 0u;
  counters[1] = GPU_DEVICE_FENCE_GROUPS;

  for (i = 0u; i < 4u; i++) {
    total[i] = GPU_DEVICE_FENCE_UNTOUCHED;
  }

  if (GPUQueueWriteBuffer(queue, buffers[0], 0u, values, sizes[0]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[1], 0u, partials, sizes[1]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[2], 0u, counters, sizes[2]) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffers[3], 0u, total, sizes[3]) != GPU_OK) {
    fprintf(stderr, "device fence upload failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLayout(device, library, &layout) != GPU_OK
      || !layout || !layout->pipelineLayout || layout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "device fence layout failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = layout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "fence_total";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "device fence pipeline failed\n");
    goto cleanup;
  }

  for (i = 0u; i < 4u; i++) {
    entries[i].binding       = i;
    entries[i].bindingType   = i == 0u ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER : GPU_BINDING_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.size   = sizes[i];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.entryCount       = 4u;
  groupInfo.pEntries         = entries;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group
      || GPUAcquireCommandBuffer(queue, "device-fence-test", &cmdb) != GPU_OK || !cmdb
      || !(pass = GPUBeginComputePass(cmdb, "device-fence-test"))) {
    fprintf(stderr, "device fence command setup failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, GPU_DEVICE_FENCE_GROUPS, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "device fence fence create failed\n");
    goto cleanup;
  }

  submitList[0]                 = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.fence              = fence;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK
      || GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "device fence submit failed\n");
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUQueueReadBuffer(queue, buffers[1], 0u, partials, sizes[1]) != GPU_OK
      || GPUQueueReadBuffer(queue, buffers[2], 0u, counters, sizes[2]) != GPU_OK
      || GPUQueueReadBuffer(queue, buffers[3], 0u, total, sizes[3]) != GPU_OK) {
    fprintf(stderr, "device fence readback failed\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(layout);
  for (i = 0u; i < 4u; i++) {
    GPUDestroyBuffer(buffers[i]);
  }
  free(values);
  return ok;
}

/* the artifact at path through GPUCreateShaderLibraryFromUSL */
static GPUResult
gpu_deviceFenceLibrary(GPUDevice *device, const char *path, GPUShaderLibrary **outLibrary) {
  GPUResult result;
  uint64_t  size     = 0u;
  void     *bytecode = NULL;

  *outLibrary = NULL;

  if (!(bytecode = gpu_test_read_file(path, &size))) {
    fprintf(stderr, "device fence artifact load failed: %s\n", path);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUCreateShaderLibraryFromUSL(device, bytecode, size, outLibrary);
  free(bytecode);

  return result;
}

int
gpu_test_device_fence(GPUAdapter *adapter,
                      GPUDevice  *device,
                      const char *fencePath,
                      const char *splitPath) {
  GPUAdapterProperties   properties   = {0};
  GPUDeviceCapabilities  capabilities = {0};
  GPUDeviceCreateInfo    info         = {0};
  GPUFeature             feature      = GPU_FEATURE_DEVICE_FENCE;
  GPUDevice             *asked        = NULL;
  GPUShaderLibrary      *library      = NULL;
  uint32_t              *partials     = NULL;
  uint32_t               counters[2];
  uint32_t               total[4];
  uint32_t               expectedTotal;
  uint32_t               sum;
  uint32_t               i;
  GPUResult              result;
  bool                   expected;
  bool                   supported;
  int                    ok = 0;

  if (GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    fprintf(stderr, "device fence adapter properties failed\n");
    return 0;
  }

  expected  = gpu_deviceFenceExpected(properties.backend);
  supported = GPUIsFeatureSupported(adapter, feature);

  if (getenv("GPU_API_VERBOSE")) {
    printf("api:device-fence:backend=%u supported=%d\n", (unsigned)properties.backend, (int)supported);
  }

  if (supported != expected) {
    fprintf(stderr, "device fence: the adapter says %s, backend %u %s\n", supported ? "yes" : "no",
            (unsigned)properties.backend, expected ? "has one" : "has none");
    return 0;
  }

  /* on without asking: the test's device was made with no create info */
  if (GPUIsFeatureEnabled(device, feature) != supported
      || GPUGetDeviceCapabilities(device, &capabilities) != GPU_OK
      || gpu_deviceFenceListed(&capabilities.enabled) != supported) {
    fprintf(stderr, "device fence: the device's answer differs from the adapter's\n");
    return 0;
  }

  /* asked for: given where supported, refused elsewhere */
  info.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.chain.structSize      = sizeof(info);
  info.required.pFeatures    = &feature;
  info.required.featureCount = 1u;
  result                     = gpu_test_create_device(adapter, &info, &asked);

  if (supported ? result != GPU_OK || !asked || !GPUIsFeatureEnabled(asked, feature)
                : result != GPU_ERROR_UNSUPPORTED || asked) {
    fprintf(stderr, "device fence: asking for it gave %d\n", (int)result);
    goto cleanup;
  }

  GPUDestroyDevice(asked);
  asked = NULL;

  if (!fencePath) {
    ok = 1;
    goto cleanup;
  }

  if (!(partials = malloc(GPU_DEVICE_FENCE_VALUES * sizeof(uint32_t)))) {
    goto cleanup;
  }

  expectedTotal = 0u;

  for (i = 0u; i < GPU_DEVICE_FENCE_VALUES; i++) {
    expectedTotal += i * 3u + i / 64u;
  }

  result = gpu_deviceFenceLibrary(device, fencePath, &library);

  if (supported) {
    if (result != GPU_OK || !library) {
      fprintf(stderr, "device fence kernel failed to compile: %d\n", (int)result);
      goto cleanup;
    }

    if (!gpu_deviceFenceRun(device, library, partials, counters, total)) {
      goto cleanup;
    }

    if (counters[0] != GPU_DEVICE_FENCE_GROUPS || total[0] != expectedTotal
        || total[1] != GPU_DEVICE_FENCE_UNTOUCHED) {
      fprintf(stderr, "device fence: %u groups counted, total %u (expected %u), split mark %08x\n",
              (unsigned)counters[0], (unsigned)total[0], (unsigned)expectedTotal, (unsigned)total[1]);
      goto cleanup;
    }

    ok = 1;
    goto cleanup;
  }

  /* no fence on this target: the kernel holding one is an answer, not a crash */
  if (result != GPU_ERROR_UNSUPPORTED || library) {
    fprintf(stderr, "device fence kernel gave %d where the target has no fence\n", (int)result);
    goto cleanup;
  }

  if (!splitPath) {
    ok = 1;
    goto cleanup;
  }

  if ((result = gpu_deviceFenceLibrary(device, splitPath, &library)) != GPU_OK || !library) {
    fprintf(stderr, "device fence split kernel failed to compile: %d\n", (int)result);
    goto cleanup;
  }

  if (!gpu_deviceFenceRun(device, library, partials, counters, total)) {
    goto cleanup;
  }

  sum = 0u;

  for (i = 0u; i < GPU_DEVICE_FENCE_VALUES; i++) {
    sum += partials[i];
  }

  if (sum != expectedTotal || total[0] != GPU_DEVICE_FENCE_UNTOUCHED
      || total[1] != GPU_DEVICE_FENCE_GROUPS || counters[0] != GPU_DEVICE_FENCE_GROUPS) {
    fprintf(stderr, "device fence split: partials sum %u (expected %u), total %08x, mark %u, counter %u\n",
            (unsigned)sum, (unsigned)expectedTotal, (unsigned)total[0], (unsigned)total[1],
            (unsigned)counters[0]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(asked);
  free(partials);
  return ok;
}
