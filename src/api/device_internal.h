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

#if !GPU_BUILD_WITH_DEBUG_MARKERS
#  define deviceDebugMarkersEnabled(device) false
#  define deviceDebugLabel(device, label) NULL
#endif

#if !GPU_BUILD_WITH_VALIDATION
#  define deviceValidationEnabled(device) false
#  define deviceRecordValidationError(device, message) ((void)0)
#endif

typedef struct TransientChunk {
  GPUBuffer                *buffer;
  void                     *cpuPtr;
  struct TransientChunk    *next;
  uint64_t                  sizeBytes;
  uint64_t                  offset;
  GPUBufferUsageFlags       usage;
  uint32_t                  frameIndex;
  bool                      cpuPtrOwned;
} TransientChunk;

typedef struct GPUPipelineCache GPUPipelineCache;

typedef struct MeshLimits {
  uint32_t taskWorkgroupSize[3];
  uint32_t meshWorkgroupSize[3];
  uint32_t maxTaskWorkgroupInvocations;
  uint32_t maxMeshWorkgroupInvocations;
  uint32_t maxPayloadSizeBytes;
  uint32_t maxOutputVertices;
  uint32_t maxOutputPrimitives;
} MeshLimits;

typedef struct RayTracingLimits {
  uint64_t maxDispatchCount;
  uint32_t maxDispatchSize[3];
  uint32_t maxRecursionDepth;
  uint32_t maxHitAttributeSizeBytes;
} RayTracingLimits;

struct GPUDevice {
  GPUInstance                *inst;
  GPUAdapter                 *adapter;
  Api                        *_api;
  void                       *_priv;
  GPUBuffer                  *transientBuffer;
  TransientChunk             *transientChunks;
  GPUFence                  **transientFrameFences;
  GPUPipelineCache           *_pipelineCaches;
  void                       *_pipelineCacheLock;
  void                       *_bindGroupCache;
  void                       *_blitContext;
  void                       *transientCpuPtr;
  GPUDeviceErrorCallback      errorCallback;
  void                       *errorUserData;
  GPUFeatureSet               enabledFeatures;
  GPUCacheStats               cacheStats;
  GPUVRSCapabilitiesEXT       vrsCapabilities;
  RayTracingLimits            rayTracingLimits;
  MeshLimits                  meshLimits;
  GPURuntimeConfig            runtimeConfig;
  GPUFrameStats               currentFrameStats;
  GPUFrameStats               lastFrameStats;
  GPUAllocatorStats           allocatorStats;
  GPUTransientAllocatorConfig transientConfig;
  uint64_t                    enabledFeatureMask;
  uint64_t                    transientFrameOffset;
  uint64_t                    transientFrameStride;
  uint64_t                    _nextPipelineCompileId;
  uint64_t                    _completedGPUFrameTimeBits;
  GPUQueueFlagBits            queueFamilies;
  GPUBufferUsageFlags         transientBufferUsage;
  uint32_t                    uslTargetProfile;
  uint32_t                    uslTargetArchitecture;
  uint32_t                    uslTargetVersion;
  uint32_t                    transientFrameIndex;
  uint32_t                    deviceLostReported;
  uint8_t                     uslFloatPreserve; /* f16/f32/f64 bits */
  uint8_t                     uslFma; /* same width bits, enabled native fma */
  uint8_t                     uslDenormPreserve;
  uint8_t                     uslRoundingRTE;
  uint8_t                     uslFloatAtomicAdd; /* buffer/workgroup bits, enabled on the device */
  bool                        uslFloatControls2;
  bool                        uslHalfRoundtrip;
  bool                        uslStorageF16;
  bool                        uslCubeGradFixup;
  bool                        transientConfigured;
  bool                        transientFrameBegun;
  bool                        transientCpuPtrOwned;
  bool                        uslStorageExtAccess;
  bool                        uslStorageExtFormats;
  bool                        uslBoundedDescriptorIndexing;
  bool                        uslUntypedPointers;
  GPUFeature                  enabledFeatureStorage[GPU_FEATURE_BUFFER_HOST_MEMORY_EXT + 1u];
};

GPU_HIDE
GPUResult
devicePrepareFrame(GPUDevice *device, uint32_t *outFrameIndex);

GPU_HIDE
void
deviceActivateFrame(GPUDevice *device, uint32_t frameIndex);

GPU_HIDE
void
deviceEndFrame(GPUDevice *device);

GPU_HIDE
void
deviceRecordHotPathAlloc(GPUDevice *device, uint64_t sizeBytes);

GPU_HIDE
void
deviceRecordHotPathFree(GPUDevice *device, uint64_t sizeBytes);

GPU_HIDE
void
deviceRecordGPUFrameTime(GPUDevice *device, double milliseconds);

