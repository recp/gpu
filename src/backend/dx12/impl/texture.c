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
#include "../impl.h"

static void
dx12__setTextureName(ID3D12Resource *resource, const char *label) {
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

static bool
dx12__textureDimension(GPUTextureDimension dimension,
                       D3D12_RESOURCE_DIMENSION *outDimension) {
  if (!outDimension) {
    return false;
  }

  switch (dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
      return true;
    case GPU_TEXTURE_DIMENSION_2D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      return true;
    case GPU_TEXTURE_DIMENSION_3D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      return true;
    default:
      return false;
  }
}

static D3D12_RESOURCE_STATES
dx12__textureFinalState(GPUTextureUsageFlags usage) {
  if ((usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u) {
    return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  if ((usage & GPU_TEXTURE_USAGE_COPY_SRC) != 0u) {
    return D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  return D3D12_RESOURCE_STATE_COPY_DEST;
}

GPU_HIDE
GPUResult
dx12_createTexture(GPUDevice                  * __restrict device,
                   const GPUTextureCreateInfo * __restrict info,
                   GPUTexture                ** __restrict outTexture) {
  const GPUTextureUsageFlags unsupported = GPU_TEXTURE_USAGE_STORAGE |
                                           GPU_TEXTURE_USAGE_COLOR_TARGET |
                                           GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  GPUDeviceDX12           *deviceDX12;
  GPUTexture              *texture;
  GPUTextureDX12          *native;
  D3D12_HEAP_PROPERTIES    heap = {0};
  D3D12_RESOURCE_DESC      desc = {0};
  D3D12_RESOURCE_DIMENSION dimension;
  D3D12_RESOURCE_STATES    initialState;
  DXGI_FORMAT              format;
  HRESULT                  result;

  if (!device || !device->_priv || !info || !outTexture ||
      info->mipLevelCount == 0u || info->mipLevelCount > UINT16_MAX ||
      info->depthOrLayers > UINT16_MAX || info->sampleCount != 1u ||
      (info->usage & unsupported) != 0u ||
      !dx12__textureDimension(info->dimension, &dimension)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if ((info->dimension == GPU_TEXTURE_DIMENSION_1D && info->height != 1u) ||
      (info->dimension == GPU_TEXTURE_DIMENSION_3D &&
       info->depthOrLayers == 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outTexture = NULL;
  format = dx12_format(info->format);
  if (format == DXGI_FORMAT_UNKNOWN) {
    return GPU_ERROR_UNSUPPORTED;
  }

  texture = calloc(1, sizeof(*texture) + sizeof(*native));
  if (!texture) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  deviceDX12            = device->_priv;
  native                = (GPUTextureDX12 *)(texture + 1);
  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  desc.Dimension        = dimension;
  desc.Width            = info->width;
  desc.Height           = info->dimension == GPU_TEXTURE_DIMENSION_1D
                            ? 1u
                            : info->height;
  desc.DepthOrArraySize = (UINT16)info->depthOrLayers;
  desc.MipLevels        = (UINT16)info->mipLevelCount;
  desc.Format           = format;
  desc.SampleDesc.Count = 1u;
  desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  initialState = (info->usage & GPU_TEXTURE_USAGE_COPY_DST) != 0u
                   ? D3D12_RESOURCE_STATE_COPY_DEST
                   : D3D12_RESOURCE_STATE_COMMON;
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
    free(texture);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__setTextureName(native->resource, info->label);
  native->state          = initialState;
  texture->_priv         = native;
  texture->device        = device;
  texture->format        = info->format;
  texture->dimension     = info->dimension;
  texture->width         = info->width;
  texture->height        = info->height;
  texture->depthOrLayers = info->depthOrLayers;
  texture->mipLevelCount = info->mipLevelCount;
  texture->sampleCount   = info->sampleCount;
  texture->usage         = info->usage;
  texture->_ownsNative   = true;
  *outTexture            = texture;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyTexture(GPUTexture * __restrict texture) {
  GPUTextureDX12 *native;

  if (!texture) {
    return;
  }

  native = texture->_priv;
  if (native && native->resource) {
    native->resource->lpVtbl->Release(native->resource);
  }
  free(texture);
}

static bool
dx12__fillTextureSrv(const GPUTextureViewCreateInfo *info,
                     D3D12_SHADER_RESOURCE_VIEW_DESC *srv) {
  switch (info->viewType) {
    case GPU_TEXTURE_VIEW_1D:
      if (info->arrayLayerCount != 1u) {
        return false;
      }
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE1D;
      srv->Texture1D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture1D.MipLevels           = info->mipLevelCount;
      srv->Texture1D.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_2D:
      if (info->arrayLayerCount != 1u) {
        return false;
      }
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv->Texture2D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture2D.MipLevels           = info->mipLevelCount;
      srv->Texture2D.PlaneSlice          = 0u;
      srv->Texture2D.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      srv->ViewDimension                      =
        D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srv->Texture2DArray.MostDetailedMip     = info->baseMipLevel;
      srv->Texture2DArray.MipLevels           = info->mipLevelCount;
      srv->Texture2DArray.FirstArraySlice     = info->baseArrayLayer;
      srv->Texture2DArray.ArraySize           = info->arrayLayerCount;
      srv->Texture2DArray.PlaneSlice          = 0u;
      srv->Texture2DArray.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_CUBE:
      if (info->arrayLayerCount != 6u) {
        return false;
      }
      srv->ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srv->TextureCube.MostDetailedMip     = info->baseMipLevel;
      srv->TextureCube.MipLevels           = info->mipLevelCount;
      srv->TextureCube.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      if (info->baseArrayLayer % 6u != 0u ||
          info->arrayLayerCount % 6u != 0u) {
        return false;
      }
      srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      srv->TextureCubeArray.MostDetailedMip = info->baseMipLevel;
      srv->TextureCubeArray.MipLevels       = info->mipLevelCount;
      srv->TextureCubeArray.First2DArrayFace = info->baseArrayLayer;
      srv->TextureCubeArray.NumCubes        = info->arrayLayerCount / 6u;
      srv->TextureCubeArray.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_3D:
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE3D;
      srv->Texture3D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture3D.MipLevels           = info->mipLevelCount;
      srv->Texture3D.ResourceMinLODClamp = 0.0f;
      return true;
    default:
      return false;
  }
}

GPU_HIDE
GPUResult
dx12_createTextureView(GPUTexture                     * __restrict texture,
                       const GPUTextureViewCreateInfo * __restrict info,
                       GPUTextureView                ** __restrict outView) {
  GPUTextureDX12     *textureDX12;
  GPUTextureView     *view;
  GPUTextureViewDX12 *native;
  DXGI_FORMAT         format;

  textureDX12 = texture ? texture->_priv : NULL;
  if (!textureDX12 || !textureDX12->resource || !info || !outView ||
      info->format != texture->format ||
      (texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->viewType == GPU_TEXTURE_VIEW_1D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_1D ||
        texture->depthOrLayers != 1u || info->baseArrayLayer != 0u)) ||
      (info->viewType == GPU_TEXTURE_VIEW_2D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_2D ||
        texture->depthOrLayers != 1u || info->baseArrayLayer != 0u)) ||
      ((info->viewType == GPU_TEXTURE_VIEW_2D_ARRAY ||
        info->viewType == GPU_TEXTURE_VIEW_CUBE ||
        info->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY) &&
       texture->dimension != GPU_TEXTURE_DIMENSION_2D) ||
      (info->viewType == GPU_TEXTURE_VIEW_CUBE &&
       info->baseArrayLayer != 0u) ||
      (info->viewType == GPU_TEXTURE_VIEW_3D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_3D ||
        info->baseArrayLayer != 0u || info->arrayLayerCount != 1u))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outView = NULL;
  format = dx12_format(info->format);
  if (format == DXGI_FORMAT_UNKNOWN) {
    return GPU_ERROR_UNSUPPORTED;
  }

  view = calloc(1, sizeof(*view) + sizeof(*native));
  if (!view) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native = (GPUTextureViewDX12 *)(view + 1);
  native->srv.Format                  = format;
  native->srv.Shader4ComponentMapping =
    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (!dx12__fillTextureSrv(info, &native->srv)) {
    free(view);
    return GPU_ERROR_UNSUPPORTED;
  }

  native->resource = textureDX12->resource;
  native->state    = &textureDX12->state;
  native->width    = texture->width;
  native->height   = texture->height;
  native->hasSrv   = true;
  view->_priv      = native;
  view->_ownsNative = true;
  *outView         = view;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyTextureView(GPUTextureView * __restrict view) {
  free(view);
}

static uint32_t
dx12__formatBytes(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UNORM_SRGB:
    case GPU_FORMAT_BGRA8_UNORM:
    case GPU_FORMAT_BGRA8_UNORM_SRGB:
      return 4u;
    case GPU_FORMAT_RGBA16_FLOAT:
      return 8u;
    case GPU_FORMAT_RGBA32_FLOAT:
      return 16u;
    default:
      return 0u;
  }
}

static void
dx12__transitionTexture(ID3D12GraphicsCommandList *commandList,
                        ID3D12Resource            *resource,
                        D3D12_RESOURCE_STATES      before,
                        D3D12_RESOURCE_STATES      after) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (before == after) {
    return;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter  = after;
  commandList->lpVtbl->ResourceBarrier(commandList, 1u, &barrier);
}

GPU_HIDE
GPUResult
dx12_writeTexture(GPUCommandQueue             * __restrict queue,
                   GPUTexture                  * __restrict texture,
                   const GPUTextureWriteRegion * __restrict region,
                   const void                  * __restrict data,
                   uint64_t                                 sizeBytes) {
  GPUCommandQueueDX12       *queueDX12;
  GPUDeviceDX12             *deviceDX12;
  GPUTextureDX12            *native;
  ID3D12CommandAllocator    *allocator;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *upload;
  ID3D12Fence               *fence;
  ID3D12CommandList         *commandLists[1];
  D3D12_HEAP_PROPERTIES      heap = {0};
  D3D12_RESOURCE_DESC        uploadDesc = {0};
  D3D12_RESOURCE_DESC        textureDesc;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {0};
  D3D12_TEXTURE_COPY_LOCATION source = {0};
  D3D12_TEXTURE_COPY_LOCATION destination = {0};
  D3D12_BOX                   sourceBox = {0};
  D3D12_RANGE                 readRange = {0};
  D3D12_RESOURCE_STATES       finalState;
  uint8_t                    *mapped;
  HANDLE                      event;
  uint64_t                    rowSize;
  uint64_t                    totalSize;
  uint64_t                    sourceRowBytes;
  uint32_t                    rowCount;
  uint32_t                    formatBytes;
  uint32_t                    subresource;
  HRESULT                     result;
  DWORD                       waitResult;
  GPUResult                   gpuResult;

  queueDX12  = queue ? queue->_priv : NULL;
  deviceDX12 = queue && queue->_device ? queue->_device->_priv : NULL;
  native     = texture ? texture->_priv : NULL;
  if (!queueDX12 || !queueDX12->commandQueue || !deviceDX12 || !native ||
      !native->resource ||
      !region || !data || sizeBytes > SIZE_MAX ||
      queueDX12->type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
      texture->dimension != GPU_TEXTURE_DIMENSION_2D ||
      region->depth != 1u || region->layerCount != 1u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  formatBytes = dx12__formatBytes(texture->format);
  if (formatBytes == 0u ||
      region->width > UINT64_MAX / formatBytes) {
    return GPU_ERROR_UNSUPPORTED;
  }
  sourceRowBytes = (uint64_t)region->width * formatBytes;
  if (region->bytesPerRow < sourceRowBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  allocator   = NULL;
  commandList = NULL;
  upload      = NULL;
  fence       = NULL;
  mapped      = NULL;
  event       = NULL;
  gpuResult   = GPU_ERROR_BACKEND_FAILURE;
  native->resource->lpVtbl->GetDesc(native->resource, &textureDesc);
  subresource = region->mipLevel +
                region->baseArrayLayer * texture->mipLevelCount;
  deviceDX12->d3dDevice->lpVtbl->GetCopyableFootprints(
    deviceDX12->d3dDevice,
    &textureDesc,
    subresource,
    1u,
    0u,
    &footprint,
    &rowCount,
    &rowSize,
    &totalSize
  );
  if (totalSize == 0u || totalSize > SIZE_MAX ||
      rowCount < region->height ||
      rowSize < sourceRowBytes) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  heap.Type                  = D3D12_HEAP_TYPE_UPLOAD;
  heap.CreationNodeMask      = 1u;
  heap.VisibleNodeMask       = 1u;
  uploadDesc.Dimension       = D3D12_RESOURCE_DIMENSION_BUFFER;
  uploadDesc.Width           = totalSize;
  uploadDesc.Height          = 1u;
  uploadDesc.DepthOrArraySize = 1u;
  uploadDesc.MipLevels       = 1u;
  uploadDesc.SampleDesc.Count = 1u;
  uploadDesc.Layout          = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &uploadDesc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    NULL,
    &IID_ID3D12Resource,
    (void **)&upload
  );
  if (FAILED(result)) {
    goto cleanup;
  }

  result = upload->lpVtbl->Map(upload, 0u, &readRange, (void **)&mapped);
  if (FAILED(result) || !mapped) {
    goto cleanup;
  }
  for (uint32_t row = 0u; row < region->height; row++) {
    memcpy(mapped + (size_t)footprint.Offset +
             (size_t)row * footprint.Footprint.RowPitch,
           (const uint8_t *)data + (size_t)row * region->bytesPerRow,
           (size_t)sourceRowBytes);
  }
  upload->lpVtbl->Unmap(upload, 0u, NULL);
  mapped = NULL;

  result = deviceDX12->d3dDevice->lpVtbl->CreateCommandAllocator(
    deviceDX12->d3dDevice,
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    &IID_ID3D12CommandAllocator,
    (void **)&allocator
  );
  if (FAILED(result)) {
    goto cleanup;
  }
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommandList(
    deviceDX12->d3dDevice,
    0u,
    D3D12_COMMAND_LIST_TYPE_DIRECT,
    allocator,
    NULL,
    &IID_ID3D12GraphicsCommandList,
    (void **)&commandList
  );
  if (FAILED(result)) {
    goto cleanup;
  }

  dx12__transitionTexture(commandList,
                          native->resource,
                          native->state,
                          D3D12_RESOURCE_STATE_COPY_DEST);
  source.pResource             = upload;
  source.Type                  = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint       = footprint;
  destination.pResource       = native->resource;
  destination.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = subresource;
  sourceBox.right             = region->width;
  sourceBox.bottom            = region->height;
  sourceBox.back              = 1u;
  commandList->lpVtbl->CopyTextureRegion(commandList,
                                         &destination,
                                         0u,
                                         0u,
                                         0u,
                                         &source,
                                         &sourceBox);
  finalState = dx12__textureFinalState(texture->usage);
  dx12__transitionTexture(commandList,
                          native->resource,
                          D3D12_RESOURCE_STATE_COPY_DEST,
                          finalState);
  result = commandList->lpVtbl->Close(commandList);
  if (FAILED(result)) {
    goto cleanup;
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateFence(
    deviceDX12->d3dDevice,
    0u,
    D3D12_FENCE_FLAG_NONE,
    &IID_ID3D12Fence,
    (void **)&fence
  );
  if (FAILED(result)) {
    goto cleanup;
  }
  event = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!event) {
    goto cleanup;
  }

  commandLists[0] = (ID3D12CommandList *)commandList;
  queueDX12->commandQueue->lpVtbl->ExecuteCommandLists(queueDX12->commandQueue,
                                                       1u,
                                                       commandLists);
  native->state = finalState;
  result = queueDX12->commandQueue->lpVtbl->Signal(queueDX12->commandQueue,
                                                   fence,
                                                   1u);
  if (SUCCEEDED(result)) {
    result = fence->lpVtbl->SetEventOnCompletion(fence, 1u, event);
  }
  waitResult = SUCCEEDED(result)
                 ? WaitForSingleObject(event, INFINITE)
                 : WAIT_FAILED;
  if (SUCCEEDED(result) && waitResult == WAIT_OBJECT_0) {
    gpuResult = GPU_OK;
  }

cleanup:
  if (mapped && upload) {
    upload->lpVtbl->Unmap(upload, 0u, NULL);
  }
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
  if (upload) {
    upload->lpVtbl->Release(upload);
  }
  return gpuResult;
}

GPU_HIDE
void
dx12_initTexture(GPUApiTexture *api) {
  api->create      = dx12_createTexture;
  api->destroy     = dx12_destroyTexture;
  api->createView  = dx12_createTextureView;
  api->destroyView = dx12_destroyTextureView;
  api->write       = dx12_writeTexture;
}
