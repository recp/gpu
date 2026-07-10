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

int
main(void) {
  GPUInstanceCreateInfo  instanceInfo  = {0};
  GPURuntimeConfig       runtimeConfig = {0};
  CompletionProbe        probe         = {0};
  GPUFrameStats          stats;
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUCommandQueue       *graphics;
  GPUCommandQueue       *compute;
  GPUFence              *fence;
  uint32_t               adapterCount;
  int                    ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
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

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "vulkan device failed\n");
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

  ok = submit_empty_batch(graphics, fence, &probe) &&
       probe.count == 1u && probe.cmdb != NULL;
  GPUResetStats(device);
  for (uint32_t i = 0; ok && i < VULKAN_WARM_ITERATIONS; i++) {
    ok = submit_empty_batch(graphics, fence, NULL);
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
