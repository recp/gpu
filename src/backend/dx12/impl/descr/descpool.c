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

#include "../../common.h"

typedef struct GPUDescriptorPoolDX12 {
  ID3D12DescriptorHeap *descHeap;
} GPUDescriptorPoolDX12;

GPU_HIDE
GPUDescriptorPool*
dx12_createDescriptorPool(GPUApi    * __restrict api,
                          GPUDevice *__restrict device) {
  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {0};
  GPUDeviceDX12             *deviceDX12;
  ID3D12Device              *d3dDevice;
  GPU__DX12                 *dx12api;
  ID3D12DescriptorHeap      *descHeap;
  GPUDescriptorPoolDX12     *descPoolDX12;
  GPUDescriptorPool         *descPool;
  HRESULT                    hr;

  deviceDX12 = device->_priv;
  d3dDevice  = deviceDX12->d3dDevice;
  dx12api    = api->reserved;

  srvHeapDesc.NumDescriptors = 1;
  srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  DXCHECK(d3dDevice->lpVtbl->CreateDescriptorHeap(d3dDevice, 
                                                  &srvHeapDesc, 
                                                  &IID_ID3D12DescriptorHeap, 
                                                  (void **)&descHeap));

  descPoolDX12 = calloc(1, sizeof(*descPoolDX12));
  descPoolDX12->descHeap = descHeap;

  descPool = calloc(1, sizeof(*descPool));
  descPool->_priv = descPoolDX12;

  return descPool;

err:
  dxThrowIfFailed(hr);
  return NULL;
}

GPU_HIDE
void
dx12_initDescriptor(GPUApiDescriptor* apiDescriptor) {
  apiDescriptor->createDescriptorPool = dx12_createDescriptorPool;
}
