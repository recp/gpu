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
#include "../../impl.h"
#include "../pipeline_cache.h"

enum {
  DX12_ROOT_SIGNATURE_DWORD_LIMIT   = 64u,
  DX12_RESOURCE_DESCRIPTOR_CAPACITY = 65536u,
  DX12_SAMPLER_DESCRIPTOR_CAPACITY  = 2048u
};

enum {
  GPU_DX12_COMMAND_SAMPLER_HEAP_MIN = 16u,
  GPU_DX12_COMMAND_SAMPLER_HEAP_MAX = 2048u
};

typedef struct DX12LayoutPlan {
  uint32_t bindingCount;
  uint32_t rangeCount;
  uint32_t rootParameterCount;
  uint32_t rootDwordCount;
  uint32_t staticSamplerCount;
} DX12LayoutPlan;

typedef struct DX12BindGroupWriteContext {
  BindGroupDX12    *group;
  bool              valid;
} DX12BindGroupWriteContext;

typedef struct DX12BindContext {
  ID3D12GraphicsCommandList *commandList;
  PipelineLayoutDX12        *layout;
  BindGroupDX12             *group;
  GPUDevice                 *device;
  uint32_t                   resourceOffset;
  uint32_t                   groupIndex;
  uint32_t                   boundCount;
  bool                       compute;
  bool                       valid;
} DX12BindContext;

static void
dx12__logRootSignatureError(ID3DBlob *errors) {
  if (errors && errors->lpVtbl->GetBufferPointer(errors)) {
    fprintf(stderr,
            "GPU Direct3D 12 root signature failed: %s\n",
            (const char *)errors->lpVtbl->GetBufferPointer(errors));
  }
}

static bool
dx12__storageTextureReadOnly(GPUStorageTextureAccess access) {
  return access == GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY;
}

static bool
dx12__sourceSamplerSelected(const ShaderStaticSamplerInfo    *sampler,
                            uint64_t                          entryMask) {
  return sampler
         && sampler->hlslIndex != UINT32_MAX
         && (sampler->entryMask & entryMask) != 0u;
}

static DescriptorHeapDX12*
dx12__descriptorHeap(DeviceDX12                *device,
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

  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
    return &device->rtvDescriptors;
  }

  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
    return &device->dsvDescriptors;
  }

  return NULL;
}

static GPUResult
dx12__ensureDescriptorHeap(DeviceDX12                *device,
                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                           DescriptorHeapDX12        *heap) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {0};
  size_t                     wordCount;
  uint32_t                   capacity;
  HRESULT                    result;

  if (!device || !device->d3dDevice || !heap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (heap->heap) {
    return GPU_OK;
  }

  capacity  = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
               ? DX12_SAMPLER_DESCRIPTOR_CAPACITY
               : DX12_RESOURCE_DESCRIPTOR_CAPACITY;
  wordCount = (capacity + 63u) / 64u;

  if (!(heap->used = calloc(wordCount, sizeof(*heap->used)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  desc.Type           = type;
  desc.NumDescriptors = capacity;
  desc.Flags          = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                        || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                          ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                          : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  result = device->d3dDevice->lpVtbl->CreateDescriptorHeap(device->d3dDevice,
                                                           &desc,
                                                           &IID_ID3D12DescriptorHeap,
                                                           (void **)&heap->heap);

  if (FAILED(result) || !heap->heap) {
    free(heap->used);
    memset(heap, 0, sizeof(*heap));
    return GPU_ERROR_BACKEND_FAILURE;
  }

  heap->descriptorSize = device->d3dDevice->lpVtbl->GetDescriptorHandleIncrementSize(device->d3dDevice, type);
  heap->capacity       = capacity;

  return GPU_OK;
}

static bool
dx12__descriptorRangeFree(const DescriptorHeapDX12    *heap,
                          uint32_t                     offset,
                          uint32_t                     count) {
  uint32_t i;
  uint32_t index;

  for (i = 0u; i < count; i++) {
    index = offset + i;

    if ((heap->used[index >> 6u] & (1ull << (index & 63u))) != 0u) {
      return false;
    }
  }

  return true;
}

static void
dx12__markDescriptorRange(DescriptorHeapDX12    *heap,
                          uint32_t               offset,
                          uint32_t               count,
                          bool                   used) {
  uint64_t mask;
  uint32_t i;
  uint32_t index;

  for (i = 0u; i < count; i++) {
    index = offset + i;
    mask  = 1ull << (index & 63u);

    if (used) {
      heap->used[index >> 6u] |= mask;
    } else {
      heap->used[index >> 6u] &= ~mask;
    }
  }
}

static bool
dx12__recordCommandDescriptorAllocation(CommandBufferDX12    *command,
                                        uint32_t              offset,
                                        uint32_t              count) {
  DescriptorAllocationChunkDX12    *chunk;
  GPUDevice                        *device;
  DescriptorAllocationDX12         *allocation;
  DescriptorAllocationChunkDX12    *tail;

  if (!command || count == 0u) {
    return false;
  }

  if (command->descriptorAllocationCount <
      GPU_DX12_INLINE_DESCRIPTOR_ALLOCATION_COUNT) {
    allocation = &command->descriptorAllocations[command->descriptorAllocationCount++];
    allocation->offset = offset;
    allocation->count  = count;

    return true;
  }

  chunk = command->descriptorAllocationChunks;

  while (chunk && chunk->count ==
                    GPU_DX12_DESCRIPTOR_ALLOCATION_CHUNK_COUNT) {
    chunk = chunk->next;
  }

  if (!chunk) {
    if (!(chunk = calloc(1, sizeof(*chunk)))) {
      return false;
    }

    device = command->owner && command->owner->queue ? command->owner->queue->_device : NULL;
    deviceRecordHotPathAlloc(device, sizeof(*chunk));

    tail = command->descriptorAllocationChunks;

    if (!tail) {
      command->descriptorAllocationChunks = chunk;
    } else {
      while (tail->next) {
        tail = tail->next;
      }

      tail->next = chunk;
    }
  }

  chunk->allocations[chunk->count].offset = offset;
  chunk->allocations[chunk->count].count  = count;
  chunk->count++;

  return true;
}

static CommandSamplerHeapDX12*
dx12__takeCommandSamplerHeap(CommandBufferDX12    *command,
                             uint32_t              requiredCount) {
  D3D12_DESCRIPTOR_HEAP_DESC  desc = {0};
  CommandSamplerHeapDX12    **link;
  CommandSamplerHeapDX12     *node;
  GPUDevice                  *device;
  DeviceDX12                 *deviceDX12;
  uint32_t                    capacity;
  uint32_t                    slot;
  HRESULT                     result;

  device     = command && command->owner && command->owner->queue ? command->owner->queue->_device : NULL;
  deviceDX12 = device ? device->_priv : NULL;

  if (!command || !deviceDX12 || requiredCount == 0u
      || requiredCount > GPU_DX12_COMMAND_SAMPLER_HEAP_MAX) {
    return NULL;
  }

  link = &command->samplerHeaps;
  slot = 0u;

  while (*link && slot < command->samplerHeapUseCount) {
    link = &(*link)->next;
    slot++;
  }

  node = *link;

  if (!node) {
    if (!(node = calloc(1, sizeof(*node)))) {
      return NULL;
    }

    deviceRecordHotPathAlloc(device, sizeof(*node));

    *link = node;
  }

  capacity = GPU_DX12_COMMAND_SAMPLER_HEAP_MIN;

  while (capacity < requiredCount) {
    capacity <<= 1u;
  }

  if (!node->heap || node->capacity < requiredCount) {
    if (node->heap) {
      node->heap->lpVtbl->Release(node->heap);
      node->heap     = NULL;
      node->capacity = 0u;
    }

    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = capacity;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    result              = deviceDX12->d3dDevice->lpVtbl->CreateDescriptorHeap(deviceDX12->d3dDevice,
                                                                              &desc,
                                                                              &IID_ID3D12DescriptorHeap,
                                                                              (void **)&node->heap);

    if (FAILED(result) || !node->heap) {
      return NULL;
    }

    node->capacity = capacity;
  }

  command->samplerHeapUseCount++;

  return node;
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

static D3D12_ROOT_PARAMETER_TYPE
dx12__rootBufferType(GPUBindingType type) {
  switch (type) {
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
      return D3D12_ROOT_PARAMETER_TYPE_SRV;
    case GPU_BINDING_STORAGE_BUFFER:
      return D3D12_ROOT_PARAMETER_TYPE_UAV;
    case GPU_BINDING_UNIFORM_BUFFER:
    default:
      return D3D12_ROOT_PARAMETER_TYPE_CBV;
  }
}

static bool
dx12__bufferBindingType(GPUBindingType type) {
  return type == GPU_BINDING_UNIFORM_BUFFER
         || type == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
         || type == GPU_BINDING_STORAGE_BUFFER;
}

static bool
dx12__resourceTableBindingType(const DeviceDX12    *device,
                               GPUBindingType       type,
                               uint32_t             arrayCount) {
  if (type == GPU_BINDING_UNIFORM_BUFFER
      && device && !device->rootCbvSpacesReliable) {
    return true;
  }

  if (dx12__bufferBindingType(type)) {
    return arrayCount > 1u;
  }

  return type == GPU_BINDING_SAMPLED_TEXTURE
         || type == GPU_BINDING_STORAGE_TEXTURE
         || type == GPU_BINDING_SAMPLER_FEEDBACK_EXT
         || type == GPU_BINDING_ACCELERATION_STRUCTURE;
}

static bool
dx12__resourceTableBinding(const DeviceDX12              *device,
                           const GPUBindGroupLayoutEntry *entry) {
  if (!entry) {
    return false;
  }

  return dx12__resourceTableBindingType(device,
                                        entry->bindingType,
                                        entry->arrayCount);
}

static D3D12_DESCRIPTOR_RANGE_TYPE
dx12__resourceRangeType(const GPUBindGroupLayoutEntry *entry) {
  switch (entry->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER:
      return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case GPU_BINDING_STORAGE_BUFFER:
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT:
      return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case GPU_BINDING_STORAGE_TEXTURE:
      return dx12__storageTextureReadOnly(entry->storageTexture.access)
               ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
               : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
    case GPU_BINDING_SAMPLED_TEXTURE:
    case GPU_BINDING_ACCELERATION_STRUCTURE:
    default:
      return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  }
}

static GPUResult
dx12__makeLayoutPlan(DeviceDX12                *device,
                     GPUPipelineLayout         *layout,
                     GPUBindGroupLayout *const *groups,
                     uint32_t                   groupCount,
                     DX12LayoutPlan            *outPlan) {
  DX12LayoutPlan plan;

  if (!outPlan || groupCount > GPU_ENCODER_MAX_BIND_GROUPS
      || (groupCount > 0u && !groups)) {
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

    entries         = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = getPipelineLayoutBackendBindings(layout,
                                                       groupIndex,
                                                       &backendBindingCount);

    if (entryCount != backendBindingCount
        || (entryCount > 0u && (!entries || !backendBindings))) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    resourceCount = 0u;
    samplerCount  = 0u;

    for (uint32_t i = 0u; i < entryCount; i++) {
      if (entries[i].arrayCount == 0u) {
        return GPU_ERROR_UNSUPPORTED;
      }

      if (entries[i].visibility == 0u || backendBindings[i] == UINT32_MAX) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      if (entries[i].arrayCount - 1u >
          UINT32_MAX - backendBindings[i]) {
        return GPU_ERROR_UNSUPPORTED;
      }

      if (entries[i].immutableSampler) {
        if (entries[i].bindingType != GPU_BINDING_SAMPLER
            || entries[i].hasDynamicOffset) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        if (entries[i].arrayCount > UINT32_MAX - plan.staticSamplerCount) {
          return GPU_ERROR_UNSUPPORTED;
        }

        plan.staticSamplerCount += entries[i].arrayCount;
        continue;
      }

      switch (entries[i].bindingType) {
        case GPU_BINDING_UNIFORM_BUFFER:
        case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
        case GPU_BINDING_STORAGE_BUFFER:
          if (dx12__resourceTableBinding(device, &entries[i])) {
            resourceCount++;
            plan.rangeCount++;
          } else {
            if (plan.bindingCount == UINT32_MAX
                || plan.rootParameterCount == UINT32_MAX
                || plan.rootDwordCount > UINT32_MAX - 2u) {
              return GPU_ERROR_UNSUPPORTED;
            }

            plan.bindingCount++;
            plan.rootParameterCount++;
            plan.rootDwordCount += 2u;
          }

          break;
        case GPU_BINDING_SAMPLED_TEXTURE:
        case GPU_BINDING_STORAGE_TEXTURE:
        case GPU_BINDING_SAMPLER_FEEDBACK_EXT:
        case GPU_BINDING_ACCELERATION_STRUCTURE:
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
dx12__fillLayoutPlan(DeviceDX12                *device,
                     GPUPipelineLayout         *layout,
                     GPUBindGroupLayout *const *groups,
                     uint32_t                   groupCount,
                     PipelineLayoutDX12        *native) {
  uint32_t bindingCursor;
  uint32_t rangeCursor;
  uint32_t rootCursor;
  uint32_t samplerCursor;

  bindingCursor = 0u;
  rangeCursor   = 0u;
  rootCursor    = 0u;
  samplerCursor = 0u;

  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    DescriptorTableDX12           *resourceTable;
    DescriptorTableDX12           *samplerTable;
    uint32_t                       entryCount;

    entries         = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = getPipelineLayoutBackendBindings(layout,
                                                       groupIndex,
                                                       NULL);
    resourceTable                    = &native->resourceTables[groupIndex];
    samplerTable                     = &native->samplerTables[groupIndex];
    resourceTable->rootParameter     = UINT32_MAX;
    samplerTable->rootParameter      = UINT32_MAX;
    resourceTable->nullOffset        = UINT32_MAX;
    samplerTable->nullOffset         = UINT32_MAX;
    native->groupOffsets[groupIndex] = bindingCursor;

    for (uint32_t i = 0u; i < entryCount; i++) {
      if (dx12__bufferBindingType(entries[i].bindingType)
          && !dx12__resourceTableBinding(device, &entries[i])) {
        native->bindings[bindingCursor].groupIndex    = groupIndex;
        native->bindings[bindingCursor].binding       = backendBindings[i];
        native->bindings[bindingCursor].rootParameter = rootCursor++;
        native->bindings[bindingCursor].visibility    = entries[i].visibility;
        native->bindings[bindingCursor].bindingType   = entries[i].bindingType;
        bindingCursor++;
      } else if (dx12__resourceTableBinding(device, &entries[i])) {
        resourceTable->descriptorCount += entries[i].arrayCount;
        resourceTable->rangeCount++;
        resourceTable->visibility |= entries[i].visibility;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER
                 && !entries[i].immutableSampler) {
        samplerTable->descriptorCount += entries[i].arrayCount;
        samplerTable->rangeCount++;
        samplerTable->visibility |= entries[i].visibility;
      }
    }

    native->groupOffsets[groupIndex + 1u] = bindingCursor;

    if (resourceTable->descriptorCount > 0u) {
      resourceTable->rootParameter = rootCursor++;
      resourceTable->rangeOffset   = rangeCursor;
      rangeCursor += resourceTable->rangeCount;
    }

    if (samplerTable->descriptorCount > 0u) {
      samplerTable->rootParameter    = rootCursor++;
      samplerTable->rangeOffset      = rangeCursor;
      samplerTable->descriptorOffset = samplerCursor;
      samplerCursor += samplerTable->descriptorCount;
      rangeCursor += samplerTable->rangeCount;
    }
  }

  native->samplerDescriptorCount = samplerCursor;
}

static DXGI_FORMAT
dx12__nullSampledFormat(GPUTextureSampleType type) {
  switch (type) {
    case GPU_TEXTURE_SAMPLE_TYPE_SINT:
      return DXGI_FORMAT_R32_SINT;
    case GPU_TEXTURE_SAMPLE_TYPE_UINT:
      return DXGI_FORMAT_R32_UINT;
    case GPU_TEXTURE_SAMPLE_TYPE_FLOAT:
    case GPU_TEXTURE_SAMPLE_TYPE_UNFILTERABLE_FLOAT:
    case GPU_TEXTURE_SAMPLE_TYPE_DEPTH:
    default:
      return DXGI_FORMAT_R32_FLOAT;
  }
}

static bool
dx12__fillNullSrvTexture(D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
                         GPUTextureViewType               viewType,
                         bool                             multisampled) {
  if (!desc) {
    return false;
  }

  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      if (multisampled) {
        return false;
      }

      desc->ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE1D;
      desc->Texture1D.MipLevels = 1u;
      break;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      if (multisampled) {
        return false;
      }

      desc->ViewDimension            = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      desc->Texture1DArray.MipLevels = 1u;
      desc->Texture1DArray.ArraySize = 1u;
      break;
    case GPU_TEXTURE_VIEW_2D:
      desc->ViewDimension = multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;

      if (!multisampled) {
        desc->Texture2D.MipLevels = 1u;
      }

      break;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      desc->ViewDimension = multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;

      if (multisampled) {
        desc->Texture2DMSArray.ArraySize = 1u;
      } else {
        desc->Texture2DArray.MipLevels = 1u;
        desc->Texture2DArray.ArraySize = 1u;
      }

      break;
    case GPU_TEXTURE_VIEW_CUBE:
      if (multisampled) {
        return false;
      }

      desc->ViewDimension         = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc->TextureCube.MipLevels = 1u;
      break;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      if (multisampled) {
        return false;
      }

      desc->ViewDimension              = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      desc->TextureCubeArray.MipLevels = 1u;
      desc->TextureCubeArray.NumCubes  = 1u;
      break;
    case GPU_TEXTURE_VIEW_3D:
      if (multisampled) {
        return false;
      }

      desc->ViewDimension       = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc->Texture3D.MipLevels = 1u;
      break;
    default:
      return false;
  }

  return true;
}

static bool
dx12__fillNullUavTexture(D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
                         GPUTextureViewType                viewType) {
  if (!desc) {
    return false;
  }

  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      desc->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
      break;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      desc->ViewDimension            = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
      desc->Texture1DArray.ArraySize = 1u;
      break;
    case GPU_TEXTURE_VIEW_2D:
      desc->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      break;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
    case GPU_TEXTURE_VIEW_CUBE:
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      desc->ViewDimension            = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      desc->Texture2DArray.ArraySize = 1u;
      break;
    case GPU_TEXTURE_VIEW_3D:
      desc->ViewDimension   = D3D12_UAV_DIMENSION_TEXTURE3D;
      desc->Texture3D.WSize = 1u;
      break;
    default:
      return false;
  }

  return true;
}

static bool
dx12__writeNullResourceDescriptor(DeviceDX12                    *device,
                                  const GPUBindGroupLayoutEntry *entry,
                                  D3D12_CPU_DESCRIPTOR_HANDLE    handle) {
  bool readRaw;
  bool writeRaw;

  if (!device || !device->d3dDevice || !entry) {
    return false;
  }

  switch (entry->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER:
      device->d3dDevice->lpVtbl->CreateConstantBufferView(device->d3dDevice,
                                                          NULL,
                                                          handle);

      return true;
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER: {
      D3D12_SHADER_RESOURCE_VIEW_DESC readBufferDesc = {0};
      readRaw = entry->buffer.byteAddress || entry->buffer.strideBytes == 0u;

      readBufferDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      readBufferDesc.Format                     = readRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
      readBufferDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
      readBufferDesc.Buffer.NumElements         = 1u;
      readBufferDesc.Buffer.StructureByteStride = readRaw ? 0u : entry->buffer.strideBytes;
      readBufferDesc.Buffer.Flags               = readRaw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                          NULL,
                                                          &readBufferDesc,
                                                          handle);

      return true;
    }
    case GPU_BINDING_STORAGE_BUFFER: {
      D3D12_UNORDERED_ACCESS_VIEW_DESC writeBufferDesc = {0};
      writeRaw = entry->buffer.byteAddress || entry->buffer.strideBytes == 0u;

      writeBufferDesc.Format                     = writeRaw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
      writeBufferDesc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
      writeBufferDesc.Buffer.NumElements         = 1u;
      writeBufferDesc.Buffer.StructureByteStride = writeRaw ? 0u : entry->buffer.strideBytes;
      writeBufferDesc.Buffer.Flags               = writeRaw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
      device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                           NULL,
                                                           NULL,
                                                           &writeBufferDesc,
                                                           handle);

      return true;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      D3D12_SHADER_RESOURCE_VIEW_DESC textureDesc = {0};

      textureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      textureDesc.Format                  = dx12__nullSampledFormat(entry->sampledTexture.sampleType);

      if (!dx12__fillNullSrvTexture(&textureDesc,
                                    entry->sampledTexture.viewType,
                                    entry->sampledTexture.multisampled)) {
        return false;
      }

      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                          NULL,
                                                          &textureDesc,
                                                          handle);

      return true;
    }
    case GPU_BINDING_STORAGE_TEXTURE:
      if (dx12__storageTextureReadOnly(entry->storageTexture.access)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC readTextureDesc = {0};

        readTextureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        readTextureDesc.Format                  = dx12_format(entry->storageTexture.format);

        if (readTextureDesc.Format == DXGI_FORMAT_UNKNOWN
            || !dx12__fillNullSrvTexture(&readTextureDesc,
                                         entry->storageTexture.viewType,
                                         false)) {
          return false;
        }

        device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                            NULL,
                                                            &readTextureDesc,
                                                            handle);
      } else {
        D3D12_UNORDERED_ACCESS_VIEW_DESC writeTextureDesc = {0};

        writeTextureDesc.Format = dx12_format(entry->storageTexture.format);

        if (writeTextureDesc.Format == DXGI_FORMAT_UNKNOWN
            || !dx12__fillNullUavTexture(&writeTextureDesc,
                                         entry->storageTexture.viewType)) {
          return false;
        }

        device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                             NULL,
                                                             NULL,
                                                             &writeTextureDesc,
                                                             handle);
      }

      return true;
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
      D3D12_UNORDERED_ACCESS_VIEW_DESC feedbackDesc = {0};

      feedbackDesc.Format        = DXGI_FORMAT_R8_UINT;
      feedbackDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                           NULL,
                                                           NULL,
                                                           &feedbackDesc,
                                                           handle);

      return true;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      D3D12_SHADER_RESOURCE_VIEW_DESC structureDesc = {0};

      structureDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      structureDesc.ViewDimension           = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                          NULL,
                                                          &structureDesc,
                                                          handle);

      return true;
    }
    default:
      return false;
  }
}

