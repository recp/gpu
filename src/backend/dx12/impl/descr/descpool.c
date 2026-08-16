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
  DX12_ROOT_SIGNATURE_DWORD_LIMIT       = 64u,
  DX12_RESOURCE_DESCRIPTOR_CAPACITY     = 65536u,
  DX12_SAMPLER_DESCRIPTOR_CAPACITY      = 2048u
};

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
dx12__sourceSamplerSelected(const GPUShaderStaticSamplerInfo *sampler,
                            uint64_t                          entryMask) {
  return sampler &&
         sampler->hlslIndex != UINT32_MAX &&
         (sampler->entryMask & entryMask) != 0u;
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
  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
    return &device->rtvDescriptors;
  }
  if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
    return &device->dsvDescriptors;
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
  desc.Flags          = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
                        type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                          ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                          : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
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

GPU_HIDE
GPUResult
dx12_allocateDescriptors(GPUDeviceDX12             *device,
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
  result = heap ? dx12__ensureDescriptorHeap(device, type, heap)
                : GPU_ERROR_INVALID_ARGUMENT;
  if (result == GPU_OK && count <= heap->capacity) {
    uint32_t maxOffset;
    uint32_t startOffset;

    result = GPU_ERROR_OUT_OF_MEMORY;
    maxOffset   = heap->capacity - count;
    startOffset = heap->searchOffset <= maxOffset ? heap->searchOffset : 0u;
    for (uint32_t offset = startOffset; offset <= maxOffset; offset++) {
      if (!dx12__descriptorRangeFree(heap, offset, count)) {
        continue;
      }

      dx12__markDescriptorRange(heap, offset, count, true);
      *outOffset = offset;
      heap->searchOffset = offset + count < heap->capacity
                             ? offset + count
                             : 0u;
      result = GPU_OK;
      break;
    }
    for (uint32_t offset = 0u;
         result != GPU_OK && offset < startOffset && offset <= maxOffset;
         offset++) {
      if (!dx12__descriptorRangeFree(heap, offset, count)) {
        continue;
      }

      dx12__markDescriptorRange(heap, offset, count, true);
      *outOffset = offset;
      heap->searchOffset = offset + count < heap->capacity
                             ? offset + count
                             : 0u;
      result = GPU_OK;
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
dx12_freeDescriptors(GPUDeviceDX12             *device,
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
    if (offset < heap->searchOffset) {
      heap->searchOffset = offset;
    }
  }
  ReleaseSRWLockExclusive(&device->descriptorLock);
}

static bool
dx12__recordCommandDescriptorAllocation(GPUCommandBufferDX12 *command,
                                        uint32_t                offset,
                                        uint32_t                count) {
  GPUDescriptorAllocationChunkDX12 *chunk;
  GPUDevice                        *device;

  if (!command || count == 0u) {
    return false;
  }

  if (command->descriptorAllocationCount <
      GPU_DX12_INLINE_DESCRIPTOR_ALLOCATION_COUNT) {
    GPUDescriptorAllocationDX12 *allocation;

    allocation = &command->descriptorAllocations[
      command->descriptorAllocationCount++
    ];
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
    GPUDescriptorAllocationChunkDX12 *tail;

    chunk = calloc(1, sizeof(*chunk));
    if (!chunk) {
      return false;
    }
    device = command->owner && command->owner->queue
               ? command->owner->queue->_device
               : NULL;
    gpuDeviceRecordHotPathAlloc(device, sizeof(*chunk));
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

GPU_HIDE
GPUResult
dx12_allocateCommandDescriptors(GPUCommandBufferDX12 *command,
                                uint32_t                count,
                                uint32_t               *outOffset) {
  GPUDeviceDX12 *device;
  GPUResult      result;

  device = command && command->owner && command->owner->queue &&
           command->owner->queue->_device
             ? command->owner->queue->_device->_priv
             : NULL;
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
dx12_resetCommandDescriptors(GPUCommandBufferDX12 *command) {
  GPUDescriptorAllocationChunkDX12 *chunk;
  GPUDeviceDX12                    *device;

  device = command && command->owner && command->owner->queue &&
           command->owner->queue->_device
             ? command->owner->queue->_device->_priv
             : NULL;
  if (!command || !device) {
    return;
  }

  for (uint32_t i = 0u; i < command->descriptorAllocationCount; i++) {
    dx12_freeDescriptors(device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         command->descriptorAllocations[i].offset,
                         command->descriptorAllocations[i].count);
  }
  command->descriptorAllocationCount = 0u;

  for (chunk = command->descriptorAllocationChunks; chunk; chunk = chunk->next) {
    for (uint32_t i = 0u; i < chunk->count; i++) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                           chunk->allocations[i].offset,
                           chunk->allocations[i].count);
    }
    chunk->count = 0u;
  }
}

GPU_HIDE
void
dx12_destroyCommandDescriptors(GPUCommandBufferDX12 *command) {
  GPUDescriptorAllocationChunkDX12 *chunk;
  GPUDevice                        *device;

  if (!command) {
    return;
  }
  device = command->owner && command->owner->queue
             ? command->owner->queue->_device
             : NULL;
  dx12_resetCommandDescriptors(command);
  chunk = command->descriptorAllocationChunks;
  while (chunk) {
    GPUDescriptorAllocationChunkDX12 *next;

    next = chunk->next;
    gpuDeviceRecordHotPathFree(device, sizeof(*chunk));
    free(chunk);
    chunk = next;
  }
  command->descriptorAllocationChunks = NULL;
}

enum {
  GPU_DX12_COMMAND_SAMPLER_HEAP_MIN = 16u,
  GPU_DX12_COMMAND_SAMPLER_HEAP_MAX = 2048u
};

static GPUCommandSamplerHeapDX12 *
dx12__takeCommandSamplerHeap(GPUCommandBufferDX12 *command,
                             uint32_t                requiredCount) {
  GPUCommandSamplerHeapDX12 **link;
  GPUCommandSamplerHeapDX12  *node;
  GPUDevice                  *device;
  GPUDeviceDX12              *deviceDX12;
  D3D12_DESCRIPTOR_HEAP_DESC  desc = {0};
  uint32_t                    capacity;
  uint32_t                    slot;
  HRESULT                     result;

  device = command && command->owner && command->owner->queue
             ? command->owner->queue->_device
             : NULL;
  deviceDX12 = device ? device->_priv : NULL;
  if (!command || !deviceDX12 || requiredCount == 0u ||
      requiredCount > GPU_DX12_COMMAND_SAMPLER_HEAP_MAX) {
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
    node = calloc(1, sizeof(*node));
    if (!node) {
      return NULL;
    }
    gpuDeviceRecordHotPathAlloc(device, sizeof(*node));
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
    result = deviceDX12->d3dDevice->lpVtbl->CreateDescriptorHeap(
      deviceDX12->d3dDevice,
      &desc,
      &IID_ID3D12DescriptorHeap,
      (void **)&node->heap
    );
    if (FAILED(result) || !node->heap) {
      return NULL;
    }
    node->capacity = capacity;
  }

  command->samplerHeapUseCount++;
  return node;
}

GPU_HIDE
void
dx12_resetCommandSamplerHeaps(GPUCommandBufferDX12 *command) {
  if (command) {
    command->samplerHeapUseCount = 0u;
  }
}

GPU_HIDE
void
dx12_destroyCommandSamplerHeaps(GPUCommandBufferDX12 *command) {
  GPUCommandSamplerHeapDX12 *node;
  GPUDevice                 *device;

  if (!command) {
    return;
  }
  device = command->owner && command->owner->queue
             ? command->owner->queue->_device
             : NULL;
  node = command->samplerHeaps;
  while (node) {
    GPUCommandSamplerHeapDX12 *next;

    next = node->next;
    if (node->heap) {
      node->heap->lpVtbl->Release(node->heap);
    }
    gpuDeviceRecordHotPathFree(device, sizeof(*node));
    free(node);
    node = next;
  }
  command->samplerHeaps        = NULL;
  command->samplerHeapUseCount = 0u;
}

GPU_HIDE
D3D12_CPU_DESCRIPTOR_HANDLE
dx12_cpuDescriptor(const GPUDescriptorHeapDX12 *heap, uint32_t offset) {
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
dx12_gpuDescriptor(const GPUDescriptorHeapDX12 *heap, uint32_t offset) {
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
  GPUDescriptorHeapDX12 *heaps[4];

  if (!device) {
    return;
  }

  heaps[0] = &device->resourceDescriptors;
  heaps[1] = &device->samplerDescriptors;
  heaps[2] = &device->rtvDescriptors;
  heaps[3] = &device->dsvDescriptors;
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
  return type == GPU_BINDING_UNIFORM_BUFFER ||
         type == GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
         type == GPU_BINDING_STORAGE_BUFFER;
}

static bool
dx12__resourceTableBindingType(const GPUDeviceDX12 *device,
                               GPUBindingType       type,
                               uint32_t             arrayCount) {
  if (type == GPU_BINDING_UNIFORM_BUFFER &&
      device && !device->rootCbvSpacesReliable) {
    return true;
  }
  if (dx12__bufferBindingType(type)) {
    return arrayCount > 1u;
  }
  return type == GPU_BINDING_SAMPLED_TEXTURE ||
         type == GPU_BINDING_STORAGE_TEXTURE ||
         type == GPU_BINDING_SAMPLER_FEEDBACK_EXT ||
         type == GPU_BINDING_ACCELERATION_STRUCTURE;
}

static bool
dx12__resourceTableBinding(const GPUDeviceDX12          *device,
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

typedef struct DX12LayoutPlan {
  uint32_t bindingCount;
  uint32_t rangeCount;
  uint32_t rootParameterCount;
  uint32_t rootDwordCount;
  uint32_t staticSamplerCount;
} DX12LayoutPlan;

static GPUResult
dx12__makeLayoutPlan(GPUDeviceDX12             *device,
                     GPUPipelineLayout         *layout,
                     GPUBindGroupLayout * const *groups,
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
    backendBindings = gpuGetPipelineLayoutBackendBindings(
      layout,
      groupIndex,
      &backendBindingCount
    );
    if (entryCount != backendBindingCount ||
        (entryCount > 0u && (!entries || !backendBindings))) {
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
        if (entries[i].bindingType != GPU_BINDING_SAMPLER ||
            entries[i].hasDynamicOffset) {
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
            if (plan.bindingCount == UINT32_MAX ||
                plan.rootParameterCount == UINT32_MAX ||
                plan.rootDwordCount > UINT32_MAX - 2u) {
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
dx12__fillLayoutPlan(GPUDeviceDX12             *device,
                     GPUPipelineLayout         *layout,
                     GPUBindGroupLayout * const *groups,
                     uint32_t                    groupCount,
                     GPUPipelineLayoutDX12      *native) {
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
    GPUDescriptorTableDX12        *resourceTable;
    GPUDescriptorTableDX12        *samplerTable;
    uint32_t                       entryCount;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetPipelineLayoutBackendBindings(layout,
                                                          groupIndex,
                                                          NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceTable->rootParameter = UINT32_MAX;
    samplerTable->rootParameter  = UINT32_MAX;
    resourceTable->nullOffset    = UINT32_MAX;
    samplerTable->nullOffset     = UINT32_MAX;
    native->groupOffsets[groupIndex] = bindingCursor;

    for (uint32_t i = 0u; i < entryCount; i++) {
      if (dx12__bufferBindingType(entries[i].bindingType) &&
          !dx12__resourceTableBinding(device, &entries[i])) {
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
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER &&
                 !entries[i].immutableSampler) {
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
      samplerTable->rootParameter = rootCursor++;
      samplerTable->rangeOffset   = rangeCursor;
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
                         GPUTextureViewType                viewType,
                         bool                              multisampled) {
  if (!desc) {
    return false;
  }

  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      if (multisampled) {
        return false;
      }
      desc->ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE1D;
      desc->Texture1D.MipLevels       = 1u;
      break;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      if (multisampled) {
        return false;
      }
      desc->ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      desc->Texture1DArray.MipLevels  = 1u;
      desc->Texture1DArray.ArraySize  = 1u;
      break;
    case GPU_TEXTURE_VIEW_2D:
      desc->ViewDimension = multisampled
                              ? D3D12_SRV_DIMENSION_TEXTURE2DMS
                              : D3D12_SRV_DIMENSION_TEXTURE2D;
      if (!multisampled) {
        desc->Texture2D.MipLevels = 1u;
      }
      break;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      desc->ViewDimension = multisampled
                              ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY
                              : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
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
      desc->ViewDimension           = D3D12_SRV_DIMENSION_TEXTURECUBE;
      desc->TextureCube.MipLevels   = 1u;
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
      desc->ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE3D;
      desc->Texture3D.MipLevels     = 1u;
      break;
    default:
      return false;
  }

  return true;
}

static bool
dx12__fillNullUavTexture(D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
                         GPUTextureViewType                 viewType) {
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
      desc->ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE3D;
      desc->Texture3D.WSize    = 1u;
      break;
    default:
      return false;
  }

  return true;
}

static bool
dx12__writeNullResourceDescriptor(
  GPUDeviceDX12                 *device,
  const GPUBindGroupLayoutEntry *entry,
  D3D12_CPU_DESCRIPTOR_HANDLE    handle) {
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
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};

      desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.Format              = entry->buffer.strideBytes == 0u
                                   ? DXGI_FORMAT_R32_TYPELESS
                                   : DXGI_FORMAT_UNKNOWN;
      desc.ViewDimension       = D3D12_SRV_DIMENSION_BUFFER;
      desc.Buffer.NumElements  = 1u;
      desc.Buffer.StructureByteStride = entry->buffer.strideBytes;
      desc.Buffer.Flags        = entry->buffer.strideBytes == 0u
                                   ? D3D12_BUFFER_SRV_FLAG_RAW
                                   : D3D12_BUFFER_SRV_FLAG_NONE;
      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                           NULL,
                                                           &desc,
                                                           handle);
      return true;
    }
    case GPU_BINDING_STORAGE_BUFFER: {
      D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};

      desc.Format              = entry->buffer.strideBytes == 0u
                                   ? DXGI_FORMAT_R32_TYPELESS
                                   : DXGI_FORMAT_UNKNOWN;
      desc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
      desc.Buffer.NumElements  = 1u;
      desc.Buffer.StructureByteStride = entry->buffer.strideBytes;
      desc.Buffer.Flags        = entry->buffer.strideBytes == 0u
                                   ? D3D12_BUFFER_UAV_FLAG_RAW
                                   : D3D12_BUFFER_UAV_FLAG_NONE;
      device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                            NULL,
                                                            NULL,
                                                            &desc,
                                                            handle);
      return true;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};

      desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.Format = dx12__nullSampledFormat(entry->sampledTexture.sampleType);
      if (!dx12__fillNullSrvTexture(&desc,
                                    entry->sampledTexture.viewType,
                                    entry->sampledTexture.multisampled)) {
        return false;
      }
      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                           NULL,
                                                           &desc,
                                                           handle);
      return true;
    }
    case GPU_BINDING_STORAGE_TEXTURE:
      if (dx12__storageTextureReadOnly(entry->storageTexture.access)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};

        desc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Format = dx12_format(entry->storageTexture.format);
        if (desc.Format == DXGI_FORMAT_UNKNOWN ||
            !dx12__fillNullSrvTexture(&desc,
                                      entry->storageTexture.viewType,
                                      false)) {
          return false;
        }
        device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                             NULL,
                                                             &desc,
                                                             handle);
      } else {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};

        desc.Format = dx12_format(entry->storageTexture.format);
        if (desc.Format == DXGI_FORMAT_UNKNOWN ||
            !dx12__fillNullUavTexture(&desc,
                                      entry->storageTexture.viewType)) {
          return false;
        }
        device->d3dDevice->lpVtbl->CreateUnorderedAccessView(
          device->d3dDevice,
          NULL,
          NULL,
          &desc,
          handle
        );
      }
      return true;
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
      D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};

      desc.Format        = DXGI_FORMAT_R8_UINT;
      desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      device->d3dDevice->lpVtbl->CreateUnorderedAccessView(device->d3dDevice,
                                                            NULL,
                                                            NULL,
                                                            &desc,
                                                            handle);
      return true;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};

      desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.ViewDimension =
        D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
      device->d3dDevice->lpVtbl->CreateShaderResourceView(device->d3dDevice,
                                                           NULL,
                                                           &desc,
                                                           handle);
      return true;
    }
    default:
      return false;
  }
}

