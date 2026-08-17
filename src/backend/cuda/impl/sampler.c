/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUResult
cuda_createSampler(GPUApi                    *__restrict api,
                   GPUDevice                 *__restrict device,
                   const GPUSamplerCreateInfo *info,
                   bool                       staticIfSupported,
                   GPUSampler                **outSampler) {
  GPUSamplerCuda    *native;
  GPUSampler        *sampler;
  CUDA_TEXTURE_DESC  desc;

  GPU__UNUSED(api);
  GPU__UNUSED(device);
  GPU__UNUSED(staticIfSupported);
  if (!info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSampler = NULL;
  if (!cuda_samplerTextureDesc(&info->desc, &desc)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  sampler = calloc(1, sizeof(*sampler) + sizeof(*native));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native         = (GPUSamplerCuda *)(sampler + 1);
  native->desc   = desc;
  sampler->_priv = native;
  *outSampler    = sampler;
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
