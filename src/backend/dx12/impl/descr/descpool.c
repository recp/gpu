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

static void
dx12__logRootSignatureError(ID3DBlob *errors) {
  if (errors && errors->lpVtbl->GetBufferPointer(errors)) {
    fprintf(stderr,
            "GPU Direct3D 12 root signature failed: %s\n",
            (const char *)errors->lpVtbl->GetBufferPointer(errors));
  }
}

GPU_HIDE
GPUResult
dx12_createPipelineLayout(GPUDevice         *device,
                          GPUPipelineLayout *layout) {
  GPUPipelineLayoutDX12 *native;
  GPUDeviceDX12         *deviceDX12;
  ID3DBlob              *serialized;
  ID3DBlob              *errors;
  uint32_t               groupCount;
  uint32_t               pushSize;
  HRESULT                result;

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  (void)gpuGetPipelineLayoutGroups(layout, &groupCount);
  gpuGetPipelineLayoutPushConstants(layout, &pushSize, NULL);
  if (groupCount != 0u || pushSize != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  deviceDX12 = device->_priv;
  serialized = NULL;
  errors     = NULL;
  if (deviceDX12->rootSignatureVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {0};

    desc.Version                    = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.Flags             =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeVersionedRootSignature(&desc,
                                                   &serialized,
                                                   &errors);
  } else {
    D3D12_ROOT_SIGNATURE_DESC desc = {0};

    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeRootSignature(&desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         &serialized,
                                         &errors);
  }

  if (FAILED(result) || !serialized) {
    dx12__logRootSignatureError(errors);
    if (errors) {
      errors->lpVtbl->Release(errors);
    }
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateRootSignature(
    deviceDX12->d3dDevice,
    0u,
    serialized->lpVtbl->GetBufferPointer(serialized),
    serialized->lpVtbl->GetBufferSize(serialized),
    &IID_ID3D12RootSignature,
    (void **)&native->rootSignature
  );
  serialized->lpVtbl->Release(serialized);
  if (errors) {
    errors->lpVtbl->Release(errors);
  }
  if (FAILED(result)) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  layout->_native = native;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyPipelineLayout(GPUPipelineLayout *layout) {
  GPUPipelineLayoutDX12 *native;

  native = layout ? layout->_native : NULL;
  if (!native) {
    return;
  }

  if (native->rootSignature) {
    native->rootSignature->lpVtbl->Release(native->rootSignature);
  }
  free(native);
  layout->_native = NULL;
}

GPU_HIDE
void
dx12_initDescriptor(GPUApiDescriptor *api) {
  memset(api, 0, sizeof(*api));
  api->createPipelineLayout  = dx12_createPipelineLayout;
  api->destroyPipelineLayout = dx12_destroyPipelineLayout;
}
