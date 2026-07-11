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
  DX12_ROOT_SIGNATURE_DWORD_LIMIT       = 64u,
  DX12_RESOURCE_DESCRIPTOR_CAPACITY     = 1024u,
  DX12_SAMPLER_DESCRIPTOR_CAPACITY      = 256u
};

static void
dx12__logRootSignatureError(ID3DBlob *errors) {
  if (errors && errors->lpVtbl->GetBufferPointer(errors)) {
    fprintf(stderr,
            "GPU Direct3D 12 root signature failed: %s\n",
            (const char *)errors->lpVtbl->GetBufferPointer(errors));
  }
}

static GPUDescriptorHeapDX12 *
dx12__descriptorHeap(GPUDeviceDX12            *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type) {
  if (!device) {
    return NULL;
  }

  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
    return &device->resourceDescriptors;
  }
  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
    return &device->samplerDescriptors;
  }

  return NULL;
}

static GPUResult
dx12__ensureDescriptorHeap(GPUDeviceDX12             *device,
                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                           GPUDescriptorHeapDX12     *heap) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {0};
  uint32_t                   capacity;
  size_t                     wordCount;
  HRESULT                    result;

  if (!device || !device->d3dDevice || !heap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (heap->heap) {
    return GPU_OK;
  }

  capacity = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
               ? DX12_SAMPLER_DESCRIPTOR_CAPACITY
               : DX12_RESOURCE_DESCRIPTOR_CAPACITY;
  wordCount = (capacity + 63u) / 64u;
  heap->used = calloc(wordCount, sizeof(*heap->used));
  if (!heap->used) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  desc.Type           = type;
  desc.NumDescriptors = capacity;
  desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  result = device->d3dDevice->lpVtbl->CreateDescriptorHeap(
    device->d3dDevice,
    &desc,
    &IID_ID3D12DescriptorHeap,
    (void **)&heap->heap
  );
  if (FAILED(result) || !heap->heap) {
    free(heap->used);
    memset(heap, 0, sizeof(*heap));
    return GPU_ERROR_BACKEND_FAILURE;
  }

  heap->descriptorSize = device->d3dDevice->lpVtbl
    ->GetDescriptorHandleIncrementSize(device->d3dDevice, type);
  heap->capacity = capacity;
  return GPU_OK;
}

static GPUResult
dx12__ensureDescriptorHeaps(GPUDeviceDX12 *device) {
  GPUResult result;

  result = dx12__ensureDescriptorHeap(device,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                      &device->resourceDescriptors);
  if (result != GPU_OK) {
    return result;
  }

  return dx12__ensureDescriptorHeap(device,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                    &device->samplerDescriptors);
}

static bool
dx12__descriptorRangeFree(const GPUDescriptorHeapDX12 *heap,
                          uint32_t                       offset,
                          uint32_t                       count) {
  for (uint32_t i = 0u; i < count; i++) {
    uint32_t index;

    index = offset + i;
    if ((heap->used[index >> 6u] & (1ull << (index & 63u))) != 0u) {
      return false;
    }
  }

  return true;
}

static void
dx12__markDescriptorRange(GPUDescriptorHeapDX12 *heap,
                          uint32_t                 offset,
                          uint32_t                 count,
                          bool                     used) {
  for (uint32_t i = 0u; i < count; i++) {
    uint32_t index;
    uint64_t mask;

    index = offset + i;
    mask  = 1ull << (index & 63u);
    if (used) {
      heap->used[index >> 6u] |= mask;
    } else {
      heap->used[index >> 6u] &= ~mask;
    }
  }
}

static GPUResult
dx12__allocateDescriptors(GPUDeviceDX12             *device,
                          D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t                    count,
                          uint32_t                   *outOffset) {
  GPUDescriptorHeapDX12 *heap;
  GPUResult              result;

  if (!device || !outOffset) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outOffset = 0u;
  if (count == 0u) {
    return GPU_OK;
  }

  AcquireSRWLockExclusive(&device->descriptorLock);
  heap   = dx12__descriptorHeap(device, type);
  result = heap ? dx12__ensureDescriptorHeaps(device)
                : GPU_ERROR_INVALID_ARGUMENT;
  if (result == GPU_OK && count <= heap->capacity) {
    result = GPU_ERROR_OUT_OF_MEMORY;
    for (uint32_t offset = 0u; offset <= heap->capacity - count; offset++) {
      if (!dx12__descriptorRangeFree(heap, offset, count)) {
        continue;
      }

      dx12__markDescriptorRange(heap, offset, count, true);
      *outOffset = offset;
      result     = GPU_OK;
      break;
    }
  } else if (result == GPU_OK) {
    result = GPU_ERROR_OUT_OF_MEMORY;
  }
  ReleaseSRWLockExclusive(&device->descriptorLock);
  return result;
}

