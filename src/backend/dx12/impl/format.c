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

static const DXGI_FORMAT dx12_formats[GPUPixelFormatX24_Stencil8 + 1u] = {
  [GPUPixelFormatR8Unorm]               = DXGI_FORMAT_R8_UNORM,
  [GPUPixelFormatR8Snorm]               = DXGI_FORMAT_R8_SNORM,
  [GPUPixelFormatR8Uint]                = DXGI_FORMAT_R8_UINT,
  [GPUPixelFormatR8Sint]                = DXGI_FORMAT_R8_SINT,
  [GPUPixelFormatR16Unorm]              = DXGI_FORMAT_R16_UNORM,
  [GPUPixelFormatR16Snorm]              = DXGI_FORMAT_R16_SNORM,
  [GPUPixelFormatR16Uint]               = DXGI_FORMAT_R16_UINT,
  [GPUPixelFormatR16Sint]               = DXGI_FORMAT_R16_SINT,
  [GPUPixelFormatR16Float]              = DXGI_FORMAT_R16_FLOAT,
  [GPUPixelFormatRG8Unorm]              = DXGI_FORMAT_R8G8_UNORM,
  [GPUPixelFormatRG8Snorm]              = DXGI_FORMAT_R8G8_SNORM,
  [GPUPixelFormatRG8Uint]               = DXGI_FORMAT_R8G8_UINT,
  [GPUPixelFormatRG8Sint]               = DXGI_FORMAT_R8G8_SINT,
  [GPUPixelFormatR32Uint]               = DXGI_FORMAT_R32_UINT,
  [GPUPixelFormatR32Sint]               = DXGI_FORMAT_R32_SINT,
  [GPUPixelFormatR32Float]              = DXGI_FORMAT_R32_FLOAT,
  [GPUPixelFormatRG16Unorm]             = DXGI_FORMAT_R16G16_UNORM,
  [GPUPixelFormatRG16Snorm]             = DXGI_FORMAT_R16G16_SNORM,
  [GPUPixelFormatRG16Uint]              = DXGI_FORMAT_R16G16_UINT,
  [GPUPixelFormatRG16Sint]              = DXGI_FORMAT_R16G16_SINT,
  [GPUPixelFormatRG16Float]             = DXGI_FORMAT_R16G16_FLOAT,
  [GPUPixelFormatRGBA8Unorm]            = DXGI_FORMAT_R8G8B8A8_UNORM,
  [GPUPixelFormatRGBA8Unorm_sRGB]       = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
  [GPUPixelFormatRGBA8Snorm]            = DXGI_FORMAT_R8G8B8A8_SNORM,
  [GPUPixelFormatRGBA8Uint]             = DXGI_FORMAT_R8G8B8A8_UINT,
  [GPUPixelFormatRGBA8Sint]             = DXGI_FORMAT_R8G8B8A8_SINT,
  [GPUPixelFormatBGRA8Unorm]            = DXGI_FORMAT_B8G8R8A8_UNORM,
  [GPUPixelFormatBGRA8Unorm_sRGB]       = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
  [GPUPixelFormatRGB10A2Unorm]          = DXGI_FORMAT_R10G10B10A2_UNORM,
  [GPUPixelFormatRGB10A2Uint]           = DXGI_FORMAT_R10G10B10A2_UINT,
  [GPUPixelFormatRG11B10Float]          = DXGI_FORMAT_R11G11B10_FLOAT,
  [GPUPixelFormatRG32Uint]              = DXGI_FORMAT_R32G32_UINT,
  [GPUPixelFormatRG32Sint]              = DXGI_FORMAT_R32G32_SINT,
  [GPUPixelFormatRG32Float]             = DXGI_FORMAT_R32G32_FLOAT,
  [GPUPixelFormatRGBA16Unorm]           = DXGI_FORMAT_R16G16B16A16_UNORM,
  [GPUPixelFormatRGBA16Snorm]           = DXGI_FORMAT_R16G16B16A16_SNORM,
  [GPUPixelFormatRGBA16Uint]            = DXGI_FORMAT_R16G16B16A16_UINT,
  [GPUPixelFormatRGBA16Sint]            = DXGI_FORMAT_R16G16B16A16_SINT,
  [GPUPixelFormatRGBA16Float]           = DXGI_FORMAT_R16G16B16A16_FLOAT,
  [GPUPixelFormatRGBA32Uint]            = DXGI_FORMAT_R32G32B32A32_UINT,
  [GPUPixelFormatRGBA32Sint]            = DXGI_FORMAT_R32G32B32A32_SINT,
  [GPUPixelFormatRGBA32Float]           = DXGI_FORMAT_R32G32B32A32_FLOAT,
  [GPUPixelFormatBC1_RGBA]              = DXGI_FORMAT_BC1_UNORM,
  [GPUPixelFormatBC1_RGBA_sRGB]         = DXGI_FORMAT_BC1_UNORM_SRGB,
  [GPUPixelFormatBC2_RGBA]              = DXGI_FORMAT_BC2_UNORM,
  [GPUPixelFormatBC2_RGBA_sRGB]         = DXGI_FORMAT_BC2_UNORM_SRGB,
  [GPUPixelFormatBC3_RGBA]              = DXGI_FORMAT_BC3_UNORM,
  [GPUPixelFormatBC3_RGBA_sRGB]         = DXGI_FORMAT_BC3_UNORM_SRGB,
  [GPUPixelFormatBC4_RUnorm]            = DXGI_FORMAT_BC4_UNORM,
  [GPUPixelFormatBC4_RSnorm]            = DXGI_FORMAT_BC4_SNORM,
  [GPUPixelFormatBC5_RGUnorm]           = DXGI_FORMAT_BC5_UNORM,
  [GPUPixelFormatBC5_RGSnorm]           = DXGI_FORMAT_BC5_SNORM,
  [GPUPixelFormatBC6H_RGBFloat]         = DXGI_FORMAT_BC6H_SF16,
  [GPUPixelFormatBC6H_RGBUfloat]        = DXGI_FORMAT_BC6H_UF16,
  [GPUPixelFormatBC7_RGBAUnorm]         = DXGI_FORMAT_BC7_UNORM,
  [GPUPixelFormatBC7_RGBAUnorm_sRGB]    = DXGI_FORMAT_BC7_UNORM_SRGB,
  [GPUPixelFormatDepth32Float]          = DXGI_FORMAT_D32_FLOAT,
  [GPUPixelFormatDepth24Unorm_Stencil8] = DXGI_FORMAT_D24_UNORM_S8_UINT,
  [GPUPixelFormatDepth32Float_Stencil8] = DXGI_FORMAT_D32_FLOAT_S8X24_UINT
};

static DXGI_FORMAT
dx12_sampledFormat(GPUFormat format) {
  switch (format) {
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
