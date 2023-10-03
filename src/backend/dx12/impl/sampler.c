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

GPUSampler*
dx12_createStaticSampler(GPUApi * __restrict api, GPUDevice * __restrict device) {
  GPUSwapChain               *swapChain;
  ID3D12Device               *d3dDevice;
  ID3D12CommandQueue         *cmdQueDX12;
  GPU__DX12                  *dx12api;
  IDXGIFactory4              *dxgiFactory;
  IDXGISwapChain1            *swapChain1;
  IDXGISwapChain3            *swapChain3;
  ID3D12DescriptorHeap       *rtvHeap;
  GPUSwapChainDX12           *swapChainDX12;
  GPUFrameDX12               *frame;
  HRESULT                     hr;
  DXGI_SWAP_CHAIN_DESC1       swapChainDesc = {0};
  D3D12_DESCRIPTOR_HEAP_DESC  rtvHeapDesc   = {0};
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, *p_rtvHandle;
  UINT                        i, frameIndex;
  SIZE_T                      rtvDescriptorSize;

  D3D12_STATIC_SAMPLER_DESC sampler = {0};

  sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.MipLODBias       = 0;
  sampler.MaxAnisotropy    = 0;
  sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MinLOD           = 0.0f;
  sampler.MaxLOD           = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister   = 0;
  sampler.RegisterSpace    = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  swapChainDX12                  = NULL;
  swapChainDesc.BufferCount      = FrameCount;
  swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  return NULL;
err:

  dxThrowIfFailed(hr);
  return NULL;
}

GPU_HIDE
GPUSampler *
dx12_createDynSampler(GPUApi *__restrict api, GPUDevice *__restrict device) {
  GPUSwapChain *swapChain;
  ID3D12Device *d3dDevice;
  ID3D12CommandQueue *cmdQueDX12;
  GPU__DX12 *dx12api;
  IDXGIFactory4 *dxgiFactory;
  IDXGISwapChain1 *swapChain1;
  IDXGISwapChain3 *swapChain3;
  ID3D12DescriptorHeap *rtvHeap;
  GPUSwapChainDX12 *swapChainDX12;
  GPUFrameDX12 *frame;
  HRESULT                     hr;
  DXGI_SWAP_CHAIN_DESC1       swapChainDesc = { 0 };
  D3D12_DESCRIPTOR_HEAP_DESC  rtvHeapDesc = { 0 };
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, *p_rtvHandle;
  UINT                        i, frameIndex;
  SIZE_T                      rtvDescriptorSize;

  D3D12_STATIC_SAMPLER_DESC sampler = { 0 };

  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  sampler.MipLODBias = 0;
  sampler.MaxAnisotropy = 0;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MinLOD = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  swapChainDX12 = NULL;
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  return NULL;
err:

  dxThrowIfFailed(hr);
  return NULL;
}


GPU_HIDE
GPUSampler*
dx12_createSampler(GPUApi    * __restrict api,
                   GPUDevice * __restrict device, 
                   bool                   staticIfSupported) {
  return staticIfSupported ? 
    dx12_createStaticSampler(api, device) : dx12_createDynSampler(api, device);
}

GPU_HIDE
void
dx12_initSampler(GPUApiSampler* apiSampler) {
  apiSampler->createSampler = dx12_createStaticSampler;
}
