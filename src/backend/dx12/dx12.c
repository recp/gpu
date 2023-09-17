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

#include <stdio.h>
#include <stdlib.h>


inline void ThrowIfFailed(HRESULT hr)
{
  if (FAILED(hr))
  {
    // Print an error message and exit.
    fprintf(stderr, "An error occurred: 0x%08lx\n", hr);
    exit(EXIT_FAILURE);
  }
}

void
GetHardwareAdapter(IDXGIFactory4* dxgiFactory, IDXGIAdapter1** ppAdapter) {
  IDXGIAdapter1* adapter = NULL;
  *ppAdapter = NULL;

  UINT adapterIndex = 0;
  while (dxgiFactory->lpVtbl->EnumAdapters1(dxgiFactory, adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->lpVtbl->GetDesc1(adapter, &desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      // Don't select the Basic Render Driver adapter.
      adapter->lpVtbl->Release(adapter);  // Release the current adapter before moving to next
      adapterIndex++;
      continue;
    }

    // Check to see if the adapter supports Direct3D 12, but don't create the
    // actual device yet.
    if (SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter,
                                    D3D_FEATURE_LEVEL_11_0, 
                                    &IID_ID3D12Device, NULL))) {
      *ppAdapter = adapter; // Transfer ownership to caller
      return;
    }

    adapter->lpVtbl->Release(adapter); // Release the current adapter before moving to next
    adapterIndex++;
  }
}

void
dx12__CreateDevice() {
  ID3D12Device   *m_d3dDevice;
  IDXGIFactory4  *m_dxgiFactory;
  IDXGIAdapter1  *adapter;
  HRESULT         hr;

#if defined(_DEBUG)
  // If the project is in a debug build, enable debugging via SDK Layers.
  {
    ID3D12Debug* debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, 
                  (void**)&debugController))) {
      debugController->lpVtbl->EnableDebugLayer(debugController);
      debugController->lpVtbl->Release(debugController);
    }
  }
#endif

  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&m_dxgiFactory);
  ThrowIfFailed(hr);

  GetHardwareAdapter(m_dxgiFactory, &adapter);

  // Create the Direct3D 12 API device object
  hr = D3D12CreateDevice((IUnknown*)adapter,
                         D3D_FEATURE_LEVEL_11_0,
                         &IID_ID3D12Device,
                         (void**)&m_d3dDevice);

  if (adapter) {
    adapter->lpVtbl->Release(adapter);
  }

#if defined(_DEBUG)
  if (FAILED(hr)) {
    IDXGIAdapter* warpAdapter;
    hr = m_dxgiFactory->lpVtbl->EnumWarpAdapter(m_dxgiFactory, &IID_IDXGIAdapter, (void**)&warpAdapter);

    ThrowIfFailed(hr);

    hr = D3D12CreateDevice((IUnknown*)warpAdapter,
                           D3D_FEATURE_LEVEL_11_0,
                           &IID_ID3D12Device,
                           (void**)&m_d3dDevice);

    if (warpAdapter) {
      warpAdapter->lpVtbl->Release(warpAdapter);
    }
#endif

    ThrowIfFailed(hr);
  }
}