static void
dx12__writeNullSamplerDescriptor(DeviceDX12                    *device,
                                 const GPUBindGroupLayoutEntry *entry,
                                 D3D12_CPU_DESCRIPTOR_HANDLE    handle) {
  D3D12_SAMPLER_DESC desc = {0};

  desc.Filter         = entry->sampler.type == GPU_SAMPLER_BINDING_COMPARISON
                  ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT
                  : entry->sampler.type == GPU_SAMPLER_BINDING_FILTERING
                      ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                      : D3D12_FILTER_MIN_MAG_MIP_POINT;
  desc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  desc.MaxAnisotropy  = 1u;
  desc.ComparisonFunc = entry->sampler.type == GPU_SAMPLER_BINDING_COMPARISON
                          ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                          : D3D12_COMPARISON_FUNC_NEVER;
  desc.MaxLOD         = D3D12_FLOAT32_MAX;
  device->d3dDevice->lpVtbl->CreateSampler(device->d3dDevice, &desc, handle);
}

static void
dx12__destroyNullTables(DeviceDX12            *device,
                        PipelineLayoutDX12    *native) {
  DescriptorTableDX12    *resourceTable;
  DescriptorTableDX12    *samplerTable;
  uint32_t                i;

  if (!device || !native) {
    return;
  }

  for (i = 0u; i < native->groupCount; i++) {
    resourceTable = &native->resourceTables[i];
    samplerTable  = &native->samplerTables[i];

    if (resourceTable->nullOffset != UINT32_MAX) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                           resourceTable->nullOffset,
                           resourceTable->descriptorCount);
      resourceTable->nullOffset = UINT32_MAX;
    }

    if (samplerTable->nullOffset != UINT32_MAX) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                           samplerTable->nullOffset,
                           samplerTable->descriptorCount);
      samplerTable->nullOffset = UINT32_MAX;
    }
  }
}

static GPUResult
dx12__createNullTables(GPUDevice                 *device,
                       GPUPipelineLayout         *layout,
                       GPUBindGroupLayout *const *groups,
                       uint32_t                   groupCount,
                       PipelineLayoutDX12        *native) {
  D3D12_CPU_DESCRIPTOR_HANDLE    resourceHandle;
  D3D12_CPU_DESCRIPTOR_HANDLE    samplerHandle;
  DeviceDX12                    *deviceDX12;
  const GPUBindGroupLayoutEntry *entries;
  DescriptorTableDX12           *resourceTable;
  DescriptorTableDX12           *samplerTable;
  GPUResult                      result;
  uint32_t                       groupIndex;
  uint32_t                       resourceCursor;
  uint32_t                       samplerCursor;
  uint32_t                       i;
  uint32_t                       resourceIndex;
  uint32_t                       samplerIndex;

  deviceDX12 = device ? device->_priv : NULL;

  if (!deviceDX12 || !layout || !native || groupCount != native->groupCount
      || (groupCount > 0u && !groups)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    uint32_t entryCount;

    entries       = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];

    if (entryCount > 0u && !entries) {
      result = GPU_ERROR_BACKEND_FAILURE;
      goto fail;
    }

    if (resourceTable->descriptorCount > 0u) {
      result = dx12_allocateDescriptors(deviceDX12,
                                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                        resourceTable->descriptorCount,
                                        &resourceTable->nullOffset);

      if (result != GPU_OK) {
        goto fail;
      }
    }

    if (samplerTable->descriptorCount > 0u) {
      result = dx12_allocateDescriptors(deviceDX12,
                                        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                        samplerTable->descriptorCount,
                                        &samplerTable->nullOffset);

      if (result != GPU_OK) {
        goto fail;
      }
    }

    resourceCursor = 0u;
    samplerCursor  = 0u;

    for (i = 0u; i < entryCount; i++) {
      if (dx12__resourceTableBinding(deviceDX12, &entries[i])) {
        for (resourceIndex = 0u; resourceIndex < entries[i].arrayCount; resourceIndex++) {
          resourceHandle = dx12_cpuDescriptor(&deviceDX12->resourceDescriptors,
                                              resourceTable->nullOffset + resourceCursor++);

          if (!dx12__writeNullResourceDescriptor(deviceDX12,
                                                 &entries[i],
                                                 resourceHandle)) {
            result = GPU_ERROR_BACKEND_FAILURE;
            goto fail;
          }
        }

      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER
                 && !entries[i].immutableSampler) {
        for (samplerIndex = 0u; samplerIndex < entries[i].arrayCount; samplerIndex++) {
          samplerHandle = dx12_cpuDescriptor(&deviceDX12->samplerDescriptors,
                                             samplerTable->nullOffset + samplerCursor++);
          dx12__writeNullSamplerDescriptor(deviceDX12, &entries[i], samplerHandle);
        }
      }
    }

    if (resourceCursor != resourceTable->descriptorCount
        || samplerCursor != samplerTable->descriptorCount) {
      result = GPU_ERROR_BACKEND_FAILURE;
      goto fail;
    }
  }

  return GPU_OK;

fail:
  dx12__destroyNullTables(deviceDX12, native);

  return result;
}

