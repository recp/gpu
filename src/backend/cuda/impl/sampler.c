/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

#include <us/compiler.h>

bool
cuda_samplerDescSupported(const GPUSamplerDesc *desc) {
  return desc &&
         desc->minFilter == desc->magFilter &&
         !desc->compareEnable;
}

bool
cuda_staticSamplerDescSupported(const GPUStaticSamplerDesc *desc) {
  if (!gpuStaticSamplerDescIsValid(desc) ||
      desc->minFilter != desc->magFilter ||
      desc->hasCompare || desc->maxAnisotropy > 16u) {
    return false;
  }
  if (desc->coordSpace == USL_RUNTIME_COORD_PIXEL &&
      desc->addressMode != USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE) {
    return false;
  }
  return true;
}

static GPUResult
cuda_createSampler(GPUApi                    *__restrict api,
                   GPUDevice                 *__restrict device,
                   const GPUSamplerCreateInfo *info,
                   bool                       staticIfSupported,
                   GPUSampler                **outSampler) {
  GPUSampler *sampler;

  GPU__UNUSED(api);
  GPU__UNUSED(device);
  GPU__UNUSED(staticIfSupported);
  if (!info || !outSampler || !cuda_samplerDescSupported(&info->desc)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  sampler = calloc(1, sizeof(*sampler));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  *outSampler = sampler;
  return GPU_OK;
}

static void
cuda_destroySampler(GPUSampler *__restrict sampler) {
  free(sampler);
}

void
cuda_initSampler(GPUApiSampler *api) {
  api->createSampler  = cuda_createSampler;
  api->destroySampler = cuda_destroySampler;
}
