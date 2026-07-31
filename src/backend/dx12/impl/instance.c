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
#include "../impl.h"

#if defined(DIRECT3D_LINEAR_ALGEBRA)
static bool
dx12_createLinearAlgebraFactory(ID3D12DeviceFactory **outFactory) {
  return dx12_createExperimentalFactory(
    1u,
    &D3D12ExperimentalShaderModels,
    outFactory
  );
}
#else
static bool
dx12_createLinearAlgebraFactory(ID3D12DeviceFactory **outFactory) {
  if (outFactory) {
    *outFactory = NULL;
  }
  return false;
}
#endif

#if GPU_BUILD_WITH_VALIDATION && defined(_DEBUG)
static bool
dx12_enableDebugLayer(ID3D12DeviceFactory *factory) {
  ID3D12Debug *debugController;

  debugController = NULL;
  if (FAILED(dx12_getConfigurationInterface(factory,
                                            &CLSID_D3D12Debug,
                                            &IID_ID3D12Debug,
                                            (void **)&debugController)) ||
      !debugController) {
    return false;
  }
  debugController->lpVtbl->EnableDebugLayer(debugController);
  debugController->lpVtbl->Release(debugController);
  return true;
}
#endif

GPU_HIDE
GPUInstance *
dx12_createInstance(struct GPUApi * __restrict api,
                    const GPUInstanceCreateInfo * __restrict info) {
  GPUInstance     *inst;
  GPUInstanceDX12 *instDX12;
  IDXGIFactory5   *factory5;
  BOOL             allowTearing;
  HRESULT          hr;

  GPU__UNUSED(api);

  inst     = calloc(1, sizeof(*inst));
  instDX12 = calloc(1, sizeof(*instDX12));
  if (!inst || !instDX12) {
    if (inst)     { free(inst);     }
    if (instDX12) { free(instDX12); }
    return NULL;
  }

  if (!dx12_createAgilityFactory(&instDX12->deviceFactory)) {
    goto err;
  }
  (void)dx12_createLinearAlgebraFactory(
    &instDX12->linearAlgebraFactory
  );

#if GPU_BUILD_WITH_VALIDATION && defined(_DEBUG)
  /* Enable the debug layer (requires the Graphics Tools "optional feature").
     NOTE: Enabling the debug layer after device creation will invalidate the active device.
   */
  if (info && info->enableValidation) {
    if (dx12_enableDebugLayer(instDX12->deviceFactory)) {
      /* Enable additional debug layers. */
      instDX12->dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
    if (instDX12->linearAlgebraFactory) {
      (void)dx12_enableDebugLayer(instDX12->linearAlgebraFactory);
    }
  }
#endif

  hr = CreateDXGIFactory2(instDX12->dxgiFactoryFlags,
                          &IID_IDXGIFactory4,
                          (void **)&instDX12->dxgiFactory);
  if (FAILED(hr)) {
    goto err;
  }

  factory5      = NULL;
  allowTearing = FALSE;
  if (SUCCEEDED(instDX12->dxgiFactory->lpVtbl->QueryInterface(
        instDX12->dxgiFactory,
        &IID_IDXGIFactory5,
        (void **)&factory5)) && factory5) {
    if (FAILED(factory5->lpVtbl->CheckFeatureSupport(
          factory5,
          DXGI_FEATURE_PRESENT_ALLOW_TEARING,
          &allowTearing,
          sizeof(allowTearing)))) {
      allowTearing = FALSE;
    }
    factory5->lpVtbl->Release(factory5);
  }
  instDX12->allowTearing = allowTearing == TRUE;

  inst->_priv      = instDX12;
  if (info) {
    inst->createInfo = *info;
  }

  return inst;

err:
  if (instDX12 && instDX12->dxgiFactory) {
    instDX12->dxgiFactory->lpVtbl->Release(instDX12->dxgiFactory);
  }
  if (instDX12 && instDX12->deviceFactory) {
    instDX12->deviceFactory->lpVtbl->Release(instDX12->deviceFactory);
  }
  if (instDX12 && instDX12->linearAlgebraFactory) {
    instDX12->linearAlgebraFactory->lpVtbl->Release(
      instDX12->linearAlgebraFactory
    );
  }
  if (inst)     { free(inst);     }
  if (instDX12) { free(instDX12); }

  return NULL;
}

GPU_HIDE
void
dx12_destroyInstance(struct GPUApi * __restrict api,
                     GPUInstance * __restrict inst) {
  GPUInstanceDX12 *instDX12;

  GPU__UNUSED(api);

  if (!inst) {
    return;
  }

  instDX12 = inst->_priv;
  if (instDX12) {
    if (instDX12->dxgiFactory) {
      instDX12->dxgiFactory->lpVtbl->Release(instDX12->dxgiFactory);
    }
    if (instDX12->deviceFactory) {
      instDX12->deviceFactory->lpVtbl->Release(instDX12->deviceFactory);
    }
    if (instDX12->linearAlgebraFactory) {
      instDX12->linearAlgebraFactory->lpVtbl->Release(
        instDX12->linearAlgebraFactory
      );
    }
    free(instDX12);
  }
  free(inst);
}

GPU_HIDE
void
dx12_initInstance(GPUApiInstance *apiInstance) {
  apiInstance->createInstance = dx12_createInstance;
  apiInstance->destroyInstance = dx12_destroyInstance;
}
