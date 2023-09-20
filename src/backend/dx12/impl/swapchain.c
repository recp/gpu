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

#define FrameCount 2

typedef struct GPUSwapChainDX12 {
  IDXGISwapChain3* swapChain;
  ID3D12DescriptorHeap* rtvHeap;
  ID3D12DescriptorHeap* srvHeap;
  ID3D12Resource* renderTargets[FrameCount];
  // ... other members as needed
} GPUSwapChainDX12;

GPUSwapChainDX12* 
dx12_createSwapChain(GPUApi* api, ID3D12CommandQueue* commandQueue, HWND hwnd, UINT width, UINT height) {
  IDXGIFactory4        *dxgiFactory;
  IDXGISwapChain1      *swapChain1;
  GPU__DX12            *dx12api;
  IDXGISwapChain3      *swapChain3;
  HRESULT               hr;
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
  UINT                  frameIndex;

  swapChainDesc.BufferCount      = FrameCount;
  swapChainDesc.Width            = width;
  swapChainDesc.Height           = height;
  swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  dx12api     = api->reserved;
  dxgiFactory = dx12api->dxgiFactory;
  hr          = dxgiFactory->lpVtbl->CreateSwapChainForHwnd(dxgiFactory, commandQueue, hwnd, &swapChainDesc, NULL, NULL, &swapChain1);
  ThrowIfFailed(hr);

  hr          = dxgiFactory->lpVtbl->MakeWindowAssociation(dxgiFactory, hwnd, DXGI_MWA_NO_ALT_ENTER);
  ThrowIfFailed(hr);

  hr          = swapChain1->lpVtbl->QueryInterface(swapChain1, &IID_IDXGISwapChain3, (void**)&swapChain3);
  swapChain1->lpVtbl->Release(swapChain1);  // We don't need swapChain1 anymore.
  ThrowIfFailed(hr);

  frameIndex  = swapChain3->lpVtbl->GetCurrentBackBufferIndex(swapChain3);

  // Allocate GPUSwapChainDX12 and fill its members...
  GPUSwapChainDX12* swapChain = malloc(sizeof(GPUSwapChainDX12));
  if (!swapChain) {
    // Handle allocation failure
    return NULL;
  }
  swapChain->swapChain = swapChain3;
  // ... 

  return swapChain;
}
