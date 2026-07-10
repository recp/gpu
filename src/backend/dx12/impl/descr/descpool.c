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

enum {
  DX12_ROOT_SIGNATURE_DWORD_LIMIT = 64u
};

static void
dx12__logRootSignatureError(ID3DBlob *errors) {
  if (errors && errors->lpVtbl->GetBufferPointer(errors)) {
    fprintf(stderr,
            "GPU Direct3D 12 root signature failed: %s\n",
            (const char *)errors->lpVtbl->GetBufferPointer(errors));
  }
}

static D3D12_SHADER_VISIBILITY
dx12__shaderVisibility(GPUShaderStageFlags visibility) {
  if (visibility == GPU_SHADER_STAGE_VERTEX_BIT) {
    return D3D12_SHADER_VISIBILITY_VERTEX;
  }
  if (visibility == GPU_SHADER_STAGE_FRAGMENT_BIT) {
    return D3D12_SHADER_VISIBILITY_PIXEL;
  }

  return D3D12_SHADER_VISIBILITY_ALL;
}

static GPUResult
dx12__rootBindingCount(GPUBindGroupLayout * const *groups,
                       uint32_t                    groupCount,
                       uint32_t                   *outCount) {
  uint32_t bindingCount;

  if (!outCount || groupCount > GPU_ENCODER_MAX_BIND_GROUPS ||
      (groupCount > 0u && !groups)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  bindingCount = 0u;
  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    uint32_t                       entryCount;
    uint32_t                       backendBindingCount;

    if (!groups[groupIndex]) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetBindGroupLayoutBackendBindings(
      groups[groupIndex],
      &backendBindingCount
    );
    if (entryCount != backendBindingCount ||
        (entryCount > 0u && (!entries || !backendBindings))) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t i = 0u; i < entryCount; i++) {
      if (entries[i].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
          entries[i].arrayCount != 1u || entries[i].immutableSampler) {
        return GPU_ERROR_UNSUPPORTED;
      }
      if (entries[i].visibility == 0u || backendBindings[i] == UINT32_MAX) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      if (bindingCount == UINT32_MAX) {
        return GPU_ERROR_UNSUPPORTED;
      }
      bindingCount++;
    }
  }

  if (bindingCount > DX12_ROOT_SIGNATURE_DWORD_LIMIT / 2u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outCount = bindingCount;
  return GPU_OK;
}

static void
dx12__fillRootBindingPlan(GPUBindGroupLayout * const *groups,
                          uint32_t                    groupCount,
                          GPUPipelineLayoutDX12      *native) {
  uint32_t cursor;

  cursor = 0u;
  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    uint32_t                       entryCount;

    native->groupOffsets[groupIndex] = cursor;
    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetBindGroupLayoutBackendBindings(groups[groupIndex],
                                                           NULL);
    for (uint32_t i = 0u; i < entryCount; i++) {
      native->bindings[cursor].groupIndex    = groupIndex;
      native->bindings[cursor].binding       = backendBindings[i];
      native->bindings[cursor].rootParameter = cursor;
      native->bindings[cursor].visibility    = entries[i].visibility;
      native->bindings[cursor].bindingType   = entries[i].bindingType;
      cursor++;
    }
  }
  native->groupOffsets[groupCount] = cursor;
}

static const GPURootBindingDX12 *
dx12__findRootBinding(const GPUPipelineLayoutDX12 *layout,
                      uint32_t                     groupIndex,
                      uint32_t                     binding) {
  uint32_t begin;
  uint32_t end;

  if (!layout || groupIndex >= layout->groupCount) {
    return NULL;
  }

  begin = layout->groupOffsets[groupIndex];
  end   = layout->groupOffsets[groupIndex + 1u];
  for (uint32_t i = begin; i < end; i++) {
    if (layout->bindings[i].binding == binding) {
      return &layout->bindings[i];
    }
  }

  return NULL;
}

