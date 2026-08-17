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
