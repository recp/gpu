/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_device_internal_h
#define gpu_device_internal_h

#include "adapter_internal.h"
#include "../common.h"

typedef struct GPUTransientChunk {
  GPUBuffer                *buffer;
  void                     *cpuPtr;
  struct GPUTransientChunk *next;
  uint64_t                  sizeBytes;
  uint64_t                  offset;
  GPUBufferUsageFlags       usage;
  uint32_t                  frameIndex;
} GPUTransientChunk;

typedef struct GPUPipelineCache GPUPipelineCache;

struct GPUDevice {
  GPUInstance                 *inst;
  GPUAdapter                  *adapter;
  GPUApi                      *_api;
  void                        *_priv;
  GPUBuffer                   *transientBuffer;
  GPUTransientChunk           *transientChunks;
  GPUFence                   **transientFrameFences;
  GPUPipelineCache            *_pipelineCaches;
  void                        *_pipelineCacheLock;
  void                        *_bindGroupCache;
  void                        *transientCpuPtr;
  GPUDeviceErrorCallback       errorCallback;
  void                        *errorUserData;
  GPUFeatureSet                enabledFeatures;
  GPUCacheStats                cacheStats;
  GPURuntimeConfig             runtimeConfig;
  GPUFrameStats                currentFrameStats;
  GPUFrameStats                lastFrameStats;
  GPUAllocatorStats            allocatorStats;
  GPUTransientAllocatorConfig transientConfig;
  uint64_t                     enabledFeatureMask;
  uint64_t                     transientFrameOffset;
  uint64_t                     _nextPipelineCompileId;
  uint64_t                     _completedGPUFrameTimeBits;
  GPUQueueFlagBits             queueFamilies;
  GPUBufferUsageFlags          transientBufferUsage;
  uint32_t                     transientFrameIndex;
  uint32_t                     deviceLostReported;
  bool                         transientConfigured;
  bool                         transientFrameBegun;
  bool                         uslUntypedPointers;
  GPUFeature                   enabledFeatureStorage[
    GPU_FEATURE_SAMPLER_FEEDBACK + 1u
  ];
};

static inline void
gpuDeviceCacheCounterAdd(uint64_t *counter, uint64_t value) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchangeAdd64((volatile LONG64 *)counter, (LONG64)value);
#else
  __atomic_fetch_add(counter, value, __ATOMIC_RELAXED);
#endif
}

static inline uint64_t
gpuDeviceCacheCounterLoad(const uint64_t *counter) {
#if defined(_WIN32) || defined(WIN32)
  return (uint64_t)InterlockedCompareExchange64(
    (volatile LONG64 *)counter,
    0,
    0
  );
#else
  return __atomic_load_n(counter, __ATOMIC_RELAXED);
#endif
}

static inline void
gpuDeviceCacheCounterReset(uint64_t *counter) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange64((volatile LONG64 *)counter, 0);
#else
  __atomic_store_n(counter, 0u, __ATOMIC_RELAXED);
#endif
}

static inline void
gpuDeviceGetCacheStats(const GPUDevice *device, GPUCacheStats *stats) {
  stats->bindGroupHits =
    gpuDeviceCacheCounterLoad(&device->cacheStats.bindGroupHits);
  stats->bindGroupMisses =
    gpuDeviceCacheCounterLoad(&device->cacheStats.bindGroupMisses);
  stats->bindGroupCollisions =
    gpuDeviceCacheCounterLoad(&device->cacheStats.bindGroupCollisions);
  stats->pipelineHits =
    gpuDeviceCacheCounterLoad(&device->cacheStats.pipelineHits);
  stats->pipelineMisses =
    gpuDeviceCacheCounterLoad(&device->cacheStats.pipelineMisses);
  stats->pipelineCompiles =
    gpuDeviceCacheCounterLoad(&device->cacheStats.pipelineCompiles);
}

static inline void
gpuDeviceResetCacheStats(GPUDevice *device) {
  gpuDeviceCacheCounterReset(&device->cacheStats.bindGroupHits);
  gpuDeviceCacheCounterReset(&device->cacheStats.bindGroupMisses);
  gpuDeviceCacheCounterReset(&device->cacheStats.bindGroupCollisions);
  gpuDeviceCacheCounterReset(&device->cacheStats.pipelineHits);
  gpuDeviceCacheCounterReset(&device->cacheStats.pipelineMisses);
  gpuDeviceCacheCounterReset(&device->cacheStats.pipelineCompiles);
}