static const RootBindingDX12*
dx12__findRootBinding(const PipelineLayoutDX12    *layout,
                      uint32_t                     groupIndex,
                      uint32_t                     binding,
                      GPUBindingType               bindingType) {
  uint32_t begin;
  uint32_t end;
  uint32_t i;

  if (!layout || groupIndex >= layout->groupCount) {
    return NULL;
  }

  begin = layout->groupOffsets[groupIndex];
  end   = layout->groupOffsets[groupIndex + 1u];

  for (i = begin; i < end; i++) {
    if (layout->bindings[i].binding == binding
        && layout->bindings[i].bindingType == bindingType) {
      return &layout->bindings[i];
    }
  }

  return NULL;
}

static bool
dx12__fillStaticSamplers(GPUPipelineLayout                *layout,
                         GPUBindGroupLayout        *const *groups,
                         uint32_t                          groupCount,
                         const ShaderStaticSamplerInfo    *sourceSamplers,
                         uint32_t                          sourceSamplerCount,
                         uint64_t                          entryMask,
                         D3D12_STATIC_SAMPLER_DESC        *samplers,
                         uint32_t                          samplerCount) {
  uint32_t cursor;

  cursor = 0u;

  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    uint32_t                       entryCount;

    entries         = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = getPipelineLayoutBackendBindings(layout,
                                                       groupIndex,
                                                       NULL);

    for (uint32_t entryIndex = 0u; entryIndex < entryCount; entryIndex++) {
      if (!entries[entryIndex].immutableSampler) {
        continue;
      }

      for (uint32_t arrayIndex = 0u;
           arrayIndex < entries[entryIndex].arrayCount;
           arrayIndex++) {
        if (cursor >= samplerCount
            || !dx12_fillStaticSamplerDesc(&entries[entryIndex].immutableSamplerDesc,
                                           backendBindings[entryIndex] + arrayIndex,
                                           groupIndex,
                                           dx12__shaderVisibility(
                                          entries[entryIndex].visibility
                                        ),
                                           &samplers[cursor])) {
          return false;
        }

        cursor++;
      }
    }
  }

  for (uint32_t sourceIndex = 0u; sourceIndex < sourceSamplerCount; sourceIndex++) {
    if (!dx12__sourceSamplerSelected(&sourceSamplers[sourceIndex], entryMask)) {
      continue;
    }

    if (cursor >= samplerCount
        || !dx12_fillSourceSamplerDesc(&sourceSamplers[sourceIndex].desc,
                                       sourceSamplers[sourceIndex].hlslIndex,
                                       dx12__shaderVisibility(sourceSamplers[sourceIndex].visibility),
                                       &samplers[cursor])) {
      return false;
    }

    cursor++;
  }

  return cursor == samplerCount;
}

static void
dx12__fillRanges11(const DeviceDX12            *device,
                   GPUPipelineLayout           *layout,
                   GPUBindGroupLayout   *const *groups,
                   const PipelineLayoutDX12    *native,
                   D3D12_ROOT_PARAMETER1       *parameters,
                   D3D12_DESCRIPTOR_RANGE1     *ranges) {
  for (uint32_t rootIndex = 0u; rootIndex < native->bindingCount; rootIndex++) {
    parameters[native->bindings[rootIndex].rootParameter].ParameterType =
      dx12__rootBufferType(native->bindings[rootIndex].bindingType);
    parameters[native->bindings[rootIndex].rootParameter].Descriptor.ShaderRegister = native->bindings[rootIndex].binding;
    parameters[native->bindings[rootIndex].rootParameter].Descriptor.RegisterSpace = native->bindings[rootIndex].groupIndex;
    parameters[native->bindings[rootIndex].rootParameter].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    parameters[native->bindings[rootIndex].rootParameter].ShaderVisibility =
      dx12__shaderVisibility(native->bindings[rootIndex].visibility);
  }

  for (uint32_t groupIndex = 0u;
       groupIndex < native->groupCount;
       groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    const DescriptorTableDX12     *resourceTable;
    const DescriptorTableDX12     *samplerTable;
    uint32_t                       entryCount;
    uint32_t                       resourceOffset;
    uint32_t                       resourceRange;
    uint32_t                       samplerOffset;
    uint32_t                       samplerRange;

    entries         = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = getPipelineLayoutBackendBindings(layout,
                                                       groupIndex,
                                                       NULL);
    resourceTable  = &native->resourceTables[groupIndex];
    samplerTable   = &native->samplerTables[groupIndex];
    resourceOffset = 0u;
    resourceRange  = 0u;
    samplerOffset  = native->samplerTableBaseOnly ? samplerTable->descriptorOffset : 0u;
    samplerRange   = 0u;

    for (uint32_t entryIndex = 0u; entryIndex < entryCount; entryIndex++) {
      D3D12_DESCRIPTOR_RANGE1 *range;
      uint32_t                 tableOffset;

      if (dx12__resourceTableBinding(device, &entries[entryIndex])) {
        tableOffset = resourceOffset;
        range       = &ranges[resourceTable->rangeOffset + resourceRange++];
        resourceOffset += entries[entryIndex].arrayCount;
        range->RangeType = dx12__resourceRangeType(&entries[entryIndex]);
        range->Flags     =
          range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV
            ? D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE
            : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
      } else if (entries[entryIndex].bindingType == GPU_BINDING_SAMPLER
                 && !entries[entryIndex].immutableSampler) {
        tableOffset = samplerOffset;
        range       = &ranges[samplerTable->rangeOffset + samplerRange++];
        samplerOffset += entries[entryIndex].arrayCount;
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range->Flags     = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
      } else {
        continue;
      }

      range->NumDescriptors                    = entries[entryIndex].arrayCount;
      range->BaseShaderRegister                = backendBindings[entryIndex];
      range->RegisterSpace                     = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter].DescriptorTable.NumDescriptorRanges = resourceTable->rangeCount;
      parameters[resourceTable->rootParameter].DescriptorTable.pDescriptorRanges = &ranges[resourceTable->rangeOffset];
      parameters[resourceTable->rootParameter].ShaderVisibility = dx12__shaderVisibility(resourceTable->visibility);
    }

    if (samplerTable->descriptorCount > 0u) {
      parameters[samplerTable->rootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[samplerTable->rootParameter].DescriptorTable.NumDescriptorRanges = samplerTable->rangeCount;
      parameters[samplerTable->rootParameter].DescriptorTable.pDescriptorRanges   = &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility = dx12__shaderVisibility(samplerTable->visibility);
    }
  }
}

static void
dx12__fillRanges10(const DeviceDX12            *device,
                   GPUPipelineLayout           *layout,
                   GPUBindGroupLayout   *const *groups,
                   const PipelineLayoutDX12    *native,
                   D3D12_ROOT_PARAMETER        *parameters,
                   D3D12_DESCRIPTOR_RANGE      *ranges) {
  for (uint32_t rootIndex = 0u; rootIndex < native->bindingCount; rootIndex++) {
    parameters[native->bindings[rootIndex].rootParameter].ParameterType =
      dx12__rootBufferType(native->bindings[rootIndex].bindingType);
    parameters[native->bindings[rootIndex].rootParameter].Descriptor.ShaderRegister = native->bindings[rootIndex].binding;
    parameters[native->bindings[rootIndex].rootParameter].Descriptor.RegisterSpace = native->bindings[rootIndex].groupIndex;
    parameters[native->bindings[rootIndex].rootParameter].ShaderVisibility =
      dx12__shaderVisibility(native->bindings[rootIndex].visibility);
  }

  for (uint32_t groupIndex = 0u;
       groupIndex < native->groupCount;
       groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    const uint32_t                *backendBindings;
    const DescriptorTableDX12     *resourceTable;
    const DescriptorTableDX12     *samplerTable;
    uint32_t                       entryCount;
    uint32_t                       resourceOffset;
    uint32_t                       resourceRange;
    uint32_t                       samplerOffset;
    uint32_t                       samplerRange;

    entries         = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = getPipelineLayoutBackendBindings(layout,
                                                       groupIndex,
                                                       NULL);
    resourceTable  = &native->resourceTables[groupIndex];
    samplerTable   = &native->samplerTables[groupIndex];
    resourceOffset = 0u;
    resourceRange  = 0u;
    samplerOffset  = native->samplerTableBaseOnly ? samplerTable->descriptorOffset : 0u;
    samplerRange   = 0u;

    for (uint32_t entryIndex = 0u; entryIndex < entryCount; entryIndex++) {
      D3D12_DESCRIPTOR_RANGE *range;
      uint32_t                tableOffset;

      if (dx12__resourceTableBinding(device, &entries[entryIndex])) {
        tableOffset = resourceOffset;
        range       = &ranges[resourceTable->rangeOffset + resourceRange++];
        resourceOffset += entries[entryIndex].arrayCount;
        range->RangeType = dx12__resourceRangeType(&entries[entryIndex]);
      } else if (entries[entryIndex].bindingType == GPU_BINDING_SAMPLER
                 && !entries[entryIndex].immutableSampler) {
        tableOffset = samplerOffset;
        range       = &ranges[samplerTable->rangeOffset + samplerRange++];
        samplerOffset += entries[entryIndex].arrayCount;
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      } else {
        continue;
      }

      range->NumDescriptors                    = entries[entryIndex].arrayCount;
      range->BaseShaderRegister                = backendBindings[entryIndex];
      range->RegisterSpace                     = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter].DescriptorTable.NumDescriptorRanges = resourceTable->rangeCount;
      parameters[resourceTable->rootParameter].DescriptorTable.pDescriptorRanges = &ranges[resourceTable->rangeOffset];
      parameters[resourceTable->rootParameter].ShaderVisibility = dx12__shaderVisibility(resourceTable->visibility);
    }

    if (samplerTable->descriptorCount > 0u) {
      parameters[samplerTable->rootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[samplerTable->rootParameter].DescriptorTable.NumDescriptorRanges = samplerTable->rangeCount;
      parameters[samplerTable->rootParameter].DescriptorTable.pDescriptorRanges   = &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility = dx12__shaderVisibility(samplerTable->visibility);
    }
  }
}

