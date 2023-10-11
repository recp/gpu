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

extern GPUInitParams gpu__defaultInitParams;

GPU_HIDE
GPUInstance *
dx12_createInstance(struct GPUApi * __restrict api, 
                    GPUInitParams * __restrict params) {
  GPUInstance     *inst;
  GPUInstanceDX12 *instDX12;
  HRESULT          hr;

  if (!params) { params = &gpu__defaultInitParams; }

  inst     = calloc(1, sizeof(*inst));
  instDX12 = calloc(1, sizeof(*instDX12));

#if defined(_DEBUG)
  /* Enable the debug layer (requires the Graphics Tools "optional feature").
     NOTE: Enabling the debug layer after device creation will invalidate the active device.
   */
  {
    ID3D12Debug *debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debugController))) {
      debugController->lpVtbl->EnableDebugLayer(debugController);
      debugController->lpVtbl->Release(debugController);

      /* Enable additional debug layers. */
      instDX12->dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  DXCHECK(CreateDXGIFactory2(instDX12->dxgiFactoryFlags, 
                             &IID_IDXGIFactory1, 
                             (void **)&instDX12->dxgiFactory));

  inst->_priv      = instDX12;
  inst->initParams = params;

  return inst;

err:

  if (inst)     { free(inst);     }
  if (instDX12) { free(instDX12); }

  dxThrowIfFailed(hr);

  return NULL;
}

GPU_HIDE
void
dx12_initInstance(GPUApiInstance *apiInstance) {
  apiInstance->createInstance = dx12_createInstance;
}
