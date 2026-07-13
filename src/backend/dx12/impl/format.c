/*
 * Copyright (C) 2026 Recep Aslantas
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

static const DXGI_FORMAT dx12_formats[GPU_FORMAT_COUNT] = {
  [GPU_FORMAT_R8_UNORM]               = DXGI_FORMAT_R8_UNORM,
  [GPU_FORMAT_R8_SNORM]               = DXGI_FORMAT_R8_SNORM,
  [GPU_FORMAT_R8_UINT]                = DXGI_FORMAT_R8_UINT,
  [GPU_FORMAT_R8_SINT]                = DXGI_FORMAT_R8_SINT,
  [GPU_FORMAT_R16_UNORM]              = DXGI_FORMAT_R16_UNORM,
  [GPU_FORMAT_R16_SNORM]              = DXGI_FORMAT_R16_SNORM,
  [GPU_FORMAT_R16_UINT]               = DXGI_FORMAT_R16_UINT,
  [GPU_FORMAT_R16_SINT]               = DXGI_FORMAT_R16_SINT,
  [GPU_FORMAT_R16_FLOAT]              = DXGI_FORMAT_R16_FLOAT,
  [GPU_FORMAT_RG8_UNORM]              = DXGI_FORMAT_R8G8_UNORM,
  [GPU_FORMAT_RG8_SNORM]              = DXGI_FORMAT_R8G8_SNORM,
  [GPU_FORMAT_RG8_UINT]               = DXGI_FORMAT_R8G8_UINT,
  [GPU_FORMAT_RG8_SINT]               = DXGI_FORMAT_R8G8_SINT,
  [GPU_FORMAT_R32_UINT]               = DXGI_FORMAT_R32_UINT,
  [GPU_FORMAT_R32_SINT]               = DXGI_FORMAT_R32_SINT,
  [GPU_FORMAT_R32_FLOAT]              = DXGI_FORMAT_R32_FLOAT,
  [GPU_FORMAT_RG16_UNORM]             = DXGI_FORMAT_R16G16_UNORM,
  [GPU_FORMAT_RG16_SNORM]             = DXGI_FORMAT_R16G16_SNORM,
  [GPU_FORMAT_RG16_UINT]              = DXGI_FORMAT_R16G16_UINT,
  [GPU_FORMAT_RG16_SINT]              = DXGI_FORMAT_R16G16_SINT,
  [GPU_FORMAT_RG16_FLOAT]             = DXGI_FORMAT_R16G16_FLOAT,
  [GPU_FORMAT_RGBA8_UNORM]            = DXGI_FORMAT_R8G8B8A8_UNORM,
  [GPU_FORMAT_RGBA8_UNORM_SRGB]       = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
  [GPU_FORMAT_RGBA8_SNORM]            = DXGI_FORMAT_R8G8B8A8_SNORM,
  [GPU_FORMAT_RGBA8_UINT]             = DXGI_FORMAT_R8G8B8A8_UINT,
  [GPU_FORMAT_RGBA8_SINT]             = DXGI_FORMAT_R8G8B8A8_SINT,
  [GPU_FORMAT_BGRA8_UNORM]            = DXGI_FORMAT_B8G8R8A8_UNORM,
  [GPU_FORMAT_BGRA8_UNORM_SRGB]       = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
  [GPU_FORMAT_RGB10A2_UNORM]          = DXGI_FORMAT_R10G10B10A2_UNORM,
  [GPU_FORMAT_RGB10A2_UINT]           = DXGI_FORMAT_R10G10B10A2_UINT,
  [GPU_FORMAT_RG11B10_UFLOAT]         = DXGI_FORMAT_R11G11B10_FLOAT,
  [GPU_FORMAT_RGB9E5_UFLOAT]          = DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
  [GPU_FORMAT_RG32_UINT]              = DXGI_FORMAT_R32G32_UINT,
  [GPU_FORMAT_RG32_SINT]              = DXGI_FORMAT_R32G32_SINT,
  [GPU_FORMAT_RG32_FLOAT]             = DXGI_FORMAT_R32G32_FLOAT,
  [GPU_FORMAT_RGBA16_UNORM]           = DXGI_FORMAT_R16G16B16A16_UNORM,
  [GPU_FORMAT_RGBA16_SNORM]           = DXGI_FORMAT_R16G16B16A16_SNORM,
  [GPU_FORMAT_RGBA16_UINT]            = DXGI_FORMAT_R16G16B16A16_UINT,
  [GPU_FORMAT_RGBA16_SINT]            = DXGI_FORMAT_R16G16B16A16_SINT,
  [GPU_FORMAT_RGBA16_FLOAT]           = DXGI_FORMAT_R16G16B16A16_FLOAT,
  [GPU_FORMAT_RGBA32_UINT]            = DXGI_FORMAT_R32G32B32A32_UINT,
  [GPU_FORMAT_RGBA32_SINT]            = DXGI_FORMAT_R32G32B32A32_SINT,
  [GPU_FORMAT_RGBA32_FLOAT]           = DXGI_FORMAT_R32G32B32A32_FLOAT,
  [GPU_FORMAT_BC1_RGBA_UNORM]         = DXGI_FORMAT_BC1_UNORM,
  [GPU_FORMAT_BC1_RGBA_UNORM_SRGB]    = DXGI_FORMAT_BC1_UNORM_SRGB,
  [GPU_FORMAT_BC2_RGBA_UNORM]         = DXGI_FORMAT_BC2_UNORM,
  [GPU_FORMAT_BC2_RGBA_UNORM_SRGB]    = DXGI_FORMAT_BC2_UNORM_SRGB,
  [GPU_FORMAT_BC3_RGBA_UNORM]         = DXGI_FORMAT_BC3_UNORM,
  [GPU_FORMAT_BC3_RGBA_UNORM_SRGB]    = DXGI_FORMAT_BC3_UNORM_SRGB,
  [GPU_FORMAT_BC4_R_UNORM]            = DXGI_FORMAT_BC4_UNORM,
  [GPU_FORMAT_BC4_R_SNORM]            = DXGI_FORMAT_BC4_SNORM,
  [GPU_FORMAT_BC5_RG_UNORM]           = DXGI_FORMAT_BC5_UNORM,
  [GPU_FORMAT_BC5_RG_SNORM]           = DXGI_FORMAT_BC5_SNORM,
  [GPU_FORMAT_BC6H_RGB_FLOAT]         = DXGI_FORMAT_BC6H_SF16,
  [GPU_FORMAT_BC6H_RGB_UFLOAT]        = DXGI_FORMAT_BC6H_UF16,
  [GPU_FORMAT_BC7_RGBA_UNORM]         = DXGI_FORMAT_BC7_UNORM,
  [GPU_FORMAT_BC7_RGBA_UNORM_SRGB]    = DXGI_FORMAT_BC7_UNORM_SRGB,
  [GPU_FORMAT_DEPTH16_UNORM]          = DXGI_FORMAT_D16_UNORM,
  [GPU_FORMAT_DEPTH32_FLOAT]          = DXGI_FORMAT_D32_FLOAT,
  [GPU_FORMAT_DEPTH24_UNORM_STENCIL8] = DXGI_FORMAT_D24_UNORM_S8_UINT,
  [GPU_FORMAT_DEPTH32_FLOAT_STENCIL8] = DXGI_FORMAT_D32_FLOAT_S8X24_UINT
};

static DXGI_FORMAT
dx12_sampledFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH16_UNORM:
      return DXGI_FORMAT_R16_UNORM;
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case GPU_FORMAT_DEPTH32_FLOAT:
      return DXGI_FORMAT_R32_FLOAT;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return (uint32_t)format < GPU_ARRAY_LEN(dx12_formats)
               ? dx12_formats[format]
               : DXGI_FORMAT_UNKNOWN;
  }
}

static void
dx12_queryFormatCapabilities(GPUPhysicalDeviceDX12 *adapter) {
  ID3D12Device *device;
  HRESULT       result;

  memset(adapter->formatCaps, 0, sizeof(adapter->formatCaps));
  adapter->formatCapsReady = true;
  device = NULL;
  result = D3D12CreateDevice(adapter->dxgiAdapter,
                             D3D_FEATURE_LEVEL_11_0,
                             &IID_ID3D12Device,
                             (void **)&device);
  if (FAILED(result) || !device) {
    return;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(dx12_formats); i++) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT  attachmentSupport = {0};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT  sampledSupport = {0};
    GPUFormatCapabilities             *caps;
    DXGI_FORMAT                        attachmentFormat;
    DXGI_FORMAT                        sampledFormat;

    attachmentFormat = dx12_formats[i];
    if (attachmentFormat == DXGI_FORMAT_UNKNOWN) {
      continue;
    }

    attachmentSupport.Format = attachmentFormat;
    if (FAILED(device->lpVtbl->CheckFeatureSupport(
          device,
          D3D12_FEATURE_FORMAT_SUPPORT,
          &attachmentSupport,
          sizeof(attachmentSupport)))) {
      continue;
    }

    sampledFormat = dx12_sampledFormat((GPUFormat)i);
    sampledSupport.Format = sampledFormat;
    if (sampledFormat == attachmentFormat) {
      sampledSupport = attachmentSupport;
    } else if (FAILED(device->lpVtbl->CheckFeatureSupport(
                 device,
                 D3D12_FEATURE_FORMAT_SUPPORT,
                 &sampledSupport,
                 sizeof(sampledSupport)))) {
      memset(&sampledSupport, 0, sizeof(sampledSupport));
    }

    caps = &adapter->formatCaps[i];
    caps->sampled =
      (sampledSupport.Support1 &
       (D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
        D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)) != 0u;
    caps->filterable =
      (sampledSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0u;
    caps->storage =
      (attachmentSupport.Support1 &
       D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0u &&
      (attachmentSupport.Support2 &
       (D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) ==
        (D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
         D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    caps->colorAttachment =
      (attachmentSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0u;
    caps->blendable =
      (attachmentSupport.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) != 0u;
    caps->depthStencil =
      (attachmentSupport.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0u;
  }

  device->lpVtbl->Release(device);
}

GPU_HIDE
DXGI_FORMAT
dx12_format(GPUFormat format) {
  if ((uint32_t)format >= GPU_ARRAY_LEN(dx12_formats)) {
    return DXGI_FORMAT_UNKNOWN;
  }

  return dx12_formats[format];
}

GPU_HIDE
void
dx12_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps) {
  GPUPhysicalDeviceDX12 *adapterDX12;

  adapterDX12 = adapter ? adapter->_priv : NULL;
  if (!outCaps) {
    return;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (!adapterDX12 || (uint32_t)format >= GPU_ARRAY_LEN(dx12_formats)) {
    return;
  }
  if (!adapterDX12->formatCapsReady) {
    dx12_queryFormatCapabilities(adapterDX12);
  }

  *outCaps = adapterDX12->formatCaps[format];
}