static GPUResult
dx12__createPipelineLayout(GPUDevice                        *device,
                           GPUPipelineLayout                *layout,
                           const ShaderStaticSamplerInfo    *sourceSamplers,
                           uint32_t                          sourceSamplerCount,
                           uint64_t                          entryMask,
                           PipelineLayoutDX12              **outNative) {
  PipelineLayoutDX12        *native;
  GPUBindGroupLayout *const *groups;
  DeviceDX12                *deviceDX12;
  ID3DBlob                  *serialized;
  ID3DBlob                  *errors;
  DX12LayoutPlan             plan;
  GPUResult                  planResult;
  uint32_t                   groupCount;
  uint32_t                   pushSize;
  uint32_t                   pushDwordCount;
  uint32_t                   pushRootParameter;
  uint32_t                   selectedSamplerCount;
  GPUShaderStageFlags        pushStages;
  HRESULT                    result;

  if (!device || !device->_priv || !layout || !outNative
      || (sourceSamplerCount > 0u && !sourceSamplers)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outNative = NULL;

  groups = getPipelineLayoutGroups(layout, &groupCount);
  getPipelineLayoutPushConstants(layout, &pushSize, &pushStages);

  if ((pushSize & 3u) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  deviceDX12 = device->_priv;
  planResult = dx12__makeLayoutPlan(deviceDX12,
                                    layout,
                                    groups,
                                    groupCount,
                                    &plan);

  if (planResult != GPU_OK) {
    return planResult;
  }

  selectedSamplerCount = 0u;

  for (uint32_t i = 0u; i < sourceSamplerCount; i++) {
    selectedSamplerCount += dx12__sourceSamplerSelected(&sourceSamplers[i],
                                                        entryMask);
  }

  if (selectedSamplerCount > UINT32_MAX - plan.staticSamplerCount) {
    return GPU_ERROR_UNSUPPORTED;
  }

  plan.staticSamplerCount += selectedSamplerCount;
  pushDwordCount    = pushSize / 4u;
  pushRootParameter = UINT32_MAX;

  if (pushDwordCount > 0u) {
    if (pushDwordCount > DX12_ROOT_SIGNATURE_DWORD_LIMIT -
                           plan.rootDwordCount) {
      return GPU_ERROR_UNSUPPORTED;
    }

    pushRootParameter = plan.rootParameterCount++;
    plan.rootDwordCount += pushDwordCount;
  }

  if (!(native = calloc(1,
                        sizeof(*native) +
                    (size_t)plan.bindingCount * sizeof(*native->bindings)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->bindings                  = plan.bindingCount > 0u ? (RootBindingDX12 *)(native + 1) : NULL;
  native->bindingCount              = plan.bindingCount;
  native->rangeCount                = plan.rangeCount;
  native->rootParameterCount        = plan.rootParameterCount;
  native->groupCount                = groupCount;
  native->pushConstantRootParameter = pushRootParameter;
  native->pushConstantDwordCount    = pushDwordCount;
  native->samplerTableBaseOnly      = !deviceDX12->samplerTableOffsetsReliable;
  dx12__fillLayoutPlan(deviceDX12, layout, groups, groupCount, native);

  serialized = NULL;
  errors     = NULL;

  if (deviceDX12->rootSignatureVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {0};
    D3D12_ROOT_PARAMETER1              *parameters11;
    D3D12_DESCRIPTOR_RANGE1            *ranges11;
    D3D12_STATIC_SAMPLER_DESC          *staticSamplers11;

    parameters11     = plan.rootParameterCount > 0u ? calloc(plan.rootParameterCount, sizeof(*parameters11)) : NULL;
    ranges11         = plan.rangeCount > 0u ? calloc(plan.rangeCount, sizeof(*ranges11)) : NULL;
    staticSamplers11 = plan.staticSamplerCount > 0u
                       ? calloc(plan.staticSamplerCount,
                                sizeof(*staticSamplers11))
                       : NULL;

    if ((plan.rootParameterCount > 0u && !parameters11)
        || (plan.rangeCount > 0u && !ranges11)
        || (plan.staticSamplerCount > 0u && !staticSamplers11)) {
      free(staticSamplers11);
      free(ranges11);
      free(parameters11);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    if (!dx12__fillStaticSamplers(layout,
                                  groups,
                                  groupCount,
                                  sourceSamplers,
                                  sourceSamplerCount,
                                  entryMask,
                                  staticSamplers11,
                                  plan.staticSamplerCount)) {
      free(staticSamplers11);
      free(ranges11);
      free(parameters11);
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    dx12__fillRanges11(deviceDX12,
                       layout,
                       groups,
                       native,
                       parameters11,
                       ranges11);

    if (pushDwordCount > 0u) {
      parameters11[pushRootParameter].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters11[pushRootParameter].Constants.ShaderRegister = 0u;
      parameters11[pushRootParameter].Constants.RegisterSpace  = GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE;
      parameters11[pushRootParameter].Constants.Num32BitValues = pushDwordCount;
      parameters11[pushRootParameter].ShaderVisibility         = dx12__shaderVisibility(pushStages);
    }

    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1.NumParameters = plan.rootParameterCount;
    versionedDesc.Desc_1_1.pParameters = parameters11;
    versionedDesc.Desc_1_1.NumStaticSamplers = plan.staticSamplerCount;
    versionedDesc.Desc_1_1.pStaticSamplers = staticSamplers11;
    versionedDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result                       = D3D12SerializeVersionedRootSignature(&versionedDesc,
                                                                        &serialized,
                                                                        &errors);
    free(staticSamplers11);
    free(ranges11);
    free(parameters11);
  } else {
    D3D12_ROOT_SIGNATURE_DESC  rootDesc = {0};
    D3D12_ROOT_PARAMETER      *parameters10;
    D3D12_DESCRIPTOR_RANGE    *ranges10;
    D3D12_STATIC_SAMPLER_DESC *staticSamplers10;

    parameters10     = plan.rootParameterCount > 0u ? calloc(plan.rootParameterCount, sizeof(*parameters10)) : NULL;
    ranges10         = plan.rangeCount > 0u ? calloc(plan.rangeCount, sizeof(*ranges10)) : NULL;
    staticSamplers10 = plan.staticSamplerCount > 0u
                       ? calloc(plan.staticSamplerCount,
                                sizeof(*staticSamplers10))
                       : NULL;

    if ((plan.rootParameterCount > 0u && !parameters10)
        || (plan.rangeCount > 0u && !ranges10)
        || (plan.staticSamplerCount > 0u && !staticSamplers10)) {
      free(staticSamplers10);
      free(ranges10);
      free(parameters10);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    if (!dx12__fillStaticSamplers(layout,
                                  groups,
                                  groupCount,
                                  sourceSamplers,
                                  sourceSamplerCount,
                                  entryMask,
                                  staticSamplers10,
                                  plan.staticSamplerCount)) {
      free(staticSamplers10);
      free(ranges10);
      free(parameters10);
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    dx12__fillRanges10(deviceDX12,
                       layout,
                       groups,
                       native,
                       parameters10,
                       ranges10);

    if (pushDwordCount > 0u) {
      parameters10[pushRootParameter].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters10[pushRootParameter].Constants.ShaderRegister = 0u;
      parameters10[pushRootParameter].Constants.RegisterSpace  = GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE;
      parameters10[pushRootParameter].Constants.Num32BitValues = pushDwordCount;
      parameters10[pushRootParameter].ShaderVisibility         = dx12__shaderVisibility(pushStages);
    }

    rootDesc.NumParameters     = plan.rootParameterCount;
    rootDesc.pParameters       = parameters10;
    rootDesc.NumStaticSamplers = plan.staticSamplerCount;
    rootDesc.pStaticSamplers   = staticSamplers10;
    rootDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result                     = D3D12SerializeRootSignature(&rootDesc,
                                                             D3D_ROOT_SIGNATURE_VERSION_1_0,
                                                             &serialized,
                                                             &errors);
    free(staticSamplers10);
    free(ranges10);
    free(parameters10);
  }

  if (FAILED(result) || !serialized) {
    dx12__logRootSignatureError(errors);
    if (errors) {
      errors->lpVtbl->Release(errors);
    }
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  {
    DX12PipelineKey key;

    dx12_keyInit(&key);
    dx12_keyWrite(&key,
                  serialized->lpVtbl->GetBufferPointer(serialized),
                  serialized->lpVtbl->GetBufferSize(serialized));
    memcpy(native->rootSignatureKey, key.value, sizeof(key.value));
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateRootSignature(deviceDX12->d3dDevice,
                                                              0u,
                                                              serialized->lpVtbl->GetBufferPointer(serialized),
                                                              serialized->lpVtbl->GetBufferSize(serialized),
                                                              &IID_ID3D12RootSignature,
                                                              (void **)&native->rootSignature);
  serialized->lpVtbl->Release(serialized);

  if (errors) {
    errors->lpVtbl->Release(errors);
  }

  if (FAILED(result)) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outNative = native;
  return GPU_OK;
}

static bool
dx12__bindGroupDescriptorOffset(const BindGroupDX12           *group,
                                const BindGroupBindingView    *binding,
                                bool                           sampler,
                                uint32_t                      *outOffset) {
  uint32_t base;
  uint32_t count;

  if (!group || !binding || !outOffset
      || binding->layoutEntryIndex >= group->entryCount) {
    return false;
  }

  base  = group->descriptorOffsets[binding->layoutEntryIndex];
  count = sampler ? group->samplerCount : group->resourceCount;

  if (base == UINT32_MAX || binding->arrayIndex >= binding->arrayCount
      || base > count || binding->arrayIndex >= count - base) {
    return false;
  }

  *outOffset = base + binding->arrayIndex;
  return true;
}

static void
dx12__bufferAlignments(GPUBindingType                type,
                       const GPUBufferBindingLayout *layout,
                       bool                          table,
                       uint32_t                     *address,
                       uint32_t                     *size) {
  *address = 1u;
  *size    = 1u;

  if (type == GPU_BINDING_UNIFORM_BUFFER) {
    *address = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    *size    = table ? D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT : 1u;
  } else if (layout->byteAddress || layout->strideBytes == 0u) {
    *address = D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
    *size    = 4u;
  }
}

static bool
dx12__bufferBindingSize(const BindGroupBindingView    *binding,
                        const BufferDX12              *buffer,
                        bool                           table,
                        uint64_t                      *outSize) {
  uint32_t addressAlignment;
  uint32_t sizeAlignment;

  if (!buffer || !buffer->resource)
    return false;

  dx12__bufferAlignments(binding->bindingType,
                         &binding->bufferLayout,
                         table,
                         &addressAlignment,
                         &sizeAlignment);

  return dx12_bufferViewRange(buffer->gpuAddress,
                              buffer->sizeBytes,
                              binding->offset,
                              binding->size,
                              addressAlignment,
                              sizeAlignment,
                              outSize);
}

static bool
dx12__writeBufferDescriptor(DeviceDX12                    *device,
                            uint32_t                       resourceOffset,
                            uint32_t                       resourceCount,
                            const BindGroupBindingView    *binding,
                            uint32_t                       descriptorOffset) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle;
  BufferDX12                 *buffer;
  uint64_t                    stride;
  uint64_t                    size;
  bool                        raw;

  buffer = binding->buffer ? binding->buffer->_priv : NULL;

  if (!device || !binding->buffer || !buffer || !buffer->resource
      || !binding->buffer->device
      || binding->buffer->device->_priv != device
      || descriptorOffset >= resourceCount
      || !dx12__bufferBindingSize(binding, buffer, true, &size)) {
    return false;
  }

  handle = dx12_cpuDescriptor(&device->resourceDescriptors,
                              resourceOffset + descriptorOffset);
  stride = binding->bufferLayout.strideBytes;

  if (binding->bindingType == GPU_BINDING_UNIFORM_BUFFER) {
    D3D12_CONSTANT_BUFFER_VIEW_DESC constantDesc = {0};

    if (size > D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u) {
      return false;
    }

    constantDesc.BufferLocation = buffer->gpuAddress + binding->offset;
    constantDesc.SizeInBytes    = (UINT)size;
    device->d3dDevice->lpVtbl->CreateConstantBufferView(device->d3dDevice,
                                                        &constantDesc,
                                                        handle);

    return true;
  }

  raw = binding->bufferLayout.byteAddress || stride == 0u;

  if (raw) {
    if ((binding->offset & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1u)) != 0u)
      return false;

    stride = 4u;
  }

  if (binding->offset % stride != 0u
      || size % stride != 0u || size / stride > UINT32_MAX) {
    return false;
  }

  if (binding->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER) {
    D3D12_SHADER_RESOURCE_VIEW_DESC readDesc = {0};

    readDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    readDesc.Format                     = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
    readDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    readDesc.Buffer.FirstElement        = binding->offset / stride;
    readDesc.Buffer.NumElements         = (UINT)(size / stride);
    readDesc.Buffer.StructureByteStride = raw ? 0u : binding->bufferLayout.strideBytes;
    readDesc.Buffer.Flags               = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
    device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                        buffer->resource,
                                                        &readDesc,
                                                        handle);

    return true;
  }

  if (binding->bindingType == GPU_BINDING_STORAGE_BUFFER) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC writeDesc = {0};

    writeDesc.Format                     = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
    writeDesc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
    writeDesc.Buffer.FirstElement        = binding->offset / stride;
    writeDesc.Buffer.NumElements         = (UINT)(size / stride);
    writeDesc.Buffer.StructureByteStride = raw ? 0u : binding->bufferLayout.strideBytes;
    writeDesc.Buffer.Flags               = raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
    device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                         buffer->resource,
                                                         NULL,
                                                         &writeDesc,
                                                         handle);

    return true;
  }

  return false;
}

static void
dx12__writeBindGroup(void                          *context,
                     const BindGroupBindingView    *binding) {
  D3D12_CPU_DESCRIPTOR_HANDLE   handle;
  DX12BindGroupWriteContext    *writeContext;
  TextureViewDX12              *sampledView;
  TextureViewDX12              *storageView;
  SamplerDX12                  *sampler;
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  SamplerFeedbackMapDX12       *map;
#endif
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  TextureDX12                  *target;
#endif
  AccelerationStructureDX12    *structure;
  bool                          readOnly;

  writeContext = context;

  if (!writeContext || !writeContext->valid || !binding) {
    return;
  }

  switch (binding->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER:
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER: {
      uint32_t bufferOffset;
      uint64_t size;

      if (binding->kind != GPUBindKindBuffer) {
        writeContext->valid = false;
        return;
      }

      if (!binding->buffer) {
        return;
      }

      if (!binding->buffer->device
          || binding->buffer->device->_priv != writeContext->group->device
          || ((binding->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
               || binding->bindingType == GPU_BINDING_STORAGE_BUFFER)
              && !bufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE))) {
        writeContext->valid = false;
        return;
      }

      if (dx12__resourceTableBindingType(writeContext->group->device,
                                         binding->bindingType,
                                         binding->arrayCount)) {
        if (!dx12__bindGroupDescriptorOffset(writeContext->group,
                                             binding,
                                             false,
                                             &bufferOffset)
            || !dx12__writeBufferDescriptor(writeContext->group->device,
                                            writeContext->group->resourceOffset,
                                            writeContext->group->resourceCount,
                                            binding,
                                            bufferOffset)) {
          writeContext->valid = false;
        }

      } else if (!dx12__bufferBindingSize(binding, binding->buffer->_priv, false, &size)) {
        writeContext->valid = false;
      }

      break;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      uint32_t textureOffset;

      if (binding->kind != GPUBindKindTexture
          || !dx12__bindGroupDescriptorOffset(writeContext->group,
                                              binding,
                                              false,
                                              &textureOffset)) {
        writeContext->valid = false;
        return;
      }

      if (!binding->textureView) {
        return;
      }

      sampledView = binding->textureView ? binding->textureView->_priv : NULL;

      if (!sampledView || !sampledView->resource
          || !sampledView->hasSrv || !binding->textureView->_texture
          || !binding->textureView->_texture->device
          || binding->textureView->_texture->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(&writeContext->group->device->resourceDescriptors,
                                  writeContext->group->resourceOffset + textureOffset);
      writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(writeContext->group->device->d3dDevice,
                                                                               sampledView->resource,
                                                                               &sampledView->srv,
                                                                               handle);
      break;
    }
    case GPU_BINDING_STORAGE_TEXTURE: {
      uint32_t storageOffset;

      if (binding->kind != GPUBindKindTexture
          || !dx12__bindGroupDescriptorOffset(writeContext->group,
                                              binding,
                                              false,
                                              &storageOffset)) {
        writeContext->valid = false;
        return;
      }

      if (!binding->textureView) {
        return;
      }

      storageView = binding->textureView ? binding->textureView->_priv : NULL;
      readOnly    = dx12__storageTextureReadOnly(
        binding->storageTextureAccess
      );

      if (!storageView || !storageView->resource
          || (readOnly ? !storageView->hasSrv : !storageView->hasUav)
          || !binding->textureView->_texture
          || !binding->textureView->_texture->device
          || binding->textureView->_texture->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(&writeContext->group->device->resourceDescriptors,
                                  writeContext->group->resourceOffset + storageOffset);

      if (readOnly) {
        writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(writeContext->group->device->d3dDevice,
                                                                                 storageView->resource,
                                                                                 &storageView->srv,
                                                                                 handle);
      } else {
        writeContext->group->device->d3dDevice->lpVtbl->CreateUnorderedAccessView(writeContext->group->device->d3dDevice,
                                                                                  storageView->resource,
                                                                                  NULL,
                                                                                  &storageView->uav,
                                                                                  handle);
      }

      break;
    }
    case GPU_BINDING_SAMPLER: {
      uint32_t samplerOffset;

      if (binding->kind != GPUBindKindSampler
          || !dx12__bindGroupDescriptorOffset(writeContext->group,
                                              binding,
                                              true,
                                              &samplerOffset)) {
        writeContext->valid = false;
        return;
      }

      if (!binding->sampler) {
        return;
      }

      sampler = binding->sampler ? binding->sampler->_priv : NULL;

      if (!sampler || sampler->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(&writeContext->group->device->samplerDescriptors,
                                  writeContext->group->samplerOffset + samplerOffset);
      writeContext->group->device->d3dDevice->lpVtbl->CreateSampler(writeContext->group->device->d3dDevice,
                                                                    &sampler->desc,
                                                                    handle);
      break;
    }
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
      uint32_t feedbackOffset;

      if (binding->kind != GPUBindKindSamplerFeedback
          || !dx12__bindGroupDescriptorOffset(writeContext->group,
                                              binding,
                                              false,
                                              &feedbackOffset)) {
        writeContext->valid = false;
        return;
      }

      if (!binding->samplerFeedback) {
        return;
      }

      map    = binding->samplerFeedback->_priv;
      target = binding->samplerFeedback->texture ? binding->samplerFeedback->texture->_priv : NULL;

      if (!map || map->device != writeContext->group->device
          || !map->resource || !target || !target->resource
          || !writeContext->group->device->d3dDevice8) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(&writeContext->group->device->resourceDescriptors,
                                  writeContext->group->resourceOffset + feedbackOffset);
      writeContext->group->device->d3dDevice8->lpVtbl->CreateSamplerFeedbackUnorderedAccessView(writeContext->group->device->d3dDevice8,
                                                                                                target->resource,
                                                                                                map->resource,
                                                                                                handle);
#else
      writeContext->valid = false;
#endif
      break;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
      uint32_t                        structureOffset;

      if (binding->kind != GPUBindKindAccelerationStructure
          || !dx12__bindGroupDescriptorOffset(writeContext->group,
                                              binding,
                                              false,
                                              &structureOffset)) {
        writeContext->valid = false;
        return;
      }

      if (!binding->accelerationStructure) {
        return;
      }

      structure = binding->accelerationStructure->_priv;

      if (!structure || !structure->resource || !structure->address
          || !binding->accelerationStructure->device
          || binding->accelerationStructure->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.ViewDimension           = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
      desc.RaytracingAccelerationStructure.Location = structure->address;
      handle = dx12_cpuDescriptor(&writeContext->group->device->resourceDescriptors,
                                  writeContext->group->resourceOffset + structureOffset);
      writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(writeContext->group->device->d3dDevice,
                                                                               NULL,
                                                                               &desc,
                                                                               handle);
      break;
    }
    default:
      writeContext->valid = false;
      break;
  }
}

static DX12DynamicBufferRange*
dx12__dynamicRanges(BindGroupDX12    *group) {
  return (DX12DynamicBufferRange *)(group->descriptorOffsets + group->entryCount);
}

static bool
dx12__initDynamicRanges(BindGroupDX12                 *native,
                        const BindGroupPriv           *group,
                        const GPUBindGroupLayoutEntry *entries) {
  DX12DynamicBufferRange        *ranges;
  const BindGroupBindingPriv    *binding;
  const GPUBindGroupLayoutEntry *entry;
  const BufferDX12              *buffer;
  uint32_t                       count = 0u;
  uint32_t                       i;

  ranges = dx12__dynamicRanges(native);

  /* bindless groups reject dynamic offsets; these records never need updates. */
  for (i = 0u; i < group->count; i++) {
    binding = &group->bindings[i];
    uint32_t addressAlignment;
    uint32_t sizeAlignment;

    if (binding->dynamicOffsetIndex == UINT32_MAX)
      continue;

    if (binding->kind != GPUBindKindBuffer || !binding->buffer
        || binding->layoutEntryIndex >= native->entryCount
        || binding->dynamicOffsetIndex >= native->dynamicOffsetCount
        || !(buffer = binding->buffer->_priv) || !buffer->resource) {
      return false;
    }

    entry = &entries[binding->layoutEntryIndex];
    dx12__bufferAlignments(entry->bindingType,
                           &entry->buffer,
                           dx12__resourceTableBinding(native->device, entry),
                           &addressAlignment,
                           &sizeAlignment);

    if (!dx12_dynamicBufferRange(buffer->gpuAddress,
                                 buffer->sizeBytes,
                                 binding->buffer->sizeBytes,
                                 binding->offset,
                                 binding->size,
                                 addressAlignment,
                                 sizeAlignment,
                                 entry->buffer.strideBytes,
                                 &ranges[binding->dynamicOffsetIndex])) {
      return false;
    }

    count++;
  }

  return count == native->dynamicOffsetCount;
}

static uint32_t
dx12__runtimeBindingCount(GPUBindGroupLayout *layout) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  uint32_t                       runtimeCount;
  uint32_t                       i;

  entries      = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  runtimeCount = 0u;

  if (!entries && entryCount > 0u) {
    return 0u;
  }

  for (i = 0u; i < entryCount; i++) {
    if (!entries[i].immutableSampler) {
      if (entries[i].arrayCount > UINT32_MAX - runtimeCount) {
        return 0u;
      }

      runtimeCount += entries[i].arrayCount;
    }
  }

  return runtimeCount;
}

static bool
dx12__prepareResourceTable(CommandBufferDX12    *command,
                           GPUBindGroupLayout   *layout,
                           BindGroupDX12        *group,
                           uint32_t             *outResourceOffset) {
  D3D12_CPU_DESCRIPTOR_HANDLE    dst;
  D3D12_CPU_DESCRIPTOR_HANDLE    src;
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  uint32_t                       i;
  bool                           dynamic;

  if (!layout || !group || !outResourceOffset) {
    return false;
  }

  *outResourceOffset = group->resourceOffset;

  if (group->resourceCount == 0u) {
    return true;
  }

  entries = GPUGetBindGroupLayoutEntries(layout, &entryCount);

  if (!entries && entryCount > 0u) {
    return false;
  }

  dynamic = false;

  for (i = 0u; i < entryCount; i++) {
    if (dx12__resourceTableBinding(group->device, &entries[i])
        && dx12__bufferBindingType(entries[i].bindingType)
        && entries[i].hasDynamicOffset) {
      dynamic = true;
      break;
    }
  }

  if (!dynamic) {
    return true;
  }

  if (!command
      || dx12_allocateCommandDescriptors(command,
                                         group->resourceCount,
                                         outResourceOffset) != GPU_OK) {
    return false;
  }

  dst = dx12_cpuDescriptor(&group->device->resourceDescriptors,
                           *outResourceOffset);
  src = dx12_cpuDescriptor(&group->device->resourceDescriptors,
                           group->resourceOffset);
  group->device->d3dDevice->lpVtbl->CopyDescriptorsSimple(group->device->d3dDevice,
                                                          group->resourceCount,
                                                          dst,
                                                          src,
                                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  return true;
}

static bool
dx12__bindDescriptorHeaps(ID3D12GraphicsCommandList *commandList,
                          ID3D12DescriptorHeap     **boundResourceHeap,
                          ID3D12DescriptorHeap     **boundSamplerHeap,
                          DeviceDX12                *device,
                          ID3D12DescriptorHeap      *samplerHeap,
                          bool                       needsResources,
                          bool                       needsSamplers) {
  ID3D12DescriptorHeap *heaps[2];
  ID3D12DescriptorHeap *desiredSamplerHeap;
  uint32_t              count;

  if (!commandList || !boundResourceHeap || !boundSamplerHeap || !device) {
    return false;
  }

  desiredSamplerHeap = samplerHeap ? samplerHeap : device->samplerDescriptors.heap;

  if ((needsResources && !device->resourceDescriptors.heap)
      || (needsSamplers && !desiredSamplerHeap)) {
    return false;
  }

  if (*boundResourceHeap == device->resourceDescriptors.heap
      && *boundSamplerHeap == desiredSamplerHeap) {
    return true;
  }

  count = 0u;

  if (device->resourceDescriptors.heap) {
    heaps[count++] = device->resourceDescriptors.heap;
  }

  if (desiredSamplerHeap) {
    heaps[count++] = desiredSamplerHeap;
  }

  if (count > 0u) {
    commandList->lpVtbl->SetDescriptorHeaps(commandList, count, heaps);
  }

  *boundResourceHeap = device->resourceDescriptors.heap;
  *boundSamplerHeap  = desiredSamplerHeap;
  return true;
}

static bool
dx12__bindSamplerSnapshot(CommandBufferDX12           *command,
                          ID3D12GraphicsCommandList   *commandList,
                          ID3D12DescriptorHeap       **boundResourceHeap,
                          ID3D12DescriptorHeap       **boundSamplerHeap,
                          DeviceDX12                  *device,
                          const PipelineLayoutDX12    *layout,
                          GPUBindGroup         *const *boundGroups,
                          uint32_t                     overrideIndex,
                          GPUBindGroup                *overrideGroup,
                          const uint32_t              *resourceOffsets,
                          uint32_t                     resourceOffsetMask,
                          bool                         compute) {
  D3D12_CPU_DESCRIPTOR_HANDLE dstBase = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE samplerBase = {0};
  CommandSamplerHeapDX12     *snapshot;
  bool                        needsResources;

  if (!command || !commandList || !boundResourceHeap
      || !boundSamplerHeap || !device || !layout
      || !layout->samplerTableBaseOnly
      || layout->samplerDescriptorCount == 0u) {
    return false;
  }

  if (!(snapshot = dx12__takeCommandSamplerHeap(command,
                                                layout->samplerDescriptorCount))) {
    return false;
  }

  snapshot->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(snapshot->heap,
                                                             &dstBase);
  snapshot->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(snapshot->heap,
                                                             &samplerBase);

  needsResources = false;

  for (uint32_t copyIndex = 0u; copyIndex < layout->groupCount; copyIndex++) {
    const DescriptorTableDX12    *resourceTable;
    const DescriptorTableDX12    *samplerTable;
    GPUBindGroup                 *group;
    BindGroupDX12                *nativeGroup;
    D3D12_CPU_DESCRIPTOR_HANDLE   dst;
    D3D12_CPU_DESCRIPTOR_HANDLE   src;
    uint32_t                      sourceOffset;

    resourceTable = &layout->resourceTables[copyIndex];
    samplerTable  = &layout->samplerTables[copyIndex];
    needsResources |= resourceTable->descriptorCount > 0u;

    if (samplerTable->descriptorCount == 0u) {
      continue;
    }

    group        = copyIndex == overrideIndex
              ? overrideGroup
              : boundGroups ? boundGroups[copyIndex] : NULL;
    nativeGroup  = group ? group->_native : NULL;
    sourceOffset = nativeGroup && nativeGroup->device == device
                   && nativeGroup->samplerCount ==
                     samplerTable->descriptorCount ? nativeGroup->samplerOffset : samplerTable->nullOffset;

    if (sourceOffset == UINT32_MAX) {
      return false;
    }

    dst = dstBase;
    dst.ptr += (SIZE_T)samplerTable->descriptorOffset *
               device->samplerDescriptors.descriptorSize;
    src = dx12_cpuDescriptor(&device->samplerDescriptors, sourceOffset);
    device->d3dDevice->lpVtbl->CopyDescriptorsSimple(device->d3dDevice,
                                                     samplerTable->descriptorCount,
                                                     dst,
                                                     src,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  }

  if (!dx12__bindDescriptorHeaps(commandList,
                                 boundResourceHeap,
                                 boundSamplerHeap,
                                 device,
                                 snapshot->heap,
                                 needsResources,
                                 true)) {
    return false;
  }

  for (uint32_t bindIndex = 0u; bindIndex < layout->groupCount; bindIndex++) {
    const DescriptorTableDX12    *boundResourceTable;
    const DescriptorTableDX12    *boundSamplerTable;
    D3D12_GPU_DESCRIPTOR_HANDLE   handle;
    uint32_t                      resourceOffset;

    boundResourceTable = &layout->resourceTables[bindIndex];
    boundSamplerTable  = &layout->samplerTables[bindIndex];

    if (boundResourceTable->descriptorCount > 0u) {
      resourceOffset =
        (resourceOffsetMask & (1u << bindIndex)) != 0u && resourceOffsets
          ? resourceOffsets[bindIndex]
          : boundResourceTable->nullOffset;

      if (resourceOffset == UINT32_MAX) {
        return false;
      }

      handle = dx12_gpuDescriptor(&device->resourceDescriptors,
                                  resourceOffset);

      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                           boundResourceTable->rootParameter,
                                                           handle);
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(commandList,
                                                            boundResourceTable->rootParameter,
                                                            handle);
      }
    }

    if (boundSamplerTable->descriptorCount > 0u) {
      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                           boundSamplerTable->rootParameter,
                                                           samplerBase);
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(commandList,
                                                            boundSamplerTable->rootParameter,
                                                            samplerBase);
      }
    }
  }

  return true;
}

static bool
dx12__bindNullTables(CommandBufferDX12           *command,
                     ID3D12GraphicsCommandList   *commandList,
                     ID3D12DescriptorHeap       **boundResourceHeap,
                     ID3D12DescriptorHeap       **boundSamplerHeap,
                     DeviceDX12                  *device,
                     const PipelineLayoutDX12    *layout,
                     uint32_t                    *resourceOffsets,
                     uint32_t                    *resourceOffsetMask,
                     bool                         compute) {
  bool needsResources;
  bool needsSamplers;

  if (!commandList || !boundResourceHeap || !boundSamplerHeap
      || !device || !layout) {
    return false;
  }

  needsResources = false;
  needsSamplers  = false;

  if (resourceOffsetMask) {
    *resourceOffsetMask = 0u;
  }

  for (uint32_t planIndex = 0u; planIndex < layout->groupCount; planIndex++) {
    needsResources |= layout->resourceTables[planIndex].descriptorCount > 0u;
    needsSamplers  |= layout->samplerTables[planIndex].descriptorCount > 0u;

    if (layout->resourceTables[planIndex].descriptorCount > 0u
        && resourceOffsets && resourceOffsetMask) {
      resourceOffsets[planIndex] = layout->resourceTables[planIndex].nullOffset;
      *resourceOffsetMask |= 1u << planIndex;
    }
  }

  if (layout->samplerTableBaseOnly && needsSamplers) {
    return dx12__bindSamplerSnapshot(command,
                                     commandList,
                                     boundResourceHeap,
                                     boundSamplerHeap,
                                     device,
                                     layout,
                                     NULL,
                                     UINT32_MAX,
                                     NULL,
                                     resourceOffsets,
                                     resourceOffsetMask ? *resourceOffsetMask : 0u,
                                     compute);
  }

  if (!dx12__bindDescriptorHeaps(commandList,
                                 boundResourceHeap,
                                 boundSamplerHeap,
                                 device,
                                 NULL,
                                 needsResources,
                                 needsSamplers)) {
    return false;
  }

  for (uint32_t bindIndex = 0u; bindIndex < layout->groupCount; bindIndex++) {
    const DescriptorTableDX12    *resourceTable;
    const DescriptorTableDX12    *samplerTable;
    D3D12_GPU_DESCRIPTOR_HANDLE   handle;

    resourceTable = &layout->resourceTables[bindIndex];
    samplerTable  = &layout->samplerTables[bindIndex];

    if ((resourceTable->descriptorCount > 0u
         && resourceTable->nullOffset == UINT32_MAX)
        || (samplerTable->descriptorCount > 0u
            && samplerTable->nullOffset == UINT32_MAX)) {
      return false;
    }

    if (resourceTable->descriptorCount > 0u) {
      handle = dx12_gpuDescriptor(&device->resourceDescriptors,
                                  resourceTable->nullOffset);

      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                           resourceTable->rootParameter,
                                                           handle);
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(commandList,
                                                            resourceTable->rootParameter,
                                                            handle);
      }
    }

    if (samplerTable->descriptorCount > 0u) {
      handle = dx12_gpuDescriptor(&device->samplerDescriptors,
                                  samplerTable->nullOffset);

      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                           samplerTable->rootParameter,
                                                           handle);
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(commandList,
                                                            samplerTable->rootParameter,
                                                            handle);
      }
    }
  }

  return true;
}

static bool
dx12__transitionSampledTexture(ID3D12GraphicsCommandList *commandList,
                               TextureViewDX12           *view) {
  D3D12_RESOURCE_BARRIER barrier = {0};
  const D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  if (!commandList || !view || !view->resource || !view->state) {
    return false;
  }

  if (view->texture) {
    return dx12_transitionTexture(commandList,
                                  view->texture,
                                  view->baseMip,
                                  view->mipCount,
                                  view->baseLayer,
                                  view->layerCount,
                                  requiredState);
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
dx12__transitionReadOnlyStorageBuffer(ID3D12GraphicsCommandList *commandList,
                                      BufferDX12                *buffer) {
  const D3D12_RESOURCE_STATES requiredState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  return dx12_transitionBuffer(commandList, buffer, requiredState);
}

static bool
dx12__transitionStorageBuffer(ID3D12GraphicsCommandList *commandList,
                              BufferDX12                *buffer) {
  return dx12_transitionBuffer(commandList,
                               buffer,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

static bool
dx12__transitionStorageTexture(ID3D12GraphicsCommandList *commandList,
                               TextureViewDX12           *view) {
  if (!commandList || !view || !view->resource || !view->texture) {
    return false;
  }

  return dx12_transitionTexture(commandList,
                                view->texture,
                                view->baseMip,
                                view->mipCount,
                                view->baseLayer,
                                view->layerCount,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

static void
dx12__bindRoot(void *context, const BindGroupBindingView    *binding) {
  DX12BindContext              *bindContext;
  const RootBindingDX12        *uniformRoot;
  BufferDX12                   *uniformBuffer;
  const RootBindingDX12        *readRoot;
  BufferDX12                   *readBuffer;
  const RootBindingDX12        *writeRoot;
  BufferDX12                   *writeBuffer;
  TextureViewDX12              *sampledView;
  TextureViewDX12              *storageView;
  SamplerDX12                  *sampler;
  SamplerFeedbackMapDX12       *map;
  AccelerationStructureDX12    *structure;
  D3D12_GPU_VIRTUAL_ADDRESS     uniformAddress;
  D3D12_GPU_VIRTUAL_ADDRESS     readAddress;
  D3D12_GPU_VIRTUAL_ADDRESS     writeAddress;
  bool                          tableBinding;
  bool                          readOnly;

  bindContext = context;

  if (!bindContext || !bindContext->valid || !binding) {
    if (bindContext) {
      bindContext->valid = false;
    }

    return;
  }

  switch (binding->bindingType) {
    case GPU_BINDING_UNIFORM_BUFFER: {
      tableBinding = dx12__resourceTableBindingType(bindContext->group->device,
                                                    binding->bindingType,
                                                    binding->arrayCount);
      uniformRoot = !tableBinding
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      uniformBuffer = binding->buffer ? binding->buffer->_priv : NULL;

      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }

      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }

      if (binding->buffer->device != bindContext->device
          || (!tableBinding && !uniformRoot)
          || !uniformBuffer || !uniformBuffer->resource || uniformBuffer->gpuAddress == 0u
          || !dx12_transitionBuffer(bindContext->commandList,
                                    uniformBuffer,
                                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
          || binding->offset > UINT64_MAX - uniformBuffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }

      if (tableBinding) {
        uint32_t uniformOffset;

        if (binding->hasDynamicOffset
            && (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                                 binding,
                                                 false,
                                                 &uniformOffset)
                || !dx12__writeBufferDescriptor(bindContext->group->device,
                                                bindContext->resourceOffset,
                                                bindContext->group->resourceCount,
                                                binding,
                                                uniformOffset))) {
          bindContext->valid = false;
          return;
        }

        break;
      }

      uniformAddress = uniformBuffer->gpuAddress + binding->offset;

      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootConstantBufferView(bindContext->commandList,
                                                                           uniformRoot->rootParameter,
                                                                           uniformAddress);
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootConstantBufferView(bindContext->commandList,
                                                                            uniformRoot->rootParameter,
                                                                            uniformAddress);
      }

      break;
    }
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER: {
      readRoot = binding->arrayCount == 1u
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      readBuffer = binding->buffer ? binding->buffer->_priv : NULL;

      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }

      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }

      if (binding->buffer->device != bindContext->device
          || (binding->arrayCount == 1u && !readRoot)
          || !bufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE)
          || !dx12__transitionReadOnlyStorageBuffer(bindContext->commandList,
                                                    readBuffer)
          || binding->offset > UINT64_MAX - readBuffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }

      if (binding->arrayCount > 1u) {
        uint32_t readOffset;

        if (binding->hasDynamicOffset
            && (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                                 binding,
                                                 false,
                                                 &readOffset)
                || !dx12__writeBufferDescriptor(bindContext->group->device,
                                                bindContext->resourceOffset,
                                                bindContext->group->resourceCount,
                                                binding,
                                                readOffset))) {
          bindContext->valid = false;
          return;
        }

        break;
      }

      readAddress = readBuffer->gpuAddress + binding->offset;

      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootShaderResourceView(bindContext->commandList,
                                                                           readRoot->rootParameter,
                                                                           readAddress);
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootShaderResourceView(bindContext->commandList,
                                                                            readRoot->rootParameter,
                                                                            readAddress);
      }

      break;
    }
    case GPU_BINDING_STORAGE_BUFFER: {
      writeRoot = binding->arrayCount == 1u
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      writeBuffer = binding->buffer ? binding->buffer->_priv : NULL;

      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }

      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }

      if (binding->buffer->device != bindContext->device
          || (binding->arrayCount == 1u && !writeRoot)
          || !bufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE)
          || !dx12__transitionStorageBuffer(bindContext->commandList, writeBuffer)
          || binding->offset > UINT64_MAX - writeBuffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }

      if (binding->arrayCount > 1u) {
        uint32_t writeOffset;

        if (binding->hasDynamicOffset
            && (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                                 binding,
                                                 false,
                                                 &writeOffset)
                || !dx12__writeBufferDescriptor(bindContext->group->device,
                                                bindContext->resourceOffset,
                                                bindContext->group->resourceCount,
                                                binding,
                                                writeOffset))) {
          bindContext->valid = false;
          return;
        }

        break;
      }

      writeAddress = writeBuffer->gpuAddress + binding->offset;

      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootUnorderedAccessView(bindContext->commandList,
                                                                            writeRoot->rootParameter,
                                                                            writeAddress);
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootUnorderedAccessView(bindContext->commandList,
                                                                             writeRoot->rootParameter,
                                                                             writeAddress);
      }

      break;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      if (binding->kind != GPUBindKindTexture) {
        bindContext->valid = false;
        return;
      }

      if (!binding->textureView) {
        bindContext->boundCount++;
        return;
      }

      sampledView = binding->textureView ? binding->textureView->_priv : NULL;

      if (!sampledView || !sampledView->hasSrv
          || !binding->textureView->_texture
          || binding->textureView->_texture->device != bindContext->device
          || !dx12__transitionSampledTexture(bindContext->commandList, sampledView)) {
        bindContext->valid = false;
        return;
      }

      break;
    }
    case GPU_BINDING_STORAGE_TEXTURE: {
      if (binding->kind != GPUBindKindTexture) {
        bindContext->valid = false;
        return;
      }

      if (!binding->textureView) {
        bindContext->boundCount++;
        return;
      }

      storageView = binding->textureView ? binding->textureView->_priv : NULL;
      readOnly    = dx12__storageTextureReadOnly(
        binding->storageTextureAccess
      );

      if (!storageView || (readOnly ? !storageView->hasSrv : !storageView->hasUav)
          || !binding->textureView->_texture
          || binding->textureView->_texture->device != bindContext->device
          || !(readOnly
              ? dx12__transitionSampledTexture(bindContext->commandList, storageView)
              : dx12__transitionStorageTexture(bindContext->commandList,
                                               storageView))) {
        bindContext->valid = false;
        return;
      }

      break;
    }
    case GPU_BINDING_SAMPLER: {
      if (binding->kind != GPUBindKindSampler) {
        bindContext->valid = false;
        return;
      }

      if (!binding->sampler) {
        bindContext->boundCount++;
        return;
      }

      sampler = binding->sampler ? binding->sampler->_priv : NULL;

      if (!sampler
          || sampler->device != bindContext->device->_priv) {
        bindContext->valid = false;
        return;
      }

      break;
    }
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
      map = binding->samplerFeedback ? binding->samplerFeedback->_priv : NULL;

      if (binding->kind != GPUBindKindSamplerFeedback) {
        bindContext->valid = false;
        return;
      }

      if (!binding->samplerFeedback) {
        bindContext->boundCount++;
        return;
      }

      if (binding->samplerFeedback->device != bindContext->device
          || !map || map->device != bindContext->device->_priv
          || !dx12_transitionSamplerFeedback(bindContext->commandList,
                                             map,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
        bindContext->valid = false;
        return;
      }

      break;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      structure = binding->accelerationStructure ? binding->accelerationStructure->_priv : NULL;

      if (binding->kind != GPUBindKindAccelerationStructure) {
        bindContext->valid = false;
        return;
      }

      if (!binding->accelerationStructure) {
        bindContext->boundCount++;
        return;
      }

      if (binding->accelerationStructure->device != bindContext->device
          || !structure || !structure->resource || !structure->address) {
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

static bool
dx12__bindComputeLikeGroup(CommandBufferDX12         *command,
                           ID3D12GraphicsCommandList *commandList,
                           ID3D12RootSignature       *rootSignature,
                           ID3D12DescriptorHeap     **resourceHeap,
                           ID3D12DescriptorHeap     **samplerHeap,
                           GPUBindGroup       *const *boundGroups,
                           uint32_t                  *resourceOffsets,
                           uint32_t                  *resourceOffsetMask,
                           GPUPipelineLayout         *pipelineLayout,
                           uint32_t                   groupIndex,
                           GPUBindGroup              *group,
                           uint32_t                   dynamicOffsetCount,
                           const uint32_t            *dynamicOffsets) {
  DX12BindContext        context;
  PipelineLayoutDX12    *layout;
  BindGroupDX12         *nativeGroup;
  DeviceDX12            *device;
  GPUBindGroupLayout    *groupLayout;
  ID3D12DescriptorHeap  *desiredSamplerHeap;
  uint32_t               resourceOffset;
  uint32_t               expectedCount;
  bool                   valid;

  layout      = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device      = pipelineLayout && pipelineLayout->_device ? pipelineLayout->_device->_priv : NULL;

  if (!commandList || !rootSignature || !resourceHeap || !samplerHeap
      || !resourceOffsets || !resourceOffsetMask
      || !layout || !layout->rootSignature
      || !nativeGroup || nativeGroup->device != device
      || groupIndex >= layout->groupCount
      || !dx12_dynamicOffsetsValid(dx12__dynamicRanges(nativeGroup),
                                   nativeGroup->dynamicOffsetCount,
                                   dynamicOffsetCount,
                                   dynamicOffsets)) {
    return false;
  }

  groupLayout   = bindGroupGetLayout(group);
  expectedCount = dx12__runtimeBindingCount(groupLayout);

  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount
      || nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount
      || !dx12__prepareResourceTable(command,
                                     groupLayout,
                                     nativeGroup,
                                     &resourceOffset)) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    resourceOffsets[groupIndex] = resourceOffset;
    *resourceOffsetMask |= 1u << groupIndex;
  }

  if (layout->samplerTableBaseOnly
      && nativeGroup->samplerCount > 0u) {
    if (!dx12__bindSamplerSnapshot(command,
                                   commandList,
                                   resourceHeap,
                                   samplerHeap,
                                   device,
                                   layout,
                                   boundGroups,
                                   groupIndex,
                                   group,
                                   resourceOffsets,
                                   *resourceOffsetMask,
                                   true)) {
      return false;
    }

  } else {
    desiredSamplerHeap = layout->samplerTableBaseOnly ? *samplerHeap : NULL;

    if (layout->samplerTableBaseOnly
        && layout->samplerDescriptorCount > 0u
        && !desiredSamplerHeap) {
      return false;
    }

    if (!dx12__bindDescriptorHeaps(commandList,
                                   resourceHeap,
                                   samplerHeap,
                                   device,
                                   desiredSamplerHeap,
                                   nativeGroup->resourceCount > 0u,
                                   nativeGroup->samplerCount > 0u)) {
      return false;
    }
  }

  memset(&context, 0, sizeof(context));
  context.commandList    = commandList;
  context.layout         = layout;
  context.group          = nativeGroup;
  context.device         = pipelineLayout->_device;
  context.resourceOffset = resourceOffset;
  context.groupIndex     = groupIndex;
  context.compute        = true;
  context.valid          = true;
  valid                  = forEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                                     groupIndex,
                                                                     group,
                                                                     dynamicOffsetCount,
                                                                     dynamicOffsets,
                                                                     dx12__bindRoot,
                                                                     &context)
                           && context.valid && context.boundCount == expectedCount;

  if (!valid) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                       layout->resourceTables[groupIndex].rootParameter,
                                                       dx12_gpuDescriptor(&device->resourceDescriptors,
                                                                          resourceOffset));
  }

  if (nativeGroup->samplerCount > 0u
      && !layout->samplerTableBaseOnly) {
    commandList->lpVtbl->SetComputeRootDescriptorTable(commandList,
                                                       layout->samplerTables[groupIndex].rootParameter,
                                                       dx12_gpuDescriptor(&device->samplerDescriptors,
                                                                          nativeGroup->samplerOffset));
  }

  return true;
}

