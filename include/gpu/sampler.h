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

#ifndef gpu_sampler_h
#define gpu_sampler_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"
#include "usl.h"

typedef struct GPUDevice GPUDevice;

typedef struct GPUSampler GPUSampler;

typedef enum GPUFilter {
  GPU_FILTER_NEAREST = 0,
  GPU_FILTER_LINEAR = 1
} GPUFilter;

typedef enum GPUMipFilter {
  GPU_MIP_FILTER_NEAREST = 0,
  GPU_MIP_FILTER_LINEAR = 1
} GPUMipFilter;

typedef enum GPUAddressMode {
  GPU_ADDRESS_MODE_REPEAT = 0,
  GPU_ADDRESS_MODE_MIRRORED_REPEAT = 1,
  GPU_ADDRESS_MODE_CLAMP_TO_EDGE = 2
} GPUAddressMode;

typedef struct GPUSamplerDesc {
  GPUFilter      minFilter;
  GPUFilter      magFilter;
  GPUMipFilter   mipFilter;
  GPUAddressMode addressU;
  GPUAddressMode addressV;
  GPUAddressMode addressW;
} GPUSamplerDesc;

typedef struct GPUSamplerCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  GPUSamplerDesc   desc;
} GPUSamplerCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateSampler(GPUDevice                  * __restrict device,
                 const GPUSamplerCreateInfo * __restrict info,
                 bool                                    staticIfSupported,
                 GPUSampler                ** __restrict outSampler);

GPU_EXPORT
GPUResult
GPUCreateSamplerFromUSLStaticSampler(GPUDevice * __restrict device,
                                     const GPUUSLStaticSamplerDesc *desc,
                                     bool staticIfSupported,
                                     GPUSampler ** __restrict outSampler);

GPU_EXPORT
void
GPUDestroySampler(GPUSampler * __restrict sampler);

#ifdef __cplusplus
}
#endif
#endif /* gpu_sampler_h */
