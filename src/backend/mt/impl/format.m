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

static const MTLPixelFormat mt_formats[GPU_FORMAT_COUNT] = {
  [GPU_FORMAT_R8_UNORM]                    = MTLPixelFormatR8Unorm,
  [GPU_FORMAT_R8_SNORM]                    = MTLPixelFormatR8Snorm,
  [GPU_FORMAT_R8_UINT]                     = MTLPixelFormatR8Uint,
  [GPU_FORMAT_R8_SINT]                     = MTLPixelFormatR8Sint,
  [GPU_FORMAT_R16_UNORM]                   = MTLPixelFormatR16Unorm,
  [GPU_FORMAT_R16_SNORM]                   = MTLPixelFormatR16Snorm,
  [GPU_FORMAT_R16_UINT]                    = MTLPixelFormatR16Uint,
  [GPU_FORMAT_R16_SINT]                    = MTLPixelFormatR16Sint,
  [GPU_FORMAT_R16_FLOAT]                   = MTLPixelFormatR16Float,
  [GPU_FORMAT_RG8_UNORM]                   = MTLPixelFormatRG8Unorm,
  [GPU_FORMAT_RG8_SNORM]                   = MTLPixelFormatRG8Snorm,
  [GPU_FORMAT_RG8_UINT]                    = MTLPixelFormatRG8Uint,
  [GPU_FORMAT_RG8_SINT]                    = MTLPixelFormatRG8Sint,
  [GPU_FORMAT_R32_UINT]                    = MTLPixelFormatR32Uint,
  [GPU_FORMAT_R32_SINT]                    = MTLPixelFormatR32Sint,
  [GPU_FORMAT_R32_FLOAT]                   = MTLPixelFormatR32Float,
  [GPU_FORMAT_RG16_UNORM]                  = MTLPixelFormatRG16Unorm,
  [GPU_FORMAT_RG16_SNORM]                  = MTLPixelFormatRG16Snorm,
  [GPU_FORMAT_RG16_UINT]                   = MTLPixelFormatRG16Uint,
  [GPU_FORMAT_RG16_SINT]                   = MTLPixelFormatRG16Sint,
  [GPU_FORMAT_RG16_FLOAT]                  = MTLPixelFormatRG16Float,
  [GPU_FORMAT_RGBA8_UNORM]                 = MTLPixelFormatRGBA8Unorm,
  [GPU_FORMAT_RGBA8_UNORM_SRGB]            = MTLPixelFormatRGBA8Unorm_sRGB,
  [GPU_FORMAT_RGBA8_SNORM]                 = MTLPixelFormatRGBA8Snorm,
  [GPU_FORMAT_RGBA8_UINT]                  = MTLPixelFormatRGBA8Uint,
  [GPU_FORMAT_RGBA8_SINT]                  = MTLPixelFormatRGBA8Sint,
  [GPU_FORMAT_BGRA8_UNORM]                 = MTLPixelFormatBGRA8Unorm,
  [GPU_FORMAT_BGRA8_UNORM_SRGB]            = MTLPixelFormatBGRA8Unorm_sRGB,
  [GPU_FORMAT_RGB10A2_UNORM]               = MTLPixelFormatRGB10A2Unorm,
  [GPU_FORMAT_RGB10A2_UINT]                = MTLPixelFormatRGB10A2Uint,
  [GPU_FORMAT_RG11B10_UFLOAT]              = MTLPixelFormatRG11B10Float,
  [GPU_FORMAT_RGB9E5_UFLOAT]               = MTLPixelFormatRGB9E5Float,
  [GPU_FORMAT_RG32_UINT]                   = MTLPixelFormatRG32Uint,
  [GPU_FORMAT_RG32_SINT]                   = MTLPixelFormatRG32Sint,
  [GPU_FORMAT_RG32_FLOAT]                  = MTLPixelFormatRG32Float,
  [GPU_FORMAT_RGBA16_UNORM]                = MTLPixelFormatRGBA16Unorm,
  [GPU_FORMAT_RGBA16_SNORM]                = MTLPixelFormatRGBA16Snorm,
  [GPU_FORMAT_RGBA16_UINT]                 = MTLPixelFormatRGBA16Uint,
  [GPU_FORMAT_RGBA16_SINT]                 = MTLPixelFormatRGBA16Sint,
  [GPU_FORMAT_RGBA16_FLOAT]                = MTLPixelFormatRGBA16Float,
  [GPU_FORMAT_RGBA32_UINT]                 = MTLPixelFormatRGBA32Uint,
  [GPU_FORMAT_RGBA32_SINT]                 = MTLPixelFormatRGBA32Sint,
  [GPU_FORMAT_RGBA32_FLOAT]                = MTLPixelFormatRGBA32Float,
#if !TARGET_OS_IOS
  [GPU_FORMAT_BC1_RGBA_UNORM]              = MTLPixelFormatBC1_RGBA,
  [GPU_FORMAT_BC1_RGBA_UNORM_SRGB]         = MTLPixelFormatBC1_RGBA_sRGB,
  [GPU_FORMAT_BC2_RGBA_UNORM]              = MTLPixelFormatBC2_RGBA,
  [GPU_FORMAT_BC2_RGBA_UNORM_SRGB]         = MTLPixelFormatBC2_RGBA_sRGB,
  [GPU_FORMAT_BC3_RGBA_UNORM]              = MTLPixelFormatBC3_RGBA,
  [GPU_FORMAT_BC3_RGBA_UNORM_SRGB]         = MTLPixelFormatBC3_RGBA_sRGB,
  [GPU_FORMAT_BC4_R_UNORM]                 = MTLPixelFormatBC4_RUnorm,
  [GPU_FORMAT_BC4_R_SNORM]                 = MTLPixelFormatBC4_RSnorm,
  [GPU_FORMAT_BC5_RG_UNORM]                = MTLPixelFormatBC5_RGUnorm,
  [GPU_FORMAT_BC5_RG_SNORM]                = MTLPixelFormatBC5_RGSnorm,
  [GPU_FORMAT_BC6H_RGB_FLOAT]              = MTLPixelFormatBC6H_RGBFloat,
  [GPU_FORMAT_BC6H_RGB_UFLOAT]             = MTLPixelFormatBC6H_RGBUfloat,
  [GPU_FORMAT_BC7_RGBA_UNORM]              = MTLPixelFormatBC7_RGBAUnorm,
  [GPU_FORMAT_BC7_RGBA_UNORM_SRGB]         = MTLPixelFormatBC7_RGBAUnorm_sRGB,
#endif
  [GPU_FORMAT_EAC_R11_UNORM]               = MTLPixelFormatEAC_R11Unorm,
  [GPU_FORMAT_EAC_R11_SNORM]               = MTLPixelFormatEAC_R11Snorm,
  [GPU_FORMAT_EAC_RG11_UNORM]              = MTLPixelFormatEAC_RG11Unorm,
  [GPU_FORMAT_EAC_RG11_SNORM]              = MTLPixelFormatEAC_RG11Snorm,
  [GPU_FORMAT_ETC2_RGBA8_UNORM]             = MTLPixelFormatEAC_RGBA8,
  [GPU_FORMAT_ETC2_RGBA8_UNORM_SRGB]        = MTLPixelFormatEAC_RGBA8_sRGB,
  [GPU_FORMAT_ETC2_RGB8_UNORM]             = MTLPixelFormatETC2_RGB8,
  [GPU_FORMAT_ETC2_RGB8_UNORM_SRGB]        = MTLPixelFormatETC2_RGB8_sRGB,
  [GPU_FORMAT_ETC2_RGB8A1_UNORM]           = MTLPixelFormatETC2_RGB8A1,
  [GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB]      = MTLPixelFormatETC2_RGB8A1_sRGB,
  [GPU_FORMAT_ASTC_4X4_UNORM]              = MTLPixelFormatASTC_4x4_LDR,
  [GPU_FORMAT_ASTC_4X4_UNORM_SRGB]         = MTLPixelFormatASTC_4x4_sRGB,
  [GPU_FORMAT_ASTC_5X4_UNORM]              = MTLPixelFormatASTC_5x4_LDR,
  [GPU_FORMAT_ASTC_5X4_UNORM_SRGB]         = MTLPixelFormatASTC_5x4_sRGB,
  [GPU_FORMAT_ASTC_5X5_UNORM]              = MTLPixelFormatASTC_5x5_LDR,
  [GPU_FORMAT_ASTC_5X5_UNORM_SRGB]         = MTLPixelFormatASTC_5x5_sRGB,
  [GPU_FORMAT_ASTC_6X5_UNORM]              = MTLPixelFormatASTC_6x5_LDR,
  [GPU_FORMAT_ASTC_6X5_UNORM_SRGB]         = MTLPixelFormatASTC_6x5_sRGB,
  [GPU_FORMAT_ASTC_6X6_UNORM]              = MTLPixelFormatASTC_6x6_LDR,
  [GPU_FORMAT_ASTC_6X6_UNORM_SRGB]         = MTLPixelFormatASTC_6x6_sRGB,
  [GPU_FORMAT_ASTC_8X5_UNORM]              = MTLPixelFormatASTC_8x5_LDR,
  [GPU_FORMAT_ASTC_8X5_UNORM_SRGB]         = MTLPixelFormatASTC_8x5_sRGB,
  [GPU_FORMAT_ASTC_8X6_UNORM]              = MTLPixelFormatASTC_8x6_LDR,
  [GPU_FORMAT_ASTC_8X6_UNORM_SRGB]         = MTLPixelFormatASTC_8x6_sRGB,
  [GPU_FORMAT_ASTC_8X8_UNORM]              = MTLPixelFormatASTC_8x8_LDR,
  [GPU_FORMAT_ASTC_8X8_UNORM_SRGB]         = MTLPixelFormatASTC_8x8_sRGB,
  [GPU_FORMAT_ASTC_10X5_UNORM]             = MTLPixelFormatASTC_10x5_LDR,
  [GPU_FORMAT_ASTC_10X5_UNORM_SRGB]        = MTLPixelFormatASTC_10x5_sRGB,
  [GPU_FORMAT_ASTC_10X6_UNORM]             = MTLPixelFormatASTC_10x6_LDR,
  [GPU_FORMAT_ASTC_10X6_UNORM_SRGB]        = MTLPixelFormatASTC_10x6_sRGB,
  [GPU_FORMAT_ASTC_10X8_UNORM]             = MTLPixelFormatASTC_10x8_LDR,
  [GPU_FORMAT_ASTC_10X8_UNORM_SRGB]        = MTLPixelFormatASTC_10x8_sRGB,
  [GPU_FORMAT_ASTC_10X10_UNORM]            = MTLPixelFormatASTC_10x10_LDR,
  [GPU_FORMAT_ASTC_10X10_UNORM_SRGB]       = MTLPixelFormatASTC_10x10_sRGB,
  [GPU_FORMAT_ASTC_12X10_UNORM]            = MTLPixelFormatASTC_12x10_LDR,
  [GPU_FORMAT_ASTC_12X10_UNORM_SRGB]       = MTLPixelFormatASTC_12x10_sRGB,
  [GPU_FORMAT_ASTC_12X12_UNORM]            = MTLPixelFormatASTC_12x12_LDR,
  [GPU_FORMAT_ASTC_12X12_UNORM_SRGB]       = MTLPixelFormatASTC_12x12_sRGB,
  [GPU_FORMAT_DEPTH16_UNORM]               = MTLPixelFormatDepth16Unorm,
  [GPU_FORMAT_STENCIL8]                    = MTLPixelFormatStencil8,
#if !TARGET_OS_IOS
  [GPU_FORMAT_DEPTH24_UNORM_STENCIL8]      = MTLPixelFormatDepth24Unorm_Stencil8,
#endif
  [GPU_FORMAT_DEPTH32_FLOAT]               = MTLPixelFormatDepth32Float,
  [GPU_FORMAT_DEPTH32_FLOAT_STENCIL8]      = MTLPixelFormatDepth32Float_Stencil8
};

