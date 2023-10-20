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
dx12_getAvailablePhysicalDevicesBy(GPUInstance * __restrict inst,
                                   uint32_t                 maxNumberOfItems) {
  GPUPhysicalDeviceDX12 *phyDeviceDX12;
  GPUInstanceDX12       *instDX12;
  IDXGIFactory4         *dxgiFactory;
  GPUPhysicalDevice     *firstDevice, *lastDevice, *item;
  IDXGIAdapter1         *dxgiAdapter;
  IDXGIAdapter          *warpAdapter;
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
      phyDeviceDX12              = calloc(1, sizeof(*phyDeviceDX12));
      phyDeviceDX12->dxgiAdapter = dxgiAdapter;

      dxgiAdapter->lpVtbl->GetDesc1(dxgiAdapter, &phyDeviceDX12->desc1);

      if (phyDeviceDX12->desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        /* Don't select the Basic Render Driver adapter.*/
        /* Release the current adapter before moving to next */
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        free(phyDeviceDX12);
        goto nxt;
      }

      /* Check to see if the adapter supports Direct3D 12, but don't create the
         actual device yet.*/
      if (FAILED(D3D12CreateDevice((IUnknown *)dxgiAdapter,
                                   D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, NULL))) {
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        free(phyDeviceDX12);
        goto nxt;
      }

      item = calloc(1, sizeof(*item));
      item->_priv                      = phyDeviceDX12;
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
    item->_priv                      = phyDeviceDX12;
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

GPU_EXPORT
GPUPhysicalDevice *
dx12_autoSelectPhysicalDeviceIn(GPUInstance       * __restrict inst,
                                GPUPhysicalDevice * __restrict deviceList) {
  GPUPhysicalDevice     *item;
  GPUPhysicalDeviceDX12 *itemDX12;
  GPUPhysicalDevice     *devicesByType[4] = {0}; /* Four main types for DX12 */
  DXGI_ADAPTER_DESC1    *desc;
  uint32_t               i;

  item = deviceList;

  /* Classify devices into different categories based on criteria */
  while (item) {
    itemDX12 = item->_priv;
    desc     = &itemDX12->desc1;

    if (itemDX12->isWarp) {
      devicesByType[3] = item; /* WARP */
    } else if (desc->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      devicesByType[2] = item; /* Other (software adapters excluding WARP) */
    } else if (desc->DedicatedVideoMemory > 1 * 1024 * 1024 * 1024) {
      devicesByType[0] = item; /* Discrete GPU */
    } else if (desc->DedicatedVideoMemory > 0) {
      devicesByType[1] = item; /* Integrated GPU */
    }

    item = item->next;
  }

  /* Device selection based on priority */
  GPUPhysicalDevice *priorityList[] = {
      devicesByType[0], /* Discrete GPU   */
      devicesByType[1], /* Integrated GPU */
      devicesByType[2], /* Other          */
      devicesByType[3]  /* WARP           */
  };

  /* select device based on priority */
  for (i = 0; i < 4 && !(item = priorityList[i]); ++i);

  if (!item) { goto err; }

#if DEBUG
  desc = &((GPUPhysicalDeviceDX12 *)item->_priv)->desc1; /* Directly access stored description */
  fprintf(stderr, "Selected GPU: %S, type: %d\n", desc->Description, desc->VendorId);
#endif

  return item;

err:
  return NULL;
}

GPU_HIDE
GPUPhysicalDevice *
dx12_getAutoSelectedPhysicalDevice(GPUInstance *__restrict inst) {
  GPUPhysicalDevice *deviceList;
  GPUPhysicalDevice *selectedDevice;

  deviceList     = dx12_getAvailablePhysicalDevicesBy(inst, 3);
  selectedDevice = dx12_autoSelectPhysicalDeviceIn(inst, deviceList);

  /* TODO: keep link for mem management */
  return selectedDevice;
}

GPU_HIDE
GPUDevice *
dx12_createDevice(GPUPhysicalDevice * __restrict phyDevice,
                  GPUCommandQueueCreateInfo      queCI[],
                  uint32_t                       nQueCI) {
  GPUInstance           *inst;
  GPUInstanceDX12       *instDX12;
  GPUPhysicalDeviceDX12 *phyDeviceDX12;
  GPUDevice             *device;
  ID3D12Device          *d3dDevice;
  GPU__DX12             *dx12api;
  HRESULT                hr;

  if (!(inst = phyDevice->inst)
    || !(phyDeviceDX12 = phyDevice->_priv)) {
    goto err;
  }

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI);

  instDX12  = inst->_priv;
  d3dDevice = NULL;

  DXCHECK(D3D12CreateDevice(phyDeviceDX12->dxgiAdapter,
                            D3D_FEATURE_LEVEL_11_0,
                            &IID_ID3D12Device,
                            (void **)&d3dDevice));
                    
  device            = calloc(1, sizeof(*device));
  device->inst      = inst;
  device->_priv     = d3dDevice;
  device->phyDevice = phyDevice;

  return device;
err:
  dxThrowIfFailed(hr);

  return NULL;
}

GPU_HIDE
GPUDevice*
dx12_createSystemDefaultDevice(GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;

  if (!(phyDevice = dx12_getAutoSelectedPhysicalDevice(inst))) {
    goto err;
  }

  return dx12_createDevice(phyDevice, NULL, 0);

err:
  return NULL;
}

GPU_HIDE
void
dx12_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = dx12_getAvailablePhysicalDevicesBy;
  apiDevice->autoSelectPhysicalDeviceIn    = dx12_autoSelectPhysicalDeviceIn;
  apiDevice->getAvailablePhysicalDevicesBy = dx12_getAvailablePhysicalDevicesBy;
  apiDevice->createDevice                  = dx12_createDevice;
  apiDevice->createSystemDefaultDevice     = dx12_createSystemDefaultDevice;
}
