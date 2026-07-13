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

static GPUResult
dx12__validateBufferUsage(GPUBufferUsageFlags usage) {
  const GPUBufferUsageFlags known = GPU_BUFFER_USAGE_VERTEX |
                                    GPU_BUFFER_USAGE_INDEX |
                                    GPU_BUFFER_USAGE_UNIFORM |
                                    GPU_BUFFER_USAGE_STORAGE |
                                    GPU_BUFFER_USAGE_COPY_SRC |
                                    GPU_BUFFER_USAGE_COPY_DST |
                                    GPU_BUFFER_USAGE_INDIRECT;

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
  GPU__UNUSED(usage);
  return D3D12_RESOURCE_STATE_COMMON;
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

static ID3D12Resource *
dx12__createStagingBuffer(GPUDeviceDX12 *device,
                          uint64_t        sizeBytes,
                          D3D12_HEAP_TYPE heapType) {
  ID3D12Resource        *resource;
  D3D12_HEAP_PROPERTIES  heap = {0};
  D3D12_RESOURCE_DESC    desc = {0};
  D3D12_RESOURCE_STATES  initialState;
  HRESULT                result;

  if (!device || !device->d3dDevice || sizeBytes == 0u ||
      (heapType != D3D12_HEAP_TYPE_UPLOAD &&
       heapType != D3D12_HEAP_TYPE_READBACK)) {
    return NULL;
  }

  resource                 = NULL;
  heap.Type                = heapType;
  heap.CreationNodeMask    = 1u;
  heap.VisibleNodeMask     = 1u;
  desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width               = sizeBytes;
  desc.Height              = 1u;
  desc.DepthOrArraySize    = 1u;
  desc.MipLevels           = 1u;
  desc.SampleDesc.Count    = 1u;
  desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  initialState = heapType == D3D12_HEAP_TYPE_UPLOAD
                   ? D3D12_RESOURCE_STATE_GENERIC_READ
                   : D3D12_RESOURCE_STATE_COPY_DEST;
  result       = device->d3dDevice->lpVtbl->CreateCommittedResource(
    device->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    initialState,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  return SUCCEEDED(result) ? resource : NULL;
}

static uint64_t
dx12__transferCapacity(uint64_t sizeBytes) {
  uint64_t capacity;

  capacity = 64u * 1024u;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }
    capacity *= 2u;
  }
  return capacity;
}

static bool
dx12__ensureTransferContext(GPUCommandQueueDX12 *queue,
                            GPUDeviceDX12       *device) {
  ID3D12CommandAllocator    *allocator;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Fence               *fence;
  HANDLE                     event;
  HRESULT                    result;

  if (!queue || !device || !device->d3dDevice) {
    return false;
  }
  if (queue->transferAllocator && queue->transferCommandList &&
      queue->transferFence && queue->transferEvent) {
    return true;
  }
  if (queue->transferAllocator || queue->transferCommandList ||
      queue->transferFence || queue->transferEvent) {
    return false;
  }

  allocator   = NULL;
  commandList = NULL;
  fence       = NULL;
  event       = NULL;
  result      = device->d3dDevice->lpVtbl->CreateCommandAllocator(
    device->d3dDevice,
    queue->type,
    &IID_ID3D12CommandAllocator,
    (void **)&allocator
  );
  if (FAILED(result)) {
    goto fail;
  }
  result = device->d3dDevice->lpVtbl->CreateCommandList(
    device->d3dDevice,
    0u,
    queue->type,
    allocator,
    NULL,
    &IID_ID3D12GraphicsCommandList,
    (void **)&commandList
  );
  if (FAILED(result) || FAILED(commandList->lpVtbl->Close(commandList))) {
    goto fail;
  }
  result = device->d3dDevice->lpVtbl->CreateFence(device->d3dDevice,
                                                   0u,
                                                   D3D12_FENCE_FLAG_NONE,
                                                   &IID_ID3D12Fence,
                                                   (void **)&fence);
  if (FAILED(result) || !(event = CreateEventW(NULL, FALSE, FALSE, NULL))) {
    goto fail;
  }

  queue->transferAllocator   = allocator;
  queue->transferCommandList = commandList;
  queue->transferFence       = fence;
  queue->transferEvent       = event;
  return true;

fail:
  if (event) {
    CloseHandle(event);
  }
  if (fence) {
    fence->lpVtbl->Release(fence);
  }
  if (commandList) {
    commandList->lpVtbl->Release(commandList);
  }
  if (allocator) {
    allocator->lpVtbl->Release(allocator);
  }
  return false;
}

