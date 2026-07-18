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

#include "../common.h"

#include <us/compiler.h>

GPU_HIDE
D3D12_FILTER
dx12_staticSamplerFilter(const GPUStaticSamplerDesc *desc) {
  bool minLinear;
  bool magLinear;
  bool mipLinear;

  if (!desc) {
    return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  }

  if (desc->maxAnisotropy > 1u) {
    return desc->hasCompare ?
      D3D12_FILTER_COMPARISON_ANISOTROPIC :
      D3D12_FILTER_ANISOTROPIC;
  }

  minLinear = desc->minFilter == USL_RUNTIME_FILTER_LINEAR;
  magLinear = desc->magFilter == USL_RUNTIME_FILTER_LINEAR;
  mipLinear = desc->mipFilter == USL_RUNTIME_FILTER_LINEAR;

  if (desc->hasCompare) {
    if (!minLinear && !magLinear && !mipLinear) return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    if (!minLinear && !magLinear &&  mipLinear) return D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
    if (!minLinear &&  magLinear && !mipLinear) return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (!minLinear &&  magLinear &&  mipLinear) return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
    if ( minLinear && !magLinear && !mipLinear) return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
    if ( minLinear && !magLinear &&  mipLinear) return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if ( minLinear &&  magLinear && !mipLinear) return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
  }

  if (!minLinear && !magLinear && !mipLinear) return D3D12_FILTER_MIN_MAG_MIP_POINT;
  if (!minLinear && !magLinear &&  mipLinear) return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
  if (!minLinear &&  magLinear && !mipLinear) return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
  if (!minLinear &&  magLinear &&  mipLinear) return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
  if ( minLinear && !magLinear && !mipLinear) return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
  if ( minLinear && !magLinear &&  mipLinear) return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
  if ( minLinear &&  magLinear && !mipLinear) return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
}

