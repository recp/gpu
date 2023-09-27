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
GPUFrame*
dx12_beginFrame(GPUApi       *__restrict api,
                GPUSwapChain *__restrict swapChain) {
  ID3D12Device           *d3dDevice;
  GPU__DX12              *dx12api;
  GPUFrameDX12           *frameDX12;
  GPUSwapChainDX12       *swapChainDX12;
  ID3D12CommandAllocator *commandAllocator;
  GPUFrameDX12           *frame;
  HRESULT                 hr;
  UINT                    i, frameIndex;
  SIZE_T                  rtvDescriptorSize;

  swapChainDX12    = swapChain->_priv;
  frameIndex       = swapChainDX12->frameIndex;
  frame            = &swapChainDX12->frames[frameIndex];
  commandAllocator = frame->commandAllocator;

  DXCHECK(commandAllocator->lpVtbl->Reset(commandAllocator));

  return frame;

err:
  return NULL;
}

GPU_HIDE
void
dx12_endFrame(GPUApi *__restrict api, GPUFrame *__restrict frame) {
}

GPU_HIDE
void
dx12_initFrame(GPUApiFrame *apiFrame) {
  apiFrame->beginFrame = dx12_beginFrame;
  apiFrame->endFrame   = dx12_endFrame;
}
