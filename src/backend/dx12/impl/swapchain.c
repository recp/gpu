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
  IDXGISwapChain3        *swapChain;
  ID3D12DescriptorHeap   *rtvHeap;
  ID3D12Resource         *renderTargets[FrameCount];
  ID3D12CommandAllocator *commandAllocators[FrameCount];
  GPUFrame                frames[FrameCount];

  UINT                    frameIndex;
  UINT                    rtvDescriptorSize;
} GPUSwapChainDX12;

GPUSwapChain*
dx12_createSwapChainForView(GPUApi          * __restrict api,
                            GPUDevice       * __restrict device,
                            GPUCommandQueue * __restrict cmdQue,
                            void            * __restrict viewHandle,
                            GPUWindowType                viewHandleType,
                            float                        backingScaleFactor,
                            uint32_t                     width,
                            uint32_t                     height,
                            bool                         autoResize) {
  GPUSwapChain               *swapChain;
  ID3D12Device               *d3dDevice;
  ID3D12CommandQueue         *cmdQueDX12;
  GPU__DX12                  *dx12api;
  IDXGIFactory4              *dxgiFactory;
  IDXGISwapChain1            *swapChain1;
  IDXGISwapChain3            *swapChain3;
  ID3D12DescriptorHeap       *rtvHeap;
  GPUSwapChainDX12           *swapChainDX12;
  HRESULT                     hr;
  DXGI_SWAP_CHAIN_DESC1       swapChainDesc = {0};
  D3D12_DESCRIPTOR_HEAP_DESC  rtvHeapDesc   = {0};
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, *p_rtvHandle;
  UINT                        i, frameIndex;
  SIZE_T                      rtvDescriptorSize;

  swapChainDesc.BufferCount      = FrameCount;
  swapChainDesc.Width            = width;
  swapChainDesc.Height           = height;
  swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  d3dDevice   = device->priv;
  dx12api     = api->reserved;
  cmdQueDX12  = cmdQue->priv;
  dxgiFactory = dx12api->dxgiFactory;

  switch (viewHandleType) {
  case GPU_WINDOW_TYPE_COREWINDOW:
    hr = dxgiFactory->lpVtbl->CreateSwapChainForCoreWindow(dxgiFactory, (IUnknown *)cmdQueDX12, viewHandle, &swapChainDesc, NULL, &swapChain1);
    dxThrowIfFailed(hr);
    break;
  case GPU_WINDOW_TYPE_HWND:
    hr = dxgiFactory->lpVtbl->CreateSwapChainForHwnd(dxgiFactory, (IUnknown *)cmdQueDX12, viewHandle, &swapChainDesc, NULL, NULL, &swapChain1);
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

  swapChainDX12 = calloc(1, sizeof(*swapChainDX12));
  frameIndex    = swapChain3->lpVtbl->GetCurrentBackBufferIndex(swapChain3);

  // Create Descriptor Heap for Render Target Views
  rtvHeapDesc.NumDescriptors = FrameCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  hr = d3dDevice->lpVtbl->CreateDescriptorHeap(d3dDevice, &rtvHeapDesc, &IID_ID3D12DescriptorHeap, (void **)&rtvHeap);
  if (FAILED(hr)) {
    goto err;
  }

  // Create Render Target Views for each back buffer
  p_rtvHandle       = rtvHeap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtvHeap, &rtvHandle);
  rtvDescriptorSize = d3dDevice->lpVtbl->GetDescriptorHandleIncrementSize(d3dDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  for (i = 0; i < FrameCount; i++) {
    hr = swapChain3->lpVtbl->GetBuffer(swapChain3, i, &IID_ID3D12Resource, (void **)&swapChainDX12->renderTargets[i]);
    if (FAILED(hr)) {
      goto err;
    }
    d3dDevice->lpVtbl->CreateRenderTargetView(d3dDevice, swapChainDX12->renderTargets[i], NULL, rtvHandle);
    rtvHandle.ptr += rtvDescriptorSize;
  }

  for (i = 0; i < FrameCount; i++) {
    hr = d3dDevice->lpVtbl->CreateCommandAllocator(d3dDevice, 
                                                   D3D12_COMMAND_LIST_TYPE_DIRECT, 
                                                   &IID_ID3D12CommandAllocator, 
                                                   (void **)&(swapChainDX12->commandAllocators[i]));
    if (FAILED(hr)) {
      goto err;
    }
  }

  swapChainDX12->swapChain         = swapChain3;
  swapChainDX12->frameIndex        = frameIndex;
  swapChainDX12->rtvHeap           = rtvHeap;
  swapChainDX12->rtvDescriptorSize = rtvDescriptorSize;

  swapChain        = calloc(1, sizeof(*swapChain));
  swapChain->_priv = swapChainDX12;

  return swapChain;
err:
  if (swapChainDX12) {
    for (i = 0; i < FrameCount; i++) {
      if (swapChainDX12->renderTargets[i]) {
        swapChainDX12->renderTargets[i]->lpVtbl->Release(swapChainDX12->renderTargets[i]);
      }

      if (swapChainDX12->commandAllocators[i]) {
        swapChainDX12->commandAllocators[i]->lpVtbl->Release(swapChainDX12->commandAllocators[i]);
      }
    }

    if (swapChainDX12->rtvHeap) {
      swapChainDX12->rtvHeap->lpVtbl->Release(swapChainDX12->rtvHeap);
    }

    if (swapChainDX12->swapChain) {
      swapChainDX12->swapChain->lpVtbl->Release(swapChainDX12->swapChain);
    }

    free(swapChainDX12);
  }
  return NULL;

}

GPU_HIDE
void
dx12_initSwapChain(GPUApiSwapChain* apiSwapChain) {
  apiSwapChain->createSwapChainForView = dx12_createSwapChainForView;
}
