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
void
dx12__getHardwareAdapter(IDXGIFactory4* dxgiFactory, IDXGIAdapter1** ppAdapter) {
  IDXGIAdapter1 *adapter;
  UINT           adapterIndex;

  adapter      = NULL;
  *ppAdapter   = NULL;
  adapterIndex = 0;

  while (dxgiFactory->lpVtbl->EnumAdapters1(dxgiFactory, adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->lpVtbl->GetDesc1(adapter, &desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      /* Don't select the Basic Render Driver adapter.*/
      adapter->lpVtbl->Release(adapter);  /* Release the current adapter before moving to next */
      adapterIndex++;
      continue;
    }

    /* Check to see if the adapter supports Direct3D 12, but don't create the
       actual device yet.*/
    if (SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,
                                    D3D_FEATURE_LEVEL_11_0,
                                    &IID_ID3D12Device, NULL))) {
      *ppAdapter = adapter; /* Transfer ownership to caller */
      return;
    }

    adapter->lpVtbl->Release(adapter); /* Release the current adapter before moving to next */
    adapterIndex++;
  }
}

GPU_EXPORT
HRESULT
dx12__createDevice(ID3D12Device** p_d3dDevice, 
                   IDXGIFactory4** p_dxgiFactory, 
                   IDXGIAdapter1** p_adapter) {
  ID3D12Device  *d3dDevice;
  IDXGIFactory4 *dxgiFactory;
  IDXGIAdapter1 *adapter;
  UINT           dxgiFactoryFlags;
  HRESULT        hr;

  dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  /* Enable the debug layer (requires the Graphics Tools "optional feature").
     NOTE: Enabling the debug layer after device creation will invalidate the active device.
   */
  {
    ID3D12Debug* debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,
      (void**)&debugController))) {
      debugController->lpVtbl->EnableDebugLayer(debugController);
      debugController->lpVtbl->Release(debugController);

      /* Enable additional debug layers. */
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  hr = CreateDXGIFactory2(dxgiFactoryFlags, 
                          &IID_IDXGIFactory1, 
                          (void**)&dxgiFactory);
  dxThrowIfFailed(hr);

  dx12__getHardwareAdapter(dxgiFactory, &adapter);

  /* Create the Direct3D 12 API device object */
  hr = D3D12CreateDevice((IUnknown*)adapter,
                         D3D_FEATURE_LEVEL_11_0,
                         &IID_ID3D12Device,
                         (void**)&d3dDevice);

  if (adapter) {
    adapter->lpVtbl->Release(adapter);
  }

#if defined(_DEBUG)
  if (FAILED(hr)) {
    IDXGIAdapter* warpAdapter;
    hr = dxgiFactory->lpVtbl->EnumWarpAdapter(dxgiFactory, 
                                              &IID_IDXGIAdapter, 
                                              (void**)&warpAdapter);

    dxThrowIfFailed(hr);

    hr = D3D12CreateDevice((IUnknown*)warpAdapter,
                           D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device,
                           (void**)&d3dDevice);

    if (warpAdapter) {
      warpAdapter->lpVtbl->Release(warpAdapter);
    }
#endif

    dxThrowIfFailed(hr);
  }

  *p_d3dDevice   = d3dDevice;
  *p_dxgiFactory = dxgiFactory;
  *p_adapter     = adapter;

  return hr;
}

GPU_HIDE
GPUPhysicalDevice *
dx12_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                   GPUInstance * __restrict inst,
                                   uint32_t                 maxNumberOfItems) {
  GPUPhysicalDevice *phyDevice;

  phyDevice = calloc(1, sizeof(*phyDevice));
  phyDevice->separatePresentQueue       = 1;
  phyDevice->supportsDisplayTiming      = 1;
  phyDevice->supportsIncrementalPresent = 1;
  phyDevice->supportsSwapchain          = 1;

  return phyDevice;
}

GPU_HIDE
GPUDevice*
dx12_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUDevice     *device;
  ID3D12Device  *d3dDevice;
  IDXGIFactory4 *dxgiFactory;
  IDXGIAdapter1 *adapter;
  GPU__DX12     *dx12api;
  HRESULT        hr;

  dx12api      = api->reserved;

  d3dDevice    = NULL;
  dxgiFactory  = NULL;
  adapter      = NULL;

  hr           = dx12__createDevice(&d3dDevice, &dxgiFactory, &adapter);

  device       = calloc(1, sizeof(*device));
  device->priv = d3dDevice;

  dx12api              = api->reserved;
  dx12api->adapter     = adapter;
  dx12api->d3dDevice   = d3dDevice;
  dx12api->dxgiFactory = dxgiFactory;

  return device;
}

GPU_HIDE
void
dx12_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = dx12_getAvailablePhysicalDevicesBy;
  apiDevice->createSystemDefaultDevice = dx12_createSystemDefaultDevice;
}
