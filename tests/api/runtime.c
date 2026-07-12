#include "test.h"
#include "../../src/api/device_internal.h"

static int
check_runtime_config(GPUDevice *device) {
  GPURuntimeConfig config = {0};

  config.chain.sType = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  config.chain.structSize = sizeof(config);
  config.validationMode = GPU_VALIDATION_BASIC;
  config.enableStats = true;

  if (GPUConfigureRuntime(NULL, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "runtime config accepted null device\n");
    return 0;
  }
  if (GPUConfigureRuntime(device, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "runtime config accepted null config\n");
    return 0;
  }

  config.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUConfigureRuntime(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "runtime config accepted wrong sType\n");
    return 0;
  }

  config.chain.sType = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  config.chain.structSize = (uint32_t)(sizeof(config) - 1u);
  if (GPUConfigureRuntime(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "runtime config accepted short structSize\n");
    return 0;
  }

  config.chain.structSize = sizeof(config);
  config.validationMode = (GPUValidationMode)99;
  if (GPUConfigureRuntime(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "runtime config accepted invalid validation mode\n");
    return 0;
  }

  config.validationMode = GPU_VALIDATION_BASIC;
  if (GPUConfigureRuntime(device, &config) != GPU_OK) {
    fprintf(stderr, "runtime config failed\n");
    return 0;
  }

  return 1;
}

static int
check_transient_validation(GPUDevice *device) {
  GPUTransientAllocatorConfig config = {0};
  GPUTransientBufferSlice slice;
  GPUAllocatorStats stats;

  config.chain.sType = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  config.chain.structSize = sizeof(config);
  config.ringBytesPerFrame = 256u;
  config.framesInFlight = 2u;
  config.chunkBytes = 128u;
  config.allowChunkFallback = true;

  if (GPUConfigureTransientAllocator(NULL, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted null device\n");
    return 0;
  }
  if (GPUConfigureTransientAllocator(device, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted null config\n");
    return 0;
  }

  config.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted wrong sType\n");
    return 0;
  }

  config.chain.sType = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  config.chain.structSize = (uint32_t)(sizeof(config) - 1u);
  if (GPUConfigureTransientAllocator(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted short structSize\n");
    return 0;
  }

  config.chain.structSize = sizeof(config);
  config.ringBytesPerFrame = 0u;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted zero ring bytes\n");
    return 0;
  }

  config.ringBytesPerFrame = 256u;
  config.framesInFlight = 0u;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted zero frames in flight\n");
    return 0;
  }

  config.framesInFlight = 2u;
  config.chunkBytes = 0u;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient config accepted fallback without chunk size\n");
    return 0;
  }

  config.chunkBytes = 128u;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_OK) {
    fprintf(stderr, "transient config failed\n");
    return 0;
  }

  if (GPUGetAllocatorStats(device, &stats) != GPU_OK ||
      stats.ringCapacityBytes != 512u ||
      stats.ringUsedBytes != 0u) {
    fprintf(stderr, "transient allocator stats after configure are wrong\n");
    return 0;
  }

  if (GPUAllocateTransientBuffer(NULL,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 64u,
                                 16u,
                                 &slice) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient alloc accepted null device\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 64u,
                                 16u,
                                 NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient alloc accepted null output\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 0u,
                                 64u,
                                 16u,
                                 &slice) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient alloc accepted zero usage\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 0u,
                                 16u,
                                 &slice) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient alloc accepted zero size\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 64u,
                                 3u,
                                 &slice) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "transient alloc accepted non-power-of-two alignment\n");
    return 0;
  }

  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 64u,
                                 16u,
                                 &slice) != GPU_OK ||
      !slice.buffer ||
      !slice.cpuPtr ||
      slice.sizeBytes != 64u ||
      (slice.offset % 16u) != 0u) {
    fprintf(stderr, "transient ring alloc failed\n");
    return 0;
  }

  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_VERTEX,
                                 32u,
                                 64u,
                                 &slice) != GPU_OK ||
      !slice.buffer ||
      !slice.cpuPtr ||
      slice.sizeBytes != 32u ||
      (slice.offset % 64u) != 0u) {
    fprintf(stderr, "transient aligned ring alloc failed\n");
    return 0;
  }

  if (GPUGetAllocatorStats(device, &stats) != GPU_OK ||
      stats.ringUsedBytes != 96u ||
      stats.ringHighWaterBytes != 96u) {
    fprintf(stderr, "transient allocator ring stats are wrong\n");
    return 0;
  }

  return 1;
}

