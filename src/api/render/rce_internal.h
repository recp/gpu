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

#ifndef gpu_rce_internal_h
#define gpu_rce_internal_h

#include "../../common.h"

GPU_HIDE
void
setRenderVertexBuffer(GPURenderPassEncoder *pass,
                      GPUBuffer            *buf,
                      uint64_t              off,
                      uint32_t              index);

GPU_HIDE
void
setRenderVertexTexture(GPURenderPassEncoder *pass,
                       GPUTextureView       *view,
                       uint32_t              index);

GPU_HIDE
void
setRenderVertexSampler(GPURenderPassEncoder *pass,
                       GPUSampler           *sampler,
                       uint32_t              index);

GPU_HIDE
void
setRenderVertexAccelerationStructure(GPURenderPassEncoder        *pass,
                                     GPUAccelerationStructureEXT *structure,
                                     uint32_t                     index);

GPU_HIDE
void
setRenderTaskBuffer(GPURenderPassEncoder *pass,
                    GPUBuffer            *buf,
                    uint64_t              off,
                    uint32_t              index);

GPU_HIDE
void
setRenderTaskTexture(GPURenderPassEncoder *pass,
                     GPUTextureView       *view,
                     uint32_t              index);

GPU_HIDE
void
setRenderTaskSampler(GPURenderPassEncoder *pass,
                     GPUSampler           *sampler,
                     uint32_t              index);

GPU_HIDE
void
setRenderMeshBuffer(GPURenderPassEncoder *pass,
                    GPUBuffer            *buf,
                    uint64_t              off,
                    uint32_t              index);

GPU_HIDE
void
setRenderMeshTexture(GPURenderPassEncoder *pass,
                     GPUTextureView       *view,
                     uint32_t              index);

GPU_HIDE
void
setRenderMeshSampler(GPURenderPassEncoder *pass,
                     GPUSampler           *sampler,
                     uint32_t              index);

GPU_HIDE
void
setRenderFragmentBuffer(GPURenderPassEncoder *pass,
                        GPUBuffer            *buf,
                        uint64_t              off,
                        uint32_t              index);

GPU_HIDE
void
setRenderFragmentTexture(GPURenderPassEncoder *pass,
                         GPUTextureView       *view,
                         uint32_t              index);

GPU_HIDE
void
setRenderFragmentSampler(GPURenderPassEncoder *pass,
                         GPUSampler           *sampler,
                         uint32_t              index);

GPU_HIDE
void
setRenderFragmentAccelerationStructure(GPURenderPassEncoder        *pass,
                                       GPUAccelerationStructureEXT *structure,
                                       uint32_t                     index);

#endif /* gpu_rce_internal_h */
