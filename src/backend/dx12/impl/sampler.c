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

GPU_HIDE
D3D12_FILTER
dx12_uslSamplerFilter(const GPUUSLStaticSamplerDesc *desc) {
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

  minLinear = desc->minFilter == GPUUSLSamplerFilterLinear;
  magLinear = desc->magFilter == GPUUSLSamplerFilterLinear;
  mipLinear = desc->mipFilter == GPUUSLSamplerFilterLinear;

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
dx12_uslSamplerAddressMode(uint32_t mode) {
  switch (mode) {
    case GPUUSLSamplerAddressRepeat:
      return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case GPUUSLSamplerAddressMirroredRepeat:
      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case GPUUSLSamplerAddressClampToZero:
    case GPUUSLSamplerAddressClampToBorder:
      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case GPUUSLSamplerAddressClampToEdge:
    default:
      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
}

GPU_HIDE
D3D12_COMPARISON_FUNC
dx12_uslSamplerCompareFunc(uint32_t func) {
  switch (func) {
    case GPUUSLSamplerCompareLess:
      return D3D12_COMPARISON_FUNC_LESS;
    case GPUUSLSamplerCompareEqual:
      return D3D12_COMPARISON_FUNC_EQUAL;
    case GPUUSLSamplerCompareLessEqual:
      return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case GPUUSLSamplerCompareGreater:
      return D3D12_COMPARISON_FUNC_GREATER;
    case GPUUSLSamplerCompareNotEqual:
      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case GPUUSLSamplerCompareGreaterEqual:
      return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case GPUUSLSamplerCompareAlways:
      return D3D12_COMPARISON_FUNC_ALWAYS;
    case GPUUSLSamplerCompareNever:
    default:
      return D3D12_COMPARISON_FUNC_NEVER;
  }
}

GPU_HIDE
int
dx12_fillStaticSamplerDescFromUSL(const GPUUSLStaticSamplerDesc *uslDesc,
                                  uint32_t shaderRegister,
                                  D3D12_SHADER_VISIBILITY visibility,
                                  D3D12_STATIC_SAMPLER_DESC *outDesc) {
  D3D12_TEXTURE_ADDRESS_MODE addressMode;

  if (!outDesc || !GPUUSLStaticSamplerDescIsValid(uslDesc)) {
    return 0;
  }

  memset(outDesc, 0, sizeof(*outDesc));
  outDesc->Filter = dx12_uslSamplerFilter(uslDesc);
  addressMode = uslDesc ?
    dx12_uslSamplerAddressMode(uslDesc->addressMode) :
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  outDesc->AddressU = addressMode;
  outDesc->AddressV = addressMode;
  outDesc->AddressW = addressMode;
  outDesc->MipLODBias = 0.0f;
  outDesc->MaxAnisotropy = uslDesc && uslDesc->maxAnisotropy > 1u ?
    (uslDesc->maxAnisotropy > 16u ? 16u : uslDesc->maxAnisotropy) :
    1u;
  outDesc->ComparisonFunc = uslDesc && uslDesc->hasCompare ?
    dx12_uslSamplerCompareFunc(uslDesc->compareFunc) :
    D3D12_COMPARISON_FUNC_NEVER;
  outDesc->BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  outDesc->MinLOD = 0.0f;
  outDesc->MaxLOD = D3D12_FLOAT32_MAX;
  outDesc->ShaderRegister = shaderRegister;
  outDesc->RegisterSpace = 0;
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
GPUResult
dx12_createSampler(GPUApi                    * __restrict api,
                   GPUDevice                 * __restrict device,
                   const GPUSamplerCreateInfo *info,
                   bool                       staticIfSupported,
                   GPUSampler               **outSampler) {
  GPUUSLStaticSamplerDesc uslDesc = {0};
  D3D12_SAMPLER_DESC      desc = {0};

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  uslDesc.minFilter = info->desc.minFilter == GPU_FILTER_LINEAR
                        ? GPUUSLSamplerFilterLinear
                        : GPUUSLSamplerFilterNearest;
  uslDesc.magFilter = info->desc.magFilter == GPU_FILTER_LINEAR
                        ? GPUUSLSamplerFilterLinear
                        : GPUUSLSamplerFilterNearest;
  uslDesc.mipFilter = info->desc.mipFilter == GPU_MIP_FILTER_LINEAR
                        ? GPUUSLSamplerFilterLinear
                        : GPUUSLSamplerFilterNearest;
  uslDesc.maxAnisotropy = 1u;

  desc.Filter         = dx12_uslSamplerFilter(&uslDesc);
  desc.AddressU       = dx12__addressMode(info->desc.addressU);
  desc.AddressV       = dx12__addressMode(info->desc.addressV);
  desc.AddressW       = dx12__addressMode(info->desc.addressW);
  desc.MaxAnisotropy  = 1u;
  desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  desc.MaxLOD         = D3D12_FLOAT32_MAX;
  return dx12__createSampler(device, &desc, outSampler);
}

GPU_HIDE
GPUResult
dx12_createSamplerFromUSL(GPUApi                        * __restrict api,
                          GPUDevice                     * __restrict device,
                          const GPUUSLStaticSamplerDesc *uslDesc,
                          bool                           staticIfSupported,
                          GPUSampler                   **outSampler) {
  D3D12_SAMPLER_DESC desc = {0};
  D3D12_TEXTURE_ADDRESS_MODE addressMode;

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!GPUUSLStaticSamplerDescIsValid(uslDesc)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (uslDesc->coordSpace == GPUUSLSamplerCoordPixel) {
    return GPU_ERROR_UNSUPPORTED;
  }

  addressMode = dx12_uslSamplerAddressMode(uslDesc->addressMode);
  desc.Filter         = dx12_uslSamplerFilter(uslDesc);
  desc.AddressU       = addressMode;
  desc.AddressV       = addressMode;
  desc.AddressW       = addressMode;
  desc.MaxAnisotropy  = uslDesc->maxAnisotropy > 1u
                          ? (uslDesc->maxAnisotropy > 16u
                               ? 16u
                               : uslDesc->maxAnisotropy)
                          : 1u;
  desc.ComparisonFunc = uslDesc->hasCompare
                          ? dx12_uslSamplerCompareFunc(uslDesc->compareFunc)
                          : D3D12_COMPARISON_FUNC_NEVER;
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
  api->createSampler                     = dx12_createSampler;
  api->createSamplerFromUSLStaticSampler = dx12_createSamplerFromUSL;
  api->destroySampler                    = dx12_destroySampler;
}
