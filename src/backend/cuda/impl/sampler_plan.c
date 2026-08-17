/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../sampler_plan.h"

#include <string.h>

#include <us/compiler.h>

static bool
cuda__addressMode(GPUAddressMode mode, CUaddress_mode *outMode) {
  if (!outMode) {
    return false;
  }
  switch (mode) {
    case GPU_ADDRESS_MODE_REPEAT:
      *outMode = CU_TR_ADDRESS_MODE_WRAP;
      return true;
    case GPU_ADDRESS_MODE_MIRRORED_REPEAT:
      *outMode = CU_TR_ADDRESS_MODE_MIRROR;
      return true;
    case GPU_ADDRESS_MODE_CLAMP_TO_EDGE:
      *outMode = CU_TR_ADDRESS_MODE_CLAMP;
      return true;
    default:
      return false;
  }
}

static bool
cuda__staticAddressMode(uint32_t mode, CUaddress_mode *outMode) {
  if (!outMode) {
    return false;
  }
  switch (mode) {
    case USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE:
      *outMode = CU_TR_ADDRESS_MODE_CLAMP;
      return true;
    case USL_RUNTIME_ADDRESS_REPEAT:
      *outMode = CU_TR_ADDRESS_MODE_WRAP;
      return true;
    case USL_RUNTIME_ADDRESS_MIRRORED_REPEAT:
      *outMode = CU_TR_ADDRESS_MODE_MIRROR;
      return true;
    case USL_RUNTIME_ADDRESS_CLAMP_TO_ZERO:
    case USL_RUNTIME_ADDRESS_CLAMP_TO_BORDER:
      *outMode = CU_TR_ADDRESS_MODE_BORDER;
      return true;
    default:
      return false;
  }
}

bool
cuda_samplerTextureDesc(const GPUSamplerDesc *source,
                        CUDA_TEXTURE_DESC    *outDesc) {
  CUaddress_mode addressU, addressV, addressW;

  if (outDesc) {
    memset(outDesc, 0, sizeof(*outDesc));
  }
  if (!source || !outDesc ||
      (source->minFilter != GPU_FILTER_NEAREST &&
       source->minFilter != GPU_FILTER_LINEAR) ||
      source->magFilter != source->minFilter ||
      (source->mipFilter != GPU_MIP_FILTER_NEAREST &&
       source->mipFilter != GPU_MIP_FILTER_LINEAR) ||
      (uint32_t)source->compare > GPU_COMPARE_ALWAYS ||
      source->compareEnable ||
      source->maxAnisotropy > CUDA_MAX_SAMPLER_ANISOTROPY ||
      (source->maxAnisotropy > 1u &&
       (source->minFilter != GPU_FILTER_LINEAR ||
        source->mipFilter != GPU_MIP_FILTER_LINEAR)) ||
      !cuda__addressMode(source->addressU, &addressU) ||
      !cuda__addressMode(source->addressV, &addressV) ||
      !cuda__addressMode(source->addressW, &addressW)) {
    return false;
  }

  outDesc->addressMode[0]   = addressU;
  outDesc->addressMode[1]   = addressV;
  outDesc->addressMode[2]   = addressW;
  outDesc->filterMode       = source->minFilter == GPU_FILTER_LINEAR
                                ? CU_TR_FILTER_MODE_LINEAR
                                : CU_TR_FILTER_MODE_POINT;
  outDesc->mipmapFilterMode = source->mipFilter == GPU_MIP_FILTER_LINEAR
                                ? CU_TR_FILTER_MODE_LINEAR
                                : CU_TR_FILTER_MODE_POINT;
  outDesc->flags            = CU_TRSF_NORMALIZED_COORDINATES;
  outDesc->maxAnisotropy    = source->maxAnisotropy > 1u
                                ? source->maxAnisotropy
                                : 1u;
  return true;
}

bool
cuda_staticSamplerTextureDesc(const GPUStaticSamplerDesc *source,
                              CUDA_TEXTURE_DESC          *outDesc) {
  CUaddress_mode addressMode;

  if (outDesc) {
    memset(outDesc, 0, sizeof(*outDesc));
  }
  if (!source || !outDesc ||
      source->minFilter > USL_RUNTIME_FILTER_LINEAR ||
      source->magFilter != source->minFilter ||
      source->mipFilter > USL_RUNTIME_FILTER_LINEAR ||
      source->coordSpace > USL_RUNTIME_COORD_PIXEL ||
      source->compareFunc > USL_RUNTIME_COMPARE_ALWAYS ||
      source->hasCompare > 1u || source->hasCompare ||
      source->maxAnisotropy > CUDA_MAX_SAMPLER_ANISOTROPY ||
      (source->maxAnisotropy > 1u &&
       (source->minFilter != USL_RUNTIME_FILTER_LINEAR ||
        source->mipFilter != USL_RUNTIME_FILTER_LINEAR)) ||
      (source->coordSpace == USL_RUNTIME_COORD_PIXEL &&
       source->addressMode != USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE) ||
      !cuda__staticAddressMode(source->addressMode, &addressMode)) {
    return false;
  }

  outDesc->addressMode[0]   = addressMode;
  outDesc->addressMode[1]   = addressMode;
  outDesc->addressMode[2]   = addressMode;
  outDesc->filterMode       = source->minFilter == USL_RUNTIME_FILTER_LINEAR
                                ? CU_TR_FILTER_MODE_LINEAR
                                : CU_TR_FILTER_MODE_POINT;
  outDesc->mipmapFilterMode = source->mipFilter == USL_RUNTIME_FILTER_LINEAR
                                ? CU_TR_FILTER_MODE_LINEAR
                                : CU_TR_FILTER_MODE_POINT;
  outDesc->flags            = source->coordSpace == USL_RUNTIME_COORD_NORMALIZED
                                ? CU_TRSF_NORMALIZED_COORDINATES
                                : 0u;
  outDesc->maxAnisotropy    = source->maxAnisotropy > 1u
                                ? source->maxAnisotropy
                                : 1u;
  return true;
}

const CUDA_TEXTURE_DESC *
cuda_exactTextureDesc(void) {
  static const CUDA_TEXTURE_DESC desc = {
    .addressMode       = {
      CU_TR_ADDRESS_MODE_CLAMP,
      CU_TR_ADDRESS_MODE_CLAMP,
      CU_TR_ADDRESS_MODE_CLAMP
    },
    .filterMode       = CU_TR_FILTER_MODE_POINT,
    .maxAnisotropy    = 1u,
    .mipmapFilterMode = CU_TR_FILTER_MODE_POINT
  };

  return &desc;
}