static inline GPUResult
gpuDevicePrepareFrameSlot(GPUDevice *device, uint32_t *outFrameIndex) {
  GPUFence *fence;
  GPUResult result;
  uint32_t  nextFrameIndex;

  if (!device || !outFrameIndex) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!device->transientConfigured) {
    *outFrameIndex = device->transientFrameIndex;
    return GPU_OK;
  }

  nextFrameIndex = device->transientFrameIndex + 1u;
  if (nextFrameIndex >= device->transientConfig.framesInFlight) {
    nextFrameIndex = 0u;
  }

  fence = device->transientFrameFences
            ? device->transientFrameFences[nextFrameIndex]
            : NULL;
  if (fence && !GPUIsFenceSignaled(fence)) {
    device->allocatorStats.uploadStallCount++;
    result = GPUWaitFence(fence, UINT64_MAX);
    if (result != GPU_OK) {
      return result;
    }
  }

  *outFrameIndex = nextFrameIndex;
  return GPU_OK;
}

static inline void
gpuDeviceActivateFrameSlot(GPUDevice *device, uint32_t frameIndex) {
  GPUTransientChunk *chunk;

  if (!device || !device->transientConfigured) {
    return;
  }

  if (device->transientFrameBegun &&
      frameIndex <= device->transientFrameIndex) {
    device->allocatorStats.ringWrapCount++;
  }

  device->transientFrameIndex          = frameIndex;
  device->transientFrameOffset         = 0u;
  device->transientFrameBegun          = true;
  device->allocatorStats.ringUsedBytes = 0u;

  for (chunk = device->transientChunks; chunk; chunk = chunk->next) {
    if (chunk->frameIndex == frameIndex) {
      chunk->offset = 0u;
    }
  }
}

static inline GPUResult
gpuDeviceAdvanceFrameSlot(GPUDevice *device) {
  GPUResult result;
  uint32_t  frameIndex;

  result = gpuDevicePrepareFrameSlot(device, &frameIndex);
  if (result != GPU_OK) {
    return result;
  }

  gpuDeviceActivateFrameSlot(device, frameIndex);
  return GPU_OK;
}

static inline GPUApi *
gpuDeviceApi(const GPUDevice *device) {
  return device ? device->_api : NULL;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static inline bool
gpuDeviceDebugMarkersEnabled(const GPUDevice *device) {
  return device && device->runtimeConfig.enableDebugMarkers;
}

static inline const char *
gpuDeviceDebugLabel(const GPUDevice *device, const char *label) {
  return gpuDeviceDebugMarkersEnabled(device) ? label : NULL;
}
#else
#  define gpuDeviceDebugMarkersEnabled(device) false
#  define gpuDeviceDebugLabel(device, label) NULL
#endif

static inline void
gpuFrameStatsRecordBindRequest(GPUFrameStats *stats) {
  if (stats) {
    stats->requestedBindCalls++;
  }
}

static inline void
gpuFrameStatsRecordBindEmission(GPUFrameStats *stats) {
  if (stats) {
    stats->emittedBindCalls++;
  }
}

static inline void
gpuFrameStatsRecordStateRequest(GPUFrameStats *stats) {
  if (stats) {
    stats->requestedStateCalls++;
  }
}

static inline void
gpuFrameStatsRecordStateEmission(GPUFrameStats *stats) {
  if (stats) {
    stats->emittedStateCalls++;
  }
}

static inline void
gpuFrameStatsRecordDraws(GPUFrameStats *stats, uint32_t drawCount) {
  if (stats) {
    stats->drawCalls += drawCount;
  }
}

GPU_HIDE
GPUResult
gpuDevicePrepareFrame(GPUDevice *device, uint32_t *outFrameIndex);

GPU_HIDE
void
gpuDeviceActivateFrame(GPUDevice *device, uint32_t frameIndex);

GPU_HIDE
void
gpuDeviceEndFrame(GPUDevice *device);

GPU_HIDE
void
gpuDeviceRecordHotPathAlloc(GPUDevice *device, uint64_t sizeBytes);

GPU_HIDE
void
gpuDeviceRecordHotPathFree(GPUDevice *device, uint64_t sizeBytes);

GPU_HIDE
void
gpuDeviceRecordGPUFrameTime(GPUDevice *device, double milliseconds);

GPU_HIDE
void
gpuDeviceReportError(GPUDevice           *device,
                     GPUDeviceErrorType    type,
                     GPUDeviceLostReason  lostReason,
                     GPUResult            result,
                     const char          *message);

#if GPU_BUILD_WITH_VALIDATION
static inline bool
gpuDeviceValidationEnabled(const GPUDevice *device) {
  return device &&
         device->runtimeConfig.validationMode != GPU_VALIDATION_OFF;
}

GPU_HIDE
void
gpuDeviceRecordValidationError(GPUDevice *device, const char *message);
#else
#  define gpuDeviceValidationEnabled(device) false
#  define gpuDeviceRecordValidationError(device, message) ((void)0)
#endif

#endif /* gpu_device_internal_h */
