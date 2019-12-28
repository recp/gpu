/*
 * Copyright (c), Recep Aslantas.
 * MIT License (MIT), http://opensource.org/licenses/MIT
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