static bool
dx12__ensureTransferStaging(GPUCommandQueueDX12 *queue,
                            GPUDeviceDX12       *device,
                            uint64_t              sizeBytes,
                            bool                  upload) {
  ID3D12Resource *resource;
  void           *mapped;
  D3D12_RANGE     readRange = {0};
  uint64_t        capacity;

  if (!queue || !device || sizeBytes == 0u) {
    return false;
  }
  if (upload && queue->uploadStaging &&
      queue->uploadCapacity >= sizeBytes) {
    return queue->uploadMapped != NULL;
  }
  if (!upload && queue->readbackStaging &&
      queue->readbackCapacity >= sizeBytes) {
    return true;
  }

  capacity = dx12__transferCapacity(sizeBytes);
  resource = dx12__createStagingBuffer(
    device,
    capacity,
    upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK
  );
  mapped = NULL;
  if (!resource ||
      (upload &&
       (FAILED(resource->lpVtbl->Map(resource, 0u, &readRange, &mapped)) ||
        !mapped))) {
    if (resource) {
      resource->lpVtbl->Release(resource);
    }
    return false;
  }

  if (upload) {
    if (queue->uploadStaging) {
      queue->uploadStaging->lpVtbl->Unmap(queue->uploadStaging, 0u, NULL);
      queue->uploadStaging->lpVtbl->Release(queue->uploadStaging);
    }
    queue->uploadStaging  = resource;
    queue->uploadMapped   = mapped;
    queue->uploadCapacity = capacity;
  } else {
    if (queue->readbackStaging) {
      queue->readbackStaging->lpVtbl->Release(queue->readbackStaging);
    }
    queue->readbackStaging  = resource;
    queue->readbackCapacity = capacity;
  }
  return true;
}

