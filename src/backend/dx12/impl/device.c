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
GPUPhysicalDevice *
dx12_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                   GPUInstance * __restrict inst,
                                   uint32_t                 maxNumberOfItems) {
  GPUPhysicalDeviceDX12 *phyDeviceDX12;
  GPUInstanceDX12       *instDX12;
  IDXGIFactory4         *dxgiFactory;
  GPUPhysicalDevice     *firstDevice, *lastDevice, *item;
  IDXGIAdapter1         *dxgiAdapter;
  IDXGIAdapter          *warpAdapter;
  DXGI_ADAPTER_DESC1     desc;
  UINT                   adapterIndex, i;
  HRESULT                hr;
  HRESULT              (*EnumAdapters1)(IDXGIFactory4*, UINT, IDXGIAdapter1**);

  firstDevice   = lastDevice = NULL;
  adapterIndex  = i = 0;
  instDX12      = inst->_priv;
  dxgiFactory   = instDX12->dxgiFactory;
  EnumAdapters1 = dxgiFactory->lpVtbl->EnumAdapters1;

  /* loop until we either enumerate all devices or hit the maximum count. */ 
  while (i < maxNumberOfItems
         && SUCCEEDED(EnumAdapters1(dxgiFactory, adapterIndex, &dxgiAdapter))) {
    if (dxgiAdapter) {
      dxgiAdapter->lpVtbl->GetDesc1(dxgiAdapter, &desc);

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        /* Don't select the Basic Render Driver adapter.*/
        /* Release the current adapter before moving to next */
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        goto nxt;
      }

      /* Check to see if the adapter supports Direct3D 12, but don't create the
         actual device yet.*/
      if (FAILED(D3D12CreateDevice((IUnknown *)dxgiAdapter,
                                   D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, NULL))) {
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        goto nxt;
      }

      item          = calloc(1, sizeof(*item));
      phyDeviceDX12 = calloc(1, sizeof(*phyDeviceDX12));

      phyDeviceDX12->dxgiAdapter       = dxgiAdapter;
      item->priv                       = phyDeviceDX12;
      item->inst                       = inst;
      item->separatePresentQueue       = 1; /* builtin */
      item->supportsDisplayTiming      = 1; /* TODO: not builtin, implement later */
      item->supportsIncrementalPresent = 1; /* TODO: not builtin, implement later */
      item->supportsSwapchain          = 1; /* builtin */

      /* add to linked list of devices */
      if (lastDevice) { lastDevice->next = item; } 
      else            { firstDevice      = item; }
      lastDevice = item;

      /* move on to the next adapter. */
      /* dxgiAdapter->lpVtbl->Release(dxgiAdapter); */
      dxgiAdapter = NULL;
      i++;
    }
  nxt:
    adapterIndex++;
  }

  /* use WARP if there is no Phy Device */
  if (!firstDevice) {
    DXCHECK(dxgiFactory->lpVtbl->EnumWarpAdapter(dxgiFactory, 
                                                 &IID_IDXGIAdapter, 
                                                 (void **)&warpAdapter));
     item          = calloc(1, sizeof(*item));
     phyDeviceDX12 = calloc(1, sizeof(*phyDeviceDX12));

     phyDeviceDX12->dxgiAdapter       = (IUnknown *)warpAdapter;
     phyDeviceDX12->isWarp            = true;
     item->priv                       = phyDeviceDX12;
     item->inst                       = inst;
     item->separatePresentQueue       = 1; /* builtin */
     item->supportsDisplayTiming      = 1; /* TODO: not builtin, implement later */
     item->supportsIncrementalPresent = 1; /* TODO: not builtin, implement later */
     item->supportsSwapchain          = 1; /* builtin */

     firstDevice = item;
  }

  return firstDevice;
err:
  dxThrowIfFailed(hr);
  return NULL;
}

GPU_HIDE
GPUDevice*
dx12_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUInstanceDX12       *instDX12;
  GPUPhysicalDevice     *phyDevice;
  GPUPhysicalDeviceDX12 *phyDeviceDX12;
  GPUDevice             *device;
  ID3D12Device          *d3dDevice;
  GPU__DX12             *dx12api;
  HRESULT                hr;

  if (!(phyDevice = GPUGetFirstPhysicalDevice(inst))
    || !(phyDeviceDX12 = phyDevice->priv)) {
    goto err;
  }

  dx12api   = api->reserved;
  instDX12  = inst->_priv;
  d3dDevice = NULL;

  DXCHECK(D3D12CreateDevice(phyDeviceDX12->dxgiAdapter,
                            D3D_FEATURE_LEVEL_11_0,
                            &IID_ID3D12Device,
                            (void **)&d3dDevice));

  device            = calloc(1, sizeof(*device));
  device->inst      = inst;
  device->priv      = d3dDevice;
  device->phyDevice = phyDevice;

  // TODO: remove
  dx12api              = api->reserved;
  dx12api->adapter     = phyDeviceDX12->dxgiAdapter;
  dx12api->d3dDevice   = d3dDevice;
  dx12api->dxgiFactory = instDX12->dxgiFactory;

  return device;

err:
  dxThrowIfFailed(hr);

  return NULL;
}

GPU_HIDE
GPUDevice *
dx12_createDevice(GPUPhysicalDevice *phyDevice) {
  GPUInstance           *inst;
  GPUInstanceDX12       *instDX12;
  GPUPhysicalDeviceDX12 *phyDeviceDX12;
  GPUDevice             *device;
  ID3D12Device          *d3dDevice;
  GPU__DX12             *dx12api;
  HRESULT                hr;

  if (!(inst = phyDevice->inst)
    || !(phyDeviceDX12 = phyDevice->priv)) {
    goto err;
  }

  instDX12  = inst->_priv;
  d3dDevice = NULL;

  DXCHECK(D3D12CreateDevice(phyDeviceDX12->dxgiAdapter,
                            D3D_FEATURE_LEVEL_11_0,
                            &IID_ID3D12Device,
                            (void **)&d3dDevice));
                    
  device            = calloc(1, sizeof(*device));
  device->inst      = inst;
  device->priv      = d3dDevice;
  device->phyDevice = phyDevice;

  return device;
err:
  dxThrowIfFailed(hr);

  return NULL;
}

GPU_HIDE
void
dx12_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = dx12_getAvailablePhysicalDevicesBy;
  apiDevice->createSystemDefaultDevice     = dx12_createSystemDefaultDevice;
  apiDevice->createDevice                  = dx12_createDevice;
}
