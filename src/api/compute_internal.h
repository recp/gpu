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

#ifndef gpu_compute_internal_h
#define gpu_compute_internal_h

#include "../common.h"

struct GPUComputePipelineState {
  void    *_priv;
  uint32_t workgroupSize[3];
};

struct GPUComputePipeline {
  GPUApi              *_api;
  void                *_priv;
  void                *_state;
  GPUDevice           *_device;
  GPUPipelineLayout   *_layout;
  uint32_t             _requiredBindGroupMask;
  uint32_t             _pushConstantSizeBytes;
  GPUShaderStageFlags  _pushConstantStages;
  uint32_t             _refCount;
};

GPU_HIDE
void
gpuSetComputeBuffer(GPUComputePassEncoder *pass,
                    GPUBuffer             *buf,
                    uint64_t               off,
                    uint32_t               index);

GPU_HIDE
void
gpuSetComputeTexture(GPUComputePassEncoder *pass,
                     GPUTextureView        *view,
                     uint32_t               index);

GPU_HIDE
void
gpuSetComputeSampler(GPUComputePassEncoder *pass,
                     GPUSampler            *sampler,
                     uint32_t               index);

#endif /* gpu_compute_internal_h */
