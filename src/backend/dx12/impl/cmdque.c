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
GPUCommandQueue*
dx12_newCommandQueue(GPUDevice* __restrict device) {
  GPUCommandQueue         *cq;
  ID3D12Device            *d3dDevice;
  ID3D12CommandQueue      *commandQueue;
  D3D12_COMMAND_QUEUE_DESC queueDesc = { 0 };
  HRESULT                  hr;

  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;

  d3dDevice = (ID3D12Device*)device->priv;
  hr        = d3dDevice->lpVtbl->CreateCommandQueue(d3dDevice, &queueDesc, &IID_ID3D12CommandQueue, (void**)&commandQueue);

  dxThrowIfFailed(hr);

  cq        = (GPUCommandQueue*)calloc(1, sizeof(*cq));
  cq->priv  = commandQueue;

  return cq;
}

GPU_HIDE
void
dx12_initCmdQue(GPUApiCommandQueue* api) {
  api->newCommandQueue = dx12_newCommandQueue;
  // api->newCommandBuffer        = mt_newCommandBuffer;
  // api->commandBufferOnComplete = mt_ccmdbufOnComplete;
  // api->commit                  = mt_cmdbufCommit;
}
