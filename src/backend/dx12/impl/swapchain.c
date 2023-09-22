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
  IDXGISwapChain3      *swapChain;
  ID3D12DescriptorHeap *rtvHeap;
  ID3D12DescriptorHeap *srvHeap;
  ID3D12Resource       *renderTargets[FrameCount];
  UINT                  frameIndex;
} GPUSwapChainDX12;

GPUSwapChain*
dx12_createSwapChainForView(GPUApi          * __restrict api,
                            GPUDevice       * __restrict device,
                            GPUCommandQueue * __restrict cmdQue,
                            void            * __restrict viewHandle,
                            GPUWindowType                viewHandleType,
                            float                        backingScaleFactor,
                            float                        width,
                            float                        height,
                            bool                         autoResize) {
  GPUSwapChain         *swapChain;
  ID3D12Device         *d3dDevice;
  ID3D12CommandQueue   *cmdQueDX12;
  GPU__DX12            *dx12api;
  IDXGIFactory4        *dxgiFactory;
  IDXGISwapChain1      *swapChain1;
  IDXGISwapChain3      *swapChain3;
  GPUSwapChainDX12     *swapChainDX12;
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
  cmdQueDX12  = cmdQue->priv;
  dxgiFactory = dx12api->dxgiFactory;

  switch (viewHandleType) {
  case GPU_WINDOW_TYPE_COREWINDOW:
    hr = dxgiFactory->lpVtbl->CreateSwapChainForCoreWindow(dxgiFactory, cmdQueDX12, viewHandle, &swapChainDesc, NULL, &swapChain1);
    dxThrowIfFailed(hr);
    break;
  case GPU_WINDOW_TYPE_HWND:
    hr = dxgiFactory->lpVtbl->CreateSwapChainForHwnd(dxgiFactory, cmdQueDX12, viewHandle, &swapChainDesc, NULL, NULL, &swapChain1);
    dxThrowIfFailed(hr);

    hr = dxgiFactory->lpVtbl->MakeWindowAssociation(dxgiFactory, viewHandle, DXGI_MWA_NO_ALT_ENTER);
    dxThrowIfFailed(hr);
    break;
  default:
    /* TODO: ? */
    return NULL;
  }

  hr = swapChain1->lpVtbl->QueryInterface(swapChain1, &IID_IDXGISwapChain3, (void**)&swapChain3);
  swapChain1->lpVtbl->Release(swapChain1);
  dxThrowIfFailed(hr);

  frameIndex = swapChain3->lpVtbl->GetCurrentBackBufferIndex(swapChain3);

  swapChainDX12             = calloc(1, sizeof(GPUSwapChainDX12));
  swapChainDX12->swapChain  = swapChain3;
  swapChainDX12->frameIndex = frameIndex;
  
  swapChain                 = calloc(1, sizeof(*swapChain));
  swapChain->_priv          = swapChainDX12;

  return swapChain;
}

GPU_HIDE
void
dx12_initSwapChain(GPUApiSwapChain* apiSwapChain) {
  apiSwapChain->createSwapChainForView = dx12_createSwapChainForView;
}
