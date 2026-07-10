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
