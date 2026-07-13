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

#ifndef gpu_api_compute_h
#define gpu_api_compute_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>
#include "descriptor.h"

typedef struct GPUComputePipelineState GPUComputePipelineState;
typedef struct GPUPipelineLayout       GPUPipelineLayout;
typedef struct GPUBindGroupLayout      GPUBindGroupLayout;
typedef struct GPUBindGroup            GPUBindGroup;

struct GPUComputePassEncoder {
  void                   *_priv;
  void                   *_pipeline;
  GPUCommandBuffer       *_cmdb;
  GPUPipelineLayout      *_pipelineLayout;
  GPUBindGroup           *_boundGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBindGroupLayout     *_boundGroupLayouts[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUDynamicOffsetShadow  _boundDynamicOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                _boundDynamicOffsetCounts[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                _workgroupSize[3];
  uint32_t                _requiredBindGroupMask;
  uint32_t                _pushConstantSizeBytes;
  GPUShaderStageFlags     _pushConstantStages;
  bool                    _hasPipeline;
  bool                    _pushConstantsEmitted;
  bool                    _ended;
  uint8_t                 _pushConstants[4096];
};

typedef struct GPUApiCompute {
  GPUResult
  (*createPipeline)(GPUDevice                          *device,
                    const GPUComputePipelineCreateInfo *info,
                    GPUComputePipeline                 *pipeline);

  GPUComputePipeline*
  (*newComputePipeline)(void);

  void
  (*setFunction)(GPUComputePipeline *pipeline, GPUFunction *func);

  GPUComputePipelineState*
  (*newComputeState)(GPUDevice *device, GPUComputePipeline *pipeline);

  void
  (*destroyComputePipeline)(GPUComputePipeline *pipeline);

  GPUComputePassEncoder*
  (*computeCommandEncoder)(GPUCommandBuffer *cmdb, const char *label);

  void
  (*setComputePipelineState)(GPUComputePassEncoder *enc,
                             GPUComputePipelineState *state);

  void
  (*buffer)(GPUComputePassEncoder *enc,
            GPUBuffer             *buf,
            size_t                 off,
            uint32_t               index);

  void
  (*texture)(GPUComputePassEncoder *enc,
             GPUTextureView        *view,
             uint32_t               index);

  void
  (*sampler)(GPUComputePassEncoder *enc,
             GPUSampler            *sampler,
             uint32_t               index);

  void
  (*pushConstants)(GPUComputePassEncoder *enc,
                   const void            *data,
                   uint32_t               sizeBytes);

  void
  (*dispatch)(GPUComputePassEncoder *enc,
              uint32_t               x,
              uint32_t               y,
              uint32_t               z);

  void
  (*dispatchIndirect)(GPUComputePassEncoder *enc,
                      GPUBuffer             *argsBuffer,
                      uint64_t               argsOffset);

  bool
  (*multiDispatchIndirect)(GPUComputePassEncoder *enc,
                           GPUBuffer             *argsBuffer,
                           uint64_t               argsOffset,
                           uint32_t               dispatchCount,
                           uint32_t               strideBytes);

  void
  (*endEncoding)(GPUComputePassEncoder *enc);
} GPUApiCompute;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_compute_h */
