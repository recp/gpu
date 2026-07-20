/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPUFilterMode
webgpu_filter(GPUFilter filter) {
  return filter == GPU_FILTER_LINEAR
           ? WGPUFilterMode_Linear
           : WGPUFilterMode_Nearest;
}

static WGPUMipmapFilterMode
webgpu_mipFilter(GPUMipFilter filter) {
  return filter == GPU_MIP_FILTER_LINEAR
           ? WGPUMipmapFilterMode_Linear
           : WGPUMipmapFilterMode_Nearest;
}

static WGPUAddressMode
webgpu_addressMode(GPUAddressMode mode) {
  static const WGPUAddressMode modes[] = {
    [GPU_ADDRESS_MODE_REPEAT]          = WGPUAddressMode_Repeat,
    [GPU_ADDRESS_MODE_MIRRORED_REPEAT] = WGPUAddressMode_MirrorRepeat,
    [GPU_ADDRESS_MODE_CLAMP_TO_EDGE]   = WGPUAddressMode_ClampToEdge
  };

  return (uint32_t)mode < GPU_ARRAY_LEN(modes)
           ? modes[mode]
           : WGPUAddressMode_Undefined;
}

static GPUResult
webgpu_createSampler(GPUApi                    * __restrict api,
                     GPUDevice                 * __restrict device,
                     const GPUSamplerCreateInfo *info,
                     bool                       staticIfSupported,
                     GPUSampler                **outSampler) {
  WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU      *native;
  GPUSampler           *sampler;

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  sampler = calloc(1, sizeof(*sampler));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label         = gpu_webgpuString(info->label);
  descriptor.addressModeU  = webgpu_addressMode(info->desc.addressU);
  descriptor.addressModeV  = webgpu_addressMode(info->desc.addressV);
  descriptor.addressModeW  = webgpu_addressMode(info->desc.addressW);
  descriptor.minFilter     = webgpu_filter(info->desc.minFilter);
  descriptor.magFilter     = webgpu_filter(info->desc.magFilter);
  descriptor.mipmapFilter  = webgpu_mipFilter(info->desc.mipFilter);
  sampler->_priv = wgpuDeviceCreateSampler(native->device, &descriptor);
  if (!sampler->_priv) {
    free(sampler);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outSampler = sampler;
  return GPU_OK;
}

static void
webgpu_destroySampler(GPUSampler * __restrict sampler) {
  if (!sampler) {
    return;
  }
  if (sampler->_priv) {
    wgpuSamplerRelease(sampler->_priv);
  }
  free(sampler);
}

void
webgpu_initSampler(GPUApiSampler *api) {
  api->createSampler  = webgpu_createSampler;
  api->destroySampler = webgpu_destroySampler;
}
