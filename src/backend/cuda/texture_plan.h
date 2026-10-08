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

#ifndef gpu_cuda_texture_plan_h
#define gpu_cuda_texture_plan_h

#include "../../api/texture_internal.h"
#include "format.h"

typedef struct GPUCudaTexturePlan {
  CUDA_ARRAY3D_DESCRIPTOR desc;
  uint32_t                mipLevelCount;
  bool                    mipmapped;
} GPUCudaTexturePlan;

typedef struct GPUCudaTextureViewPlan {
  CUDA_RESOURCE_VIEW_DESC desc;
  uint32_t                mipLevel;
  bool                    hasResourceView;
  bool                    singleLevel;
  bool                    surfaceCompatible;
} GPUCudaTextureViewPlan;

GPU_HIDE
bool
cuda_texturePlan(const GPUTextureCreateInfo *info,
                 const GPUCudaFormatInfo    *format,
                 GPUCudaTexturePlan         *outPlan);

GPU_HIDE
bool
cuda_textureViewPlan(const GPUTexture               *texture,
                     const GPUTextureViewCreateInfo *info,
                     GPUCudaTextureViewPlan         *outPlan);

GPU_HIDE
bool
cuda_textureStorageViewSupported(GPUTextureViewType viewType);

#endif /* gpu_cuda_texture_plan_h */