GPU_HIDE
GPUResult
dx12_allocateDescriptors(DeviceDX12                *device,
                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                         uint32_t                   count,
                         uint32_t                  *outOffset) {
  DescriptorHeapDX12    *heap;
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
  result = heap ? dx12__ensureDescriptorHeap(device, type, heap)
                : GPU_ERROR_INVALID_ARGUMENT;

  if (result == GPU_OK && count <= heap->capacity) {
    uint32_t maxOffset;
    uint32_t startOffset;

    result      = GPU_ERROR_OUT_OF_MEMORY;
    maxOffset   = heap->capacity - count;
    startOffset = heap->searchOffset <= maxOffset ? heap->searchOffset : 0u;

    for (uint32_t offset = startOffset; offset <= maxOffset; offset++) {
      if (!dx12__descriptorRangeFree(heap, offset, count)) {
        continue;
      }

      dx12__markDescriptorRange(heap, offset, count, true);

      *outOffset         = offset;
      heap->searchOffset = offset + count < heap->capacity ? offset + count : 0u;
      result             = GPU_OK;
      break;
    }

    for (uint32_t wrappedOffset = 0u;
         result != GPU_OK && wrappedOffset < startOffset && wrappedOffset <= maxOffset;
         wrappedOffset++) {
      if (!dx12__descriptorRangeFree(heap, wrappedOffset, count)) {
        continue;
      }

      dx12__markDescriptorRange(heap, wrappedOffset, count, true);

      *outOffset         = wrappedOffset;
      heap->searchOffset = wrappedOffset + count < heap->capacity ? wrappedOffset + count : 0u;
      result             = GPU_OK;
      break;
    }

  } else if (result == GPU_OK) {
    result = GPU_ERROR_OUT_OF_MEMORY;
  }

  ReleaseSRWLockExclusive(&device->descriptorLock);

  return result;
}

