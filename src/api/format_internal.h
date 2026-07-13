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

#ifndef gpu_format_internal_h
#define gpu_format_internal_h

#include "../../include/gpu/common.h"
#include "../../include/gpu/format.h"
#include "../../include/gpu/texture.h"

typedef struct GPUFormatLayout {
  uint32_t bytesPerBlock;
  uint32_t blockWidth;
  uint32_t blockHeight;
} GPUFormatLayout;

typedef struct GPUFormatDataLayout {
  uint64_t bytesPerImage;
  uint64_t requiredBytes;
  uint32_t bytesInLastRow;
  uint32_t blockRows;
} GPUFormatDataLayout;

typedef enum GPUFormatNumericType {
  GPU_FORMAT_NUMERIC_FLOAT = 0,
  GPU_FORMAT_NUMERIC_UINT,
  GPU_FORMAT_NUMERIC_SINT
} GPUFormatNumericType;

static GPU_INLINE GPUFormatNumericType
gpuFormatNumericType(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_RG8_UINT:
    case GPU_FORMAT_R32_UINT:
    case GPU_FORMAT_RG16_UINT:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGB10A2_UINT:
    case GPU_FORMAT_RG32_UINT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA32_UINT:
      return GPU_FORMAT_NUMERIC_UINT;
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_RG8_SINT:
    case GPU_FORMAT_R32_SINT:
    case GPU_FORMAT_RG16_SINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RG32_SINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA32_SINT:
      return GPU_FORMAT_NUMERIC_SINT;
    default:
      return GPU_FORMAT_NUMERIC_FLOAT;
  }
}

GPU_HIDE
bool
gpuFormatLayout(GPUFormat format, GPUFormatLayout *outLayout);

GPU_HIDE
bool
gpuFormatResolveCopyAspect(GPUFormat        format,
                           GPUTextureAspect aspect,
                           GPUTextureAspect *outAspect);

GPU_HIDE
bool
gpuFormatAspectLayout(GPUFormat        format,
                      GPUTextureAspect aspect,
                      GPUFormatLayout *outLayout);

GPU_HIDE
uint32_t
gpuBlockCount(uint32_t extent, uint32_t blockExtent);

GPU_HIDE
bool
gpuFormatDataLayout(GPUFormat            format,
                    uint32_t             width,
                    uint32_t             height,
                    uint32_t             depth,
                    uint32_t             layerCount,
                    uint32_t             bytesPerRow,
                    uint32_t             rowsPerImage,
                    GPUFormatDataLayout *outLayout);

GPU_HIDE
bool
gpuFormatAspectDataLayout(GPUFormat            format,
                          GPUTextureAspect     aspect,
                          uint32_t             width,
                          uint32_t             height,
                          uint32_t             depth,
                          uint32_t             layerCount,
                          uint32_t             bytesPerRow,
                          uint32_t             rowsPerImage,
                          GPUFormatDataLayout *outLayout);

GPU_HIDE
bool
gpuFormatCopyAligned(GPUFormat format,
                     uint32_t  x,
                     uint32_t  y,
                     uint32_t  width,
                     uint32_t  height,
                     uint32_t  mipWidth,
                     uint32_t  mipHeight);

#endif /* gpu_format_internal_h */