static void
dx12__freeDescriptors(GPUDeviceDX12             *device,
                      D3D12_DESCRIPTOR_HEAP_TYPE type,
                      uint32_t                    offset,
                      uint32_t                    count) {
  GPUDescriptorHeapDX12 *heap;

  if (!device || count == 0u) {
    return;
  }

  AcquireSRWLockExclusive(&device->descriptorLock);
  heap = dx12__descriptorHeap(device, type);
  if (heap && heap->heap && heap->used &&
      offset <= heap->capacity && count <= heap->capacity - offset) {
    dx12__markDescriptorRange(heap, offset, count, false);
  }
  ReleaseSRWLockExclusive(&device->descriptorLock);
}

static D3D12_CPU_DESCRIPTOR_HANDLE
dx12__cpuDescriptor(const GPUDescriptorHeapDX12 *heap, uint32_t offset) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle = {0};

  if (heap && heap->heap && offset < heap->capacity) {
    heap->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap->heap,
                                                           &handle);
    handle.ptr += (SIZE_T)offset * heap->descriptorSize;
  }
  return handle;
}

static D3D12_GPU_DESCRIPTOR_HANDLE
dx12__gpuDescriptor(const GPUDescriptorHeapDX12 *heap, uint32_t offset) {
  D3D12_GPU_DESCRIPTOR_HANDLE handle = {0};

  if (heap && heap->heap && offset < heap->capacity) {
    heap->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap->heap,
                                                           &handle);
    handle.ptr += (UINT64)offset * heap->descriptorSize;
  }
  return handle;
}

GPU_HIDE
void
dx12_destroyDescriptorHeaps(GPUDeviceDX12 *device) {
  GPUDescriptorHeapDX12 *heaps[2];

  if (!device) {
    return;
  }

  heaps[0] = &device->resourceDescriptors;
  heaps[1] = &device->samplerDescriptors;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(heaps); i++) {
    if (heaps[i]->heap) {
      heaps[i]->heap->lpVtbl->Release(heaps[i]->heap);
    }
    free(heaps[i]->used);
    memset(heaps[i], 0, sizeof(*heaps[i]));
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

typedef struct DX12LayoutPlan {
  uint32_t bindingCount;
  uint32_t rangeCount;
  uint32_t rootParameterCount;
  uint32_t rootDwordCount;
} DX12LayoutPlan;

static GPUResult
dx12__makeLayoutPlan(GPUBindGroupLayout * const *groups,
                     uint32_t                    groupCount,
                     DX12LayoutPlan             *outPlan) {
  DX12LayoutPlan plan;

  if (!outPlan || groupCount > GPU_ENCODER_MAX_BIND_GROUPS ||
      (groupCount > 0u && !groups)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&plan, 0, sizeof(plan));
  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    uint32_t                       backendBindingCount;
    uint32_t                       entryCount;
    uint32_t                       resourceCount;
    uint32_t                       samplerCount;

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

    resourceCount = 0u;
    samplerCount  = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      if (entries[i].arrayCount != 1u || entries[i].immutableSampler) {
        return GPU_ERROR_UNSUPPORTED;
      }
      if (entries[i].visibility == 0u || backendBindings[i] == UINT32_MAX) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      switch (entries[i].bindingType) {
        case GPU_BINDING_UNIFORM_BUFFER:
        case GPU_BINDING_STORAGE_BUFFER:
          plan.bindingCount++;
          plan.rootParameterCount++;
          plan.rootDwordCount += 2u;
          break;
        case GPU_BINDING_SAMPLED_TEXTURE:
          if (entries[i].hasDynamicOffset) {
            return GPU_ERROR_UNSUPPORTED;
          }
          resourceCount++;
          plan.rangeCount++;
          break;
        case GPU_BINDING_SAMPLER:
          if (entries[i].hasDynamicOffset) {
            return GPU_ERROR_UNSUPPORTED;
          }
          samplerCount++;
          plan.rangeCount++;
          break;
        default:
          return GPU_ERROR_UNSUPPORTED;
      }
    }

    if (resourceCount > 0u) {
      plan.rootParameterCount++;
      plan.rootDwordCount++;
    }
    if (samplerCount > 0u) {
      plan.rootParameterCount++;
      plan.rootDwordCount++;
    }
  }

  if (plan.rootDwordCount > DX12_ROOT_SIGNATURE_DWORD_LIMIT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outPlan = plan;
  return GPU_OK;
}

