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
  GPUDeviceDX12           *deviceDX12;
  GPUCommandQueue         *cq;
  ID3D12Device            *d3dDevice;
  ID3D12CommandQueue      *commandQueue;
  D3D12_COMMAND_QUEUE_DESC queueDesc = {0};
  HRESULT                  hr;

  deviceDX12      = device->_priv;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;

  d3dDevice = deviceDX12->d3dDevice;
  hr        = d3dDevice->lpVtbl->CreateCommandQueue(d3dDevice, 
                                                    &queueDesc, 
                                                    &IID_ID3D12CommandQueue,
                                                    (void**)&commandQueue);

  dxThrowIfFailed(hr);

  cq        = (GPUCommandQueue*)calloc(1, sizeof(*cq));
  cq->_priv = commandQueue;

  return cq;
}

GPU_HIDE
GPUCommandQueue*
dx12_getCommandQueue(GPUDevice *__restrict device,
                     GPUQueueFlagBits      bits) {
  GPUCommandQueue *que;
  GPUDeviceDX12   *deviceDX12;

  deviceDX12 = device->_priv;

  /* TODO: select wisely */
  que = deviceDX12->createdQueues[0];

  return que;
}

GPU_HIDE
void
dx12_initCmdQue(GPUApiCommandQueue* api) {
  api->newCommandQueue = dx12_newCommandQueue;
  api->getCommandQueue = dx12_getCommandQueue;

  // api->newCommandBuffer        = mt_newCommandBuffer;
  // api->commandBufferOnComplete = mt_ccmdbufOnComplete;
  // api->commit                  = mt_cmdbufCommit;
}