GPU_HIDE
GPUResult
deviceFlushTransientUploads(GPUQueue *queue, uint32_t frameIndex);

GPU_HIDE
void
deviceReportError(GPUDevice          *device,
                  GPUDeviceErrorType  type,
                  GPUDeviceLostReason lostReason,
                  GPUResult           result,
                  const char         *message);

#if GPU_BUILD_WITH_VALIDATION
GPU_HIDE
void
deviceRecordValidationError(GPUDevice *device, const char *message);
#endif

static inline void
deviceCacheCounterAdd(uint64_t *counter, uint64_t value) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchangeAdd64((volatile LONG64 *)counter, (LONG64)value);
#else
  __atomic_fetch_add(counter, value, __ATOMIC_RELAXED);
#endif
}

static inline uint64_t
deviceCacheCounterLoad(const uint64_t *counter) {
#if defined(_WIN32) || defined(WIN32)
  return (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)counter,
                                                0,
                                                0);
#else
  return __atomic_load_n(counter, __ATOMIC_RELAXED);
#endif
}

static inline void
deviceCacheCounterReset(uint64_t *counter) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange64((volatile LONG64 *)counter, 0);
#else
  __atomic_store_n(counter, 0u, __ATOMIC_RELAXED);
#endif
}

static inline void
deviceGetCacheStats(const GPUDevice *device, GPUCacheStats *stats) {
  stats->bindGroupHits       = deviceCacheCounterLoad(&device->cacheStats.bindGroupHits);
  stats->bindGroupMisses     = deviceCacheCounterLoad(&device->cacheStats.bindGroupMisses);
  stats->bindGroupCollisions = deviceCacheCounterLoad(&device->cacheStats.bindGroupCollisions);
  stats->pipelineHits        = deviceCacheCounterLoad(&device->cacheStats.pipelineHits);
  stats->pipelineMisses      = deviceCacheCounterLoad(&device->cacheStats.pipelineMisses);
  stats->pipelineCompiles    = deviceCacheCounterLoad(&device->cacheStats.pipelineCompiles);
}

static inline void
deviceResetCacheStats(GPUDevice *device) {
  deviceCacheCounterReset(&device->cacheStats.bindGroupHits);
  deviceCacheCounterReset(&device->cacheStats.bindGroupMisses);
  deviceCacheCounterReset(&device->cacheStats.bindGroupCollisions);
  deviceCacheCounterReset(&device->cacheStats.pipelineHits);
  deviceCacheCounterReset(&device->cacheStats.pipelineMisses);
  deviceCacheCounterReset(&device->cacheStats.pipelineCompiles);
}

static inline GPUResult
devicePrepareFrameSlot(GPUDevice *device, uint32_t *outFrameIndex) {
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
deviceActivateFrameSlot(GPUDevice *device, uint32_t frameIndex) {
  TransientChunk    *chunk;

  if (!device || !device->transientConfigured) {
    return;
  }

  if (device->transientFrameBegun
      && frameIndex <= device->transientFrameIndex) {
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
deviceAdvanceFrameSlot(GPUDevice *device) {
  GPUResult result;
  uint32_t  frameIndex;

  result = devicePrepareFrameSlot(device, &frameIndex);

  if (result != GPU_OK) {
    return result;
  }

  deviceActivateFrameSlot(device, frameIndex);

  return GPU_OK;
}

static inline Api*
deviceApi(const GPUDevice *device) {
  return device ? device->_api : NULL;
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static inline bool
deviceDebugMarkersEnabled(const GPUDevice *device) {
  return device && device->runtimeConfig.enableDebugMarkers;
}

static inline const char*
deviceDebugLabel(const GPUDevice *device, const char *label) {
  return deviceDebugMarkersEnabled(device) ? label : NULL;
}
#endif

static inline void
frameStatsRecordBindRequest(GPUFrameStats *stats) {
  if (stats) {
    stats->requestedBindCalls++;
  }
}

static inline void
frameStatsRecordBindEmission(GPUFrameStats *stats) {
  if (stats) {
    stats->emittedBindCalls++;
  }
}

static inline void
frameStatsRecordStateRequest(GPUFrameStats *stats) {
  if (stats) {
    stats->requestedStateCalls++;
  }
}

static inline void
frameStatsRecordStateEmission(GPUFrameStats *stats) {
  if (stats) {
    stats->emittedStateCalls++;
  }
}

static inline void
frameStatsRecordDraws(GPUFrameStats *stats, uint32_t drawCount) {
  if (stats) {
    stats->drawCalls += drawCount;
  }
}

#if GPU_BUILD_WITH_VALIDATION
static inline bool
deviceValidationEnabled(const GPUDevice *device) {
  return device
         && device->runtimeConfig.validationMode != GPU_VALIDATION_OFF;
}
#endif

#endif /* gpu_device_internal_h */