static GPUResult
dx12__copyBufferSync(GPUCommandQueue *queue,
                     GPUBufferDX12  *native,
                     ID3D12Resource *staging,
                     uint64_t        nativeOffset,
                     uint64_t        sizeBytes,
                     bool            upload) {
  GPUCommandQueueDX12    *queueDX12;
  GPUDeviceDX12          *device;
  ID3D12CommandList      *commandLists[1];
  D3D12_RESOURCE_BARRIER  barriers[2] = {0};
  D3D12_RESOURCE_STATES   copyState;
  UINT64                  fenceValue;
  HRESULT                 result;
  DWORD                   waitResult;
  GPUResult               gpuResult;
  bool                    commandListOpen;
  bool                    transition;

  queueDX12       = queue ? queue->_priv : NULL;
  device          = queue && queue->_device ? queue->_device->_priv : NULL;
  gpuResult       = GPU_ERROR_BACKEND_FAILURE;
  commandListOpen = false;
  if (!queueDX12 || !queueDX12->commandQueue || !device ||
      !device->d3dDevice || !native || !native->resource || !staging ||
      queueDX12->type == D3D12_COMMAND_LIST_TYPE_COPY) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!dx12__ensureTransferContext(queueDX12, device)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = queueDX12->transferAllocator->lpVtbl->Reset(
    queueDX12->transferAllocator
  );
  if (FAILED(result)) {
    goto cleanup;
  }
  result = queueDX12->transferCommandList->lpVtbl->Reset(
    queueDX12->transferCommandList,
    queueDX12->transferAllocator,
    NULL
  );
  if (FAILED(result)) {
    goto cleanup;
  }
  commandListOpen = true;

  copyState  = upload ? D3D12_RESOURCE_STATE_COPY_DEST
                      : D3D12_RESOURCE_STATE_COPY_SOURCE;
  transition = native->state != copyState;
  barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource   = native->resource;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[0].Transition.StateBefore = native->state;
  barriers[0].Transition.StateAfter  = copyState;
  barriers[1]                        = barriers[0];
  barriers[1].Transition.StateBefore = copyState;
  barriers[1].Transition.StateAfter  = native->state;
  if (transition) {
    queueDX12->transferCommandList->lpVtbl->ResourceBarrier(
      queueDX12->transferCommandList,
      1u,
      &barriers[0]
    );
  }
  if (upload) {
    queueDX12->transferCommandList->lpVtbl->CopyBufferRegion(
      queueDX12->transferCommandList,
      native->resource,
      nativeOffset,
      staging,
      0u,
      sizeBytes
    );
  } else {
    queueDX12->transferCommandList->lpVtbl->CopyBufferRegion(
      queueDX12->transferCommandList,
      staging,
      0u,
      native->resource,
      nativeOffset,
      sizeBytes
    );
  }
  if (transition) {
    queueDX12->transferCommandList->lpVtbl->ResourceBarrier(
      queueDX12->transferCommandList,
      1u,
      &barriers[1]
    );
  }
  result = queueDX12->transferCommandList->lpVtbl->Close(
    queueDX12->transferCommandList
  );
  if (FAILED(result)) {
    goto cleanup;
  }
  commandListOpen = false;

  commandLists[0] = (ID3D12CommandList *)queueDX12->transferCommandList;
  queueDX12->commandQueue->lpVtbl->ExecuteCommandLists(queueDX12->commandQueue,
                                                       1u,
                                                       commandLists);
  fenceValue = ++queueDX12->transferFenceValue;
  result = queueDX12->commandQueue->lpVtbl->Signal(queueDX12->commandQueue,
                                                   queueDX12->transferFence,
                                                   fenceValue);
  if (SUCCEEDED(result)) {
    result = queueDX12->transferFence->lpVtbl->SetEventOnCompletion(
      queueDX12->transferFence,
      fenceValue,
      queueDX12->transferEvent
    );
  }
  waitResult = SUCCEEDED(result)
                 ? WaitForSingleObject(queueDX12->transferEvent, INFINITE)
                 : WAIT_FAILED;
  if (SUCCEEDED(result) && waitResult == WAIT_OBJECT_0) {
    gpuResult = GPU_OK;
  }

cleanup:
  if (commandListOpen) {
    (void)queueDX12->transferCommandList->lpVtbl->Close(
      queueDX12->transferCommandList
    );
  }
  return gpuResult;
}

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
  bool                     storage;
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
  defaultHeap             = storage ||
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
  desc.Flags              = storage
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
dx12_writeBuffer(GPUCommandQueue * __restrict queue,
                 GPUBuffer       * __restrict buffer,
                 uint64_t                     dstOffset,
                 const void      * __restrict data,
                 uint64_t                     sizeBytes) {
  GPUCommandQueueDX12 *queueDX12;
  GPUDeviceDX12       *device;
  GPUBufferDX12       *native;

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

  device = queue->_device->_priv;
  if (!dx12__ensureTransferStaging(queueDX12, device, sizeBytes, true)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(queueDX12->uploadMapped, data, (size_t)sizeBytes);
  return dx12__copyBufferSync(queue,
                              native,
                              queueDX12->uploadStaging,
                              dstOffset,
                              sizeBytes,
                              true);
}

GPU_HIDE
GPUResult
dx12_readBuffer(GPUCommandQueue * __restrict queue,
                GPUBuffer       * __restrict buffer,
                uint64_t                     srcOffset,
                void           * __restrict outData,
                uint64_t                     sizeBytes) {
  GPUCommandQueueDX12 *queueDX12;
  GPUDeviceDX12       *device;
  GPUBufferDX12       *native;
  void                *mapped;
  D3D12_RANGE          readRange;
  GPUResult            result;

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

  device = queue->_device->_priv;
  if (!dx12__ensureTransferStaging(queueDX12, device, sizeBytes, false)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = dx12__copyBufferSync(queue,
                                native,
                                queueDX12->readbackStaging,
                                srcOffset,
                                sizeBytes,
                                false);
  if (result == GPU_OK) {
    readRange.Begin = 0u;
    readRange.End   = (SIZE_T)sizeBytes;
    mapped          = NULL;
    if (FAILED(queueDX12->readbackStaging->lpVtbl->Map(
          queueDX12->readbackStaging,
          0u,
          &readRange,
          &mapped
        )) ||
        !mapped) {
      result = GPU_ERROR_BACKEND_FAILURE;
    } else {
      memcpy(outData, mapped, (size_t)sizeBytes);
      queueDX12->readbackStaging->lpVtbl->Unmap(
        queueDX12->readbackStaging,
        0u,
        NULL
      );
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
