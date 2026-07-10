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

static const DXGI_FORMAT dx12_formats[GPUPixelFormatX24_Stencil8 + 1u] = {
  [GPUPixelFormatR8Unorm]               = DXGI_FORMAT_R8_UNORM,
  [GPUPixelFormatR8Snorm]               = DXGI_FORMAT_R8_SNORM,
  [GPUPixelFormatR8Uint]                = DXGI_FORMAT_R8_UINT,
  [GPUPixelFormatR8Sint]                = DXGI_FORMAT_R8_SINT,
  [GPUPixelFormatR16Unorm]              = DXGI_FORMAT_R16_UNORM,
  [GPUPixelFormatR16Snorm]              = DXGI_FORMAT_R16_SNORM,
  [GPUPixelFormatR16Uint]               = DXGI_FORMAT_R16_UINT,
  [GPUPixelFormatR16Sint]               = DXGI_FORMAT_R16_SINT,
  [GPUPixelFormatR16Float]              = DXGI_FORMAT_R16_FLOAT,
  [GPUPixelFormatRG8Unorm]              = DXGI_FORMAT_R8G8_UNORM,
  [GPUPixelFormatRG8Snorm]              = DXGI_FORMAT_R8G8_SNORM,
  [GPUPixelFormatRG8Uint]               = DXGI_FORMAT_R8G8_UINT,
  [GPUPixelFormatRG8Sint]               = DXGI_FORMAT_R8G8_SINT,
  [GPUPixelFormatR32Uint]               = DXGI_FORMAT_R32_UINT,
  [GPUPixelFormatR32Sint]               = DXGI_FORMAT_R32_SINT,
  [GPUPixelFormatR32Float]              = DXGI_FORMAT_R32_FLOAT,
  [GPUPixelFormatRG16Unorm]             = DXGI_FORMAT_R16G16_UNORM,
  [GPUPixelFormatRG16Snorm]             = DXGI_FORMAT_R16G16_SNORM,
  [GPUPixelFormatRG16Uint]              = DXGI_FORMAT_R16G16_UINT,
  [GPUPixelFormatRG16Sint]              = DXGI_FORMAT_R16G16_SINT,
  [GPUPixelFormatRG16Float]             = DXGI_FORMAT_R16G16_FLOAT,
  [GPUPixelFormatRGBA8Unorm]            = DXGI_FORMAT_R8G8B8A8_UNORM,
  [GPUPixelFormatRGBA8Unorm_sRGB]       = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
  [GPUPixelFormatRGBA8Snorm]            = DXGI_FORMAT_R8G8B8A8_SNORM,
  [GPUPixelFormatRGBA8Uint]             = DXGI_FORMAT_R8G8B8A8_UINT,
  [GPUPixelFormatRGBA8Sint]             = DXGI_FORMAT_R8G8B8A8_SINT,
  [GPUPixelFormatBGRA8Unorm]            = DXGI_FORMAT_B8G8R8A8_UNORM,
  [GPUPixelFormatBGRA8Unorm_sRGB]       = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
  [GPUPixelFormatRGB10A2Unorm]          = DXGI_FORMAT_R10G10B10A2_UNORM,
  [GPUPixelFormatRGB10A2Uint]           = DXGI_FORMAT_R10G10B10A2_UINT,
  [GPUPixelFormatRG11B10Float]          = DXGI_FORMAT_R11G11B10_FLOAT,
  [GPUPixelFormatRG32Uint]              = DXGI_FORMAT_R32G32_UINT,
  [GPUPixelFormatRG32Sint]              = DXGI_FORMAT_R32G32_SINT,
  [GPUPixelFormatRG32Float]             = DXGI_FORMAT_R32G32_FLOAT,
  [GPUPixelFormatRGBA16Unorm]           = DXGI_FORMAT_R16G16B16A16_UNORM,
  [GPUPixelFormatRGBA16Snorm]           = DXGI_FORMAT_R16G16B16A16_SNORM,
  [GPUPixelFormatRGBA16Uint]            = DXGI_FORMAT_R16G16B16A16_UINT,
  [GPUPixelFormatRGBA16Sint]            = DXGI_FORMAT_R16G16B16A16_SINT,
  [GPUPixelFormatRGBA16Float]           = DXGI_FORMAT_R16G16B16A16_FLOAT,
  [GPUPixelFormatRGBA32Uint]            = DXGI_FORMAT_R32G32B32A32_UINT,
  [GPUPixelFormatRGBA32Sint]            = DXGI_FORMAT_R32G32B32A32_SINT,
  [GPUPixelFormatRGBA32Float]           = DXGI_FORMAT_R32G32B32A32_FLOAT,
  [GPUPixelFormatBC1_RGBA]              = DXGI_FORMAT_BC1_UNORM,
  [GPUPixelFormatBC1_RGBA_sRGB]         = DXGI_FORMAT_BC1_UNORM_SRGB,
  [GPUPixelFormatBC2_RGBA]              = DXGI_FORMAT_BC2_UNORM,
  [GPUPixelFormatBC2_RGBA_sRGB]         = DXGI_FORMAT_BC2_UNORM_SRGB,
  [GPUPixelFormatBC3_RGBA]              = DXGI_FORMAT_BC3_UNORM,
  [GPUPixelFormatBC3_RGBA_sRGB]         = DXGI_FORMAT_BC3_UNORM_SRGB,
  [GPUPixelFormatBC4_RUnorm]            = DXGI_FORMAT_BC4_UNORM,
  [GPUPixelFormatBC4_RSnorm]            = DXGI_FORMAT_BC4_SNORM,
  [GPUPixelFormatBC5_RGUnorm]           = DXGI_FORMAT_BC5_UNORM,
  [GPUPixelFormatBC5_RGSnorm]           = DXGI_FORMAT_BC5_SNORM,
  [GPUPixelFormatBC6H_RGBFloat]         = DXGI_FORMAT_BC6H_SF16,
  [GPUPixelFormatBC6H_RGBUfloat]        = DXGI_FORMAT_BC6H_UF16,
  [GPUPixelFormatBC7_RGBAUnorm]         = DXGI_FORMAT_BC7_UNORM,
  [GPUPixelFormatBC7_RGBAUnorm_sRGB]    = DXGI_FORMAT_BC7_UNORM_SRGB,
  [GPUPixelFormatDepth32Float]          = DXGI_FORMAT_D32_FLOAT,
  [GPUPixelFormatDepth24Unorm_Stencil8] = DXGI_FORMAT_D24_UNORM_S8_UINT,
  [GPUPixelFormatDepth32Float_Stencil8] = DXGI_FORMAT_D32_FLOAT_S8X24_UINT
};

GPU_HIDE
DXGI_FORMAT
dx12_format(GPUFormat format) {
  if ((uint32_t)format >= GPU_ARRAY_LEN(dx12_formats)) {
    return DXGI_FORMAT_UNKNOWN;
  }

  return dx12_formats[format];
}