#if TARGET_OS_IOS
static MTLPixelFormat
mt_bcFormat(GPUFormat format) {
  if (format < GPU_FORMAT_BC1_RGBA_UNORM ||
      format > GPU_FORMAT_BC7_RGBA_UNORM_SRGB) {
    return MTLPixelFormatInvalid;
  }

  if (@available(iOS 16.4, *)) {
    static const MTLPixelFormat formats[] = {
      MTLPixelFormatBC1_RGBA,
      MTLPixelFormatBC1_RGBA_sRGB,
      MTLPixelFormatBC2_RGBA,
      MTLPixelFormatBC2_RGBA_sRGB,
      MTLPixelFormatBC3_RGBA,
      MTLPixelFormatBC3_RGBA_sRGB,
      MTLPixelFormatBC4_RUnorm,
      MTLPixelFormatBC4_RSnorm,
      MTLPixelFormatBC5_RGUnorm,
      MTLPixelFormatBC5_RGSnorm,
      MTLPixelFormatBC6H_RGBFloat,
      MTLPixelFormatBC6H_RGBUfloat,
      MTLPixelFormatBC7_RGBAUnorm,
      MTLPixelFormatBC7_RGBAUnorm_sRGB
    };
    _Static_assert(GPU_ARRAY_LEN(formats) ==
                     GPU_FORMAT_BC7_RGBA_UNORM_SRGB -
                     GPU_FORMAT_BC1_RGBA_UNORM + 1u,
                   "BC format table is incomplete");

    return formats[format - GPU_FORMAT_BC1_RGBA_UNORM];
  }

  return MTLPixelFormatInvalid;
}
#endif

GPU_HIDE
MTLPixelFormat
mt_format(GPUFormat format) {
  if (format <= GPU_FORMAT_UNDEFINED || format >= GPU_FORMAT_COUNT) {
    return MTLPixelFormatInvalid;
  }
#if TARGET_OS_IOS
  if (format >= GPU_FORMAT_BC1_RGBA_UNORM &&
      format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB) {
    return mt_bcFormat(format);
  }
#endif
  return mt_formats[format];
}

GPU_HIDE
GPUFormat
mt_formatFromNative(MTLPixelFormat format) {
  if (format == MTLPixelFormatInvalid) {
    return GPU_FORMAT_UNDEFINED;
  }
  for (uint32_t i = 1u; i < GPU_FORMAT_COUNT; i++) {
    if (mt_format((GPUFormat)i) == format) {
      return (GPUFormat)i;
    }
  }
  return GPU_FORMAT_UNDEFINED;
}
