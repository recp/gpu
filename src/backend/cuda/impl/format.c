/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../format.h"

#include <string.h>

#define CUDA_FORMAT(NATIVE, FLAGS, BYTES, CHANNELS) \
  {NATIVE, FLAGS, BYTES, CHANNELS}

#define CUDA_FLOAT_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_FILTERABLE_BIT | \
   GPU_CUDA_FORMAT_STORAGE_BIT)

#define CUDA_INTEGER_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_STORAGE_BIT | \
   GPU_CUDA_FORMAT_READ_AS_INTEGER_BIT)

#define CUDA_STORAGE_FLAGS GPU_CUDA_FORMAT_STORAGE_BIT

#define CUDA_SRGB_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_FILTERABLE_BIT | \
   GPU_CUDA_FORMAT_SRGB_BIT)

static const GPUCudaFormatInfo cuda_formats[GPU_FORMAT_COUNT] = {
  [GPU_FORMAT_R8_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_FLOAT_FLAGS, 1u, 1u),
  [GPU_FORMAT_R8_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_FLOAT_FLAGS, 1u, 1u),
  [GPU_FORMAT_R8_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_INTEGER_FLAGS, 1u, 1u),
  [GPU_FORMAT_R8_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_INTEGER_FLAGS, 1u, 1u),

  [GPU_FORMAT_R16_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_FLOAT_FLAGS, 2u, 1u),
  [GPU_FORMAT_R16_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_FLOAT_FLAGS, 2u, 1u),
  [GPU_FORMAT_R16_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_INTEGER_FLAGS, 2u, 1u),
  [GPU_FORMAT_R16_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_INTEGER_FLAGS, 2u, 1u),
  [GPU_FORMAT_R16_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_HALF, CUDA_FLOAT_FLAGS, 2u, 1u),

  [GPU_FORMAT_RG8_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_FLOAT_FLAGS, 2u, 2u),
  [GPU_FORMAT_RG8_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_FLOAT_FLAGS, 2u, 2u),
  [GPU_FORMAT_RG8_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_INTEGER_FLAGS, 2u, 2u),
  [GPU_FORMAT_RG8_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_INTEGER_FLAGS, 2u, 2u),

  [GPU_FORMAT_R32_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT32, CUDA_INTEGER_FLAGS, 4u, 1u),
  [GPU_FORMAT_R32_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT32, CUDA_INTEGER_FLAGS, 4u, 1u),
  [GPU_FORMAT_R32_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_FLOAT, CUDA_FLOAT_FLAGS, 4u, 1u),

  [GPU_FORMAT_RG16_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_FLOAT_FLAGS, 4u, 2u),
  [GPU_FORMAT_RG16_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_FLOAT_FLAGS, 4u, 2u),
  [GPU_FORMAT_RG16_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_INTEGER_FLAGS, 4u, 2u),
  [GPU_FORMAT_RG16_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_INTEGER_FLAGS, 4u, 2u),
  [GPU_FORMAT_RG16_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_HALF, CUDA_FLOAT_FLAGS, 4u, 2u),

  [GPU_FORMAT_RGBA8_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_FLOAT_FLAGS, 4u, 4u),
  [GPU_FORMAT_RGBA8_UNORM_SRGB] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_SRGB_FLAGS, 4u, 4u),
  [GPU_FORMAT_RGBA8_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_FLOAT_FLAGS, 4u, 4u),
  [GPU_FORMAT_RGBA8_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_INTEGER_FLAGS, 4u, 4u),
  [GPU_FORMAT_RGBA8_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT8, CUDA_INTEGER_FLAGS, 4u, 4u),

  [GPU_FORMAT_BGRA8_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT8, CUDA_STORAGE_FLAGS, 4u, 4u),

  [GPU_FORMAT_RG32_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT32, CUDA_INTEGER_FLAGS, 8u, 2u),
  [GPU_FORMAT_RG32_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT32, CUDA_INTEGER_FLAGS, 8u, 2u),
  [GPU_FORMAT_RG32_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_FLOAT, CUDA_FLOAT_FLAGS, 8u, 2u),

  [GPU_FORMAT_RGBA16_UNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_FLOAT_FLAGS, 8u, 4u),
  [GPU_FORMAT_RGBA16_SNORM] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_FLOAT_FLAGS, 8u, 4u),
  [GPU_FORMAT_RGBA16_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT16, CUDA_INTEGER_FLAGS, 8u, 4u),
  [GPU_FORMAT_RGBA16_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT16, CUDA_INTEGER_FLAGS, 8u, 4u),
  [GPU_FORMAT_RGBA16_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_HALF, CUDA_FLOAT_FLAGS, 8u, 4u),

  [GPU_FORMAT_RGBA32_UINT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT32, CUDA_INTEGER_FLAGS, 16u, 4u),
  [GPU_FORMAT_RGBA32_SINT] =
    CUDA_FORMAT(CU_AD_FORMAT_SIGNED_INT32, CUDA_INTEGER_FLAGS, 16u, 4u),
  [GPU_FORMAT_RGBA32_FLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_FLOAT, CUDA_FLOAT_FLAGS, 16u, 4u),

  [GPU_FORMAT_RG11B10_UFLOAT] =
    CUDA_FORMAT(CU_AD_FORMAT_UNSIGNED_INT32, CUDA_STORAGE_FLAGS, 4u, 1u)
};