static void
dx12__writeNullSamplerDescriptor(
  GPUDeviceDX12                 *device,
  const GPUBindGroupLayoutEntry *entry,
  D3D12_CPU_DESCRIPTOR_HANDLE    handle) {
  D3D12_SAMPLER_DESC desc = {0};

  desc.Filter = entry->sampler.type == GPU_SAMPLER_BINDING_COMPARISON
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
  desc.MaxLOD = D3D12_FLOAT32_MAX;
  device->d3dDevice->lpVtbl->CreateSampler(device->d3dDevice, &desc, handle);
}

static void
dx12__destroyNullTables(GPUDeviceDX12         *device,
                        GPUPipelineLayoutDX12 *native) {
  if (!device || !native) {
    return;
  }

  for (uint32_t i = 0u; i < native->groupCount; i++) {
    GPUDescriptorTableDX12 *resourceTable;
    GPUDescriptorTableDX12 *samplerTable;

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
dx12__createNullTables(GPUDevice                        *device,
                       GPUPipelineLayout                *layout,
                       GPUBindGroupLayout * const       *groups,
                       uint32_t                          groupCount,
                       GPUPipelineLayoutDX12            *native) {
  GPUDeviceDX12 *deviceDX12;
  GPUResult      result;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12 || !layout || !native || groupCount != native->groupCount ||
      (groupCount > 0u && !groups)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t groupIndex = 0u; groupIndex < groupCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    GPUDescriptorTableDX12        *resourceTable;
    GPUDescriptorTableDX12        *samplerTable;
    uint32_t                       entryCount;
    uint32_t                       resourceCursor;
    uint32_t                       samplerCursor;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
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
    for (uint32_t i = 0u; i < entryCount; i++) {
      if (dx12__resourceTableBinding(deviceDX12, &entries[i])) {
        for (uint32_t j = 0u; j < entries[i].arrayCount; j++) {
          D3D12_CPU_DESCRIPTOR_HANDLE handle;

          handle = dx12_cpuDescriptor(&deviceDX12->resourceDescriptors,
                                      resourceTable->nullOffset +
                                        resourceCursor++);
          if (!dx12__writeNullResourceDescriptor(deviceDX12,
                                                  &entries[i],
                                                  handle)) {
            result = GPU_ERROR_BACKEND_FAILURE;
            goto fail;
          }
        }
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER &&
                 !entries[i].immutableSampler) {
        for (uint32_t j = 0u; j < entries[i].arrayCount; j++) {
          D3D12_CPU_DESCRIPTOR_HANDLE handle;

          handle = dx12_cpuDescriptor(&deviceDX12->samplerDescriptors,
                                      samplerTable->nullOffset +
                                        samplerCursor++);
          dx12__writeNullSamplerDescriptor(deviceDX12, &entries[i], handle);
        }
      }
    }

    if (resourceCursor != resourceTable->descriptorCount ||
        samplerCursor != samplerTable->descriptorCount) {
      result = GPU_ERROR_BACKEND_FAILURE;
      goto fail;
    }
  }

  return GPU_OK;

fail:
  dx12__destroyNullTables(deviceDX12, native);
  return result;
}

static const GPURootBindingDX12 *
dx12__findRootBinding(const GPUPipelineLayoutDX12 *layout,
                      uint32_t                     groupIndex,
                      uint32_t                     binding,
                      GPUBindingType               bindingType) {
  uint32_t begin;
  uint32_t end;

  if (!layout || groupIndex >= layout->groupCount) {
    return NULL;
  }

  begin = layout->groupOffsets[groupIndex];
  end   = layout->groupOffsets[groupIndex + 1u];
  for (uint32_t i = begin; i < end; i++) {
    if (layout->bindings[i].binding == binding &&
        layout->bindings[i].bindingType == bindingType) {
      return &layout->bindings[i];
    }
  }

  return NULL;
}

static bool
dx12__fillStaticSamplers(GPUPipelineLayout                 *layout,
                         GPUBindGroupLayout * const        *groups,
                         uint32_t                          groupCount,
                         const GPUShaderStaticSamplerInfo *sourceSamplers,
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

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetPipelineLayoutBackendBindings(layout,
                                                          groupIndex,
                                                          NULL);
    for (uint32_t i = 0u; i < entryCount; i++) {
      if (!entries[i].immutableSampler) {
        continue;
      }
      for (uint32_t arrayIndex = 0u;
           arrayIndex < entries[i].arrayCount;
           arrayIndex++) {
        if (cursor >= samplerCount ||
            !dx12_fillStaticSamplerDesc(&entries[i].immutableSamplerDesc,
                                        backendBindings[i] + arrayIndex,
                                        groupIndex,
                                        dx12__shaderVisibility(
                                          entries[i].visibility
                                        ),
                                        &samplers[cursor])) {
          return false;
        }
        cursor++;
      }
    }
  }

  for (uint32_t i = 0u; i < sourceSamplerCount; i++) {
    if (!dx12__sourceSamplerSelected(&sourceSamplers[i], entryMask)) {
      continue;
    }
    if (cursor >= samplerCount ||
        !dx12_fillSourceSamplerDesc(
          &sourceSamplers[i].desc,
          sourceSamplers[i].hlslIndex,
          dx12__shaderVisibility(sourceSamplers[i].visibility),
          &samplers[cursor])) {
      return false;
    }
    cursor++;
  }

  return cursor == samplerCount;
}

