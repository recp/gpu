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

#ifndef dx12_common_h
#define dx12_common_h

#include "../../common.h"

#include <dxgi1_4.h>
#include <d3d12.h>

#define FrameCount 2

typedef struct GPUFrameDX12 {
  IDXGISwapChain3        *swapChain;
  ID3D12Resource         *renderTarget;
  ID3D12CommandAllocator *commandAllocator;
} GPUFrameDX12;

typedef struct GPUSwapChainDX12 {
  IDXGISwapChain3        *swapChain;
  ID3D12DescriptorHeap   *rtvHeap;
  GPUFrameDX12            frames[FrameCount];

  UINT                    frameIndex;
  UINT                    rtvDescriptorSize;
} GPUSwapChainDX12;

typedef struct GPU__DX12 {
  ID3D12Device  *d3dDevice;
  IDXGIFactory4 *dxgiFactory;
  IDXGIAdapter1 *adapter;
} GPU__DX12;

GPU_INLINE
void
dxThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    /* Print an error message and exit. */
    fprintf(stderr, "An error occurred: 0x%08lx\n", hr);
    exit(EXIT_FAILURE);
  }
}

#endif /* dx12_common_h */