static void
dx12__fillLayoutPlan(GPUBindGroupLayout * const *groups,
                     uint32_t                    groupCount,
                     GPUPipelineLayoutDX12      *native) {
  uint32_t bindingCursor;
  uint32_t rangeCursor;
  uint32_t rootCursor;

  bindingCursor = 0u;
  rangeCursor   = 0u;
  rootCursor    = 0u;
  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    GPUDescriptorTableDX12        *resourceTable;
    GPUDescriptorTableDX12        *samplerTable;
    uint32_t                       entryCount;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetBindGroupLayoutBackendBindings(groups[groupIndex],
                                                           NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceTable->rootParameter = UINT32_MAX;
    samplerTable->rootParameter  = UINT32_MAX;
    native->groupOffsets[groupIndex] = bindingCursor;

    for (uint32_t i = 0u; i < entryCount; i++) {
      if (entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER ||
          entries[i].bindingType == GPU_BINDING_STORAGE_BUFFER) {
        native->bindings[bindingCursor].groupIndex    = groupIndex;
        native->bindings[bindingCursor].binding       = backendBindings[i];
        native->bindings[bindingCursor].rootParameter = rootCursor++;
        native->bindings[bindingCursor].visibility    = entries[i].visibility;
        native->bindings[bindingCursor].bindingType   = entries[i].bindingType;
        bindingCursor++;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLED_TEXTURE) {
        resourceTable->descriptorCount++;
        resourceTable->visibility |= entries[i].visibility;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER) {
        samplerTable->descriptorCount++;
        samplerTable->visibility |= entries[i].visibility;
      }
    }
    native->groupOffsets[groupIndex + 1u] = bindingCursor;

    if (resourceTable->descriptorCount > 0u) {
      resourceTable->rootParameter = rootCursor++;
      resourceTable->rangeOffset   = rangeCursor;
      rangeCursor += resourceTable->descriptorCount;
    }
    if (samplerTable->descriptorCount > 0u) {
      samplerTable->rootParameter = rootCursor++;
      samplerTable->rangeOffset   = rangeCursor;
      rangeCursor += samplerTable->descriptorCount;
    }
  }
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

static void
dx12__fillRanges11(GPUBindGroupLayout * const *groups,
                   const GPUPipelineLayoutDX12 *native,
                   D3D12_ROOT_PARAMETER1       *parameters,
                   D3D12_DESCRIPTOR_RANGE1     *ranges) {
  for (uint32_t i = 0u; i < native->bindingCount; i++) {
    parameters[native->bindings[i].rootParameter].ParameterType =
      native->bindings[i].bindingType == GPU_BINDING_STORAGE_BUFFER
        ? D3D12_ROOT_PARAMETER_TYPE_UAV
        : D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[native->bindings[i].rootParameter]
      .Descriptor.ShaderRegister = native->bindings[i].binding;
    parameters[native->bindings[i].rootParameter]
      .Descriptor.RegisterSpace = native->bindings[i].groupIndex;
    parameters[native->bindings[i].rootParameter].Descriptor.Flags =
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    parameters[native->bindings[i].rootParameter].ShaderVisibility =
      dx12__shaderVisibility(native->bindings[i].visibility);
  }

  for (uint32_t groupIndex = 0u;
       groupIndex < native->groupCount;
       groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    const GPUDescriptorTableDX12  *resourceTable;
    const GPUDescriptorTableDX12  *samplerTable;
    uint32_t                       entryCount;
    uint32_t                       resourceIndex;
    uint32_t                       samplerIndex;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetBindGroupLayoutBackendBindings(groups[groupIndex],
                                                           NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceIndex = 0u;
    samplerIndex  = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      D3D12_DESCRIPTOR_RANGE1 *range;
      uint32_t                 tableOffset;

      if (entries[i].bindingType == GPU_BINDING_SAMPLED_TEXTURE) {
        tableOffset = resourceIndex++;
        range = &ranges[resourceTable->rangeOffset + tableOffset];
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range->Flags =
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER) {
        tableOffset = samplerIndex++;
        range = &ranges[samplerTable->rangeOffset + tableOffset];
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range->Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
      } else {
        continue;
      }

      range->NumDescriptors                  = 1u;
      range->BaseShaderRegister              = backendBindings[i];
      range->RegisterSpace                   = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges =
          resourceTable->descriptorCount;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[resourceTable->rangeOffset];
      parameters[resourceTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(resourceTable->visibility);
    }
    if (samplerTable->descriptorCount > 0u) {
      parameters[samplerTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges = samplerTable->descriptorCount;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(samplerTable->visibility);
    }
  }
}

static void
dx12__fillRanges10(GPUBindGroupLayout * const *groups,
                   const GPUPipelineLayoutDX12 *native,
                   D3D12_ROOT_PARAMETER        *parameters,
                   D3D12_DESCRIPTOR_RANGE      *ranges) {
  for (uint32_t i = 0u; i < native->bindingCount; i++) {
    parameters[native->bindings[i].rootParameter].ParameterType =
      native->bindings[i].bindingType == GPU_BINDING_STORAGE_BUFFER
        ? D3D12_ROOT_PARAMETER_TYPE_UAV
        : D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[native->bindings[i].rootParameter]
      .Descriptor.ShaderRegister = native->bindings[i].binding;
    parameters[native->bindings[i].rootParameter]
      .Descriptor.RegisterSpace = native->bindings[i].groupIndex;
    parameters[native->bindings[i].rootParameter].ShaderVisibility =
      dx12__shaderVisibility(native->bindings[i].visibility);
  }

  for (uint32_t groupIndex = 0u;
       groupIndex < native->groupCount;
       groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    const GPUDescriptorTableDX12  *resourceTable;
    const GPUDescriptorTableDX12  *samplerTable;
    uint32_t                       entryCount;
    uint32_t                       resourceIndex;
    uint32_t                       samplerIndex;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetBindGroupLayoutBackendBindings(groups[groupIndex],
                                                           NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceIndex = 0u;
    samplerIndex  = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      D3D12_DESCRIPTOR_RANGE *range;
      uint32_t                tableOffset;

      if (entries[i].bindingType == GPU_BINDING_SAMPLED_TEXTURE) {
        tableOffset = resourceIndex++;
        range = &ranges[resourceTable->rangeOffset + tableOffset];
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER) {
        tableOffset = samplerIndex++;
        range = &ranges[samplerTable->rangeOffset + tableOffset];
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      } else {
        continue;
      }

      range->NumDescriptors                  = 1u;
      range->BaseShaderRegister              = backendBindings[i];
      range->RegisterSpace                   = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges =
          resourceTable->descriptorCount;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[resourceTable->rangeOffset];
      parameters[resourceTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(resourceTable->visibility);
    }
    if (samplerTable->descriptorCount > 0u) {
      parameters[samplerTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges = samplerTable->descriptorCount;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(samplerTable->visibility);
    }
  }
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
  DX12LayoutPlan              plan;
  GPUResult                   planResult;
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

  planResult = dx12__makeLayoutPlan(groups, groupCount, &plan);
  if (planResult != GPU_OK) {
    return planResult;
  }

  native = calloc(1,
                  sizeof(*native) +
                    (size_t)plan.bindingCount * sizeof(*native->bindings));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->bindings = plan.bindingCount > 0u
                       ? (GPURootBindingDX12 *)(native + 1)
                       : NULL;
  native->bindingCount       = plan.bindingCount;
  native->rangeCount         = plan.rangeCount;
  native->rootParameterCount = plan.rootParameterCount;
  native->groupCount         = groupCount;
  dx12__fillLayoutPlan(groups, groupCount, native);

  deviceDX12 = device->_priv;
  serialized = NULL;
  errors     = NULL;
  if (deviceDX12->rootSignatureVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER1              *parameters;
    D3D12_DESCRIPTOR_RANGE1            *ranges;

    parameters = plan.rootParameterCount > 0u
                   ? calloc(plan.rootParameterCount, sizeof(*parameters))
                   : NULL;
    ranges = plan.rangeCount > 0u
               ? calloc(plan.rangeCount, sizeof(*ranges))
               : NULL;
    if ((plan.rootParameterCount > 0u && !parameters) ||
        (plan.rangeCount > 0u && !ranges)) {
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    dx12__fillRanges11(groups, native, parameters, ranges);

    desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = plan.rootParameterCount;
    desc.Desc_1_1.pParameters   = parameters;
    desc.Desc_1_1.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeVersionedRootSignature(&desc,
                                                   &serialized,
                                                   &errors);
    free(ranges);
    free(parameters);
  } else {
    D3D12_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER     *parameters;
    D3D12_DESCRIPTOR_RANGE   *ranges;

    parameters = plan.rootParameterCount > 0u
                   ? calloc(plan.rootParameterCount, sizeof(*parameters))
                   : NULL;
    ranges = plan.rangeCount > 0u
               ? calloc(plan.rangeCount, sizeof(*ranges))
               : NULL;
    if ((plan.rootParameterCount > 0u && !parameters) ||
        (plan.rangeCount > 0u && !ranges)) {
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    dx12__fillRanges10(groups, native, parameters, ranges);

    desc.NumParameters = plan.rootParameterCount;
    desc.pParameters   = parameters;
    desc.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeRootSignature(&desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         &serialized,
                                         &errors);
    free(ranges);
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

typedef struct DX12BindGroupWriteContext {
  GPUBindGroupDX12 *group;
  uint32_t          resourceIndex;
  uint32_t          samplerIndex;
  bool              valid;
} DX12BindGroupWriteContext;

static void
dx12__writeBindGroup(void *context,
                     const GPUBindGroupBindingView *binding) {
  DX12BindGroupWriteContext *writeContext;
  D3D12_CPU_DESCRIPTOR_HANDLE handle;

  writeContext = context;
  if (!writeContext || !writeContext->valid || !binding) {
    return;
  }

  switch (binding->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER:
      if (binding->kind != GPUBindKindBuffer || !binding->buffer ||
          !binding->buffer->device ||
          binding->buffer->device->_priv != writeContext->group->device ||
          (binding->bindingType == GPU_BINDING_STORAGE_BUFFER &&
           !gpuBufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE))) {
        writeContext->valid = false;
      }
      break;
    case GPU_BINDING_SAMPLED_TEXTURE: {
      GPUTextureViewDX12 *view;

      view = binding->textureView ? binding->textureView->_priv : NULL;
      if (binding->kind != GPUBindKindTexture || !view || !view->resource ||
          !view->hasSrv || !binding->textureView->_texture ||
          !binding->textureView->_texture->device ||
          binding->textureView->_texture->device->_priv !=
            writeContext->group->device ||
          writeContext->resourceIndex >= writeContext->group->resourceCount) {
        writeContext->valid = false;
        return;
      }

      handle = dx12__cpuDescriptor(
        &writeContext->group->device->resourceDescriptors,
        writeContext->group->resourceOffset + writeContext->resourceIndex++
      );
      writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(
        writeContext->group->device->d3dDevice,
        view->resource,
        &view->srv,
        handle
      );
      break;
    }
    case GPU_BINDING_SAMPLER: {
      GPUSamplerDX12 *sampler;

      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (binding->kind != GPUBindKindSampler || !sampler ||
          sampler->device != writeContext->group->device ||
          writeContext->samplerIndex >= writeContext->group->samplerCount) {
        writeContext->valid = false;
        return;
      }

      handle = dx12__cpuDescriptor(
        &writeContext->group->device->samplerDescriptors,
        writeContext->group->samplerOffset + writeContext->samplerIndex++
      );
      writeContext->group->device->d3dDevice->lpVtbl->CreateSampler(
        writeContext->group->device->d3dDevice,
        &sampler->desc,
        handle
      );
      break;
    }
    default:
      writeContext->valid = false;
      break;
  }
}

GPU_HIDE
GPUResult
dx12_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  GPUBindGroupLayout              *layout;
  const GPUBindGroupLayoutEntry   *entries;
  GPUBindGroupDX12                *native;
  DX12BindGroupWriteContext        writeContext;
  GPUResult                        result;
  uint32_t                         entryCount;

  if (!device || !device->_priv || !group) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout  = gpuBindGroupGetLayout(group);
  entries = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  if (!layout || (entryCount > 0u && !entries)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device = device->_priv;

  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].arrayCount != 1u || entries[i].immutableSampler) {
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    if (entries[i].bindingType == GPU_BINDING_SAMPLED_TEXTURE) {
      native->resourceCount++;
    } else if (entries[i].bindingType == GPU_BINDING_SAMPLER) {
      native->samplerCount++;
    } else if (entries[i].bindingType != GPU_BINDING_UNIFORM_BUFFER &&
               entries[i].bindingType != GPU_BINDING_STORAGE_BUFFER) {
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  result = dx12__allocateDescriptors(native->device,
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                     native->resourceCount,
                                     &native->resourceOffset);
  if (result != GPU_OK) {
    free(native);
    return result;
  }
  result = dx12__allocateDescriptors(native->device,
                                     D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                     native->samplerCount,
                                     &native->samplerOffset);
  if (result != GPU_OK) {
    dx12__freeDescriptors(native->device,
                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                          native->resourceOffset,
                          native->resourceCount);
    free(native);
    return result;
  }

  memset(&writeContext, 0, sizeof(writeContext));
  writeContext.group = native;
  writeContext.valid = true;
  if (!gpuForEachBindGroupBinding(group,
                                  dx12__writeBindGroup,
                                  &writeContext) ||
      !writeContext.valid ||
      writeContext.resourceIndex != native->resourceCount ||
      writeContext.samplerIndex != native->samplerCount) {
    dx12__freeDescriptors(native->device,
                          D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                          native->samplerOffset,
                          native->samplerCount);
    dx12__freeDescriptors(native->device,
                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                          native->resourceOffset,
                          native->resourceCount);
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  group->_native = native;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupDX12 *native;

  native = group ? group->_native : NULL;
  if (!native) {
    return;
  }

  dx12__freeDescriptors(native->device,
                        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                        native->samplerOffset,
                        native->samplerCount);
  dx12__freeDescriptors(native->device,
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                        native->resourceOffset,
                        native->resourceCount);
  free(native);
  group->_native = NULL;
}

typedef struct DX12BindContext {
  ID3D12GraphicsCommandList *commandList;
  GPUPipelineLayoutDX12     *layout;
  GPUDevice                 *device;
  uint32_t                   groupIndex;
  uint32_t                   boundCount;
  bool                       compute;
  bool                       valid;
} DX12BindContext;

static bool
dx12__bindDescriptorHeaps(ID3D12GraphicsCommandList *commandList,
                          ID3D12DescriptorHeap      **boundResourceHeap,
                          ID3D12DescriptorHeap      **boundSamplerHeap,
                          GPUDeviceDX12              *device,
                          bool                        needsResources,
                          bool                        needsSamplers) {
  ID3D12DescriptorHeap *heaps[2];
  uint32_t              count;

  if (!commandList || !boundResourceHeap || !boundSamplerHeap || !device) {
    return false;
  }
  if ((needsResources && !device->resourceDescriptors.heap) ||
      (needsSamplers && !device->samplerDescriptors.heap)) {
    return false;
  }

  if (*boundResourceHeap == device->resourceDescriptors.heap &&
      *boundSamplerHeap == device->samplerDescriptors.heap) {
    return true;
  }

  count = 0u;
  if (device->resourceDescriptors.heap) {
    heaps[count++] = device->resourceDescriptors.heap;
  }
  if (device->samplerDescriptors.heap) {
    heaps[count++] = device->samplerDescriptors.heap;
  }
  if (count > 0u) {
    commandList->lpVtbl->SetDescriptorHeaps(commandList, count, heaps);
  }
  *boundResourceHeap = device->resourceDescriptors.heap;
  *boundSamplerHeap  = device->samplerDescriptors.heap;
  return true;
}

static bool
dx12__transitionSampledTexture(ID3D12GraphicsCommandList *commandList,
                               GPUTextureViewDX12        *view) {
  const D3D12_RESOURCE_STATES requiredState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!commandList || !view || !view->resource || !view->state) {
    return false;
  }

  if (*view->state == requiredState) {
    return true;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = view->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = *view->state;
  barrier.Transition.StateAfter  = requiredState;
  commandList->lpVtbl->ResourceBarrier(commandList, 1u, &barrier);
  *view->state = requiredState;
  return true;
}

static bool
dx12__transitionStorageBuffer(ID3D12GraphicsCommandList *commandList,
                              GPUBufferDX12             *buffer) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!commandList || !buffer || !buffer->resource || !buffer->defaultHeap) {
    return false;
  }
  if (buffer->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
    return true;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = buffer->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = buffer->state;
  barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  commandList->lpVtbl->ResourceBarrier(commandList, 1u, &barrier);
  buffer->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  return true;
}

static void
dx12__bindRoot(void *context, const GPUBindGroupBindingView *binding) {
  DX12BindContext *bindContext;

  bindContext = context;
  if (!bindContext || !bindContext->valid || !binding) {
    if (bindContext) {
      bindContext->valid = false;
    }
    return;
  }

  switch (binding->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER: {
      const GPURootBindingDX12 *rootBinding;
      GPUBufferDX12            *buffer;
      D3D12_GPU_VIRTUAL_ADDRESS address;

      rootBinding = dx12__findRootBinding(bindContext->layout,
                                          bindContext->groupIndex,
                                          binding->binding);
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (binding->kind != GPUBindKindBuffer || !binding->buffer ||
          binding->buffer->device != bindContext->device || !rootBinding ||
          !buffer || !buffer->resource || buffer->gpuAddress == 0u ||
          (binding->offset &
           (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) != 0u ||
          binding->offset > UINT64_MAX - buffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }

      address = buffer->gpuAddress + binding->offset;
      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootConstantBufferView(
          bindContext->commandList,
          rootBinding->rootParameter,
          address
        );
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootConstantBufferView(
          bindContext->commandList,
          rootBinding->rootParameter,
          address
        );
      }
      break;
    }
    case GPU_BINDING_STORAGE_BUFFER: {
      const GPURootBindingDX12 *rootBinding;
      GPUBufferDX12            *buffer;
      D3D12_GPU_VIRTUAL_ADDRESS address;

      rootBinding = dx12__findRootBinding(bindContext->layout,
                                          bindContext->groupIndex,
                                          binding->binding);
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (binding->kind != GPUBindKindBuffer || !binding->buffer ||
          binding->buffer->device != bindContext->device || !rootBinding ||
          !gpuBufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE) ||
          !dx12__transitionStorageBuffer(bindContext->commandList, buffer) ||
          binding->offset > UINT64_MAX - buffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }

      address = buffer->gpuAddress + binding->offset;
      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootUnorderedAccessView(
          bindContext->commandList,
          rootBinding->rootParameter,
          address
        );
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootUnorderedAccessView(
          bindContext->commandList,
          rootBinding->rootParameter,
          address
        );
      }
      break;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      GPUTextureViewDX12 *view;

      view = binding->textureView ? binding->textureView->_priv : NULL;
      if (binding->kind != GPUBindKindTexture || !view || !view->hasSrv ||
          !binding->textureView->_texture ||
          binding->textureView->_texture->device != bindContext->device ||
          !dx12__transitionSampledTexture(bindContext->commandList, view)) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    case GPU_BINDING_SAMPLER: {
      GPUSamplerDX12 *sampler;

      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (binding->kind != GPUBindKindSampler || !sampler ||
          sampler->device != bindContext->device->_priv) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    default:
      bindContext->valid = false;
      return;
  }

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
  DX12BindContext        context;
  GPURenderEncoderDX12  *encoder;
  GPUPipelineLayoutDX12 *layout;
  GPUBindGroupDX12      *nativeGroup;
  GPUDeviceDX12         *device;
  GPUBindGroupLayout    *groupLayout;
  uint32_t               expectedCount;
  bool                   valid;

  encoder = pass ? pass->_priv : NULL;
  layout  = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device = pipelineLayout && pipelineLayout->_device
             ? pipelineLayout->_device->_priv
             : NULL;
  if (!encoder || !encoder->commandList || !layout || !layout->rootSignature ||
      encoder->rootSignature != layout->rootSignature ||
      !nativeGroup || nativeGroup->device != device ||
      groupIndex >= layout->groupCount) {
    return false;
  }

  groupLayout = gpuBindGroupGetLayout(group);
  (void)GPUGetBindGroupLayoutEntries(groupLayout, &expectedCount);
  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount ||
      nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount ||
      !dx12__bindDescriptorHeaps(encoder->commandList,
                                &encoder->resourceHeap,
                                &encoder->samplerHeap,
                                device,
                                nativeGroup->resourceCount > 0u,
                                nativeGroup->samplerCount > 0u)) {
    return false;
  }

  memset(&context, 0, sizeof(context));
  context.commandList = encoder->commandList;
  context.layout      = layout;
  context.device      = pipelineLayout->_device;
  context.groupIndex  = groupIndex;
  context.valid       = true;
  valid = gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                        groupIndex,
                                                        group,
                                                        dynamicOffsetCount,
                                                        dynamicOffsets,
                                                        dx12__bindRoot,
                                                        &context) &&
          context.valid && context.boundCount == expectedCount;
  if (!valid) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    encoder->commandList->lpVtbl->SetGraphicsRootDescriptorTable(
      encoder->commandList,
      layout->resourceTables[groupIndex].rootParameter,
      dx12__gpuDescriptor(&device->resourceDescriptors,
                          nativeGroup->resourceOffset)
    );
  }
  if (nativeGroup->samplerCount > 0u) {
    encoder->commandList->lpVtbl->SetGraphicsRootDescriptorTable(
      encoder->commandList,
      layout->samplerTables[groupIndex].rootParameter,
      dx12__gpuDescriptor(&device->samplerDescriptors,
                          nativeGroup->samplerOffset)
    );
  }
  return true;
}

GPU_HIDE
bool
dx12_bindComputeGroup(GPUComputePassEncoder *pass,
                      GPUPipelineLayout     *pipelineLayout,
                      uint32_t               groupIndex,
                      GPUBindGroup          *group,
                      uint32_t               dynamicOffsetCount,
                      const uint32_t        *dynamicOffsets) {
  DX12BindContext         context;
  GPUComputeEncoderDX12  *encoder;
  GPUPipelineLayoutDX12  *layout;
  GPUBindGroupDX12       *nativeGroup;
  GPUDeviceDX12          *device;
  GPUBindGroupLayout     *groupLayout;
  uint32_t                expectedCount;
  bool                    valid;

  encoder = pass ? pass->_priv : NULL;
  layout  = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device = pipelineLayout && pipelineLayout->_device
             ? pipelineLayout->_device->_priv
             : NULL;
  if (!encoder || !encoder->commandList || !layout || !layout->rootSignature ||
      encoder->rootSignature != layout->rootSignature ||
      !nativeGroup || nativeGroup->device != device ||
      groupIndex >= layout->groupCount) {
    return false;
  }

  groupLayout = gpuBindGroupGetLayout(group);
  (void)GPUGetBindGroupLayoutEntries(groupLayout, &expectedCount);
  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount ||
      nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount ||
      !dx12__bindDescriptorHeaps(encoder->commandList,
                                &encoder->resourceHeap,
                                &encoder->samplerHeap,
                                device,
                                nativeGroup->resourceCount > 0u,
                                nativeGroup->samplerCount > 0u)) {
    return false;
  }

  memset(&context, 0, sizeof(context));
  context.commandList = encoder->commandList;
  context.layout      = layout;
  context.device      = pipelineLayout->_device;
  context.groupIndex  = groupIndex;
  context.compute     = true;
  context.valid       = true;
  valid = gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                        groupIndex,
                                                        group,
                                                        dynamicOffsetCount,
                                                        dynamicOffsets,
                                                        dx12__bindRoot,
                                                        &context) &&
          context.valid && context.boundCount == expectedCount;
  if (!valid) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    encoder->commandList->lpVtbl->SetComputeRootDescriptorTable(
      encoder->commandList,
      layout->resourceTables[groupIndex].rootParameter,
      dx12__gpuDescriptor(&device->resourceDescriptors,
                          nativeGroup->resourceOffset)
    );
  }
  if (nativeGroup->samplerCount > 0u) {
    encoder->commandList->lpVtbl->SetComputeRootDescriptorTable(
      encoder->commandList,
      layout->samplerTables[groupIndex].rootParameter,
      dx12__gpuDescriptor(&device->samplerDescriptors,
                          nativeGroup->samplerOffset)
    );
  }
  return true;
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
  api->createBindGroup       = dx12_createBindGroup;
  api->destroyBindGroup      = dx12_destroyBindGroup;
  api->bindRenderGroup       = dx12_bindRenderGroup;
  api->bindComputeGroup      = dx12_bindComputeGroup;
}
