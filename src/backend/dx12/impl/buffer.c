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
  if ((usage & GPU_BUFFER_USAGE_STORAGE) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
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
  GPUResult                usageResult;
  uint64_t                 allocationSize;
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
  heap.Type               = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask   = 1u;
  heap.VisibleNodeMask    = 1u;
  desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width              = allocationSize;
  desc.Height             = 1u;
  desc.DepthOrArraySize   = 1u;
  desc.MipLevels          = 1u;
  desc.SampleDesc.Count   = 1u;
  desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(result) || !native->resource) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = native->resource->lpVtbl->Map(native->resource,
                                         0u,
                                         &readRange,
                                         &native->mapped);
  if (FAILED(result) || !native->mapped) {
    native->resource->lpVtbl->Release(native->resource);
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__setBufferName(native->resource, info->label);
  native->gpuAddress     = native->resource->lpVtbl->GetGPUVirtualAddress(
    native->resource
  );
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
  GPUBufferDX12 *native;

  native = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !native || !native->mapped || !data || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, dstOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memcpy((uint8_t *)native->mapped + dstOffset, data, (size_t)sizeBytes);
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_readBuffer(GPUCommandQueue * __restrict queue,
                GPUBuffer       * __restrict buffer,
                uint64_t                     srcOffset,
                void           * __restrict outData,
                uint64_t                     sizeBytes) {
  GPUBufferDX12 *native;

  native = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !native || !native->mapped || !outData || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, srcOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memcpy(outData, (const uint8_t *)native->mapped + srcOffset, (size_t)sizeBytes);
  return GPU_OK;
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
