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

#include "../common.h"

#define GPU_TEXEL(BYTES)       {BYTES, 1u, 1u}
#define GPU_BLOCK(BYTES, W, H) {BYTES, W, H}

static const GPUFormatLayout gpu_formatLayouts[GPU_FORMAT_COUNT] = {
  [GPU_FORMAT_R8_UNORM]                    = GPU_TEXEL(1u),
  [GPU_FORMAT_R8_SNORM]                    = GPU_TEXEL(1u),
  [GPU_FORMAT_R8_UINT]                     = GPU_TEXEL(1u),
  [GPU_FORMAT_R8_SINT]                     = GPU_TEXEL(1u),
  [GPU_FORMAT_R16_UNORM]                   = GPU_TEXEL(2u),
  [GPU_FORMAT_R16_SNORM]                   = GPU_TEXEL(2u),
  [GPU_FORMAT_R16_UINT]                    = GPU_TEXEL(2u),
  [GPU_FORMAT_R16_SINT]                    = GPU_TEXEL(2u),
  [GPU_FORMAT_R16_FLOAT]                   = GPU_TEXEL(2u),
  [GPU_FORMAT_RG8_UNORM]                   = GPU_TEXEL(2u),
  [GPU_FORMAT_RG8_SNORM]                   = GPU_TEXEL(2u),
  [GPU_FORMAT_RG8_UINT]                    = GPU_TEXEL(2u),
  [GPU_FORMAT_RG8_SINT]                    = GPU_TEXEL(2u),
  [GPU_FORMAT_R32_UINT]                    = GPU_TEXEL(4u),
  [GPU_FORMAT_R32_SINT]                    = GPU_TEXEL(4u),
  [GPU_FORMAT_R32_FLOAT]                   = GPU_TEXEL(4u),
  [GPU_FORMAT_RG16_UNORM]                  = GPU_TEXEL(4u),
  [GPU_FORMAT_RG16_SNORM]                  = GPU_TEXEL(4u),
  [GPU_FORMAT_RG16_UINT]                   = GPU_TEXEL(4u),
  [GPU_FORMAT_RG16_SINT]                   = GPU_TEXEL(4u),
  [GPU_FORMAT_RG16_FLOAT]                  = GPU_TEXEL(4u),
  [GPU_FORMAT_RGBA8_UNORM]                 = GPU_TEXEL(4u),
  [GPU_FORMAT_RGBA8_UNORM_SRGB]            = GPU_TEXEL(4u),
  [GPU_FORMAT_RGBA8_SNORM]                 = GPU_TEXEL(4u),
  [GPU_FORMAT_RGBA8_UINT]                  = GPU_TEXEL(4u),
  [GPU_FORMAT_RGBA8_SINT]                  = GPU_TEXEL(4u),
  [GPU_FORMAT_BGRA8_UNORM]                 = GPU_TEXEL(4u),
  [GPU_FORMAT_BGRA8_UNORM_SRGB]            = GPU_TEXEL(4u),
  [GPU_FORMAT_RGB10A2_UNORM]               = GPU_TEXEL(4u),
  [GPU_FORMAT_RGB10A2_UINT]                = GPU_TEXEL(4u),
  [GPU_FORMAT_RG11B10_UFLOAT]              = GPU_TEXEL(4u),
  [GPU_FORMAT_RGB9E5_UFLOAT]               = GPU_TEXEL(4u),
  [GPU_FORMAT_RG32_UINT]                   = GPU_TEXEL(8u),
  [GPU_FORMAT_RG32_SINT]                   = GPU_TEXEL(8u),
  [GPU_FORMAT_RG32_FLOAT]                  = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA16_UNORM]                = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA16_SNORM]                = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA16_UINT]                 = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA16_SINT]                 = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA16_FLOAT]                = GPU_TEXEL(8u),
  [GPU_FORMAT_RGBA32_UINT]                 = GPU_TEXEL(16u),
  [GPU_FORMAT_RGBA32_SINT]                 = GPU_TEXEL(16u),
  [GPU_FORMAT_RGBA32_FLOAT]                = GPU_TEXEL(16u),
  [GPU_FORMAT_BC1_RGBA_UNORM]              = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_BC1_RGBA_UNORM_SRGB]         = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_BC2_RGBA_UNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC2_RGBA_UNORM_SRGB]         = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC3_RGBA_UNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC3_RGBA_UNORM_SRGB]         = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC4_R_UNORM]                 = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_BC4_R_SNORM]                 = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_BC5_RG_UNORM]                = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC5_RG_SNORM]                = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC6H_RGB_FLOAT]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC6H_RGB_UFLOAT]             = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC7_RGBA_UNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_BC7_RGBA_UNORM_SRGB]         = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_EAC_R11_UNORM]               = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_EAC_R11_SNORM]               = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_EAC_RG11_UNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_EAC_RG11_SNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGBA8_UNORM]            = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGBA8_UNORM_SRGB]       = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGB8_UNORM]             = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGB8_UNORM_SRGB]        = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGB8A1_UNORM]           = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB]      = GPU_BLOCK(8u, 4u, 4u),
  [GPU_FORMAT_ASTC_4X4_UNORM]              = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_ASTC_4X4_UNORM_SRGB]         = GPU_BLOCK(16u, 4u, 4u),
  [GPU_FORMAT_ASTC_5X4_UNORM]              = GPU_BLOCK(16u, 5u, 4u),
  [GPU_FORMAT_ASTC_5X4_UNORM_SRGB]         = GPU_BLOCK(16u, 5u, 4u),
  [GPU_FORMAT_ASTC_5X5_UNORM]              = GPU_BLOCK(16u, 5u, 5u),
  [GPU_FORMAT_ASTC_5X5_UNORM_SRGB]         = GPU_BLOCK(16u, 5u, 5u),
  [GPU_FORMAT_ASTC_6X5_UNORM]              = GPU_BLOCK(16u, 6u, 5u),
  [GPU_FORMAT_ASTC_6X5_UNORM_SRGB]         = GPU_BLOCK(16u, 6u, 5u),
  [GPU_FORMAT_ASTC_6X6_UNORM]              = GPU_BLOCK(16u, 6u, 6u),
  [GPU_FORMAT_ASTC_6X6_UNORM_SRGB]         = GPU_BLOCK(16u, 6u, 6u),
  [GPU_FORMAT_ASTC_8X5_UNORM]              = GPU_BLOCK(16u, 8u, 5u),
  [GPU_FORMAT_ASTC_8X5_UNORM_SRGB]         = GPU_BLOCK(16u, 8u, 5u),
  [GPU_FORMAT_ASTC_8X6_UNORM]              = GPU_BLOCK(16u, 8u, 6u),
  [GPU_FORMAT_ASTC_8X6_UNORM_SRGB]         = GPU_BLOCK(16u, 8u, 6u),
  [GPU_FORMAT_ASTC_8X8_UNORM]              = GPU_BLOCK(16u, 8u, 8u),
  [GPU_FORMAT_ASTC_8X8_UNORM_SRGB]         = GPU_BLOCK(16u, 8u, 8u),
  [GPU_FORMAT_ASTC_10X5_UNORM]             = GPU_BLOCK(16u, 10u, 5u),
  [GPU_FORMAT_ASTC_10X5_UNORM_SRGB]        = GPU_BLOCK(16u, 10u, 5u),
  [GPU_FORMAT_ASTC_10X6_UNORM]             = GPU_BLOCK(16u, 10u, 6u),
  [GPU_FORMAT_ASTC_10X6_UNORM_SRGB]        = GPU_BLOCK(16u, 10u, 6u),
  [GPU_FORMAT_ASTC_10X8_UNORM]             = GPU_BLOCK(16u, 10u, 8u),
  [GPU_FORMAT_ASTC_10X8_UNORM_SRGB]        = GPU_BLOCK(16u, 10u, 8u),
  [GPU_FORMAT_ASTC_10X10_UNORM]            = GPU_BLOCK(16u, 10u, 10u),
  [GPU_FORMAT_ASTC_10X10_UNORM_SRGB]       = GPU_BLOCK(16u, 10u, 10u),
  [GPU_FORMAT_ASTC_12X10_UNORM]            = GPU_BLOCK(16u, 12u, 10u),
  [GPU_FORMAT_ASTC_12X10_UNORM_SRGB]       = GPU_BLOCK(16u, 12u, 10u),
  [GPU_FORMAT_ASTC_12X12_UNORM]            = GPU_BLOCK(16u, 12u, 12u),
  [GPU_FORMAT_ASTC_12X12_UNORM_SRGB]       = GPU_BLOCK(16u, 12u, 12u),
  [GPU_FORMAT_DEPTH16_UNORM]               = GPU_TEXEL(2u),
  [GPU_FORMAT_STENCIL8]                    = GPU_TEXEL(1u),
  [GPU_FORMAT_DEPTH24_UNORM_STENCIL8]      = GPU_TEXEL(4u),
  [GPU_FORMAT_DEPTH32_FLOAT]               = GPU_TEXEL(4u),
  [GPU_FORMAT_DEPTH32_FLOAT_STENCIL8]      = GPU_TEXEL(8u)
};

