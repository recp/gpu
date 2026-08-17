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
const CUDA_TEXTURE_DESC *
cuda_exactTextureDesc(void);

#endif /* gpu_cuda_sampler_plan_h */