static void
dx12__fillRanges11(const GPUDeviceDX12        *device,
                   GPUPipelineLayout          *layout,
                   GPUBindGroupLayout * const *groups,
                   const GPUPipelineLayoutDX12 *native,
                   D3D12_ROOT_PARAMETER1       *parameters,
                   D3D12_DESCRIPTOR_RANGE1     *ranges) {
  for (uint32_t i = 0u; i < native->bindingCount; i++) {
    parameters[native->bindings[i].rootParameter].ParameterType =
      dx12__rootBufferType(native->bindings[i].bindingType);
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
    uint32_t                       resourceOffset;
    uint32_t                       resourceRange;
    uint32_t                       samplerOffset;
    uint32_t                       samplerRange;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetPipelineLayoutBackendBindings(layout,
                                                          groupIndex,
                                                          NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceOffset = 0u;
    resourceRange  = 0u;
    samplerOffset  = native->samplerTableBaseOnly
                       ? samplerTable->descriptorOffset
                       : 0u;
    samplerRange   = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      D3D12_DESCRIPTOR_RANGE1 *range;
      uint32_t                 tableOffset;

      if (dx12__resourceTableBinding(device, &entries[i])) {
        tableOffset = resourceOffset;
        range = &ranges[resourceTable->rangeOffset + resourceRange++];
        resourceOffset += entries[i].arrayCount;
        range->RangeType = dx12__resourceRangeType(&entries[i]);
        range->Flags =
          range->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV
            ? D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE
            : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER &&
                 !entries[i].immutableSampler) {
        tableOffset = samplerOffset;
        range = &ranges[samplerTable->rangeOffset + samplerRange++];
        samplerOffset += entries[i].arrayCount;
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        range->Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
      } else {
        continue;
      }

      range->NumDescriptors                  = entries[i].arrayCount;
      range->BaseShaderRegister              = backendBindings[i];
      range->RegisterSpace                   = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges =
          resourceTable->rangeCount;
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
        .DescriptorTable.NumDescriptorRanges = samplerTable->rangeCount;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(samplerTable->visibility);
    }
  }
}

static void
dx12__fillRanges10(const GPUDeviceDX12        *device,
                   GPUPipelineLayout          *layout,
                   GPUBindGroupLayout * const *groups,
                   const GPUPipelineLayoutDX12 *native,
                   D3D12_ROOT_PARAMETER        *parameters,
                   D3D12_DESCRIPTOR_RANGE      *ranges) {
  for (uint32_t i = 0u; i < native->bindingCount; i++) {
    parameters[native->bindings[i].rootParameter].ParameterType =
      dx12__rootBufferType(native->bindings[i].bindingType);
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
    uint32_t                       resourceOffset;
    uint32_t                       resourceRange;
    uint32_t                       samplerOffset;
    uint32_t                       samplerRange;

    entries = GPUGetBindGroupLayoutEntries(groups[groupIndex], &entryCount);
    backendBindings = gpuGetPipelineLayoutBackendBindings(layout,
                                                          groupIndex,
                                                          NULL);
    resourceTable = &native->resourceTables[groupIndex];
    samplerTable  = &native->samplerTables[groupIndex];
    resourceOffset = 0u;
    resourceRange  = 0u;
    samplerOffset  = native->samplerTableBaseOnly
                       ? samplerTable->descriptorOffset
                       : 0u;
    samplerRange   = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      D3D12_DESCRIPTOR_RANGE *range;
      uint32_t                tableOffset;

      if (dx12__resourceTableBinding(device, &entries[i])) {
        tableOffset = resourceOffset;
        range = &ranges[resourceTable->rangeOffset + resourceRange++];
        resourceOffset += entries[i].arrayCount;
        range->RangeType = dx12__resourceRangeType(&entries[i]);
      } else if (entries[i].bindingType == GPU_BINDING_SAMPLER &&
                 !entries[i].immutableSampler) {
        tableOffset = samplerOffset;
        range = &ranges[samplerTable->rangeOffset + samplerRange++];
        samplerOffset += entries[i].arrayCount;
        range->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      } else {
        continue;
      }

      range->NumDescriptors                  = entries[i].arrayCount;
      range->BaseShaderRegister              = backendBindings[i];
      range->RegisterSpace                   = groupIndex;
      range->OffsetInDescriptorsFromTableStart = tableOffset;
    }

    if (resourceTable->descriptorCount > 0u) {
      parameters[resourceTable->rootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[resourceTable->rootParameter]
        .DescriptorTable.NumDescriptorRanges =
          resourceTable->rangeCount;
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
        .DescriptorTable.NumDescriptorRanges = samplerTable->rangeCount;
      parameters[samplerTable->rootParameter]
        .DescriptorTable.pDescriptorRanges =
          &ranges[samplerTable->rangeOffset];
      parameters[samplerTable->rootParameter].ShaderVisibility =
        dx12__shaderVisibility(samplerTable->visibility);
    }
  }
}

