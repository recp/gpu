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

#ifndef gpu_cuda_format_h
#define gpu_cuda_format_h

#include "../../../include/gpu/common.h"
#include "../../../include/gpu/format.h"
#include "driver.h"

typedef uint32_t CudaFormatFlags;

enum {
  GPU_CUDA_FORMAT_SAMPLED_BIT         = 1u << 0,
  GPU_CUDA_FORMAT_FILTERABLE_BIT      = 1u << 1,
  GPU_CUDA_FORMAT_STORAGE_BIT         = 1u << 2,
  GPU_CUDA_FORMAT_READ_AS_INTEGER_BIT = 1u << 3,
  GPU_CUDA_FORMAT_SRGB_BIT            = 1u << 4
};

typedef struct CudaFormatInfo {
  CUarray_format     arrayFormat;
  CudaFormatFlags    flags;
  uint32_t           bytesPerTexel;
  uint32_t           channelCount;
} CudaFormatInfo;

_Static_assert(sizeof(CudaFormatInfo) == 16u,
               "CUDA format info ABI drift");

GPU_HIDE
bool
cuda_formatInfo(GPUFormat format, CudaFormatInfo    *outInfo);

GPU_HIDE
bool
cuda_formatResourceView(const CudaFormatInfo    *format,
                        CUresourceViewFormat    *outFormat);

GPU_HIDE
bool
cuda_formatTextureDesc(const CudaFormatInfo    *format,
                       const CUDA_TEXTURE_DESC *source,
                       CUDA_TEXTURE_DESC       *outDesc);

#endif /* gpu_cuda_format_h */