static int
check_transient_fallback(GPUDevice *device) {
  GPUTransientAllocatorConfig config = {0};
  GPUTransientBufferSlice slice;
  GPUAllocatorStats stats;

  config.chain.sType = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  config.chain.structSize = sizeof(config);
  config.ringBytesPerFrame = 64u;
  config.framesInFlight = 1u;
  config.allowChunkFallback = false;

  if (GPUConfigureTransientAllocator(device, &config) != GPU_OK) {
    fprintf(stderr, "transient no-fallback config failed\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 128u,
                                 16u,
                                 &slice) != GPU_ERROR_OUT_OF_MEMORY) {
    fprintf(stderr, "transient no-fallback alloc did not report out-of-memory\n");
    return 0;
  }
  if (GPUGetAllocatorStats(device, &stats) != GPU_OK ||
      stats.uploadStallCount != 1u) {
    fprintf(stderr, "transient no-fallback stall stat is wrong\n");
    return 0;
  }

  config.chunkBytes = 256u;
  config.allowChunkFallback = true;
  if (GPUConfigureTransientAllocator(device, &config) != GPU_OK) {
    fprintf(stderr, "transient fallback config failed\n");
    return 0;
  }
  if (GPUAllocateTransientBuffer(device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 128u,
                                 16u,
                                 &slice) != GPU_OK ||
      !slice.buffer ||
      !slice.cpuPtr ||
      slice.offset != 0u ||
      slice.sizeBytes != 128u) {
    fprintf(stderr, "transient fallback alloc failed\n");
    return 0;
  }
  if (GPUGetAllocatorStats(device, &stats) != GPU_OK ||
      stats.uploadStallCount != 1u) {
    fprintf(stderr, "transient fallback stall stat is wrong\n");
    return 0;
  }

  return 1;
}

static int
check_stats_queries(GPUDevice *device) {
  GPUFrameStats frameStats;
  GPUAllocatorStats allocatorStats;
  GPUCacheStats cacheStats;

  if (GPUGetLastFrameStats(NULL, &frameStats) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetLastFrameStats(device, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "last frame stats accepted invalid arguments\n");
    return 0;
  }
  if (GPUGetAllocatorStats(NULL, &allocatorStats) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetAllocatorStats(device, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "allocator stats accepted invalid arguments\n");
    return 0;
  }

  GPUResetStats(device);
  if (GPUGetCacheStats(device, &cacheStats) != GPU_OK ||
      GPUGetLastFrameStats(device, &frameStats) != GPU_OK ||
      GPUGetAllocatorStats(device, &allocatorStats) != GPU_OK) {
    fprintf(stderr, "stats query after reset failed\n");
    return 0;
  }

  return 1;
}

static int
submit_empty(GPUCommandQueue *queue, GPUFence *fence) {
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, NULL, &cmdb) != GPU_OK || !cmdb) {
    return 0;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

static int
check_warm_command_path(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUFence *fence;
  GPUFrameStats stats;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  fence = NULL;
  if (!queue || GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "warm command path setup failed\n");
    return 0;
  }

  if (!submit_empty(queue, fence)) {
    fprintf(stderr, "warm command path priming submit failed\n");
    GPUDestroyFence(fence);
    return 0;
  }

  GPUResetStats(device);
  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  for (uint32_t i = 0; i < 16u; i++) {
    if (!submit_empty(queue, fence)) {
      fprintf(stderr, "warm command path submit failed\n");
      GPUDestroyFence(fence);
      return 0;
    }
  }
  device->lastFrameStats = device->currentFrameStats;

  if (GPUGetLastFrameStats(device, &stats) != GPU_OK ||
      stats.hotPathAllocCount != 0u ||
      stats.hotPathFreeCount != 0u) {
    fprintf(stderr,
            "warm command path allocated: %llu allocs, %llu frees\n",
            (unsigned long long)stats.hotPathAllocCount,
            (unsigned long long)stats.hotPathFreeCount);
    GPUDestroyFence(fence);
    return 0;
  }

  GPUDestroyFence(fence);
  return 1;
}

int
gpu_test_runtime(GPUDevice *device) {
  return check_runtime_config(device) &&
         check_transient_validation(device) &&
         check_transient_fallback(device) &&
         check_stats_queries(device) &&
         check_warm_command_path(device);
}