GPU_HIDE
void
dx12_freeDescriptors(DeviceDX12                *device,
                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                     uint32_t                   offset,
                     uint32_t                   count) {
  DescriptorHeapDX12    *heap;

  if (!device || count == 0u) {
    return;
  }

  AcquireSRWLockExclusive(&device->descriptorLock);
  heap = dx12__descriptorHeap(device, type);

  if (heap && heap->heap && heap->used
      && offset <= heap->capacity && count <= heap->capacity - offset) {
    dx12__markDescriptorRange(heap, offset, count, false);

    if (offset < heap->searchOffset) {
      heap->searchOffset = offset;
    }
  }

  ReleaseSRWLockExclusive(&device->descriptorLock);
}

GPU_HIDE
GPUResult
dx12_allocateCommandDescriptors(CommandBufferDX12    *command,
                                uint32_t              count,
                                uint32_t             *outOffset) {
  DeviceDX12    *device;
  GPUResult      result;

  device = command && command->owner && command->owner->queue
           && command->owner->queue->_device ? command->owner->queue->_device->_priv : NULL;

  if (!device || !outOffset || count == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = dx12_allocateDescriptors(device,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                    count,
                                    outOffset);

  if (result != GPU_OK) {
    return result;
  }

  if (!dx12__recordCommandDescriptorAllocation(command, *outOffset, count)) {
    dx12_freeDescriptors(device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         *outOffset,
                         count);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  return GPU_OK;
}

GPU_HIDE
void
dx12_resetCommandDescriptors(CommandBufferDX12    *command) {
  DescriptorAllocationChunkDX12    *chunk;
  DeviceDX12                       *device;
  uint32_t                          inlineIndex;
  uint32_t                          chunkIndex;

  device = command && command->owner && command->owner->queue
           && command->owner->queue->_device ? command->owner->queue->_device->_priv : NULL;

  if (!command || !device) {
    return;
  }

  for (inlineIndex = 0u; inlineIndex < command->descriptorAllocationCount; inlineIndex++) {
    dx12_freeDescriptors(device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         command->descriptorAllocations[inlineIndex].offset,
                         command->descriptorAllocations[inlineIndex].count);
  }

  command->descriptorAllocationCount = 0u;

  for (chunk = command->descriptorAllocationChunks; chunk; chunk = chunk->next) {
    for (chunkIndex = 0u; chunkIndex < chunk->count; chunkIndex++) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                           chunk->allocations[chunkIndex].offset,
                           chunk->allocations[chunkIndex].count);
    }

    chunk->count = 0u;
  }
}