_Static_assert(GPU_ARRAY_LEN(gpu_formatLayouts) == GPU_FORMAT_COUNT,
               "format layout table must cover GPUFormat");

GPU_HIDE
bool
gpuFormatLayout(GPUFormat format, GPUFormatLayout *outLayout) {
  GPUFormatLayout layout;

  if (!outLayout || format <= GPU_FORMAT_UNDEFINED ||
      format >= GPU_FORMAT_COUNT) {
    return false;
  }

  layout = gpu_formatLayouts[format];
  if (layout.bytesPerBlock == 0u ||
      layout.blockWidth == 0u ||
      layout.blockHeight == 0u) {
    return false;
  }

  *outLayout = layout;
  return true;
}

GPU_HIDE
uint32_t
gpuBlockCount(uint32_t extent, uint32_t blockExtent) {
  if (extent == 0u || blockExtent == 0u) {
    return 0u;
  }
  return 1u + ((extent - 1u) / blockExtent);
}

GPU_HIDE
bool
gpuFormatDataLayout(GPUFormat            format,
                    uint32_t             width,
                    uint32_t             height,
                    uint32_t             depth,
                    uint32_t             layerCount,
                    uint32_t             bytesPerRow,
                    uint32_t             rowsPerImage,
                    GPUFormatDataLayout *outLayout) {
  GPUFormatLayout layout;
  uint64_t        bytesPerImage;
  uint64_t        imageCount;
  uint64_t        imageRows;
  uint64_t        requiredBytes;
  uint32_t        blockRows;
  uint32_t        bytesInLastRow;
  uint32_t        rowBlocks;

  if (!outLayout || width == 0u || height == 0u || depth == 0u ||
      layerCount == 0u || bytesPerRow == 0u ||
      !gpuFormatLayout(format, &layout)) {
    return false;
  }

  rowBlocks = gpuBlockCount(width, layout.blockWidth);
  blockRows = gpuBlockCount(height, layout.blockHeight);
  if (rowBlocks > UINT32_MAX / layout.bytesPerBlock) {
    return false;
  }

  bytesInLastRow = rowBlocks * layout.bytesPerBlock;
  if (bytesPerRow < bytesInLastRow ||
      bytesPerRow % layout.bytesPerBlock != 0u) {
    return false;
  }

  if (rowsPerImage != 0u) {
    if (rowsPerImage < height ||
        rowsPerImage % layout.blockHeight != 0u) {
      return false;
    }
    imageRows = rowsPerImage / layout.blockHeight;
  } else {
    imageRows = blockRows;
  }

  if (imageRows == 0u || bytesPerRow > UINT64_MAX / imageRows) {
    return false;
  }
  bytesPerImage = (uint64_t)bytesPerRow * imageRows;

  if (depth > UINT64_MAX / layerCount) {
    return false;
  }
  imageCount = (uint64_t)depth * layerCount;

  requiredBytes = bytesInLastRow;
  if (blockRows > 1u) {
    uint64_t precedingRows;

    precedingRows = (uint64_t)(blockRows - 1u) * bytesPerRow;
    if (requiredBytes > UINT64_MAX - precedingRows) {
      return false;
    }
    requiredBytes += precedingRows;
  }
  if (imageCount > 1u) {
    uint64_t precedingImages;

    if (bytesPerImage > UINT64_MAX / (imageCount - 1u)) {
      return false;
    }
    precedingImages = bytesPerImage * (imageCount - 1u);
    if (requiredBytes > UINT64_MAX - precedingImages) {
      return false;
    }
    requiredBytes += precedingImages;
  }

  outLayout->bytesPerImage  = bytesPerImage;
  outLayout->requiredBytes  = requiredBytes;
  outLayout->bytesInLastRow = bytesInLastRow;
  outLayout->blockRows      = blockRows;
  return true;
}

GPU_HIDE
bool
gpuFormatCopyAligned(GPUFormat format,
                     uint32_t  x,
                     uint32_t  y,
                     uint32_t  width,
                     uint32_t  height,
                     uint32_t  mipWidth,
                     uint32_t  mipHeight) {
  GPUFormatLayout layout;

  if (!gpuFormatLayout(format, &layout) ||
      x > mipWidth || width > mipWidth - x ||
      y > mipHeight || height > mipHeight - y) {
    return false;
  }

  return x % layout.blockWidth == 0u &&
         y % layout.blockHeight == 0u &&
         (width % layout.blockWidth == 0u || width == mipWidth - x) &&
         (height % layout.blockHeight == 0u || height == mipHeight - y);
}

#undef GPU_BLOCK
#undef GPU_TEXEL
