/*
 * Copyright (C) 2026 Recep Aslantas
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
#include "impl.h"

#include <wchar.h>

static const uint8_t dx12_agilityModuleAnchor;

static char *
dx12_agilityPath(void) {
  static const wchar_t suffix[] = L"D3D12\\";
  HMODULE               module;
  wchar_t              *widePath;
  char                 *path;
  DWORD                 capacity;
  DWORD                 length;
  int                   pathSize;

  module   = NULL;
  widePath = NULL;
  path     = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)(const void *)&dx12_agilityModuleAnchor,
                          &module)) {
    return NULL;
  }

  capacity = 512u;
  for (;;) {
    wchar_t *next;

    next = realloc(widePath, (size_t)capacity * sizeof(*widePath));
    if (!next) {
      free(widePath);
      return NULL;
    }
    widePath = next;
    length   = GetModuleFileNameW(module, widePath, capacity);
    if (length == 0u) {
      free(widePath);
      return NULL;
    }
    if (length < capacity - 1u) {
      break;
    }
    if (capacity >= 32768u) {
      free(widePath);
      return NULL;
    }
    capacity *= 2u;
  }

  while (length > 0u &&
         widePath[length - 1u] != L'\\' &&
         widePath[length - 1u] != L'/') {
    length--;
  }
  if (length == 0u) {
    free(widePath);
    return NULL;
  }
  if (length + GPU_ARRAY_LEN(suffix) > capacity) {
    wchar_t *next;

    capacity = length + (DWORD)GPU_ARRAY_LEN(suffix);
    next = realloc(widePath, (size_t)capacity * sizeof(*widePath));
    if (!next) {
      free(widePath);
      return NULL;
    }
    widePath = next;
  }
  memcpy(widePath + length, suffix, sizeof(suffix));

  pathSize = WideCharToMultiByte(CP_UTF8,
                                 WC_ERR_INVALID_CHARS,
                                 widePath,
                                 -1,
                                 NULL,
                                 0,
                                 NULL,
                                 NULL);
  if (pathSize <= 0) {
    free(widePath);
    return NULL;
  }
  path = malloc((size_t)pathSize);
  if (!path ||
      WideCharToMultiByte(CP_UTF8,
                          WC_ERR_INVALID_CHARS,
                          widePath,
                          -1,
                          path,
                          pathSize,
                          NULL,
                          NULL) != pathSize) {
    free(path);
    path = NULL;
  }
  free(widePath);
  return path;
}

GPU_HIDE
bool
dx12_createAgilityFactory(ID3D12DeviceFactory **outFactory) {
  ID3D12SDKConfiguration1 *configuration;
  ID3D12DeviceFactory     *factory;
  const char              *operation;
  char                    *path;
  HRESULT                  result;

  if (!outFactory) {
    return false;
  }
  *outFactory   = NULL;
  configuration = NULL;
  factory       = NULL;
  operation     = "query configuration";
  path          = dx12_agilityPath();
  if (!path) {
    fprintf(stderr, "GPU: failed to locate the app-local Agility SDK\n");
    return false;
  }

  result = D3D12GetInterface(&CLSID_D3D12SDKConfiguration,
                             &IID_ID3D12SDKConfiguration1,
                             (void **)&configuration);
  if (SUCCEEDED(result)) {
    operation = "create device factory";
    result = configuration->lpVtbl->CreateDeviceFactory(
      configuration,
      GPU_DX12_AGILITY_SDK_NUMBER,
      path,
      &IID_ID3D12DeviceFactory,
      (void **)&factory
    );
  }
  if (SUCCEEDED(result)) {
    operation = "configure device factory";
    result = factory->lpVtbl->SetFlags(
      factory,
      D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE
    );
  }
  if (configuration) {
    configuration->lpVtbl->Release(configuration);
  }
  free(path);
  if (FAILED(result) || !factory) {
    if (factory) {
      factory->lpVtbl->Release(factory);
    }
    fprintf(stderr,
            "GPU: failed to %s for DirectX 12 Agility SDK %u (0x%08lx)\n",
            operation,
            (unsigned int)GPU_DX12_AGILITY_SDK_NUMBER,
            (unsigned long)result);
#if GPU_DX12_AGILITY_SDK_PREVIEW
    fprintf(stderr,
            "GPU: this Agility SDK preview requires Windows Developer Mode\n");
#endif
    return false;
  }

  *outFactory = factory;
  return true;
}

GPU_HIDE
HRESULT
dx12_createNativeDevice(const GPUInstanceDX12 *instance,
                        IUnknown               *adapter,
                        REFIID                  iid,
                        void                  **outDevice) {
  if (!instance || !instance->deviceFactory || !outDevice) {
    return E_INVALIDARG;
  }
  return instance->deviceFactory->lpVtbl->CreateDevice(
    instance->deviceFactory,
    adapter,
    D3D_FEATURE_LEVEL_11_0,
    iid,
    outDevice
  );
}

GPU_HIDE
HRESULT
dx12_getConfigurationInterface(const GPUInstanceDX12 *instance,
                               REFCLSID                classId,
                               REFIID                  interfaceId,
                               void                  **outInterface) {
  if (!instance || !instance->deviceFactory || !outInterface) {
    return E_INVALIDARG;
  }
  return instance->deviceFactory->lpVtbl->GetConfigurationInterface(
    instance->deviceFactory,
    classId,
    interfaceId,
    outInterface
  );
}

GPU_HIDE
HRESULT
dx12_enableExperimentalFeatures(const GPUInstanceDX12 *instance,
                                uint32_t                featureCount,
                                const IID              *features) {
  if (!instance || !instance->deviceFactory ||
      featureCount == 0u || !features) {
    return E_INVALIDARG;
  }
  return instance->deviceFactory->lpVtbl->EnableExperimentalFeatures(
    instance->deviceFactory,
    featureCount,
    features,
    NULL,
    NULL
  );
}