GPU_HIDE
GPUResult
dx12_createPipelineLayout(GPUDevice         *device,
                          GPUPipelineLayout *layout) {
  GPUPipelineLayoutDX12      *native;
  GPUBindGroupLayout * const *groups;
  GPUDeviceDX12              *deviceDX12;
  ID3DBlob                   *serialized;
  ID3DBlob                   *errors;
  GPUResult                   bindingResult;
  uint32_t                    bindingCount;
  uint32_t                    groupCount;
  uint32_t                    pushSize;
  HRESULT                     result;

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  gpuGetPipelineLayoutPushConstants(layout, &pushSize, NULL);
  if (pushSize != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  bindingResult = dx12__rootBindingCount(groups,
                                         groupCount,
                                         &bindingCount);
  if (bindingResult != GPU_OK) {
    return bindingResult;
  }

  native = calloc(1,
                  sizeof(*native) +
                    (size_t)bindingCount * sizeof(*native->bindings));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->bindings = bindingCount > 0u
                       ? (GPURootBindingDX12 *)(native + 1)
                       : NULL;
  native->bindingCount = bindingCount;
  native->groupCount   = groupCount;
  dx12__fillRootBindingPlan(groups, groupCount, native);

  deviceDX12 = device->_priv;
  serialized = NULL;
  errors     = NULL;
  if (deviceDX12->rootSignatureVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER1              *parameters;

    parameters = bindingCount > 0u
                   ? calloc(bindingCount, sizeof(*parameters))
                   : NULL;
    if (bindingCount > 0u && !parameters) {
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0u; i < bindingCount; i++) {
      parameters[i].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameters[i].Descriptor.ShaderRegister = native->bindings[i].binding;
      parameters[i].Descriptor.RegisterSpace  = native->bindings[i].groupIndex;
      parameters[i].Descriptor.Flags          =
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
      parameters[i].ShaderVisibility =
        dx12__shaderVisibility(native->bindings[i].visibility);
    }

    desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = bindingCount;
    desc.Desc_1_1.pParameters   = parameters;
    desc.Desc_1_1.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeVersionedRootSignature(&desc,
                                                   &serialized,
                                                   &errors);
    free(parameters);
  } else {
    D3D12_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER     *parameters;

    parameters = bindingCount > 0u
                   ? calloc(bindingCount, sizeof(*parameters))
                   : NULL;
    if (bindingCount > 0u && !parameters) {
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0u; i < bindingCount; i++) {
      parameters[i].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameters[i].Descriptor.ShaderRegister = native->bindings[i].binding;
      parameters[i].Descriptor.RegisterSpace  = native->bindings[i].groupIndex;
      parameters[i].ShaderVisibility =
        dx12__shaderVisibility(native->bindings[i].visibility);
    }

    desc.NumParameters = bindingCount;
    desc.pParameters   = parameters;
    desc.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeRootSignature(&desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         &serialized,
                                         &errors);
    free(parameters);
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

typedef struct DX12RenderBindContext {
  GPURenderEncoderDX12      *encoder;
  GPUPipelineLayoutDX12     *layout;
  GPUDevice                 *device;
  uint32_t                   groupIndex;
  uint32_t                   boundCount;
  bool                       valid;
} DX12RenderBindContext;

static void
dx12__bindRenderRoot(void *context,
                     const GPUBindGroupBindingView *binding) {
  DX12RenderBindContext    *bindContext;
  const GPURootBindingDX12 *rootBinding;
  GPUBufferDX12            *buffer;
  D3D12_GPU_VIRTUAL_ADDRESS address;

  bindContext = context;
  if (!bindContext || !bindContext->valid || !binding ||
      binding->kind != GPUBindKindBuffer ||
      binding->bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !binding->buffer || binding->buffer->device != bindContext->device) {
    if (bindContext) {
      bindContext->valid = false;
    }
    return;
  }

  rootBinding = dx12__findRootBinding(bindContext->layout,
                                      bindContext->groupIndex,
                                      binding->binding);
  buffer = binding->buffer->_priv;
  if (!rootBinding ||
      rootBinding->bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !buffer || !buffer->resource || buffer->gpuAddress == 0u ||
      (binding->offset &
       (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) != 0u ||
      binding->offset > UINT64_MAX - buffer->gpuAddress) {
    bindContext->valid = false;
    return;
  }

  address = buffer->gpuAddress + binding->offset;
  bindContext->encoder->commandList->lpVtbl->SetGraphicsRootConstantBufferView(
    bindContext->encoder->commandList,
    rootBinding->rootParameter,
    address
  );
  bindContext->boundCount++;
}

GPU_HIDE
bool
dx12_bindRenderGroup(GPURenderCommandEncoder *pass,
                     GPUPipelineLayout       *pipelineLayout,
                     uint32_t                 groupIndex,
                     GPUBindGroup            *group,
                     uint32_t                 dynamicOffsetCount,
                     const uint32_t          *dynamicOffsets) {
  DX12RenderBindContext  context;
  GPURenderEncoderDX12  *encoder;
  GPUPipelineLayoutDX12 *layout;
  uint32_t               expectedCount;

  encoder = pass ? pass->_priv : NULL;
  layout  = pipelineLayout ? pipelineLayout->_native : NULL;
  if (!encoder || !encoder->commandList || !layout || !layout->rootSignature ||
      encoder->rootSignature != layout->rootSignature ||
      groupIndex >= layout->groupCount) {
    return false;
  }

  expectedCount = layout->groupOffsets[groupIndex + 1u] -
                  layout->groupOffsets[groupIndex];
  memset(&context, 0, sizeof(context));
  context.encoder    = encoder;
  context.layout     = layout;
  context.device     = pipelineLayout->_device;
  context.groupIndex = groupIndex;
  context.valid      = true;
  return gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                       groupIndex,
                                                       group,
                                                       dynamicOffsetCount,
                                                       dynamicOffsets,
                                                       dx12__bindRenderRoot,
                                                       &context) &&
         context.valid && context.boundCount == expectedCount;
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
  api->bindRenderGroup       = dx12_bindRenderGroup;
}