_Static_assert(GPU_ARRAY_LEN(cuda_formats) == GPU_FORMAT_COUNT,
               "CUDA format table must cover GPUFormat");

GPU_HIDE
bool
cuda_formatInfo(GPUFormat format, GPUCudaFormatInfo *outInfo) {
  GPUCudaFormatInfo info;

  if (!outInfo) {
    return false;
  }
  memset(outInfo, 0, sizeof(*outInfo));
  if (format <= GPU_FORMAT_UNDEFINED || format >= GPU_FORMAT_COUNT) {
    return false;
  }

  info = cuda_formats[format];
  if (info.bytesPerTexel == 0u ||
      (info.channelCount != 1u && info.channelCount != 2u &&
       info.channelCount != 4u)) {
    return false;
  }

  *outInfo = info;
  return true;
}

GPU_HIDE
bool
cuda_formatResourceView(const GPUCudaFormatInfo *format,
                        CUresourceViewFormat    *outFormat) {
  CUresourceViewFormat base;
  uint32_t             channelOffset;

  if (outFormat) {
    *outFormat = CU_RES_VIEW_FORMAT_NONE;
  }
  if (!format || !outFormat) {
    return false;
  }
  switch (format->channelCount) {
    case 1u: channelOffset = 0u; break;
    case 2u: channelOffset = 1u; break;
    case 4u: channelOffset = 2u; break;
    default: return false;
  }
  switch (format->arrayFormat) {
    case CU_AD_FORMAT_UNSIGNED_INT8:  base = CU_RES_VIEW_FORMAT_UINT_1X8; break;
    case CU_AD_FORMAT_SIGNED_INT8:    base = CU_RES_VIEW_FORMAT_SINT_1X8; break;
    case CU_AD_FORMAT_UNSIGNED_INT16: base = CU_RES_VIEW_FORMAT_UINT_1X16; break;
    case CU_AD_FORMAT_SIGNED_INT16:   base = CU_RES_VIEW_FORMAT_SINT_1X16; break;
    case CU_AD_FORMAT_UNSIGNED_INT32: base = CU_RES_VIEW_FORMAT_UINT_1X32; break;
    case CU_AD_FORMAT_SIGNED_INT32:   base = CU_RES_VIEW_FORMAT_SINT_1X32; break;
    case CU_AD_FORMAT_HALF:           base = CU_RES_VIEW_FORMAT_FLOAT_1X16; break;
    case CU_AD_FORMAT_FLOAT:          base = CU_RES_VIEW_FORMAT_FLOAT_1X32; break;
    default: return false;
  }
  *outFormat = (CUresourceViewFormat)((uint32_t)base + channelOffset);
  return true;
}

GPU_HIDE
bool
cuda_formatTextureDesc(const GPUCudaFormatInfo *format,
                       const CUDA_TEXTURE_DESC *source,
                       CUDA_TEXTURE_DESC       *outDesc) {
  CUDA_TEXTURE_DESC desc;

  if (!outDesc) {
    return false;
  }
  memset(outDesc, 0, sizeof(*outDesc));
  if (!format || !source ||
      (format->flags & GPU_CUDA_FORMAT_SAMPLED_BIT) == 0u) {
    return false;
  }

  desc        = *source;
  desc.flags &= ~(CU_TRSF_READ_AS_INTEGER | CU_TRSF_SRGB);
  if ((format->flags & GPU_CUDA_FORMAT_READ_AS_INTEGER_BIT) != 0u) {
    desc.flags |= CU_TRSF_READ_AS_INTEGER;
  }
  if ((format->flags & GPU_CUDA_FORMAT_SRGB_BIT) != 0u) {
    desc.flags |= CU_TRSF_SRGB;
  }
  if ((format->flags & GPU_CUDA_FORMAT_FILTERABLE_BIT) == 0u &&
      (desc.filterMode == CU_TR_FILTER_MODE_LINEAR ||
       desc.mipmapFilterMode == CU_TR_FILTER_MODE_LINEAR ||
       desc.maxAnisotropy > 1u)) {
    return false;
  }

  *outDesc = desc;
  return true;
}

#undef CUDA_SRGB_FLAGS
#undef CUDA_STORAGE_FLAGS
#undef CUDA_INTEGER_FLAGS
#undef CUDA_FLOAT_FLAGS
#undef CUDA_FORMAT
