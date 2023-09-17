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

#ifndef gpu_pixelformat_h
#define gpu_pixelformat_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef enum GPUPixelFormat {
  GPUPixelFormatInvalid                = 0,

  /* Normal 8 bit formats */
  GPUPixelFormatA8Unorm                = 1,
  GPUPixelFormatR8Unorm                = 10,
  GPUPixelFormatR8Unorm_sRGB           = 11,
  GPUPixelFormatR8Snorm                = 12,
  GPUPixelFormatR8Uint                 = 13,
  GPUPixelFormatR8Sint                 = 14,

  /* Normal 16 bit formats */
  GPUPixelFormatR16Unorm               = 20,
  GPUPixelFormatR16Snorm               = 22,
  GPUPixelFormatR16Uint                = 23,
  GPUPixelFormatR16Sint                = 24,
  GPUPixelFormatR16Float               = 25,

  GPUPixelFormatRG8Unorm               = 30,
  GPUPixelFormatRG8Unorm_sRGB          = 31,
  GPUPixelFormatRG8Snorm               = 32,
  GPUPixelFormatRG8Uint                = 33,
  GPUPixelFormatRG8Sint                = 34,

  /* Packed 16 bit formats */
  GPUPixelFormatB5G6R5Unorm            = 40,
  GPUPixelFormatA1BGR5Unorm            = 41,
  GPUPixelFormatABGR4Unorm             = 42,
  GPUPixelFormatBGR5A1Unorm            = 43,

  /* Normal 32 bit formats */
  GPUPixelFormatR32Uint                = 53,
  GPUPixelFormatR32Sint                = 54,
  GPUPixelFormatR32Float               = 55,
  GPUPixelFormatRG16Unorm              = 60,
  GPUPixelFormatRG16Snorm              = 62,
  GPUPixelFormatRG16Uint               = 63,
  GPUPixelFormatRG16Sint               = 64,
  GPUPixelFormatRG16Float              = 65,
  GPUPixelFormatRGBA8Unorm             = 70,
  GPUPixelFormatRGBA8Unorm_sRGB        = 71,
  GPUPixelFormatRGBA8Snorm             = 72,
  GPUPixelFormatRGBA8Uint              = 73,
  GPUPixelFormatRGBA8Sint              = 74,
  GPUPixelFormatBGRX8Unorm             = 75,
  GPUPixelFormatBGRA8Unorm             = 80,
  GPUPixelFormatBGRA8Unorm_sRGB        = 81,

  /* Packed 32 bit formats */
  GPUPixelFormatRGB10A2Unorm           = 90,
  GPUPixelFormatRGB10A2Uint            = 91,
  GPUPixelFormatRG11B10Float           = 92,
  GPUPixelFormatRGB9E5Float            = 93,
  GPUPixelFormatBGR10A2Unorm           = 94,
  GPUPixelFormatBGR10_XR               = 554,
  GPUPixelFormatBGR10_XR_sRGB          = 555,

  /* Normal 64 bit formats */

  GPUPixelFormatRG32Uint               = 103,
  GPUPixelFormatRG32Sint               = 104,
  GPUPixelFormatRG32Float              = 105,
  GPUPixelFormatRGBA16Unorm            = 110,
  GPUPixelFormatRGBA16Snorm            = 112,
  GPUPixelFormatRGBA16Uint             = 113,
  GPUPixelFormatRGBA16Sint             = 114,
  GPUPixelFormatRGBA16Float            = 115,
  GPUPixelFormatBGRA10_XR              = 552,
  GPUPixelFormatBGRA10_XR_sRGB         = 553,

  /* Normal 128 bit formats */

  GPUPixelFormatRGBA32Uint             = 123,
  GPUPixelFormatRGBA32Sint             = 124,
  GPUPixelFormatRGBA32Float            = 125,

  /* Compressed formats. */

  /* S3TC/DXT */
  GPUPixelFormatBC1_RGBA               = 130,
  GPUPixelFormatBC1_RGBA_sRGB          = 131,
  GPUPixelFormatBC2_RGBA               = 132,
  GPUPixelFormatBC2_RGBA_sRGB          = 133,
  GPUPixelFormatBC3_RGBA               = 134,
  GPUPixelFormatBC3_RGBA_sRGB          = 135,

  /* RGTC */
  GPUPixelFormatBC4_RUnorm             = 140,
  GPUPixelFormatBC4_RSnorm             = 141,
  GPUPixelFormatBC5_RGUnorm            = 142,
  GPUPixelFormatBC5_RGSnorm            = 143,

  /* BPTC */
  GPUPixelFormatBC6H_RGBFloat          = 150,
  GPUPixelFormatBC6H_RGBUfloat         = 151,
  GPUPixelFormatBC7_RGBAUnorm          = 152,
  GPUPixelFormatBC7_RGBAUnorm_sRGB     = 153,

  /* PVRTC */
  GPUPixelFormatPVRTC_RGB_2BPP         = 160,
  GPUPixelFormatPVRTC_RGB_2BPP_sRGB    = 161,
  GPUPixelFormatPVRTC_RGB_4BPP         = 162,
  GPUPixelFormatPVRTC_RGB_4BPP_sRGB    = 163,
  GPUPixelFormatPVRTC_RGBA_2BPP        = 164,
  GPUPixelFormatPVRTC_RGBA_2BPP_sRGB   = 165,
  GPUPixelFormatPVRTC_RGBA_4BPP        = 166,
  GPUPixelFormatPVRTC_RGBA_4BPP_sRGB   = 167,

  /* ETC2 */
  GPUPixelFormatEAC_R11Unorm           = 170,
  GPUPixelFormatEAC_R11Snorm           = 172,
  GPUPixelFormatEAC_RG11Unorm          = 174,
  GPUPixelFormatEAC_RG11Snorm          = 176,
  GPUPixelFormatEAC_RGBA8              = 178,
  GPUPixelFormatEAC_RGBA8_sRGB         = 179,

  GPUPixelFormatETC2_RGB8              = 180,
  GPUPixelFormatETC2_RGB8_sRGB         = 181,
  GPUPixelFormatETC2_RGB8A1            = 182,
  GPUPixelFormatETC2_RGB8A1_sRGB       = 183,

  /* ASTC */
  GPUPixelFormatASTC_4x4_sRGB          = 186,
  GPUPixelFormatASTC_5x4_sRGB          = 187,
  GPUPixelFormatASTC_5x5_sRGB          = 188,
  GPUPixelFormatASTC_6x5_sRGB          = 189,
  GPUPixelFormatASTC_6x6_sRGB          = 190,
  GPUPixelFormatASTC_8x5_sRGB          = 192,
  GPUPixelFormatASTC_8x6_sRGB          = 193,
  GPUPixelFormatASTC_8x8_sRGB          = 194,
  GPUPixelFormatASTC_10x5_sRGB         = 195,
  GPUPixelFormatASTC_10x6_sRGB         = 196,
  GPUPixelFormatASTC_10x8_sRGB         = 197,
  GPUPixelFormatASTC_10x10_sRGB        = 198,
  GPUPixelFormatASTC_12x10_sRGB        = 199,
  GPUPixelFormatASTC_12x12_sRGB        = 200,

  GPUPixelFormatASTC_4x4_LDR           = 204,
  GPUPixelFormatASTC_5x4_LDR           = 205,
  GPUPixelFormatASTC_5x5_LDR           = 206,
  GPUPixelFormatASTC_6x5_LDR           = 207,
  GPUPixelFormatASTC_6x6_LDR           = 208,
  GPUPixelFormatASTC_8x5_LDR           = 210,
  GPUPixelFormatASTC_8x6_LDR           = 211,
  GPUPixelFormatASTC_8x8_LDR           = 212,
  GPUPixelFormatASTC_10x5_LDR          = 213,
  GPUPixelFormatASTC_10x6_LDR          = 214,
  GPUPixelFormatASTC_10x8_LDR          = 215,
  GPUPixelFormatASTC_10x10_LDR         = 216,
  GPUPixelFormatASTC_12x10_LDR         = 217,
  GPUPixelFormatASTC_12x12_LDR         = 218,
  GPUPixelFormatGBGR422                = 240,
  GPUPixelFormatBGRG422                = 241,

  /* Depth */
  GPUPixelFormatDepth16Unorm           = 250,
  GPUPixelFormatDepth32Float           = 252,

  /* Stencil */
  GPUPixelFormatStencil8               = 253,

  /* Depth Stencil */
  GPUPixelFormatDepth24Unorm_Stencil8  = 255,
  GPUPixelFormatDepth32Float_Stencil8  = 260,
  GPUPixelFormatX32_Stencil8           = 261,
  GPUPixelFormatX24_Stencil8           = 262
} GPUPixelFormat;

#ifdef __cplusplus
}
#endif
#endif /* gpu_pixelformat_h */
