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

#include "common.h"

GPU_HIDE
void
dx12__getHardwareAdapter(IDXGIFactory4* dxgiFactory, IDXGIAdapter1** ppAdapter) {
  IDXGIAdapter1* adapter = NULL;
  *ppAdapter = NULL;

  UINT adapterIndex = 0;
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
void
dx12__CreateDevice(GPUApi * __restrict api) {
  GPU__DX12      *dx12api;
  HRESULT         hr;

  dx12api = api->reserved;

#if defined(_DEBUG)
  /* If the project is in a debug build, enable debugging via SDK Layers.*/
  {
    ID3D12Debug* debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, 
                  (void**)&debugController))) {
      debugController->lpVtbl->EnableDebugLayer(debugController);
      debugController->lpVtbl->Release(debugController);
    }
  }
#endif

  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&dx12api->dxgiFactory);
  ThrowIfFailed(hr);

  dx12__getHardwareAdapter(dx12api->dxgiFactory, &dx12api->adapter);

  /* Create the Direct3D 12 API device object */
  hr = D3D12CreateDevice((IUnknown*)dx12api->adapter,
                         D3D_FEATURE_LEVEL_11_0,
                         &IID_ID3D12Device,
                         (void**)&dx12api->d3dDevice);

  if (dx12api->adapter) {
    dx12api->adapter->lpVtbl->Release(dx12api->adapter);
  }

#if defined(_DEBUG)
  if (FAILED(hr)) {
    IDXGIAdapter* warpAdapter;
    hr = dx12api->dxgiFactory->lpVtbl->EnumWarpAdapter(dx12api->dxgiFactory, &IID_IDXGIAdapter, (void**)&warpAdapter);

    ThrowIfFailed(hr);

    hr = D3D12CreateDevice((IUnknown*)warpAdapter,
                           D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device,
                           (void**)&dx12api->d3dDevice);

    if (warpAdapter) {
      warpAdapter->lpVtbl->Release(warpAdapter);
    }
#endif

    ThrowIfFailed(hr);
  }
}