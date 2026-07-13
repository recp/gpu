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
  GPUPhysicalDevice           *phyDevice;
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
  GPUQueueFlagBits             queueFamilies;
  GPUBufferUsageFlags          transientBufferUsage;
  uint32_t                     transientFrameIndex;
  uint32_t                     deviceLostReported;
  bool                         transientConfigured;
  bool                         transientFrameBegun;
  GPUFeature                   enabledFeatureStorage[
    GPU_FEATURE_VARIABLE_RATE_SHADING + 1u
  ];
};

static inline GPUResult
gpuDeviceAdvanceFrameSlot(GPUDevice *device) {
  GPUTransientChunk *chunk;
  GPUFence          *fence;
  GPUResult          result;
  uint32_t           nextFrameIndex;

  if (!device || !device->transientConfigured) {
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
  if (device->transientFrameBegun &&
      nextFrameIndex <= device->transientFrameIndex) {
    device->allocatorStats.ringWrapCount++;
  }

  device->transientFrameIndex             = nextFrameIndex;
  device->transientFrameOffset            = 0u;
  device->transientFrameBegun             = true;
  device->allocatorStats.ringUsedBytes    = 0u;

  for (chunk = device->transientChunks; chunk; chunk = chunk->next) {
    if (chunk->frameIndex == nextFrameIndex) {
      chunk->offset = 0u;
    }
  }
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
gpuDeviceRecordBindRequest(GPUDevice *device) {
  if (device && device->runtimeConfig.enableStats) {
    device->currentFrameStats.requestedBindCalls++;
  }
}

static inline void
gpuDeviceRecordBindEmission(GPUDevice *device) {
  if (device && device->runtimeConfig.enableStats) {
    device->currentFrameStats.emittedBindCalls++;
  }
}

static inline void
gpuDeviceRecordStateRequest(GPUDevice *device) {
  if (device && device->runtimeConfig.enableStats) {
    device->currentFrameStats.requestedStateCalls++;
  }
}

static inline void
gpuDeviceRecordStateEmission(GPUDevice *device) {
  if (device && device->runtimeConfig.enableStats) {
    device->currentFrameStats.emittedStateCalls++;
  }
}

static inline void
gpuDeviceRecordDraws(GPUDevice *device, uint32_t drawCount) {
  if (device && device->runtimeConfig.enableStats) {
    device->currentFrameStats.drawCalls += drawCount;
  }
}

GPU_HIDE
GPUResult
gpuDeviceBeginFrame(GPUDevice *device);

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