static GPUResult
dx12__createPipelineLayout(GPUDevice                        *device,
                           GPUPipelineLayout                *layout,
                           const GPUShaderStaticSamplerInfo *sourceSamplers,
                           uint32_t                          sourceSamplerCount,
                           uint64_t                          entryMask,
                           GPUPipelineLayoutDX12           **outNative) {
  GPUPipelineLayoutDX12      *native;
  GPUBindGroupLayout * const *groups;
  GPUDeviceDX12              *deviceDX12;
  ID3DBlob                   *serialized;
  ID3DBlob                   *errors;
  DX12LayoutPlan              plan;
  GPUResult                   planResult;
  uint32_t                    groupCount;
  uint32_t                    pushSize;
  uint32_t                    pushDwordCount;
  uint32_t                    pushRootParameter;
  uint32_t                    selectedSamplerCount;
  GPUShaderStageFlags         pushStages;
  HRESULT                     result;

  if (!device || !device->_priv || !layout || !outNative ||
      (sourceSamplerCount > 0u && !sourceSamplers)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outNative = NULL;

  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  gpuGetPipelineLayoutPushConstants(layout, &pushSize, &pushStages);
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
  pushDwordCount   = pushSize / 4u;
  pushRootParameter = UINT32_MAX;
  if (pushDwordCount > 0u) {
    if (pushDwordCount > DX12_ROOT_SIGNATURE_DWORD_LIMIT -
                           plan.rootDwordCount) {
      return GPU_ERROR_UNSUPPORTED;
    }
    pushRootParameter = plan.rootParameterCount++;
    plan.rootDwordCount += pushDwordCount;
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
  native->pushConstantRootParameter = pushRootParameter;
  native->pushConstantDwordCount     = pushDwordCount;
  native->samplerTableBaseOnly =
    !deviceDX12->samplerTableOffsetsReliable;
  dx12__fillLayoutPlan(deviceDX12, layout, groups, groupCount, native);

  serialized = NULL;
  errors     = NULL;
  if (deviceDX12->rootSignatureVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER1              *parameters;
    D3D12_DESCRIPTOR_RANGE1            *ranges;
    D3D12_STATIC_SAMPLER_DESC          *staticSamplers;

    parameters = plan.rootParameterCount > 0u
                   ? calloc(plan.rootParameterCount, sizeof(*parameters))
                   : NULL;
    ranges = plan.rangeCount > 0u
               ? calloc(plan.rangeCount, sizeof(*ranges))
               : NULL;
    staticSamplers = plan.staticSamplerCount > 0u
                       ? calloc(plan.staticSamplerCount,
                                sizeof(*staticSamplers))
                       : NULL;
    if ((plan.rootParameterCount > 0u && !parameters) ||
        (plan.rangeCount > 0u && !ranges) ||
        (plan.staticSamplerCount > 0u && !staticSamplers)) {
      free(staticSamplers);
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    if (!dx12__fillStaticSamplers(layout,
                                  groups,
                                  groupCount,
                                  sourceSamplers,
                                  sourceSamplerCount,
                                  entryMask,
                                  staticSamplers,
                                  plan.staticSamplerCount)) {
      free(staticSamplers);
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    dx12__fillRanges11(deviceDX12,
                       layout,
                       groups,
                       native,
                       parameters,
                       ranges);
    if (pushDwordCount > 0u) {
      parameters[pushRootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters[pushRootParameter].Constants.ShaderRegister = 0u;
      parameters[pushRootParameter].Constants.RegisterSpace =
        GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE;
      parameters[pushRootParameter].Constants.Num32BitValues =
        pushDwordCount;
      parameters[pushRootParameter].ShaderVisibility =
        dx12__shaderVisibility(pushStages);
    }

    desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = plan.rootParameterCount;
    desc.Desc_1_1.pParameters   = parameters;
    desc.Desc_1_1.NumStaticSamplers = plan.staticSamplerCount;
    desc.Desc_1_1.pStaticSamplers   = staticSamplers;
    desc.Desc_1_1.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeVersionedRootSignature(&desc,
                                                   &serialized,
                                                   &errors);
    free(staticSamplers);
    free(ranges);
    free(parameters);
  } else {
    D3D12_ROOT_SIGNATURE_DESC desc = {0};
    D3D12_ROOT_PARAMETER     *parameters;
    D3D12_DESCRIPTOR_RANGE   *ranges;
    D3D12_STATIC_SAMPLER_DESC *staticSamplers;

    parameters = plan.rootParameterCount > 0u
                   ? calloc(plan.rootParameterCount, sizeof(*parameters))
                   : NULL;
    ranges = plan.rangeCount > 0u
               ? calloc(plan.rangeCount, sizeof(*ranges))
               : NULL;
    staticSamplers = plan.staticSamplerCount > 0u
                       ? calloc(plan.staticSamplerCount,
                                sizeof(*staticSamplers))
                       : NULL;
    if ((plan.rootParameterCount > 0u && !parameters) ||
        (plan.rangeCount > 0u && !ranges) ||
        (plan.staticSamplerCount > 0u && !staticSamplers)) {
      free(staticSamplers);
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    if (!dx12__fillStaticSamplers(layout,
                                  groups,
                                  groupCount,
                                  sourceSamplers,
                                  sourceSamplerCount,
                                  entryMask,
                                  staticSamplers,
                                  plan.staticSamplerCount)) {
      free(staticSamplers);
      free(ranges);
      free(parameters);
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    dx12__fillRanges10(deviceDX12,
                       layout,
                       groups,
                       native,
                       parameters,
                       ranges);
    if (pushDwordCount > 0u) {
      parameters[pushRootParameter].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters[pushRootParameter].Constants.ShaderRegister = 0u;
      parameters[pushRootParameter].Constants.RegisterSpace =
        GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE;
      parameters[pushRootParameter].Constants.Num32BitValues =
        pushDwordCount;
      parameters[pushRootParameter].ShaderVisibility =
        dx12__shaderVisibility(pushStages);
    }

    desc.NumParameters = plan.rootParameterCount;
    desc.pParameters   = parameters;
    desc.NumStaticSamplers = plan.staticSamplerCount;
    desc.pStaticSamplers   = staticSamplers;
    desc.Flags         =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeRootSignature(&desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         &serialized,
                                         &errors);
    free(staticSamplers);
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

  {
    DX12PipelineKey key;

    dx12_keyInit(&key);
    dx12_keyWrite(&key,
                  serialized->lpVtbl->GetBufferPointer(serialized),
                  serialized->lpVtbl->GetBufferSize(serialized));
    memcpy(native->rootSignatureKey, key.value, sizeof(key.value));
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

  *outNative = native;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createPipelineLayout(GPUDevice         *device,
                          GPUPipelineLayout *layout) {
  GPUPipelineLayoutDX12 *native;
  GPUResult              result;

  native = NULL;
  result = dx12__createPipelineLayout(device,
                                      layout,
                                      NULL,
                                      0u,
                                      0u,
                                      &native);
  if (result == GPU_OK) {
    GPUBindGroupLayout * const *groups;
    uint32_t                    groupCount;

    groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
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
dx12_createShaderRootSignature(GPUDevice             *device,
                               GPUPipelineLayout     *layout,
                               const GPUShaderLibrary *library,
                               uint64_t               entryMask,
                               ID3D12RootSignature  **outRootSignature,
                               uint64_t               outKey[2]) {
  const GPUShaderStaticSamplerInfo *sourceSamplers;
  GPUPipelineLayoutDX12            *base;
  GPUPipelineLayoutDX12            *derived;
  uint32_t                          sourceSamplerCount;
  GPUResult                         result;

  if (!device || !layout || !library || !outRootSignature || !outKey) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outRootSignature = NULL;
  outKey[0]          = 0u;
  outKey[1]          = 0u;

  base = layout->_native;
  if (!base || !base->rootSignature) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  sourceSamplers = gpuGetShaderLibraryStaticSamplers(library,
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
  result = dx12__createPipelineLayout(device,
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

typedef struct DX12BindGroupWriteContext {
  GPUBindGroupDX12 *group;
  bool              valid;
} DX12BindGroupWriteContext;

static bool
dx12__bindGroupDescriptorOffset(const GPUBindGroupDX12        *group,
                                const GPUBindGroupBindingView *binding,
                                bool                           sampler,
                                uint32_t                      *outOffset) {
  uint32_t base;
  uint32_t count;

  if (!group || !binding || !outOffset ||
      binding->layoutEntryIndex >= group->entryCount) {
    return false;
  }

  base  = group->descriptorOffsets[binding->layoutEntryIndex];
  count = sampler ? group->samplerCount : group->resourceCount;
  if (base == UINT32_MAX || binding->arrayIndex >= binding->arrayCount ||
      base > count || binding->arrayIndex >= count - base) {
    return false;
  }

  *outOffset = base + binding->arrayIndex;
  return true;
}

static bool
dx12__writeBufferDescriptor(GPUDeviceDX12                 *device,
                            uint32_t                        resourceOffset,
                            uint32_t                        resourceCount,
                            const GPUBindGroupBindingView *binding,
                            uint32_t                       descriptorOffset) {
  GPUBufferDX12                 *buffer;
  D3D12_CPU_DESCRIPTOR_HANDLE    handle;
  D3D12_RESOURCE_DESC            resourceDesc;
  uint64_t                       stride;

  buffer = binding->buffer ? binding->buffer->_priv : NULL;
  if (!device || !binding->buffer || !buffer || !buffer->resource ||
      !binding->buffer->device ||
      binding->buffer->device->_priv != device ||
      descriptorOffset >= resourceCount) {
    return false;
  }

  handle = dx12_cpuDescriptor(&device->resourceDescriptors,
                              resourceOffset + descriptorOffset);
  buffer->resource->lpVtbl->GetDesc(buffer->resource, &resourceDesc);
  stride       = binding->bufferLayout.strideBytes;
  if (binding->bindingType == GPU_BINDING_UNIFORM_BUFFER) {
    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {0};
    uint64_t                         size;

    if (binding->size >
          UINT64_MAX -
            (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) {
      return false;
    }
    size = (binding->size +
            (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) &
           ~(uint64_t)(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u);
    if ((binding->offset &
         (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) != 0u ||
        size == 0u ||
        size > D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u ||
        binding->offset > resourceDesc.Width ||
        size > resourceDesc.Width - binding->offset ||
        binding->offset > UINT64_MAX - buffer->gpuAddress) {
      return false;
    }

    desc.BufferLocation = buffer->gpuAddress + binding->offset;
    desc.SizeInBytes    = (UINT)size;
    device->d3dDevice->lpVtbl->CreateConstantBufferView(
      device->d3dDevice,
      &desc,
      handle
    );
    return true;
  }

  if (stride == 0u) {
    stride = 4u;
  }
  if (binding->offset % stride != 0u ||
      binding->size % stride != 0u ||
      binding->offset / stride > UINT32_MAX ||
      binding->size / stride > UINT32_MAX) {
    return false;
  }

  if (binding->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};

    desc.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Format                   = binding->bufferLayout.strideBytes == 0u
                                      ? DXGI_FORMAT_R32_TYPELESS
                                      : DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension            = D3D12_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement      = binding->offset / stride;
    desc.Buffer.NumElements       = (UINT)(binding->size / stride);
    desc.Buffer.StructureByteStride =
      binding->bufferLayout.strideBytes;
    desc.Buffer.Flags = binding->bufferLayout.strideBytes == 0u
                          ? D3D12_BUFFER_SRV_FLAG_RAW
                          : D3D12_BUFFER_SRV_FLAG_NONE;
    device->d3dDevice->lpVtbl->CreateShaderResourceView(
      device->d3dDevice,
      buffer->resource,
      &desc,
      handle
    );
    return true;
  }

  if (binding->bindingType == GPU_BINDING_STORAGE_BUFFER) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};

    desc.Format                   = binding->bufferLayout.strideBytes == 0u
                                      ? DXGI_FORMAT_R32_TYPELESS
                                      : DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension            = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement      = binding->offset / stride;
    desc.Buffer.NumElements       = (UINT)(binding->size / stride);
    desc.Buffer.StructureByteStride =
      binding->bufferLayout.strideBytes;
    desc.Buffer.Flags = binding->bufferLayout.strideBytes == 0u
                          ? D3D12_BUFFER_UAV_FLAG_RAW
                          : D3D12_BUFFER_UAV_FLAG_NONE;
    device->d3dDevice->lpVtbl->CreateUnorderedAccessView(
      device->d3dDevice,
      buffer->resource,
      NULL,
      &desc,
      handle
    );
    return true;
  }

  return false;
}

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
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER: {
      uint32_t descriptorOffset;

      if (binding->kind != GPUBindKindBuffer) {
        writeContext->valid = false;
        return;
      }
      if (!binding->buffer) {
        return;
      }
      if (!binding->buffer->device ||
          binding->buffer->device->_priv != writeContext->group->device ||
          ((binding->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
            binding->bindingType == GPU_BINDING_STORAGE_BUFFER) &&
           !gpuBufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE))) {
        writeContext->valid = false;
        return;
      }
      if (dx12__resourceTableBindingType(writeContext->group->device,
                                         binding->bindingType,
                                         binding->arrayCount) &&
          (!dx12__bindGroupDescriptorOffset(writeContext->group,
                                            binding,
                                            false,
                                            &descriptorOffset) ||
           !dx12__writeBufferDescriptor(writeContext->group->device,
                                        writeContext->group->resourceOffset,
                                        writeContext->group->resourceCount,
                                        binding,
                                        descriptorOffset))) {
        writeContext->valid = false;
      }
      break;
    }
    case GPU_BINDING_SAMPLED_TEXTURE: {
      GPUTextureViewDX12 *view;
      uint32_t            descriptorOffset;

      if (binding->kind != GPUBindKindTexture ||
          !dx12__bindGroupDescriptorOffset(writeContext->group,
                                           binding,
                                           false,
                                           &descriptorOffset)) {
        writeContext->valid = false;
        return;
      }
      if (!binding->textureView) {
        return;
      }
      view = binding->textureView ? binding->textureView->_priv : NULL;
      if (!view || !view->resource ||
          !view->hasSrv || !binding->textureView->_texture ||
          !binding->textureView->_texture->device ||
          binding->textureView->_texture->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(
        &writeContext->group->device->resourceDescriptors,
        writeContext->group->resourceOffset + descriptorOffset
      );
      writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(
        writeContext->group->device->d3dDevice,
        view->resource,
        &view->srv,
        handle
      );
      break;
    }
    case GPU_BINDING_STORAGE_TEXTURE: {
      GPUTextureViewDX12 *view;
      bool                readOnly;
      uint32_t            descriptorOffset;

      if (binding->kind != GPUBindKindTexture ||
          !dx12__bindGroupDescriptorOffset(writeContext->group,
                                           binding,
                                           false,
                                           &descriptorOffset)) {
        writeContext->valid = false;
        return;
      }
      if (!binding->textureView) {
        return;
      }
      view = binding->textureView ? binding->textureView->_priv : NULL;
      readOnly = dx12__storageTextureReadOnly(
        binding->storageTextureAccess
      );
      if (!view || !view->resource ||
          (readOnly ? !view->hasSrv : !view->hasUav) ||
          !binding->textureView->_texture ||
          !binding->textureView->_texture->device ||
          binding->textureView->_texture->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(
        &writeContext->group->device->resourceDescriptors,
        writeContext->group->resourceOffset + descriptorOffset
      );
      if (readOnly) {
        writeContext->group->device->d3dDevice->lpVtbl
          ->CreateShaderResourceView(writeContext->group->device->d3dDevice,
                                     view->resource,
                                     &view->srv,
                                     handle);
      } else {
        writeContext->group->device->d3dDevice->lpVtbl
          ->CreateUnorderedAccessView(writeContext->group->device->d3dDevice,
                                     view->resource,
                                     NULL,
                                     &view->uav,
                                     handle);
      }
      break;
    }
    case GPU_BINDING_SAMPLER: {
      GPUSamplerDX12 *sampler;
      uint32_t        descriptorOffset;

      if (binding->kind != GPUBindKindSampler ||
          !dx12__bindGroupDescriptorOffset(writeContext->group,
                                           binding,
                                           true,
                                           &descriptorOffset)) {
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

      handle = dx12_cpuDescriptor(
        &writeContext->group->device->samplerDescriptors,
        writeContext->group->samplerOffset + descriptorOffset
      );
      writeContext->group->device->d3dDevice->lpVtbl->CreateSampler(
        writeContext->group->device->d3dDevice,
        &sampler->desc,
        handle
      );
      break;
    }
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
      GPUSamplerFeedbackMapDX12 *map;
      GPUTextureDX12            *target;
      uint32_t                   descriptorOffset;

      if (binding->kind != GPUBindKindSamplerFeedback ||
          !dx12__bindGroupDescriptorOffset(writeContext->group,
                                           binding,
                                           false,
                                           &descriptorOffset)) {
        writeContext->valid = false;
        return;
      }
      if (!binding->samplerFeedback) {
        return;
      }
      map    = binding->samplerFeedback->_priv;
      target = binding->samplerFeedback->texture
                 ? binding->samplerFeedback->texture->_priv
                 : NULL;
      if (!map || map->device != writeContext->group->device ||
          !map->resource || !target || !target->resource ||
          !writeContext->group->device->d3dDevice8) {
        writeContext->valid = false;
        return;
      }

      handle = dx12_cpuDescriptor(
        &writeContext->group->device->resourceDescriptors,
        writeContext->group->resourceOffset + descriptorOffset
      );
      writeContext->group->device->d3dDevice8->lpVtbl
        ->CreateSamplerFeedbackUnorderedAccessView(
          writeContext->group->device->d3dDevice8,
          target->resource,
          map->resource,
          handle
        );
#else
      writeContext->valid = false;
#endif
      break;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      GPUAccelerationStructureDX12 *structure;
      D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
      uint32_t descriptorOffset;

      if (binding->kind != GPUBindKindAccelerationStructure ||
          !dx12__bindGroupDescriptorOffset(writeContext->group,
                                           binding,
                                           false,
                                           &descriptorOffset)) {
        writeContext->valid = false;
        return;
      }
      if (!binding->accelerationStructure) {
        return;
      }
      structure = binding->accelerationStructure->_priv;
      if (!structure || !structure->resource || !structure->address ||
          !binding->accelerationStructure->device ||
          binding->accelerationStructure->device->_priv !=
            writeContext->group->device) {
        writeContext->valid = false;
        return;
      }

      desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.ViewDimension =
        D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
      desc.RaytracingAccelerationStructure.Location = structure->address;
      handle = dx12_cpuDescriptor(
        &writeContext->group->device->resourceDescriptors,
        writeContext->group->resourceOffset + descriptorOffset
      );
      writeContext->group->device->d3dDevice->lpVtbl->CreateShaderResourceView(
        writeContext->group->device->d3dDevice,
        NULL,
        &desc,
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
  if (entryCount > (SIZE_MAX - sizeof(*native)) /
                     sizeof(*native->descriptorOffsets)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native = calloc(1,
                  sizeof(*native) +
                    entryCount * sizeof(*native->descriptorOffsets));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device     = device->_priv;
  native->entryCount = entryCount;
  for (uint32_t i = 0u; i < entryCount; i++) {
    native->descriptorOffsets[i] = UINT32_MAX;
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].arrayCount == 0u) {
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    if (entries[i].immutableSampler) {
      continue;
    }
    if (dx12__resourceTableBinding(native->device, &entries[i])) {
      if (entries[i].arrayCount > UINT32_MAX - native->resourceCount) {
        free(native);
        return GPU_ERROR_UNSUPPORTED;
      }
      native->descriptorOffsets[i] = native->resourceCount;
      native->resourceCount += entries[i].arrayCount;
    } else if (entries[i].bindingType == GPU_BINDING_SAMPLER) {
      if (entries[i].arrayCount > UINT32_MAX - native->samplerCount) {
        free(native);
        return GPU_ERROR_UNSUPPORTED;
      }
      native->descriptorOffsets[i] = native->samplerCount;
      native->samplerCount += entries[i].arrayCount;
    } else if (!dx12__bufferBindingType(entries[i].bindingType)) {
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
  if (!gpuForEachBindGroupBinding(group,
                                  dx12__writeBindGroup,
                                  &writeContext) ||
      !writeContext.valid) {
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
  return writeContext.valid &&
         gpuForEachBindGroupEntry(group,
                                  entryCount,
                                  entries,
                                  dx12__writeBindGroup,
                                  &writeContext) &&
         writeContext.valid;
}

GPU_HIDE
void
dx12_destroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupDX12 *native;

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

typedef struct DX12BindContext {
  ID3D12GraphicsCommandList *commandList;
  GPUPipelineLayoutDX12     *layout;
  GPUBindGroupDX12          *group;
  GPUDevice                 *device;
  uint32_t                   resourceOffset;
  uint32_t                   groupIndex;
  uint32_t                   boundCount;
  bool                       compute;
  bool                       valid;
} DX12BindContext;

static uint32_t
dx12__runtimeBindingCount(GPUBindGroupLayout *layout) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  uint32_t                       runtimeCount;

  entries      = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  runtimeCount = 0u;
  if (!entries && entryCount > 0u) {
    return 0u;
  }

  for (uint32_t i = 0u; i < entryCount; i++) {
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
dx12__prepareResourceTable(GPUCommandBufferDX12 *command,
                           GPUBindGroupLayout    *layout,
                           GPUBindGroupDX12      *group,
                           uint32_t              *outResourceOffset) {
  const GPUBindGroupLayoutEntry *entries;
  D3D12_CPU_DESCRIPTOR_HANDLE    dst;
  D3D12_CPU_DESCRIPTOR_HANDLE    src;
  uint32_t                       entryCount;
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
  for (uint32_t i = 0u; i < entryCount; i++) {
    if (dx12__resourceTableBinding(group->device, &entries[i]) &&
        dx12__bufferBindingType(entries[i].bindingType) &&
        entries[i].hasDynamicOffset) {
      dynamic = true;
      break;
    }
  }
  if (!dynamic) {
    return true;
  }
  if (!command ||
      dx12_allocateCommandDescriptors(command,
                                      group->resourceCount,
                                      outResourceOffset) != GPU_OK) {
    return false;
  }

  dst = dx12_cpuDescriptor(&group->device->resourceDescriptors,
                           *outResourceOffset);
  src = dx12_cpuDescriptor(&group->device->resourceDescriptors,
                           group->resourceOffset);
  group->device->d3dDevice->lpVtbl->CopyDescriptorsSimple(
    group->device->d3dDevice,
    group->resourceCount,
    dst,
    src,
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
  );
  return true;
}

static bool
dx12__bindDescriptorHeaps(ID3D12GraphicsCommandList *commandList,
                          ID3D12DescriptorHeap      **boundResourceHeap,
                          ID3D12DescriptorHeap      **boundSamplerHeap,
                          GPUDeviceDX12              *device,
                          ID3D12DescriptorHeap       *samplerHeap,
                          bool                        needsResources,
                          bool                        needsSamplers) {
  ID3D12DescriptorHeap *heaps[2];
  ID3D12DescriptorHeap *desiredSamplerHeap;
  uint32_t              count;

  if (!commandList || !boundResourceHeap || !boundSamplerHeap || !device) {
    return false;
  }
  desiredSamplerHeap = samplerHeap
                         ? samplerHeap
                         : device->samplerDescriptors.heap;
  if ((needsResources && !device->resourceDescriptors.heap) ||
      (needsSamplers && !desiredSamplerHeap)) {
    return false;
  }

  if (*boundResourceHeap == device->resourceDescriptors.heap &&
      *boundSamplerHeap == desiredSamplerHeap) {
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
dx12__bindSamplerSnapshot(
  GPUCommandBufferDX12       *command,
  ID3D12GraphicsCommandList  *commandList,
  ID3D12DescriptorHeap      **boundResourceHeap,
  ID3D12DescriptorHeap      **boundSamplerHeap,
  GPUDeviceDX12              *device,
  const GPUPipelineLayoutDX12 *layout,
  GPUBindGroup * const       *boundGroups,
  uint32_t                     overrideIndex,
  GPUBindGroup                *overrideGroup,
  const uint32_t              *resourceOffsets,
  uint32_t                     resourceOffsetMask,
  bool                         compute) {
  GPUCommandSamplerHeapDX12 *snapshot;
  D3D12_CPU_DESCRIPTOR_HANDLE dstBase = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE samplerBase = {0};
  bool                        needsResources;

  if (!command || !commandList || !boundResourceHeap ||
      !boundSamplerHeap || !device || !layout ||
      !layout->samplerTableBaseOnly ||
      layout->samplerDescriptorCount == 0u) {
    return false;
  }

  snapshot = dx12__takeCommandSamplerHeap(command,
                                          layout->samplerDescriptorCount);
  if (!snapshot) {
    return false;
  }
  snapshot->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(snapshot->heap,
                                                             &dstBase);
  snapshot->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(snapshot->heap,
                                                             &samplerBase);

  needsResources = false;
  for (uint32_t i = 0u; i < layout->groupCount; i++) {
    const GPUDescriptorTableDX12 *resourceTable;
    const GPUDescriptorTableDX12 *samplerTable;
    GPUBindGroup                  *group;
    GPUBindGroupDX12              *nativeGroup;
    D3D12_CPU_DESCRIPTOR_HANDLE    dst;
    D3D12_CPU_DESCRIPTOR_HANDLE    src;
    uint32_t                       sourceOffset;

    resourceTable = &layout->resourceTables[i];
    samplerTable  = &layout->samplerTables[i];
    needsResources |= resourceTable->descriptorCount > 0u;
    if (samplerTable->descriptorCount == 0u) {
      continue;
    }

    group = i == overrideIndex
              ? overrideGroup
              : boundGroups
                  ? boundGroups[i]
                  : NULL;
    nativeGroup = group ? group->_native : NULL;
    sourceOffset = nativeGroup && nativeGroup->device == device &&
                   nativeGroup->samplerCount ==
                     samplerTable->descriptorCount
                     ? nativeGroup->samplerOffset
                     : samplerTable->nullOffset;
    if (sourceOffset == UINT32_MAX) {
      return false;
    }

    dst = dstBase;
    dst.ptr += (SIZE_T)samplerTable->descriptorOffset *
               device->samplerDescriptors.descriptorSize;
    src = dx12_cpuDescriptor(&device->samplerDescriptors, sourceOffset);
    device->d3dDevice->lpVtbl->CopyDescriptorsSimple(
      device->d3dDevice,
      samplerTable->descriptorCount,
      dst,
      src,
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
    );
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

  for (uint32_t i = 0u; i < layout->groupCount; i++) {
    const GPUDescriptorTableDX12 *resourceTable;
    const GPUDescriptorTableDX12 *samplerTable;
    D3D12_GPU_DESCRIPTOR_HANDLE   handle;
    uint32_t                       resourceOffset;

    resourceTable = &layout->resourceTables[i];
    samplerTable  = &layout->samplerTables[i];
    if (resourceTable->descriptorCount > 0u) {
      resourceOffset =
        (resourceOffsetMask & (1u << i)) != 0u && resourceOffsets
          ? resourceOffsets[i]
          : resourceTable->nullOffset;
      if (resourceOffset == UINT32_MAX) {
        return false;
      }
      handle = dx12_gpuDescriptor(&device->resourceDescriptors,
                                  resourceOffset);
      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(
          commandList,
          resourceTable->rootParameter,
          handle
        );
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(
          commandList,
          resourceTable->rootParameter,
          handle
        );
      }
    }
    if (samplerTable->descriptorCount > 0u) {
      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(
          commandList,
          samplerTable->rootParameter,
          samplerBase
        );
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(
          commandList,
          samplerTable->rootParameter,
          samplerBase
        );
      }
    }
  }
  return true;
}

static bool
dx12__bindNullTables(GPUCommandBufferDX12       *command,
                     ID3D12GraphicsCommandList  *commandList,
                     ID3D12DescriptorHeap       **boundResourceHeap,
                     ID3D12DescriptorHeap       **boundSamplerHeap,
                     GPUDeviceDX12               *device,
                     const GPUPipelineLayoutDX12 *layout,
                     uint32_t                     *resourceOffsets,
                     uint32_t                     *resourceOffsetMask,
                     bool                         compute) {
  bool needsResources;
  bool needsSamplers;

  if (!commandList || !boundResourceHeap || !boundSamplerHeap ||
      !device || !layout) {
    return false;
  }

  needsResources = false;
  needsSamplers  = false;
  if (resourceOffsetMask) {
    *resourceOffsetMask = 0u;
  }
  for (uint32_t i = 0u; i < layout->groupCount; i++) {
    needsResources |= layout->resourceTables[i].descriptorCount > 0u;
    needsSamplers  |= layout->samplerTables[i].descriptorCount > 0u;
    if (layout->resourceTables[i].descriptorCount > 0u &&
        resourceOffsets && resourceOffsetMask) {
      resourceOffsets[i] = layout->resourceTables[i].nullOffset;
      *resourceOffsetMask |= 1u << i;
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
                                     resourceOffsetMask
                                       ? *resourceOffsetMask
                                       : 0u,
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

  for (uint32_t i = 0u; i < layout->groupCount; i++) {
    const GPUDescriptorTableDX12 *resourceTable;
    const GPUDescriptorTableDX12 *samplerTable;
    D3D12_GPU_DESCRIPTOR_HANDLE   handle;

    resourceTable = &layout->resourceTables[i];
    samplerTable  = &layout->samplerTables[i];
    if ((resourceTable->descriptorCount > 0u &&
         resourceTable->nullOffset == UINT32_MAX) ||
        (samplerTable->descriptorCount > 0u &&
         samplerTable->nullOffset == UINT32_MAX)) {
      return false;
    }

    if (resourceTable->descriptorCount > 0u) {
      handle = dx12_gpuDescriptor(&device->resourceDescriptors,
                                  resourceTable->nullOffset);
      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(
          commandList,
          resourceTable->rootParameter,
          handle
        );
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(
          commandList,
          resourceTable->rootParameter,
          handle
        );
      }
    }
    if (samplerTable->descriptorCount > 0u) {
      handle = dx12_gpuDescriptor(&device->samplerDescriptors,
                                  samplerTable->nullOffset);
      if (compute) {
        commandList->lpVtbl->SetComputeRootDescriptorTable(
          commandList,
          samplerTable->rootParameter,
          handle
        );
      } else {
        commandList->lpVtbl->SetGraphicsRootDescriptorTable(
          commandList,
          samplerTable->rootParameter,
          handle
        );
      }
    }
  }
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
                                      GPUBufferDX12             *buffer) {
  const D3D12_RESOURCE_STATES requiredState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  return dx12_transitionBuffer(commandList, buffer, requiredState);
}

static bool
dx12__transitionStorageBuffer(ID3D12GraphicsCommandList *commandList,
                              GPUBufferDX12             *buffer) {
  return dx12_transitionBuffer(commandList,
                               buffer,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

static bool
dx12__transitionStorageTexture(ID3D12GraphicsCommandList *commandList,
                               GPUTextureViewDX12        *view) {
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

GPU_HIDE
bool
dx12_transitionSamplerFeedback(ID3D12GraphicsCommandList *commandList,
                                GPUSamplerFeedbackMapDX12 *map,
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
      bool                       tableBinding;

      tableBinding = dx12__resourceTableBindingType(
        bindContext->group->device,
        binding->bindingType,
        binding->arrayCount
      );
      rootBinding = !tableBinding
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }
      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }
      if (binding->buffer->device != bindContext->device ||
          (!tableBinding && !rootBinding) ||
          !buffer || !buffer->resource || buffer->gpuAddress == 0u ||
          !dx12_transitionBuffer(
            bindContext->commandList,
            buffer,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
          ) ||
          (binding->offset &
           (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1u)) != 0u ||
          binding->offset > UINT64_MAX - buffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }
      if (tableBinding) {
        uint32_t descriptorOffset;

        if (binding->hasDynamicOffset &&
            (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                              binding,
                                              false,
                                              &descriptorOffset) ||
             !dx12__writeBufferDescriptor(
               bindContext->group->device,
               bindContext->resourceOffset,
               bindContext->group->resourceCount,
               binding,
               descriptorOffset
             ))) {
          bindContext->valid = false;
          return;
        }
        break;
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
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER: {
      const GPURootBindingDX12 *rootBinding;
      GPUBufferDX12            *buffer;
      D3D12_GPU_VIRTUAL_ADDRESS address;

      rootBinding = binding->arrayCount == 1u
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }
      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }
      if (binding->buffer->device != bindContext->device ||
          (binding->arrayCount == 1u && !rootBinding) ||
          !gpuBufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE) ||
          !dx12__transitionReadOnlyStorageBuffer(bindContext->commandList,
                                                 buffer) ||
          binding->offset > UINT64_MAX - buffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }
      if (binding->arrayCount > 1u) {
        uint32_t descriptorOffset;

        if (binding->hasDynamicOffset &&
            (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                              binding,
                                              false,
                                              &descriptorOffset) ||
             !dx12__writeBufferDescriptor(
               bindContext->group->device,
               bindContext->resourceOffset,
               bindContext->group->resourceCount,
               binding,
               descriptorOffset
             ))) {
          bindContext->valid = false;
          return;
        }
        break;
      }

      address = buffer->gpuAddress + binding->offset;
      if (bindContext->compute) {
        bindContext->commandList->lpVtbl->SetComputeRootShaderResourceView(
          bindContext->commandList,
          rootBinding->rootParameter,
          address
        );
      } else {
        bindContext->commandList->lpVtbl->SetGraphicsRootShaderResourceView(
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

      rootBinding = binding->arrayCount == 1u
                      ? dx12__findRootBinding(bindContext->layout,
                                              bindContext->groupIndex,
                                              binding->binding,
                                              binding->bindingType)
                      : NULL;
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (binding->kind != GPUBindKindBuffer) {
        bindContext->valid = false;
        return;
      }
      if (!binding->buffer) {
        bindContext->boundCount++;
        return;
      }
      if (binding->buffer->device != bindContext->device ||
          (binding->arrayCount == 1u && !rootBinding) ||
          !gpuBufferHasUsage(binding->buffer, GPU_BUFFER_USAGE_STORAGE) ||
          !dx12__transitionStorageBuffer(bindContext->commandList, buffer) ||
          binding->offset > UINT64_MAX - buffer->gpuAddress) {
        bindContext->valid = false;
        return;
      }
      if (binding->arrayCount > 1u) {
        uint32_t descriptorOffset;

        if (binding->hasDynamicOffset &&
            (!dx12__bindGroupDescriptorOffset(bindContext->group,
                                              binding,
                                              false,
                                              &descriptorOffset) ||
             !dx12__writeBufferDescriptor(
               bindContext->group->device,
               bindContext->resourceOffset,
               bindContext->group->resourceCount,
               binding,
               descriptorOffset
             ))) {
          bindContext->valid = false;
          return;
        }
        break;
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

      if (binding->kind != GPUBindKindTexture) {
        bindContext->valid = false;
        return;
      }
      if (!binding->textureView) {
        bindContext->boundCount++;
        return;
      }
      view = binding->textureView ? binding->textureView->_priv : NULL;
      if (!view || !view->hasSrv ||
          !binding->textureView->_texture ||
          binding->textureView->_texture->device != bindContext->device ||
          !dx12__transitionSampledTexture(bindContext->commandList, view)) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    case GPU_BINDING_STORAGE_TEXTURE: {
      GPUTextureViewDX12 *view;
      bool                readOnly;

      if (binding->kind != GPUBindKindTexture) {
        bindContext->valid = false;
        return;
      }
      if (!binding->textureView) {
        bindContext->boundCount++;
        return;
      }
      view = binding->textureView ? binding->textureView->_priv : NULL;
      readOnly = dx12__storageTextureReadOnly(
        binding->storageTextureAccess
      );
      if (!view || (readOnly ? !view->hasSrv : !view->hasUav) ||
          !binding->textureView->_texture ||
          binding->textureView->_texture->device != bindContext->device ||
          !(readOnly
              ? dx12__transitionSampledTexture(bindContext->commandList, view)
              : dx12__transitionStorageTexture(bindContext->commandList,
                                                view))) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    case GPU_BINDING_SAMPLER: {
      GPUSamplerDX12 *sampler;

      if (binding->kind != GPUBindKindSampler) {
        bindContext->valid = false;
        return;
      }
      if (!binding->sampler) {
        bindContext->boundCount++;
        return;
      }
      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (!sampler ||
          sampler->device != bindContext->device->_priv) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    case GPU_BINDING_SAMPLER_FEEDBACK_EXT: {
      GPUSamplerFeedbackMapDX12 *map;

      map = binding->samplerFeedback ? binding->samplerFeedback->_priv : NULL;
      if (binding->kind != GPUBindKindSamplerFeedback) {
        bindContext->valid = false;
        return;
      }
      if (!binding->samplerFeedback) {
        bindContext->boundCount++;
        return;
      }
      if (binding->samplerFeedback->device != bindContext->device ||
          !map || map->device != bindContext->device->_priv ||
          !dx12_transitionSamplerFeedback(
            bindContext->commandList,
            map,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
          )) {
        bindContext->valid = false;
        return;
      }
      break;
    }
    case GPU_BINDING_ACCELERATION_STRUCTURE: {
      GPUAccelerationStructureDX12 *structure;

      structure = binding->accelerationStructure
                    ? binding->accelerationStructure->_priv
                    : NULL;
      if (binding->kind != GPUBindKindAccelerationStructure) {
        bindContext->valid = false;
        return;
      }
      if (!binding->accelerationStructure) {
        bindContext->boundCount++;
        return;
      }
      if (binding->accelerationStructure->device != bindContext->device ||
          !structure || !structure->resource || !structure->address) {
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
dx12_bindRenderGroup(GPURenderPassEncoder *pass,
                     GPUPipelineLayout       *pipelineLayout,
                     uint32_t                 groupIndex,
                     GPUBindGroup            *group,
                     uint32_t                 dynamicOffsetCount,
                     const uint32_t          *dynamicOffsets) {
  DX12BindContext        context;
  GPURenderEncoderDX12  *encoder;
  GPUPipelineLayoutDX12 *layout;
  GPUBindGroupDX12      *nativeGroup;
  GPUCommandBufferDX12  *command;
  GPUDeviceDX12         *device;
  GPUBindGroupLayout    *groupLayout;
  ID3D12DescriptorHeap  *samplerHeap;
  uint32_t               resourceOffset;
  uint32_t               expectedCount;
  bool                   valid;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout  = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device = pipelineLayout && pipelineLayout->_device
             ? pipelineLayout->_device->_priv
             : NULL;
  if (!encoder || !encoder->commandList || !encoder->pipeline ||
      !layout || !layout->rootSignature ||
      pass->_pipelineLayout != pipelineLayout ||
      encoder->rootSignature != encoder->pipeline->rootSignature ||
      !nativeGroup || nativeGroup->device != device ||
      groupIndex >= layout->groupCount) {
    return false;
  }

  groupLayout = gpuBindGroupGetLayout(group);
  expectedCount = dx12__runtimeBindingCount(groupLayout);
  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount ||
      nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount ||
      !dx12__prepareResourceTable(command,
                                  groupLayout,
                                  nativeGroup,
                                  &resourceOffset)) {
    return false;
  }
  if (nativeGroup->resourceCount > 0u) {
    encoder->resourceOffsets[groupIndex] = resourceOffset;
    encoder->resourceOffsetMask |= 1u << groupIndex;
  }
  if (layout->samplerTableBaseOnly &&
      nativeGroup->samplerCount > 0u) {
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
    samplerHeap = layout->samplerTableBaseOnly
                    ? encoder->samplerHeap
                    : NULL;
    if (layout->samplerTableBaseOnly &&
        layout->samplerDescriptorCount > 0u &&
        !samplerHeap) {
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
  context.commandList = encoder->commandList;
  context.layout      = layout;
  context.group       = nativeGroup;
  context.device      = pipelineLayout->_device;
  context.resourceOffset = resourceOffset;
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
      dx12_gpuDescriptor(&device->resourceDescriptors,
                         resourceOffset)
    );
  }
  if (nativeGroup->samplerCount > 0u &&
      !layout->samplerTableBaseOnly) {
    encoder->commandList->lpVtbl->SetGraphicsRootDescriptorTable(
      encoder->commandList,
      layout->samplerTables[groupIndex].rootParameter,
      dx12_gpuDescriptor(&device->samplerDescriptors,
                         nativeGroup->samplerOffset)
    );
  }
  return true;
}

static bool
dx12__bindComputeLikeGroup(GPUCommandBufferDX12      *command,
                           ID3D12GraphicsCommandList *commandList,
                           ID3D12RootSignature       *rootSignature,
                           ID3D12DescriptorHeap     **resourceHeap,
                           ID3D12DescriptorHeap     **samplerHeap,
                           GPUBindGroup * const      *boundGroups,
                           uint32_t                  *resourceOffsets,
                           uint32_t                  *resourceOffsetMask,
                           GPUPipelineLayout         *pipelineLayout,
                           uint32_t                   groupIndex,
                           GPUBindGroup              *group,
                           uint32_t                   dynamicOffsetCount,
                           const uint32_t            *dynamicOffsets) {
  DX12BindContext         context;
  GPUPipelineLayoutDX12  *layout;
  GPUBindGroupDX12       *nativeGroup;
  GPUDeviceDX12          *device;
  GPUBindGroupLayout     *groupLayout;
  ID3D12DescriptorHeap   *desiredSamplerHeap;
  uint32_t                resourceOffset;
  uint32_t                expectedCount;
  bool                    valid;

  layout  = pipelineLayout ? pipelineLayout->_native : NULL;
  nativeGroup = group ? group->_native : NULL;
  device = pipelineLayout && pipelineLayout->_device
             ? pipelineLayout->_device->_priv
             : NULL;
  if (!commandList || !rootSignature || !resourceHeap || !samplerHeap ||
      !resourceOffsets || !resourceOffsetMask ||
      !layout || !layout->rootSignature ||
      !nativeGroup || nativeGroup->device != device ||
      groupIndex >= layout->groupCount) {
    return false;
  }

  groupLayout = gpuBindGroupGetLayout(group);
  expectedCount = dx12__runtimeBindingCount(groupLayout);
  if (nativeGroup->resourceCount !=
        layout->resourceTables[groupIndex].descriptorCount ||
      nativeGroup->samplerCount !=
        layout->samplerTables[groupIndex].descriptorCount ||
      !dx12__prepareResourceTable(command,
                                  groupLayout,
                                  nativeGroup,
                                  &resourceOffset)) {
    return false;
  }
  if (nativeGroup->resourceCount > 0u) {
    resourceOffsets[groupIndex] = resourceOffset;
    *resourceOffsetMask |= 1u << groupIndex;
  }
  if (layout->samplerTableBaseOnly &&
      nativeGroup->samplerCount > 0u) {
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
    desiredSamplerHeap = layout->samplerTableBaseOnly
                           ? *samplerHeap
                           : NULL;
    if (layout->samplerTableBaseOnly &&
        layout->samplerDescriptorCount > 0u &&
        !desiredSamplerHeap) {
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
  context.commandList = commandList;
  context.layout      = layout;
  context.group       = nativeGroup;
  context.device      = pipelineLayout->_device;
  context.resourceOffset = resourceOffset;
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
    commandList->lpVtbl->SetComputeRootDescriptorTable(
      commandList,
      layout->resourceTables[groupIndex].rootParameter,
      dx12_gpuDescriptor(&device->resourceDescriptors,
                         resourceOffset)
    );
  }
  if (nativeGroup->samplerCount > 0u &&
      !layout->samplerTableBaseOnly) {
    commandList->lpVtbl->SetComputeRootDescriptorTable(
      commandList,
      layout->samplerTables[groupIndex].rootParameter,
      dx12_gpuDescriptor(&device->samplerDescriptors,
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
  GPUComputeEncoderDX12 *encoder;
  GPUCommandBufferDX12  *command;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  return encoder && pass->_pipelineLayout == pipelineLayout &&
         dx12__bindComputeLikeGroup(command,
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
  GPURayTracingEncoderDX12 *encoder;
  GPUCommandBufferDX12     *command;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->cmdb ? pass->cmdb->_priv : NULL;
  return encoder && pass->pipelineLayout == pipelineLayout &&
         dx12__bindComputeLikeGroup(command,
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
  GPURenderEncoderDX12  *encoder;
  GPUPipelineLayoutDX12 *layout;
  GPUCommandBufferDX12  *command;
  GPUDeviceDX12         *device;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout  = pass && pass->_pipelineLayout
              ? pass->_pipelineLayout->_native
              : NULL;
  device  = pass && pass->_pipelineLayout &&
            pass->_pipelineLayout->_device
              ? pass->_pipelineLayout->_device->_priv
              : NULL;
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

  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    GPUBindGroup *group;

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
      gpuFrameStatsRecordBindEmission(pass->_stats);
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
  GPUComputeEncoderDX12 *encoder;
  GPUCommandBufferDX12  *command;
  GPUPipelineLayoutDX12 *layout;
  GPUDeviceDX12         *device;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->_cmdb ? pass->_cmdb->_priv : NULL;
  layout  = pass && pass->_pipelineLayout
              ? pass->_pipelineLayout->_native
              : NULL;
  device  = pass && pass->_pipelineLayout &&
            pass->_pipelineLayout->_device
              ? pass->_pipelineLayout->_device->_priv
              : NULL;
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

  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    GPUBindGroup *group;

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
      gpuFrameStatsRecordBindEmission(pass->_stats);
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
  GPURayTracingEncoderDX12 *encoder;
  GPUCommandBufferDX12     *command;
  GPUPipelineLayoutDX12    *layout;
  GPUDeviceDX12            *device;

  encoder = pass ? pass->_priv : NULL;
  command = pass && pass->cmdb ? pass->cmdb->_priv : NULL;
  layout  = pass && pass->pipelineLayout
              ? pass->pipelineLayout->_native
              : NULL;
  device  = pass && pass->pipelineLayout && pass->pipelineLayout->_device
              ? pass->pipelineLayout->_device->_priv
              : NULL;
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

  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    GPUBindGroup *group;

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
      gpuFrameStatsRecordBindEmission(pass->stats);
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
  GPUPipelineLayoutDX12 *native;
  GPUDeviceDX12         *device;

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
dx12_initDescriptor(GPUApiDescriptor *api) {
  memset(api, 0, sizeof(*api));
  api->createPipelineLayout  = dx12_createPipelineLayout;
  api->destroyPipelineLayout = dx12_destroyPipelineLayout;
  api->createBindGroup       = dx12_createBindGroup;
  api->updateBindGroup       = dx12_updateBindGroup;
  api->destroyBindGroup      = dx12_destroyBindGroup;
  api->bindRenderGroup       = dx12_bindRenderGroup;
  api->bindComputeGroup      = dx12_bindComputeGroup;
}