GPU_HIDE
void
dx12_destroyCommandDescriptors(CommandBufferDX12    *command) {
  DescriptorAllocationChunkDX12    *chunk;
  GPUDevice                        *device;
  DescriptorAllocationChunkDX12    *next;

  if (!command) {
    return;
  }

  device = command->owner && command->owner->queue ? command->owner->queue->_device : NULL;
  dx12_resetCommandDescriptors(command);
  chunk = command->descriptorAllocationChunks;

  while (chunk) {
    next = chunk->next;
    deviceRecordHotPathFree(device, sizeof(*chunk));
    free(chunk);
    chunk = next;
  }

  command->descriptorAllocationChunks = NULL;
}

GPU_HIDE
void
dx12_resetCommandSamplerHeaps(CommandBufferDX12    *command) {
  if (command) {
    command->samplerHeapUseCount = 0u;
  }
}

GPU_HIDE
void
dx12_destroyCommandSamplerHeaps(CommandBufferDX12    *command) {
  CommandSamplerHeapDX12    *node;
  GPUDevice                 *device;
  CommandSamplerHeapDX12    *next;

  if (!command) {
    return;
  }

  device = command->owner && command->owner->queue ? command->owner->queue->_device : NULL;
  node   = command->samplerHeaps;

  while (node) {
    next = node->next;

    if (node->heap) {
      node->heap->lpVtbl->Release(node->heap);
    }
    deviceRecordHotPathFree(device, sizeof(*node));
    free(node);
    node = next;
  }

  command->samplerHeaps        = NULL;
  command->samplerHeapUseCount = 0u;
}

GPU_HIDE
D3D12_CPU_DESCRIPTOR_HANDLE
dx12_cpuDescriptor(const DescriptorHeapDX12    *heap, uint32_t offset) {
  D3D12_CPU_DESCRIPTOR_HANDLE handle = {0};

  if (heap && heap->heap && offset < heap->capacity) {
    heap->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap->heap,
                                                           &handle);
    handle.ptr += (SIZE_T)offset * heap->descriptorSize;
  }

  return handle;
}

