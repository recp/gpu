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

#ifndef gpu_cuda_sampler_plan_h
#define gpu_cuda_sampler_plan_h

#include "../../api/library_internal.h"
#include "../../../include/gpu/sampler.h"
#include "driver.h"

enum {
  CUDA_MAX_SAMPLER_ANISOTROPY = 16u
};

GPU_HIDE
bool
cuda_samplerTextureDesc(const GPUSamplerDesc *source,
                        CUDA_TEXTURE_DESC    *outDesc);

GPU_HIDE
bool
cuda_staticSamplerTextureDesc(const GPUStaticSamplerDesc *source,
                              CUDA_TEXTURE_DESC          *outDesc);

GPU_HIDE
const CUDA_TEXTURE_DESC*
cuda_exactTextureDesc(void);

#endif /* gpu_cuda_sampler_plan_h */
