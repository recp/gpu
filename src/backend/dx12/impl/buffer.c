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

#include "../common.h"

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12__setBufferName(ID3D12Resource *resource, const char *label);
#endif

static GPUResult
dx12__validateBufferUsage(GPUBufferUsageFlags usage) {
  const GPUBufferUsageFlags known = GPU_BUFFER_USAGE_VERTEX |
                                    GPU_BUFFER_USAGE_INDEX |
                                    GPU_BUFFER_USAGE_UNIFORM |
                                    GPU_BUFFER_USAGE_STORAGE |
                                    GPU_BUFFER_USAGE_COPY_SRC |
                                    GPU_BUFFER_USAGE_COPY_DST |
                                    GPU_BUFFER_USAGE_INDIRECT |
                                    GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT |
                                    GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT |
                                    GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT;

  if (usage == 0u || (usage & ~known) != 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return GPU_OK;
}

static bool
dx12__alignUniformBufferSize(uint64_t sizeBytes, uint64_t *outSizeBytes) {
  const uint64_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

  if (!outSizeBytes || sizeBytes > UINT64_MAX - (alignment - 1u)) {
    return false;
  }

  *outSizeBytes = (sizeBytes + alignment - 1u) & ~(alignment - 1u);
  return true;
}

static D3D12_RESOURCE_STATES
dx12__bufferInitialState(GPUBufferUsageFlags usage, bool defaultHeap) {
  if (!defaultHeap) {
    return D3D12_RESOURCE_STATE_GENERIC_READ;
  }
  if ((usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT) != 0u) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if ((usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT) != 0u) {
    return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  return D3D12_RESOURCE_STATE_COMMON;
}

static GPUResult
dx12__bufferDesc(const GPUBufferCreateInfo *info,
                 D3D12_RESOURCE_DESC       *outDesc) {
  uint64_t allocationSize;
  bool     unorderedAccess;

  if (!info || !outDesc ||
      dx12__validateBufferUsage(info->usage) != GPU_OK) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  allocationSize = info->sizeBytes;
  if ((info->usage & GPU_BUFFER_USAGE_UNIFORM) != 0u &&
      !dx12__alignUniformBufferSize(info->sizeBytes, &allocationSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  unorderedAccess =
    (info->usage & (GPU_BUFFER_USAGE_STORAGE |
                    GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT)) != 0u;

  outDesc->Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  outDesc->Width            = allocationSize;
  outDesc->Height           = 1u;
  outDesc->DepthOrArraySize = 1u;
  outDesc->MipLevels        = 1u;
  outDesc->SampleDesc.Count = 1u;
  outDesc->Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  outDesc->Flags            = unorderedAccess
                                ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                : D3D12_RESOURCE_FLAG_NONE;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_getBufferMemoryRequirements(GPUDevice                 *device,
                                 const GPUBufferCreateInfo *info,
                                 GPUMemoryRequirements     *outRequirements) {
  GPUDeviceDX12                 *deviceDX12;
  D3D12_RESOURCE_DESC            desc = {0};
  D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
  GPUResult                      result;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = dx12__bufferDesc(info, &desc);
  if (result != GPU_OK) {
    return result;
  }
  deviceDX12->d3dDevice->lpVtbl->GetResourceAllocationInfo(
    deviceDX12->d3dDevice,
    &allocationInfo,
    0u,
    1u,
    &desc
  );
  if (allocationInfo.SizeInBytes == UINT64_MAX ||
      allocationInfo.Alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  outRequirements->sizeBytes         = allocationInfo.SizeInBytes;
  outRequirements->alignmentBytes    = allocationInfo.Alignment;
  outRequirements->compatibilityMask = dx12_memoryCompatibility(device,
                                                                 &desc);
  if (outRequirements->compatibilityMask == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_getSparseBufferRequirements(
  GPUDevice                   *device,
  const GPUBufferCreateInfo   *info,
  GPUSparseBufferRequirements *outRequirements
) {
  GPUDeviceDX12       *deviceDX12;
  ID3D12Resource      *resource;
  D3D12_RESOURCE_DESC  desc = {0};
  UINT                 tileCount;
  GPUResult            result;
  HRESULT              nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info ||
      !outRequirements ||
      deviceDX12->tiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1) {
    return GPU_ERROR_UNSUPPORTED;
  }
  result = dx12__bufferDesc(info, &desc);
  if (result != GPU_OK) {
    return result;
  }

  resource = NULL;
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreateReservedResource(
    deviceDX12->d3dDevice,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return GPU_ERROR_UNSUPPORTED;
  }
  tileCount = 0u;
  deviceDX12->d3dDevice->lpVtbl->GetResourceTiling(deviceDX12->d3dDevice,
                                                    resource,
                                                    &tileCount,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    0u,
                                                    NULL);
  resource->lpVtbl->Release(resource);
  if (tileCount == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  outRequirements->compatibilityMask = dx12_memoryCompatibility(device,
                                                                 &desc);
  outRequirements->pageSizeBytes =
    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  outRequirements->tileCount = tileCount;
  return outRequirements->compatibilityMask != 0u
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
GPUResult
dx12_createSparseBuffer(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        GPUBuffer                **outBuffer) {
  GPUDeviceDX12       *deviceDX12;
  GPUBuffer           *buffer;
  GPUBufferDX12       *native;
  D3D12_RESOURCE_DESC  desc = {0};
  GPUResult            result;
  HRESULT              nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info || !heap ||
      !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = dx12__bufferDesc(info, &desc);
  if (result != GPU_OK) {
    return result;
  }

  buffer = calloc(1, sizeof(*buffer) + sizeof(*native));
  if (!buffer) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native = (GPUBufferDX12 *)(buffer + 1);
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreateReservedResource(
    deviceDX12->d3dDevice,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(nativeResult) || !native->resource) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  dx12__setBufferName(native->resource,
                      gpuDeviceDebugLabel(device, info->label));
#endif
  native->gpuAddress  = native->resource->lpVtbl->GetGPUVirtualAddress(
    native->resource
  );
  native->state       = D3D12_RESOURCE_STATE_COMMON;
  native->defaultHeap = true;
  native->sparse      = true;
  buffer->_priv       = native;
  buffer->device      = device;
  buffer->sizeBytes   = info->sizeBytes;
  buffer->usage       = info->usage;
  buffer->_gpuAddress = native->gpuAddress;
  *outBuffer          = buffer;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createPlacedBuffer(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        uint64_t                   heapOffset,
                        GPUBuffer                **outBuffer) {
  GPUDeviceDX12       *deviceDX12;
  GPUHeapDX12         *heapDX12;
  GPUBuffer           *buffer;
  GPUBufferDX12       *native;
  D3D12_RESOURCE_DESC  desc = {0};
  D3D12_RESOURCE_STATES initialState;
  GPUResult            usageResult;
  HRESULT              result;

  if (!device || !(deviceDX12 = device->_priv) || !info || !heap ||
      !(heapDX12 = heap->_priv) || !heapDX12->heap || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  usageResult = dx12__bufferDesc(info, &desc);
  if (usageResult != GPU_OK) {
    return usageResult;
  }

  buffer = calloc(1, sizeof(*buffer) + sizeof(*native));
  if (!buffer) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native       = (GPUBufferDX12 *)(buffer + 1);
  initialState = dx12__bufferInitialState(info->usage, true);
  result = deviceDX12->d3dDevice->lpVtbl->CreatePlacedResource(
    deviceDX12->d3dDevice,
    heapDX12->heap,
    heapOffset,
    &desc,
    initialState,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(result) || !native->resource) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  dx12__setBufferName(native->resource,
                      gpuDeviceDebugLabel(device, info->label));
#endif
  native->gpuAddress  = native->resource->lpVtbl->GetGPUVirtualAddress(
    native->resource
  );
  native->state       = initialState;
  native->defaultHeap = true;
  buffer->_priv       = native;
  buffer->device      = device;
  buffer->sizeBytes   = info->sizeBytes;
  buffer->usage       = info->usage;
  buffer->_gpuAddress = native->gpuAddress;
  *outBuffer          = buffer;
  return GPU_OK;
}

GPU_HIDE
bool
dx12_transitionBuffer(ID3D12GraphicsCommandList *commandList,
                      GPUBufferDX12             *buffer,
                      D3D12_RESOURCE_STATES      state) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!commandList || !buffer || !buffer->resource) {
    return false;
  }
  if (!buffer->defaultHeap) {
    return (buffer->state & state) == state;
  }
  if (buffer->state == state) {
    return true;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = buffer->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = buffer->state;
  barrier.Transition.StateAfter  = state;
  commandList->lpVtbl->ResourceBarrier(commandList, 1u, &barrier);
  buffer->state = state;
  return true;
}

static bool
dx12__recordBufferTransfer(ID3D12GraphicsCommandList *commandList,
                           GPUBufferDX12             *buffer,
                           ID3D12Resource            *staging,
                           uint64_t                    stagingOffset,
                           uint64_t                    bufferOffset,
                           uint64_t                    sizeBytes,
                           bool                        upload) {
  D3D12_RESOURCE_STATES originalState;
  D3D12_RESOURCE_STATES copyState;

  if (!commandList || !buffer || !staging) {
    return false;
  }

  originalState = buffer->state;
  copyState      = upload ? D3D12_RESOURCE_STATE_COPY_DEST
                          : D3D12_RESOURCE_STATE_COPY_SOURCE;
  if (!dx12_transitionBuffer(commandList, buffer, copyState)) {
    return false;
  }
  if (upload) {
    commandList->lpVtbl->CopyBufferRegion(commandList,
                                           buffer->resource,
                                           bufferOffset,
                                           staging,
                                           stagingOffset,
                                           sizeBytes);
  } else {
    commandList->lpVtbl->CopyBufferRegion(commandList,
                                           staging,
                                           stagingOffset,
                                           buffer->resource,
                                           bufferOffset,
                                           sizeBytes);
  }
  return dx12_transitionBuffer(commandList, buffer, originalState);
}

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12__setBufferName(ID3D12Resource *resource, const char *label) {
  wchar_t name[256];

  if (!resource || !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }

  (void)resource->lpVtbl->SetName(resource, name);
}
#endif

GPU_HIDE
GPUResult
dx12_createBuffer(GPUDevice                 * __restrict device,
                  const GPUBufferCreateInfo * __restrict info,
                  GPUBuffer                ** __restrict outBuffer) {
  GPUDeviceDX12           *deviceDX12;
  GPUBuffer               *buffer;
  GPUBufferDX12           *native;
  D3D12_HEAP_PROPERTIES    heap = {0};
  D3D12_RESOURCE_DESC      desc = {0};
  D3D12_RANGE              readRange = {0};
  D3D12_RESOURCE_STATES    initialState;
  GPUResult                usageResult;
  uint64_t                 allocationSize;
  bool                     defaultHeap;
  bool                     rayInput;
  bool                     rayScratch;
  bool                     storage;
  bool                     unorderedAccess;
  HRESULT                  result;

  if (!device || !device->_priv || !info || !outBuffer ||
      info->sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBuffer = NULL;
  usageResult = dx12__validateBufferUsage(info->usage);
  if (usageResult != GPU_OK) {
    return usageResult;
  }

  allocationSize = info->sizeBytes;
  if ((info->usage & GPU_BUFFER_USAGE_UNIFORM) != 0u &&
      !dx12__alignUniformBufferSize(info->sizeBytes, &allocationSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = calloc(1, sizeof(*buffer) + sizeof(*native));
  if (!buffer) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  deviceDX12              = device->_priv;
  native                  = (GPUBufferDX12 *)(buffer + 1);
  storage                 = (info->usage & GPU_BUFFER_USAGE_STORAGE) != 0u;
  rayInput                = (info->usage &
                             GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT) != 0u;
  rayScratch              = (info->usage &
                             GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT) != 0u;
  unorderedAccess         = storage || rayScratch;
  defaultHeap             = unorderedAccess || rayInput ||
                            (info->usage & GPU_BUFFER_USAGE_COPY_DST) != 0u;
  heap.Type               = defaultHeap ? D3D12_HEAP_TYPE_DEFAULT
                                        : D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask   = 1u;
  heap.VisibleNodeMask    = 1u;
  desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width              = allocationSize;
  desc.Height             = 1u;
  desc.DepthOrArraySize   = 1u;
  desc.MipLevels          = 1u;
  desc.SampleDesc.Count   = 1u;
  desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags              = unorderedAccess
                              ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                              : D3D12_RESOURCE_FLAG_NONE;
  initialState            = dx12__bufferInitialState(info->usage, defaultHeap);
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    initialState,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(result) || !native->resource) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!defaultHeap) {
    result = native->resource->lpVtbl->Map(native->resource,
                                           0u,
                                           &readRange,
                                           &native->mapped);
    if (FAILED(result) || !native->mapped) {
      native->resource->lpVtbl->Release(native->resource);
      free(buffer);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  dx12__setBufferName(native->resource,
                      gpuDeviceDebugLabel(device, info->label));
#endif
  native->gpuAddress     = native->resource->lpVtbl->GetGPUVirtualAddress(
    native->resource
  );
  native->state          = initialState;
  native->defaultHeap    = defaultHeap;
  buffer->_priv          = native;
  buffer->device         = device;
  buffer->sizeBytes      = info->sizeBytes;
  buffer->usage          = info->usage;
  buffer->_gpuAddress    = native->gpuAddress;
  *outBuffer             = buffer;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyBuffer(GPUBuffer * __restrict buffer) {
  GPUBufferDX12 *native;

  if (!buffer) {
    return;
  }

  native = buffer->_priv;
  if (native && native->resource) {
    if (native->mapped) {
      native->resource->lpVtbl->Unmap(native->resource, 0u, NULL);
    }
    native->resource->lpVtbl->Release(native->resource);
  }
  free(buffer);
}

GPU_HIDE
GPUResult
dx12_writeBuffer(GPUQueue * __restrict queue,
                 GPUBuffer       * __restrict buffer,
                 uint64_t                     dstOffset,
                 const void      * __restrict data,
                 uint64_t                     sizeBytes) {
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *staging;
  GPUQueueDX12              *queueDX12;
  GPUBufferDX12             *native;
  void                      *mapped;
  uint64_t                   stagingOffset;
  GPUResult                  result;

  queueDX12 = queue ? queue->_priv : NULL;
  native    = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !queueDX12 || !native || !data || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, dstOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (native->mapped) {
    memcpy((uint8_t *)native->mapped + dstOffset, data, (size_t)sizeBytes);
    return GPU_OK;
  }
  if (!native->defaultHeap) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = dx12_beginTransfer(queue,
                              D3D12_HEAP_TYPE_UPLOAD,
                              sizeBytes,
                              GPU_DX12_BUFFER_TRANSFER_CAPACITY,
                              &commandList,
                              &staging,
                              &mapped,
                              &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }

  memcpy((uint8_t *)mapped + stagingOffset, data, (size_t)sizeBytes);
  if (!dx12__recordBufferTransfer(commandList,
                                  native,
                                  staging,
                                  stagingOffset,
                                  dstOffset,
                                  sizeBytes,
                                  true)) {
    dx12_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return dx12_submitTransfer(queue, false);
}

GPU_HIDE
GPUResult
dx12_readBuffer(GPUQueue * __restrict queue,
                GPUBuffer       * __restrict buffer,
                uint64_t                     srcOffset,
                void           * __restrict outData,
                uint64_t                     sizeBytes) {
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *staging;
  GPUQueueDX12              *queueDX12;
  GPUBufferDX12             *native;
  void                      *mapped;
  D3D12_RANGE                readRange;
  uint64_t                   stagingOffset;
  GPUResult                  result;

  queueDX12 = queue ? queue->_priv : NULL;
  native    = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !queueDX12 || !native || !outData || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, srcOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (native->mapped) {
    memcpy(outData,
           (const uint8_t *)native->mapped + srcOffset,
           (size_t)sizeBytes);
    return GPU_OK;
  }
  if (!native->defaultHeap) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = dx12_beginTransfer(queue,
                              D3D12_HEAP_TYPE_READBACK,
                              sizeBytes,
                              GPU_DX12_BUFFER_TRANSFER_CAPACITY,
                              &commandList,
                              &staging,
                              &mapped,
                              &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }

  if (!dx12__recordBufferTransfer(commandList,
                                  native,
                                  staging,
                                  stagingOffset,
                                  srcOffset,
                                  sizeBytes,
                                  false)) {
    dx12_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = dx12_submitTransfer(queue, true);
  if (result == GPU_OK) {
    readRange.Begin = (SIZE_T)stagingOffset;
    readRange.End   = (SIZE_T)(stagingOffset + sizeBytes);
    mapped          = NULL;
    if (FAILED(staging->lpVtbl->Map(staging, 0u, &readRange, &mapped)) ||
        !mapped) {
      result = GPU_ERROR_BACKEND_FAILURE;
    } else {
      memcpy(outData,
             (const uint8_t *)mapped + stagingOffset,
             (size_t)sizeBytes);
      staging->lpVtbl->Unmap(staging, 0u, NULL);
    }
  }
  return result;
}

GPU_HIDE
void *
dx12_bufferContents(GPUBuffer * __restrict buffer) {
  GPUBufferDX12 *native;

  native = buffer ? buffer->_priv : NULL;
  return native ? native->mapped : NULL;
}

GPU_HIDE
void
dx12_initBuff(GPUApiBuffer *api) {
  api->create   = dx12_createBuffer;
  api->destroy  = dx12_destroyBuffer;
  api->write    = dx12_writeBuffer;
  api->read     = dx12_readBuffer;
  api->contents = dx12_bufferContents;
}