GPU_HIDE
D3D12_GPU_DESCRIPTOR_HANDLE
dx12_gpuDescriptor(const DescriptorHeapDX12    *heap, uint32_t offset) {
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
dx12_destroyDescriptorHeaps(DeviceDX12    *device) {
  DescriptorHeapDX12    *heaps[4];
  uint32_t               i;

  if (!device) {
    return;
  }

  heaps[0] = &device->resourceDescriptors;
  heaps[1] = &device->samplerDescriptors;
  heaps[2] = &device->rtvDescriptors;
  heaps[3] = &device->dsvDescriptors;

  for (i = 0u; i < GPU_ARRAY_LEN(heaps); i++) {
    if (heaps[i]->heap) {
      heaps[i]->heap->lpVtbl->Release(heaps[i]->heap);
    }

    free(heaps[i]->used);
    memset(heaps[i], 0, sizeof(*heaps[i]));
  }
}

GPU_HIDE
GPUResult
dx12_createPipelineLayout(GPUDevice         *device,
                          GPUPipelineLayout *layout) {
  PipelineLayoutDX12        *native;
  GPUBindGroupLayout *const *groups;
  GPUResult                  result;

  native = NULL;
  result = dx12__createPipelineLayout(device,
                                      layout,
                                      NULL,
                                      0u,
                                      0u,
                                      &native);

  if (result == GPU_OK) {
    uint32_t groupCount;

    groups = getPipelineLayoutGroups(layout, &groupCount);
    result = dx12__createNullTables(device,
                                    layout,
                                    groups,
                                    groupCount,
                                    native);

    if (result == GPU_OK) {
      layout->_native = native;
    } else {
      native->rootSignature->lpVtbl->Release(native->rootSignature);
      free(native);
    }
  }

  return result;
}

GPU_HIDE
GPUResult
dx12_createShaderRootSignature(GPUDevice              *device,
                               GPUPipelineLayout      *layout,
                               const GPUShaderLibrary *library,
                               uint64_t                entryMask,
                               ID3D12RootSignature   **outRootSignature,
                               uint64_t                outKey[2]) {
  const ShaderStaticSamplerInfo    *sourceSamplers;
  PipelineLayoutDX12               *base;
  PipelineLayoutDX12               *derived;
  uint32_t                          sourceSamplerCount;
  GPUResult                         result;

  if (!device || !layout || !library || !outRootSignature || !outKey) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outRootSignature = NULL;
  outKey[0]         = 0u;
  outKey[1]         = 0u;

  base = layout->_native;

  if (!base || !base->rootSignature) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  sourceSamplers = getShaderLibraryStaticSamplers(library,
                                                  &sourceSamplerCount);

  if (entryMask == 0u && sourceSamplerCount > 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  {
    uint32_t selectedSamplerCount;

    selectedSamplerCount = 0u;

    for (uint32_t i = 0u; i < sourceSamplerCount; i++) {
      selectedSamplerCount += dx12__sourceSamplerSelected(&sourceSamplers[i],
                                                          entryMask);
    }

    if (selectedSamplerCount == 0u) {
      base->rootSignature->lpVtbl->AddRef(base->rootSignature);

      *outRootSignature = base->rootSignature;
      memcpy(outKey, base->rootSignatureKey, sizeof(base->rootSignatureKey));

      return GPU_OK;
    }
  }

  derived = NULL;
  result  = dx12__createPipelineLayout(device,
                                       layout,
                                       sourceSamplers,
                                       sourceSamplerCount,
                                       entryMask,
                                       &derived);

  if (result != GPU_OK) {
    return result;
  }

  *outRootSignature = derived->rootSignature;
  memcpy(outKey, derived->rootSignatureKey, sizeof(derived->rootSignatureKey));
  derived->rootSignature = NULL;
  free(derived);

  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  DX12BindGroupWriteContext      writeContext;
  GPUBindGroupLayout            *layout;
  const GPUBindGroupLayoutEntry *entries;
  BindGroupDX12                 *native;
  const BindGroupPriv           *priv;
  size_t                         allocationSize;
  GPUResult                      result;
  uint32_t                       entryCount;
  uint32_t                       offsetIndex;
  uint32_t                       entryIndex;

  if (!device || !device->_priv || !group || !(priv = group->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout  = bindGroupGetLayout(group);
  entries = GPUGetBindGroupLayoutEntries(layout, &entryCount);

  if (!layout || (entryCount > 0u && !entries)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (entryCount > (SIZE_MAX - sizeof(*native)) /
                     sizeof(*native->descriptorOffsets)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  allocationSize = sizeof(*native) + entryCount * sizeof(*native->descriptorOffsets);

  if (priv->dynamicOffsetCount >
      (SIZE_MAX - allocationSize) / sizeof(DX12DynamicBufferRange)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  allocationSize += priv->dynamicOffsetCount * sizeof(DX12DynamicBufferRange);

  if (!(native = calloc(1, allocationSize))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->device             = device->_priv;
  native->entryCount         = entryCount;
  native->dynamicOffsetCount = priv->dynamicOffsetCount;

  for (offsetIndex = 0u; offsetIndex < entryCount; offsetIndex++) {
    native->descriptorOffsets[offsetIndex] = UINT32_MAX;
  }

  if (native->dynamicOffsetCount && !dx12__initDynamicRanges(native, priv, entries)) {
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  for (entryIndex = 0u; entryIndex < entryCount; entryIndex++) {
    if (entries[entryIndex].arrayCount == 0u) {
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    if (entries[entryIndex].immutableSampler) {
      continue;
    }

    if (dx12__resourceTableBinding(native->device, &entries[entryIndex])) {
      if (entries[entryIndex].arrayCount > UINT32_MAX - native->resourceCount) {
        free(native);
        return GPU_ERROR_UNSUPPORTED;
      }

      native->descriptorOffsets[entryIndex] = native->resourceCount;
      native->resourceCount += entries[entryIndex].arrayCount;
    } else if (entries[entryIndex].bindingType == GPU_BINDING_SAMPLER) {
      if (entries[entryIndex].arrayCount > UINT32_MAX - native->samplerCount) {
        free(native);
        return GPU_ERROR_UNSUPPORTED;
      }

      native->descriptorOffsets[entryIndex] = native->samplerCount;
      native->samplerCount += entries[entryIndex].arrayCount;
    } else if (!dx12__bufferBindingType(entries[entryIndex].bindingType)) {
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  result = dx12_allocateDescriptors(native->device,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                    native->resourceCount,
                                    &native->resourceOffset);

  if (result != GPU_OK) {
    free(native);
    return result;
  }

  result = dx12_allocateDescriptors(native->device,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                    native->samplerCount,
                                    &native->samplerOffset);

  if (result != GPU_OK) {
    dx12_freeDescriptors(native->device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         native->resourceOffset,
                         native->resourceCount);
    free(native);
    return result;
  }

  memset(&writeContext, 0, sizeof(writeContext));
  writeContext.group = native;
  writeContext.valid = true;

  if (!forEachBindGroupBinding(group,
                               dx12__writeBindGroup,
                               &writeContext)
      || !writeContext.valid) {
    dx12_freeDescriptors(native->device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                         native->samplerOffset,
                         native->samplerCount);
    dx12_freeDescriptors(native->device,
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
bool
dx12_updateBindGroup(GPUBindGroup            *group,
                     uint32_t                 entryCount,
                     const GPUBindGroupEntry *entries) {
  DX12BindGroupWriteContext writeContext = {0};

  writeContext.group = group ? group->_native : NULL;
  writeContext.valid = writeContext.group != NULL;

  return writeContext.valid
         && forEachBindGroupEntry(group,
                                  entryCount,
                                  entries,
                                  dx12__writeBindGroup,
                                  &writeContext)
         && writeContext.valid;
}

GPU_HIDE
void
dx12_destroyBindGroup(GPUBindGroup *group) {
  BindGroupDX12    *native;

  native = group ? group->_native : NULL;

  if (!native) {
    return;
  }

  dx12_freeDescriptors(native->device,
                       D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                       native->samplerOffset,
                       native->samplerCount);
  dx12_freeDescriptors(native->device,
                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                       native->resourceOffset,
                       native->resourceCount);
  free(native);
  group->_native = NULL;
}

GPU_HIDE
bool
dx12_transitionSamplerFeedback(ID3D12GraphicsCommandList *commandList,
                               SamplerFeedbackMapDX12    *map,
                               D3D12_RESOURCE_STATES      state) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!commandList || !map || !map->resource) {
    return false;
  }

  if (map->state == state) {
    return true;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = map->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = map->state;
  barrier.Transition.StateAfter  = state;
  commandList->lpVtbl->ResourceBarrier(commandList, 1u, &barrier);
  map->state = state;

  return true;
}

GPU_HIDE
bool
dx12_bindRenderGroup(GPURenderPassEncoder *pass,
                     GPUPipelineLayout    *pipelineLayout,
                     uint32_t              groupIndex,
                     GPUBindGroup         *group,
                     uint32_t              dynamicOffsetCount,
                     const uint32_t       *dynamicOffsets) {
  DX12BindContext        context;
  RenderEncoderDX12     *encoder;
  PipelineLayoutDX12    *layout;
  BindGroupDX12         *nativeGroup;
  CommandBufferDX12     *command;
  DeviceDX12            *device;
  GPUBindGroupLayout    *groupLayout;
  ID3D12DescriptorHeap  *samplerHeap;
  uint32_t               resourceOffset;
  uint32_t               expectedCount;
  bool                   valid;

  encoder     = pass ? pass->_priv : NULL;
  command     = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout      = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device      = pipelineLayout && pipelineLayout->_device ? pipelineLayout->_device->_priv : NULL;

  if (!encoder || !encoder->commandList || !encoder->pipeline
      || !layout || !layout->rootSignature
      || pass->_pipelineLayout != pipelineLayout
      || encoder->rootSignature != encoder->pipeline->rootSignature
      || !nativeGroup || nativeGroup->device != device
      || groupIndex >= layout->groupCount
      || !dx12_dynamicOffsetsValid(dx12__dynamicRanges(nativeGroup),
                                   nativeGroup->dynamicOffsetCount,
                                   dynamicOffsetCount,
                                   dynamicOffsets)) {
    return false;
  }

  groupLayout   = bindGroupGetLayout(group);
  expectedCount = dx12__runtimeBindingCount(groupLayout);

  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount
      || nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount
      || !dx12__prepareResourceTable(command,
                                     groupLayout,
                                     nativeGroup,
                                     &resourceOffset)) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    encoder->resourceOffsets[groupIndex] = resourceOffset;
    encoder->resourceOffsetMask |= 1u << groupIndex;
  }

  if (layout->samplerTableBaseOnly
      && nativeGroup->samplerCount > 0u) {
    if (!dx12__bindSamplerSnapshot(command,
                                   encoder->commandList,
                                   &encoder->resourceHeap,
                                   &encoder->samplerHeap,
                                   device,
                                   layout,
                                   pass->_boundGroups,
                                   groupIndex,
                                   group,
                                   encoder->resourceOffsets,
                                   encoder->resourceOffsetMask,
                                   false)) {
      return false;
    }

  } else {
    samplerHeap = layout->samplerTableBaseOnly ? encoder->samplerHeap : NULL;

    if (layout->samplerTableBaseOnly
        && layout->samplerDescriptorCount > 0u
        && !samplerHeap) {
      return false;
    }

    if (!dx12__bindDescriptorHeaps(encoder->commandList,
                                   &encoder->resourceHeap,
                                   &encoder->samplerHeap,
                                   device,
                                   samplerHeap,
                                   nativeGroup->resourceCount > 0u,
                                   nativeGroup->samplerCount > 0u)) {
      return false;
    }
  }

  memset(&context, 0, sizeof(context));
  context.commandList    = encoder->commandList;
  context.layout         = layout;
  context.group          = nativeGroup;
  context.device         = pipelineLayout->_device;
  context.resourceOffset = resourceOffset;
  context.groupIndex     = groupIndex;
  context.valid          = true;
  valid                  = forEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                                     groupIndex,
                                                                     group,
                                                                     dynamicOffsetCount,
                                                                     dynamicOffsets,
                                                                     dx12__bindRoot,
                                                                     &context)
                           && context.valid && context.boundCount == expectedCount;

  if (!valid) {
    return false;
  }

  if (nativeGroup->resourceCount > 0u) {
    encoder->commandList->lpVtbl->SetGraphicsRootDescriptorTable(encoder->commandList,
                                                                 layout->resourceTables[groupIndex].rootParameter,
                                                                 dx12_gpuDescriptor(&device->resourceDescriptors,
                                                                                    resourceOffset));
  }

  if (nativeGroup->samplerCount > 0u
      && !layout->samplerTableBaseOnly) {
    encoder->commandList->lpVtbl->SetGraphicsRootDescriptorTable(encoder->commandList,
                                                                 layout->samplerTables[groupIndex].rootParameter,
                                                                 dx12_gpuDescriptor(&device->samplerDescriptors,
                                                                                    nativeGroup->samplerOffset));
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
  ComputeEncoderDX12    *encoder;
  CommandBufferDX12     *command;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;

  return encoder && pass->_pipelineLayout == pipelineLayout
         && dx12__bindComputeLikeGroup(command,
                                       encoder->commandList,
                                       encoder->rootSignature,
                                       &encoder->resourceHeap,
                                       &encoder->samplerHeap,
                                       pass->_boundGroups,
                                       encoder->resourceOffsets,
                                       &encoder->resourceOffsetMask,
                                       pipelineLayout,
                                       groupIndex,
                                       group,
                                       dynamicOffsetCount,
                                       dynamicOffsets);
}

GPU_HIDE
bool
dx12_bindRayTracingGroup(GPURayTracingPassEncoderEXT *pass,
                         GPUPipelineLayout           *pipelineLayout,
                         uint32_t                     groupIndex,
                         GPUBindGroup                *group,
                         uint32_t                     dynamicOffsetCount,
                         const uint32_t              *dynamicOffsets) {
  RayTracingEncoderDX12    *encoder;
  CommandBufferDX12        *command;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->cmdb ? pass->cmdb->_priv : NULL;

  return encoder && pass->pipelineLayout == pipelineLayout
         && dx12__bindComputeLikeGroup(command,
                                       encoder->commandList,
                                       encoder->rootSignature,
                                       &encoder->resourceHeap,
                                       &encoder->samplerHeap,
                                       pass->boundGroups,
                                       encoder->resourceOffsets,
                                       &encoder->resourceOffsetMask,
                                       pipelineLayout,
                                       groupIndex,
                                       group,
                                       dynamicOffsetCount,
                                       dynamicOffsets);
}

GPU_HIDE
void
dx12_rebindRenderGroups(GPURenderPassEncoder *pass) {
  RenderEncoderDX12     *encoder;
  PipelineLayoutDX12    *layout;
  CommandBufferDX12     *command;
  DeviceDX12            *device;
  GPUBindGroup          *group;
  uint32_t               i;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout  = pass && pass->_pipelineLayout ? pass->_pipelineLayout->_native : NULL;
  device  = pass && pass->_pipelineLayout
            && pass->_pipelineLayout->_device ? pass->_pipelineLayout->_device->_priv : NULL;

  if (!encoder || !encoder->commandList || !layout || !device) {
    return;
  }

  if (!dx12__bindNullTables(command,
                            encoder->commandList,
                            &encoder->resourceHeap,
                            &encoder->samplerHeap,
                            device,
                            layout,
                            encoder->resourceOffsets,
                            &encoder->resourceOffsetMask,
                            false)) {
    return;
  }

  for (i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    group = pass->_boundGroups[i];

    if (!group) {
      continue;
    }

    if (dx12_bindRenderGroup(pass,
                             pass->_pipelineLayout,
                             i,
                             group,
                             pass->_boundDynamicOffsetCounts[i],
                             pass->_boundDynamicOffsets[i])) {
      frameStatsRecordBindEmission(pass->_stats);
      continue;
    }

    pass->_boundGroups[i]              = NULL;
    pass->_boundGroupLayouts[i]        = NULL;
    pass->_boundDynamicOffsetCounts[i] = 0u;
  }
}

GPU_HIDE
void
dx12_rebindComputeGroups(GPUComputePassEncoder *pass) {
  ComputeEncoderDX12    *encoder;
  CommandBufferDX12     *command;
  PipelineLayoutDX12    *layout;
  DeviceDX12            *device;
  GPUBindGroup          *group;
  uint32_t               i;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout  = pass && pass->_pipelineLayout ? pass->_pipelineLayout->_native : NULL;
  device  = pass && pass->_pipelineLayout
            && pass->_pipelineLayout->_device ? pass->_pipelineLayout->_device->_priv : NULL;

  if (!encoder || !encoder->commandList || !layout || !device) {
    return;
  }

  if (!dx12__bindNullTables(command,
                            encoder->commandList,
                            &encoder->resourceHeap,
                            &encoder->samplerHeap,
                            device,
                            layout,
                            encoder->resourceOffsets,
                            &encoder->resourceOffsetMask,
                            true)) {
    return;
  }

  for (i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    group = pass->_boundGroups[i];

    if (!group) {
      continue;
    }

    if (dx12_bindComputeGroup(pass,
                              pass->_pipelineLayout,
                              i,
                              group,
                              pass->_boundDynamicOffsetCounts[i],
                              pass->_boundDynamicOffsets[i])) {
      frameStatsRecordBindEmission(pass->_stats);
      continue;
    }

    pass->_boundGroups[i]              = NULL;
    pass->_boundGroupLayouts[i]        = NULL;
    pass->_boundDynamicOffsetCounts[i] = 0u;
  }
}

GPU_HIDE
void
dx12_rebindRayGroups(GPURayTracingPassEncoderEXT *pass) {
  RayTracingEncoderDX12    *encoder;
  CommandBufferDX12        *command;
  PipelineLayoutDX12       *layout;
  DeviceDX12               *device;
  GPUBindGroup             *group;
  uint32_t                  i;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->cmdb ? pass->cmdb->_priv : NULL;
  layout  = pass && pass->pipelineLayout ? pass->pipelineLayout->_native : NULL;
  device  = pass && pass->pipelineLayout && pass->pipelineLayout->_device ? pass->pipelineLayout->_device->_priv : NULL;

  if (!encoder || !encoder->commandList || !layout || !device) {
    return;
  }

  if (!dx12__bindNullTables(command,
                            encoder->commandList,
                            &encoder->resourceHeap,
                            &encoder->samplerHeap,
                            device,
                            layout,
                            encoder->resourceOffsets,
                            &encoder->resourceOffsetMask,
                            true)) {
    return;
  }

  for (i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    group = pass->boundGroups[i];

    if (!group) {
      continue;
    }

    if (dx12_bindRayTracingGroup(pass,
                                 pass->pipelineLayout,
                                 i,
                                 group,
                                 pass->boundDynamicOffsetCounts[i],
                                 pass->boundDynamicOffsets[i])) {
      frameStatsRecordBindEmission(pass->stats);
      continue;
    }

    pass->boundGroups[i]              = NULL;
    pass->boundGroupLayouts[i]        = NULL;
    pass->boundDynamicOffsetCounts[i] = 0u;
  }
}

GPU_HIDE
void
dx12_destroyPipelineLayout(GPUPipelineLayout *layout) {
  PipelineLayoutDX12    *native;
  DeviceDX12            *device;

  native = layout ? layout->_native : NULL;

  if (!native) {
    return;
  }

  device = layout->_device ? layout->_device->_priv : NULL;
  dx12__destroyNullTables(device, native);

  if (native->rootSignature) {
    native->rootSignature->lpVtbl->Release(native->rootSignature);
  }
  free(native);
  layout->_native = NULL;
}

GPU_HIDE
void
dx12_initDescriptor(ApiDescriptor    *api) {
  memset(api, 0, sizeof(*api));
  api->createPipelineLayout  = dx12_createPipelineLayout;
  api->destroyPipelineLayout = dx12_destroyPipelineLayout;
  api->createBindGroup       = dx12_createBindGroup;
  api->updateBindGroup       = dx12_updateBindGroup;
  api->destroyBindGroup      = dx12_destroyBindGroup;
  api->bindRenderGroup       = dx12_bindRenderGroup;
  api->bindComputeGroup      = dx12_bindComputeGroup;
}
