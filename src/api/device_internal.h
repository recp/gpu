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
  GPUBuffer *buffer;
  void *cpuPtr;
  uint64_t sizeBytes;
  struct GPUTransientChunk *next;
} GPUTransientChunk;

struct GPUDevice {
  GPUInstance                 *inst;
  GPUPhysicalDevice           *phyDevice;
  GPUApi                      *_api;
  void                        *_priv;
  GPUBuffer                   *transientBuffer;
  GPUTransientChunk           *transientChunks;
  void                        *transientCpuPtr;
  GPUFeatureSet                enabledFeatures;
  GPUCacheStats                cacheStats;
  GPURuntimeConfig             runtimeConfig;
  GPUFrameStats                currentFrameStats;
  GPUFrameStats                lastFrameStats;
  GPUAllocatorStats            allocatorStats;
  GPUTransientAllocatorConfig transientConfig;
  uint64_t                     enabledFeatureMask;
  uint64_t                     transientFrameOffset;
  GPUQueueFlagBits             queueFamilies;
  GPUBufferUsageFlags          transientBufferUsage;
  uint32_t                     transientFrameIndex;
  bool                         transientConfigured;
  bool                         transientFrameBegun;
  GPUFeature                   enabledFeatureStorage[
    GPU_FEATURE_VARIABLE_RATE_SHADING + 1u
  ];
};

static inline GPUApi *
gpuDeviceApi(const GPUDevice *device) {
  return device ? device->_api : NULL;
}

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
void
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
gpuDeviceRecordValidationError(GPUDevice *device, const char *message);

#endif /* gpu_device_internal_h */
