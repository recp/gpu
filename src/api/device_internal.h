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
  GPUInstance       *inst;
  GPUPhysicalDevice *phyDevice;
  void              *_priv;
  GPUQueueFlagBits   queueFamilies;
  uint64_t           enabledFeatureMask;
  GPUCacheStats      cacheStats;
  GPURuntimeConfig   runtimeConfig;
  GPUFrameStats      currentFrameStats;
  GPUFrameStats      lastFrameStats;
  GPUAllocatorStats  allocatorStats;
  GPUTransientAllocatorConfig transientConfig;
  GPUBuffer         *transientBuffer;
  void              *transientCpuPtr;
  uint64_t           transientFrameOffset;
  uint32_t           transientFrameIndex;
  bool               transientConfigured;
  bool               transientFrameBegun;
  GPUTransientChunk *transientChunks;
};

GPU_HIDE
void
gpuDeviceBeginFrame(GPUDevice *device);

GPU_HIDE
void
gpuDeviceEndFrame(GPUDevice *device);

#endif /* gpu_device_internal_h */
