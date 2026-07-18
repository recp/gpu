/*
 * Copyright (C) 2026 Recep Aslantas
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

#ifndef gpu_ray_internal_h
#define gpu_ray_internal_h

#include "../common.h"

struct GPUAccelerationStructureEXT {
  void                                *_priv;
  GPUDevice                           *device;
  uint64_t                             sizeBytes;
  GPUAccelerationStructureTypeEXT      type;
};

struct GPUAccelerationStructurePassEncoderEXT {
  void              *_priv;
  GPUApi            *_api;
  GPUDevice         *device;
  GPUCommandBuffer  *cmdb;
  bool               ended;
};

struct GPURayTracingPipelineEXT {
  void                            *_priv;
  GPUApi                          *_api;
  GPUDevice                       *device;
  GPUPipelineLayout               *layout;
  GPURayTracingShaderGroupTypeEXT *groupTypes;
  GPUShaderStageFlags             *generalStages;
  uint32_t                         requiredBindGroupMask;
  uint32_t                         groupCount;
  uint32_t                         maxPayloadSizeBytes;
  uint32_t                         maxHitAttributeSizeBytes;
  uint32_t                         refCount;
};

struct GPUShaderTableEXT {
  void                     *_priv;
  GPUApi                   *_api;
  GPUDevice                *device;
  GPURayTracingPipelineEXT *pipeline;
};

struct GPURayTracingPassEncoderEXT {
  void                   *_priv;
  void                   *_pipeline;
  GPUApi                 *_api;
  GPUDevice              *device;
  GPUCommandBuffer       *cmdb;
  GPUFrameStats          *stats;
  GPUPipelineLayout      *pipelineLayout;
  GPUBindGroup           *boundGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBindGroupLayout     *boundGroupLayouts[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUDynamicOffsetShadow  boundDynamicOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                boundDynamicOffsetCounts[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                requiredBindGroupMask;
  bool                    hasPipeline;
  bool                    ended;
};

static inline bool
gpuRayDispatchFits(uint32_t        width,
                   uint32_t        height,
                   uint32_t        depth,
                   const uint32_t  maxSize[3],
                   uint64_t        maxCount) {
  if (width == 0u || height == 0u || depth == 0u || maxCount == 0u ||
      (maxSize && (width > maxSize[0] ||
                   height > maxSize[1] ||
                   depth > maxSize[2]))) {
    return false;
  }

  return (uint64_t)width * height <= maxCount / depth;
}

#endif /* gpu_ray_internal_h */
