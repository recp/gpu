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
GPUSwapChain*
dx12_createSwapChain(GPUApi          * __restrict api,
                     GPUDevice       * __restrict device,
                     GPUCommandQueue * __restrict cmdQue,
                     GPUSurface      * __restrict surface,
                     GPUExtent2D                  size,
                     bool                         autoResize) {
  GPUInstance                *inst;
  GPUInstanceDX12            *instDX12;
  GPUSwapChain               *swapChain;
  ID3D12Device               *d3dDevice;
  ID3D12CommandQueue         *cmdQueDX12;
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

  inst     = device->inst;
  instDX12 = inst->_priv;

  swapChainDX12                  = NULL;
  swapChainDesc.BufferCount      = FrameCount;
  swapChainDesc.Width            = size.width;
  swapChainDesc.Height           = size.height;
  swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  d3dDevice   = device->priv;
  cmdQueDX12  = cmdQue->priv;
  dxgiFactory = instDX12->dxgiFactory;

  switch (surface->type) {
  case GPU_SURFACE_WINDOWS_COREWINDOW:
    DXCHECK(dxgiFactory->lpVtbl->CreateSwapChainForCoreWindow(dxgiFactory, 
                                                              (IUnknown *)cmdQueDX12, 
                                                              surface->_priv, 
                                                              &swapChainDesc, 
                                                              NULL, 
                                                              &swapChain1));
    break;
  case GPU_SURFACE_WINDOWS_HWND:
    DXCHECK(dxgiFactory->lpVtbl->CreateSwapChainForHwnd(dxgiFactory, 
                                                        (IUnknown *)cmdQueDX12, 
                                                        surface->_priv, 
                                                        &swapChainDesc, 
                                                        NULL, 
                                                        NULL, 
                                                        &swapChain1));
    DXCHECK(dxgiFactory->lpVtbl->MakeWindowAssociation(dxgiFactory, 
                                                       surface->_priv, 
                                                       DXGI_MWA_NO_ALT_ENTER));
    break;
  default:
    /* TODO: ? */
    return NULL;
  }

  hr = swapChain1->lpVtbl->QueryInterface(swapChain1, &IID_IDXGISwapChain3, (void**)&swapChain3);
  swapChain1->lpVtbl->Release(swapChain1);

  swapChainDX12 = calloc(1, sizeof(*swapChainDX12));
  frameIndex    = swapChain3->lpVtbl->GetCurrentBackBufferIndex(swapChain3);

  // Create Descriptor Heap for Render Target Views
  rtvHeapDesc.NumDescriptors = FrameCount;
  rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  DXCHECK(d3dDevice->lpVtbl->CreateDescriptorHeap(d3dDevice, 
                                                  &rtvHeapDesc, 
                                                  &IID_ID3D12DescriptorHeap, 
                                                  (void **)&rtvHeap));

  // Create Render Target Views for each back buffer
  p_rtvHandle       = rtvHeap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtvHeap, &rtvHandle);
  rtvDescriptorSize = d3dDevice->lpVtbl->GetDescriptorHandleIncrementSize(d3dDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  for (i = 0; i < FrameCount; i++) {
    frame = &swapChainDX12->frames[i];
    DXCHECK(swapChain3->lpVtbl->GetBuffer(swapChain3, 
                                          i, 
                                          &IID_ID3D12Resource, 
                                          (void **)&frame->renderTarget));

    d3dDevice->lpVtbl->CreateRenderTargetView(d3dDevice, frame->renderTarget, NULL, rtvHandle);
    rtvHandle.ptr += rtvDescriptorSize;

    DXCHECK(d3dDevice->lpVtbl->CreateCommandAllocator(d3dDevice,
                                                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      &IID_ID3D12CommandAllocator,
                                                      (void **)&(frame->commandAllocator)));
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
      frame = &swapChainDX12->frames[i];
      if (frame->renderTarget) {
        frame->renderTarget->lpVtbl->Release(frame->renderTarget);
      }

      if (frame->commandAllocator) {
        frame->commandAllocator->lpVtbl->Release(frame->commandAllocator);
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

  dxThrowIfFailed(hr);
  return NULL;
}

GPU_HIDE
void
dx12_initSwapChain(GPUApiSwapChain* apiSwapChain) {
  apiSwapChain->createSwapChain = dx12_createSwapChain;
}
