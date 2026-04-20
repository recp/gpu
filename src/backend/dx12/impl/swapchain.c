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
//
//HRESULT createSwapChain(ID3D12Device* device, IDXGISwapChain3** swapChain, HWND hwnd, UINT width, UINT height, UINT bufferCount) {
//    HRESULT hr;
//
//    // Create DXGI factory
//    IDXGIFactory4* dxgiFactory;
//    hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&dxgiFactory);
//    if (FAILED(hr)) {
//        return hr;
//    }
//
//    // Create swap chain description
//    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
//    swapChainDesc.Width = width;
//    swapChainDesc.Height = height;
//    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
//    swapChainDesc.Stereo = FALSE;
//    swapChainDesc.SampleDesc.Count = 1;
//    swapChainDesc.SampleDesc.Quality = 0;
//    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
//    swapChainDesc.BufferCount = bufferCount;
//    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
//    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
//    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
//    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
//
//    // Create swap chain
//    switch (surfaceType) {
//        case GPU_SURFACE_WINDOWS_CORE_WINDOW:
//            hr = dxgiFactory->CreateSwapChainForCoreWindow((IUnknown*)commandQueue, hwnd, &swapChainDesc, NULL, swapChain);
//            break;
//        case GPU_SURFACE_WINDOWS_HWND:
//            hr = dxgiFactory->CreateSwapChainForHwnd((IUnknown*)commandQueue, hwnd, &swapChainDesc, NULL, NULL, swapChain);
//            dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
//            break;
//        default:
//            // TODO: Handle other surface types
//            return E_FAIL;
//    }
//
//    // Release DXGI factory
//    dxgiFactory->Release();
//
//    // Query IDXGISwapChain3 interface
//    IDXGISwapChain1* swapChain1;
//    hr = (*swapChain)->QueryInterface(&IID_IDXGISwapChain1, (void**)&swapChain1);
//    if (FAILED(hr)) {
//        return hr;
//    }
//    hr = swapChain1->QueryInterface(&IID_IDXGISwapChain3, (void**)swapChain);
//    swapChain1->Release();
//    if (FAILED(hr)) {
//        return hr;
//    }
//
//    // Create descriptor heap for render target views
//    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
//    rtvHeapDesc.NumDescriptors = bufferCount;
//    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
//    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
//    ID3D12DescriptorHeap* rtvHeap;
//    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
//    if (FAILED(hr)) {
//        return hr;
//    }
//
//    // Create render target views for each back buffer
//    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
//    UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
//    for (UINT i = 0; i < bufferCount; i++) {
//        ID3D12Resource* backBuffer;
//        hr = (*swapChain)->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
//        if (FAILED(hr)) {
//            return hr;
//        }
//        device->CreateRenderTargetView(backBuffer, NULL, rtvHandle);
//        rtvHandle.Offset(1, rtvDescriptorSize);
//    }
//
//    return S_OK;
//}

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
  GPUDeviceDX12              *deviceDX12;
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

  inst       = device->inst;
  deviceDX12 = device->_priv;
  instDX12   = inst->_priv;

  swapChainDX12                  = NULL;
  swapChainDesc.BufferCount      = FrameCount;
  swapChainDesc.Width            = size.width;
  swapChainDesc.Height           = size.height;
  swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  d3dDevice   = deviceDX12->d3dDevice;
  cmdQueDX12  = cmdQue->_priv;
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