GPU_HIDE
D3D12_TEXTURE_ADDRESS_MODE
dx12_staticSamplerAddressMode(uint32_t mode) {
  switch (mode) {
    case USL_RUNTIME_ADDRESS_REPEAT:
      return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case USL_RUNTIME_ADDRESS_MIRRORED_REPEAT:
      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case USL_RUNTIME_ADDRESS_CLAMP_TO_ZERO:
    case USL_RUNTIME_ADDRESS_CLAMP_TO_BORDER:
      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE:
    default:
      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
}

GPU_HIDE
D3D12_COMPARISON_FUNC
dx12_staticSamplerCompareFunc(uint32_t func) {
  switch (func) {
    case USL_RUNTIME_COMPARE_LESS:
      return D3D12_COMPARISON_FUNC_LESS;
    case USL_RUNTIME_COMPARE_EQUAL:
      return D3D12_COMPARISON_FUNC_EQUAL;
    case USL_RUNTIME_COMPARE_LESS_EQUAL:
      return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case USL_RUNTIME_COMPARE_GREATER:
      return D3D12_COMPARISON_FUNC_GREATER;
    case USL_RUNTIME_COMPARE_NOT_EQUAL:
      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case USL_RUNTIME_COMPARE_GREATER_EQUAL:
      return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case USL_RUNTIME_COMPARE_ALWAYS:
      return D3D12_COMPARISON_FUNC_ALWAYS;
    case USL_RUNTIME_COMPARE_NEVER:
    default:
      return D3D12_COMPARISON_FUNC_NEVER;
  }
}

GPU_HIDE
int
dx12_fillSourceSamplerDesc(const GPUStaticSamplerDesc *sourceDesc,
                           uint32_t                    shaderRegister,
                           D3D12_SHADER_VISIBILITY     visibility,
                           D3D12_STATIC_SAMPLER_DESC  *outDesc) {
  D3D12_TEXTURE_ADDRESS_MODE addressMode;

  if (!outDesc || !gpuStaticSamplerDescIsValid(sourceDesc)) {
    return 0;
  }

  addressMode = dx12_staticSamplerAddressMode(sourceDesc->addressMode);

  memset(outDesc, 0, sizeof(*outDesc));
  outDesc->Filter           = dx12_staticSamplerFilter(sourceDesc);
  outDesc->AddressU         = addressMode;
  outDesc->AddressV         = addressMode;
  outDesc->AddressW         = addressMode;
  outDesc->MipLODBias       = 0.0f;
  outDesc->MaxAnisotropy    = sourceDesc->maxAnisotropy > 1u
                                ? (sourceDesc->maxAnisotropy > 16u
                                     ? 16u
                                     : sourceDesc->maxAnisotropy)
                                : 1u;
  outDesc->ComparisonFunc   = sourceDesc->hasCompare
                                ? dx12_staticSamplerCompareFunc(
                                    sourceDesc->compareFunc
                                  )
                                : D3D12_COMPARISON_FUNC_NEVER;
  outDesc->BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  outDesc->MinLOD           = 0.0f;
  outDesc->MaxLOD           = D3D12_FLOAT32_MAX;
  outDesc->ShaderRegister   = shaderRegister;
  outDesc->RegisterSpace    = 0;
  outDesc->ShaderVisibility = visibility;
  return 1;
}

static GPUResult
dx12__createSampler(GPUDevice               *device,
                    const D3D12_SAMPLER_DESC *desc,
                    GPUSampler             **outSampler) {
  GPUSampler     *sampler;
  GPUSamplerDX12 *native;

  if (!device || !device->_priv || !desc || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSampler = NULL;
  sampler = calloc(1, sizeof(*sampler) + sizeof(*native));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUSamplerDX12 *)(sampler + 1);
  native->device = device->_priv;
  native->desc   = *desc;
  sampler->_priv = native;
  *outSampler    = sampler;
  return GPU_OK;
}

static D3D12_TEXTURE_ADDRESS_MODE
dx12__addressMode(GPUAddressMode mode) {
  static const D3D12_TEXTURE_ADDRESS_MODE modes[] = {
    [GPU_ADDRESS_MODE_REPEAT]          = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    [GPU_ADDRESS_MODE_MIRRORED_REPEAT] = D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
    [GPU_ADDRESS_MODE_CLAMP_TO_EDGE]   = D3D12_TEXTURE_ADDRESS_MODE_CLAMP
  };

  return (uint32_t)mode < GPU_ARRAY_LEN(modes) && modes[mode] != 0
           ? modes[mode]
           : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

GPU_HIDE
int
dx12_fillStaticSamplerDesc(const GPUSamplerDesc       *desc,
                           uint32_t                    shaderRegister,
                           uint32_t                    registerSpace,
                           D3D12_SHADER_VISIBILITY     visibility,
                           D3D12_STATIC_SAMPLER_DESC *outDesc) {
  GPUStaticSamplerDesc staticDesc = {0};

  if (!desc || !outDesc) {
    return 0;
  }

  staticDesc.minFilter     = desc->minFilter == GPU_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.magFilter     = desc->magFilter == GPU_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.mipFilter     = desc->mipFilter == GPU_MIP_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.maxAnisotropy = 1u;

  memset(outDesc, 0, sizeof(*outDesc));
  outDesc->Filter           = dx12_staticSamplerFilter(&staticDesc);
  outDesc->AddressU         = dx12__addressMode(desc->addressU);
  outDesc->AddressV         = dx12__addressMode(desc->addressV);
  outDesc->AddressW         = dx12__addressMode(desc->addressW);
  outDesc->MaxAnisotropy    = 1u;
  outDesc->ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
  outDesc->BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  outDesc->MinLOD           = 0.0f;
  outDesc->MaxLOD           = D3D12_FLOAT32_MAX;
  outDesc->ShaderRegister   = shaderRegister;
  outDesc->RegisterSpace    = registerSpace;
  outDesc->ShaderVisibility = visibility;
  return 1;
}

GPU_HIDE
GPUResult
dx12_createSampler(GPUApi                    * __restrict api,
                   GPUDevice                 * __restrict device,
                   const GPUSamplerCreateInfo *info,
                   bool                       staticIfSupported,
                   GPUSampler               **outSampler) {
  GPUStaticSamplerDesc staticDesc = {0};
  D3D12_SAMPLER_DESC   desc       = {0};

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  staticDesc.minFilter     = info->desc.minFilter == GPU_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.magFilter     = info->desc.magFilter == GPU_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.mipFilter     = info->desc.mipFilter == GPU_MIP_FILTER_LINEAR
                               ? USL_RUNTIME_FILTER_LINEAR
                               : USL_RUNTIME_FILTER_NEAREST;
  staticDesc.maxAnisotropy = 1u;

  desc.Filter         = dx12_staticSamplerFilter(&staticDesc);
  desc.AddressU       = dx12__addressMode(info->desc.addressU);
  desc.AddressV       = dx12__addressMode(info->desc.addressV);
  desc.AddressW       = dx12__addressMode(info->desc.addressW);
  desc.MaxAnisotropy  = 1u;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  desc.MaxLOD         = D3D12_FLOAT32_MAX;
  return dx12__createSampler(device, &desc, outSampler);
}

GPU_HIDE
void
dx12_destroySampler(GPUSampler * __restrict sampler) {
  free(sampler);
}

GPU_HIDE
void
dx12_initSampler(GPUApiSampler *api) {
  api->createSampler  = dx12_createSampler;
  api->destroySampler = dx12_destroySampler;
}
