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

static WGPUCompareFunction
webgpu_compareFunction(GPUCompareOp op) {
  static const WGPUCompareFunction functions[] = {
    [GPU_COMPARE_NEVER]         = WGPUCompareFunction_Never,
    [GPU_COMPARE_LESS]          = WGPUCompareFunction_Less,
    [GPU_COMPARE_EQUAL]         = WGPUCompareFunction_Equal,
    [GPU_COMPARE_LESS_EQUAL]    = WGPUCompareFunction_LessEqual,
    [GPU_COMPARE_GREATER]       = WGPUCompareFunction_Greater,
    [GPU_COMPARE_NOT_EQUAL]     = WGPUCompareFunction_NotEqual,
    [GPU_COMPARE_GREATER_EQUAL] = WGPUCompareFunction_GreaterEqual,
    [GPU_COMPARE_ALWAYS]        = WGPUCompareFunction_Always
  };

  return (uint32_t)op < GPU_ARRAY_LEN(functions)
           ? functions[op]
           : WGPUCompareFunction_Undefined;
}

WGPUSampler
gpu_webgpuCreateSampler(GPUDevice           *device,
                        const GPUSamplerDesc *desc,
                        const char           *label) {
  WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU      *native;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !desc) {
    return NULL;
  }

  descriptor.label        = gpu_webgpuString(label);
  descriptor.addressModeU = webgpu_addressMode(desc->addressU);
  descriptor.addressModeV = webgpu_addressMode(desc->addressV);
  descriptor.addressModeW = webgpu_addressMode(desc->addressW);
  descriptor.minFilter    = webgpu_filter(desc->minFilter);
  descriptor.magFilter    = webgpu_filter(desc->magFilter);
  descriptor.mipmapFilter = webgpu_mipFilter(desc->mipFilter);
  descriptor.compare      = desc->compareEnable
                              ? webgpu_compareFunction(desc->compare)
                              : WGPUCompareFunction_Undefined;
  return wgpuDeviceCreateSampler(native->device, &descriptor);
}

static GPUResult
webgpu_createSampler(GPUApi                    * __restrict api,
                     GPUDevice                 * __restrict device,
                     const GPUSamplerCreateInfo *info,
                     bool                       staticIfSupported,
                     GPUSampler                **outSampler) {
  GPUSampler *sampler;

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!device || !info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  sampler = calloc(1, sizeof(*sampler));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  sampler->_priv = gpu_webgpuCreateSampler(device, &info->desc, info->label);
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
