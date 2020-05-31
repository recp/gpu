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

#ifndef gpu_resource_h
#define gpu_resource_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef enum GPUCPUCacheMode {
  GPUCPUCacheModeDefaultCache  = 0,
  GPUCPUCacheModeWriteCombined = 1
} GPUCPUCacheMode;

typedef enum GPUHazardTrackingMode {
  GPUHazardTrackingModeDefault   = 0,
  GPUHazardTrackingModeUntracked = 1,
  GPUHazardTrackingModeTracked   = 2
} GPUHazardTrackingMode;

typedef enum GPUStorageMode {
  GPUStorageModeShared     = 0,
  GPUStorageModeManaged    = 1,
  GPUStorageModePrivate    = 2,
  GPUStorageModeMemoryless = 3
} GPUStorageMode;

typedef enum GPUResourceOptions {
  GPUResourceCPUCacheModeDefaultCache    = GPUCPUCacheModeDefaultCache,
  GPUResourceCPUCacheModeWriteCombined   = GPUCPUCacheModeWriteCombined,

  GPUResourceStorageModeShared           = GPUStorageModeShared           << 4,
  GPUResourceStorageModeManaged          = GPUStorageModeManaged          << 4,
  GPUResourceStorageModePrivate          = GPUStorageModePrivate          << 4,
  GPUResourceStorageModeMemoryless       = GPUStorageModeMemoryless       << 4,

  GPUResourceHazardTrackingModeDefault   = GPUHazardTrackingModeDefault   << 8,
  GPUResourceHazardTrackingModeUntracked = GPUHazardTrackingModeUntracked << 8,
  GPUResourceHazardTrackingModeTracked   = GPUHazardTrackingModeTracked   << 8
} GPUResourceOptions;

#endif /* gpu_resource_h */
