#ifndef gpu_cuda_format_h
#define gpu_cuda_format_h

#include "../../../include/gpu/common.h"
#include "../../../include/gpu/format.h"
#include "driver.h"

typedef uint32_t GPUCudaFormatFlags;

enum {
  GPU_CUDA_FORMAT_SAMPLED_BIT         = 1u << 0,
  GPU_CUDA_FORMAT_FILTERABLE_BIT      = 1u << 1,
  GPU_CUDA_FORMAT_STORAGE_BIT         = 1u << 2,
  GPU_CUDA_FORMAT_READ_AS_INTEGER_BIT = 1u << 3,
  GPU_CUDA_FORMAT_SRGB_BIT            = 1u << 4
};

typedef struct GPUCudaFormatInfo {
  CUarray_format     arrayFormat;
  GPUCudaFormatFlags flags;
  uint32_t           bytesPerTexel;
  uint32_t           channelCount;
} GPUCudaFormatInfo;

_Static_assert(sizeof(GPUCudaFormatInfo) == 16u,
               "CUDA format info ABI drift");

GPU_HIDE
bool
cuda_formatInfo(GPUFormat format, GPUCudaFormatInfo *outInfo);

GPU_HIDE
bool
cuda_formatTextureDesc(const GPUCudaFormatInfo *format,
                       const CUDA_TEXTURE_DESC *source,
                       CUDA_TEXTURE_DESC       *outDesc);

#endif /* gpu_cuda_format_h */
