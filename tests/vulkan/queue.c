#include <gpu/gpu.h>

#include "api/device_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  VULKAN_COMMAND_BATCH_SIZE = 4u,
  VULKAN_WARM_ITERATIONS    = 256u
};

typedef struct CompletionProbe {
  GPUCommandBuffer *cmdb;
  uint32_t          count;
} CompletionProbe;

static void
on_complete(void *sender, GPUCommandBuffer *cmdb) {
  CompletionProbe *probe;

  probe       = sender;
  probe->cmdb = cmdb;
  probe->count++;
}

static int
submit_empty_batch(GPUCommandQueue *queue,
                   GPUFence        *fence,
                   CompletionProbe *probe) {
  GPUCommandBuffer *buffers[VULKAN_COMMAND_BATCH_SIZE];
  GPUQueueSubmitInfo submitInfo = {0};

  memset(buffers, 0, sizeof(buffers));
  for (uint32_t i = 0; i < VULKAN_COMMAND_BATCH_SIZE; i++) {
    if (GPUAcquireCommandBuffer(queue,
                                "vulkan-empty",
                                &buffers[i]) != GPU_OK ||
        !buffers[i]) {
      return 0;
    }
  }
  if (probe) {
    GPUSetCommandBufferCompletionHandler(
      buffers[VULKAN_COMMAND_BATCH_SIZE - 1u],
      probe,
      on_complete
    );
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = VULKAN_COMMAND_BATCH_SIZE;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
submit_empty_compute(GPUCommandQueue *queue, GPUFence *fence) {
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUQueueSubmitInfo     submitInfo = {0};

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "vulkan-empty-compute", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "vulkan-empty-compute"))) {
    return 0;
  }
  GPUEndComputePass(pass);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
timestamp_roundtrip(GPUDevice       *device,
                    GPUCommandQueue *queue,
                    GPUFence        *fence) {
  GPUQuerySetCreateInfo queryInfo = {0};
  GPUBufferCreateInfo   bufferInfo = {0};
  GPUQueueSubmitInfo    submitInfo = {0};
  GPUCommandBuffer     *cmdb;
  GPUQuerySet          *set;
  GPUBuffer            *buffer;
  uint64_t              timestamps[2] = {UINT64_MAX, UINT64_MAX};
  int                   ok;

  set    = NULL;
  buffer = NULL;
  cmdb   = NULL;
  ok     = 0;

  queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize = sizeof(queryInfo);
  queryInfo.label            = "vulkan-timestamps";
  queryInfo.type             = GPU_QUERY_TIMESTAMP;
  queryInfo.count            = 2u;
  if (GPUCreateQuerySet(device, &queryInfo, &set) != GPU_OK || !set) {
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-timestamp-results";
  bufferInfo.sizeBytes        = sizeof(timestamps);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    goto cleanup;
  }
  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          timestamps,
                          sizeof(timestamps)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUAcquireCommandBuffer(queue,
                              "vulkan-timestamp",
                              &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }

  GPUWriteTimestamp(cmdb, set, 0u);
  GPUWriteTimestamp(cmdb, set, 1u);
  GPUResolveQuerySet(cmdb, set, 0u, 2u, buffer, 0u);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         timestamps,
                         sizeof(timestamps)) != GPU_OK ||
      timestamps[0] == UINT64_MAX || timestamps[1] == UINT64_MAX ||
      timestamps[1] < timestamps[0]) {
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyBuffer(buffer);
  GPUDestroyQuerySet(set);
  return ok;
}

int
main(void) {
  GPUInstanceCreateInfo  instanceInfo  = {0};
  GPUDeviceCreateInfo    deviceInfo    = {0};
  GPURuntimeConfig       runtimeConfig = {0};
  CompletionProbe        probe         = {0};
  GPUFrameStats          stats;
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUCommandQueue       *graphics;
  GPUCommandQueue       *compute;
  GPUFence              *fence;
  GPUFeature             requiredFeatures[4];
  uint32_t               adapterCount;
  int                    ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "vulkan instance failed\n");
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_TIMESTAMPS) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "vulkan feature reporting failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  requiredFeatures[0]                = GPU_FEATURE_COMPUTE;
  requiredFeatures[1]                = GPU_FEATURE_TIMESTAMPS;
  requiredFeatures[2]                = GPU_FEATURE_INDIRECT_DRAW;
  requiredFeatures[3]                = GPU_FEATURE_MULTI_DRAW;
  deviceInfo.chain.sType             = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize        = sizeof(deviceInfo);
  deviceInfo.required.featureCount   = GPU_ARRAY_LEN(requiredFeatures);
  deviceInfo.required.pFeatures      = requiredFeatures;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "vulkan device failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "vulkan enabled feature reporting failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  runtimeConfig.chain.sType         = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize    = sizeof(runtimeConfig);
  runtimeConfig.validationMode      = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  GPUConfigureRuntime(device, &runtimeConfig);

  graphics = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  compute  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!graphics || !compute ||
      !(GPUGetAvailableQueueBits(device) & GPU_QUEUE_GRAPHICS) ||
      !(GPUGetAvailableQueueBits(device) & GPU_QUEUE_COMPUTE)) {
    fprintf(stderr, "vulkan queues failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "vulkan fence failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  ok = timestamp_roundtrip(device, graphics, fence) &&
       submit_empty_batch(graphics, fence, &probe) &&
       submit_empty_compute(compute, fence) &&
       probe.count == 1u && probe.cmdb != NULL;
  GPUResetStats(device);
  for (uint32_t i = 0; ok && i < VULKAN_WARM_ITERATIONS; i++) {
    ok = submit_empty_batch(graphics, fence, NULL) &&
         submit_empty_compute(compute, fence);
  }

  device->lastFrameStats = device->currentFrameStats;
  memset(&stats, 0, sizeof(stats));
  ok = ok && GPUGetLastFrameStats(device, &stats) == GPU_OK &&
       stats.hotPathAllocCount == 0u &&
       stats.hotPathFreeCount == 0u;

  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);

  if (!ok) {
    fprintf(stderr,
            "vulkan warm path failed: %llu allocs, %llu frees\n",
            (unsigned long long)stats.hotPathAllocCount,
            (unsigned long long)stats.hotPathFreeCount);
    return 1;
  }

  puts("Vulkan queue validation passed");
  return 0;
}
