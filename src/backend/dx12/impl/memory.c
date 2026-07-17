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

#define DX12_MEMORY_COMPAT_ALL      UINT64_C(1)
#define DX12_MEMORY_COMPAT_BUFFER   UINT64_C(2)
#define DX12_MEMORY_COMPAT_TEXTURE  UINT64_C(4)
#define DX12_MEMORY_COMPAT_RT_DS    UINT64_C(8)

GPU_HIDE
uint64_t
dx12_memoryCompatibility(GPUDevice                 *device,
                         const D3D12_RESOURCE_DESC *desc) {
  GPUDeviceDX12 *deviceDX12;

  if (!device || !(deviceDX12 = device->_priv) || !desc) {
    return 0u;
  }
  if (deviceDX12->resourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2) {
    return DX12_MEMORY_COMPAT_ALL;
  }
  if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
    return DX12_MEMORY_COMPAT_BUFFER;
  }
  if ((desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0u) {
    return DX12_MEMORY_COMPAT_RT_DS;
  }
  return DX12_MEMORY_COMPAT_TEXTURE;
}

static GPUResult
dx12_createHeap(GPUDevice               *device,
                const GPUHeapCreateInfo *info,
                GPUHeap                **outHeap) {
  GPUDeviceDX12  *deviceDX12;
  GPUHeap        *heap;
  GPUHeapDX12    *native;
  D3D12_HEAP_DESC desc = {0};
  D3D12_HEAP_FLAGS heapFlags;
  uint64_t        alignment;
  uint64_t        nativeSize;
  uint64_t        compatibility;
  HRESULT         result;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outHeap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->usage == GPU_HEAP_USAGE_SPARSE &&
      info->pageSizeBytes != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (deviceDX12->resourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2 &&
      (info->compatibilityMask & DX12_MEMORY_COMPAT_ALL) != 0u) {
    compatibility = DX12_MEMORY_COMPAT_ALL;
    heapFlags      = D3D12_HEAP_FLAG_NONE;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_BUFFER) {
    compatibility = DX12_MEMORY_COMPAT_BUFFER;
    heapFlags      = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_TEXTURE) {
    compatibility = DX12_MEMORY_COMPAT_TEXTURE;
    heapFlags      = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_RT_DS) {
    compatibility = DX12_MEMORY_COMPAT_RT_DS;
    heapFlags      = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  } else {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  if (info->sizeBytes > UINT64_MAX - (alignment - 1u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  nativeSize = (info->sizeBytes + alignment - 1u) & ~(alignment - 1u);

  heap = calloc(1, sizeof(*heap) + sizeof(*native));
  if (!heap) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native        = (GPUHeapDX12 *)(heap + 1);
  native->type  = D3D12_HEAP_TYPE_DEFAULT;
  native->flags = heapFlags;

  desc.SizeInBytes                     = nativeSize;
  desc.Properties.Type                 = native->type;
  desc.Properties.CreationNodeMask     = 1u;
  desc.Properties.VisibleNodeMask      = 1u;
  desc.Flags                           = native->flags;
  result = deviceDX12->d3dDevice->lpVtbl->CreateHeap(
    deviceDX12->d3dDevice,
    &desc,
    &IID_ID3D12Heap,
    (void **)&native->heap
  );
  if (FAILED(result) || !native->heap) {
    free(heap);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  heap->_priv             = native;
  heap->device            = device;
  heap->compatibilityMask = compatibility;
  *outHeap                = heap;
  return GPU_OK;
}

static void
dx12_destroyHeap(GPUHeap *heap) {
  GPUHeapDX12 *native;

  if (!heap) {
    return;
  }
  native = heap->_priv;
  if (native && native->heap) {
    native->heap->lpVtbl->Release(native->heap);
  }
  free(heap);
}

static GPUResult
dx12_submitSparse(GPUQueue                       *queueHandle,
                  const GPUQueueSparseSubmitInfo *info) {
  GPUQueueDX12 *queue;
  HRESULT       result;

  queue = queueHandle ? queueHandle->_priv : NULL;
  if (!queue || !queue->commandQueue || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (dx12_flushTransfers(queueHandle) != GPU_OK) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t i = 0u; i < info->waitCount; i++) {
    ID3D12Fence *fence;

    fence = info->pWaits[i].semaphore->_priv;
    if (!fence) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    result = queue->commandQueue->lpVtbl->Wait(queue->commandQueue,
                                               fence,
                                               info->pWaits[i].value);
    if (FAILED(result)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (uint32_t i = 0u; i < info->bufferMappingCount; i++) {
    const GPUSparseBufferMapping *mapping;
    GPUBufferDX12                *buffer;
    GPUHeapDX12                  *heap;
    D3D12_TILED_RESOURCE_COORDINATE coordinate = {0};
    D3D12_TILE_REGION_SIZE          region = {0};
    D3D12_TILE_RANGE_FLAGS          rangeFlag;
    UINT                            heapOffset;
    UINT                            tileCount;

    mapping = &info->pBufferMappings[i];
    buffer  = mapping->buffer->_priv;
    heap    = mapping->heap->_priv;
    if (!buffer || !buffer->sparse || !buffer->resource ||
        !heap || !heap->heap ||
        mapping->bufferTileOffset > UINT_MAX ||
        mapping->tileCount > UINT_MAX ||
        (mapping->mode == GPU_SPARSE_MAPPING_MAP &&
         mapping->heapTileOffset > UINT_MAX)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    coordinate.X = (UINT)mapping->bufferTileOffset;
    region.UseBox = FALSE;
    region.NumTiles = (UINT)mapping->tileCount;
    rangeFlag = mapping->mode == GPU_SPARSE_MAPPING_MAP
                  ? D3D12_TILE_RANGE_FLAG_NONE
                  : D3D12_TILE_RANGE_FLAG_NULL;
    heapOffset = mapping->mode == GPU_SPARSE_MAPPING_MAP
                   ? (UINT)mapping->heapTileOffset
                   : 0u;
    tileCount = (UINT)mapping->tileCount;
    queue->commandQueue->lpVtbl->UpdateTileMappings(
      queue->commandQueue,
      buffer->resource,
      1u,
      &coordinate,
      &region,
      mapping->mode == GPU_SPARSE_MAPPING_MAP ? heap->heap : NULL,
      1u,
      &rangeFlag,
      &heapOffset,
      &tileCount,
      D3D12_TILE_MAPPING_FLAG_NONE
    );
  }

  for (uint32_t i = 0u; i < info->textureMappingCount; i++) {
    const GPUSparseTextureMapping *mapping;
    GPUTextureDX12                *texture;
    GPUHeapDX12                   *heap;
    D3D12_TILED_RESOURCE_COORDINATE coordinate = {0};
    D3D12_TILE_REGION_SIZE          region = {0};
    D3D12_TILE_RANGE_FLAGS          rangeFlag;
    UINT                            heapOffset;
    UINT                            tileCount;
    uint64_t                        tileCount64;

    mapping = &info->pTextureMappings[i];
    texture = mapping->texture->_priv;
    heap    = mapping->heap->_priv;
    if (!texture || !texture->sparse || !texture->resource || !heap ||
        !heap->heap ||
        (mapping->mode == GPU_SPARSE_MAPPING_MAP &&
         mapping->heapTileOffset > UINT_MAX)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    coordinate.Subresource = mapping->mipLevel +
                             mapping->arrayLayer *
                               texture->mipLevelCount;
    if (mapping->mipLevel ==
        mapping->texture->_sparseRequirements.firstMipInTail) {
      region.UseBox  = FALSE;
      region.NumTiles = texture->packedMipInfo.NumTilesForPackedMips;
      tileCount64     = region.NumTiles;
    } else {
      coordinate.X     = mapping->tileX;
      coordinate.Y     = mapping->tileY;
      coordinate.Z     = mapping->tileZ;
      region.UseBox    = TRUE;
      region.Width     = mapping->tileWidth;
      region.Height    = (UINT16)mapping->tileHeight;
      region.Depth     = (UINT16)mapping->tileDepth;
      tileCount64      = (uint64_t)mapping->tileWidth *
                         mapping->tileHeight * mapping->tileDepth;
    }
    if (tileCount64 == 0u || tileCount64 > UINT_MAX) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    rangeFlag = mapping->mode == GPU_SPARSE_MAPPING_MAP
                  ? D3D12_TILE_RANGE_FLAG_NONE
                  : D3D12_TILE_RANGE_FLAG_NULL;
    heapOffset = mapping->mode == GPU_SPARSE_MAPPING_MAP
                   ? (UINT)mapping->heapTileOffset
                   : 0u;
    tileCount = (UINT)tileCount64;
    queue->commandQueue->lpVtbl->UpdateTileMappings(
      queue->commandQueue,
      texture->resource,
      1u,
      &coordinate,
      &region,
      mapping->mode == GPU_SPARSE_MAPPING_MAP ? heap->heap : NULL,
      1u,
      &rangeFlag,
      &heapOffset,
      &tileCount,
      D3D12_TILE_MAPPING_FLAG_NONE
    );
  }

  for (uint32_t i = 0u; i < info->signalCount; i++) {
    ID3D12Fence *fence;

    fence = info->pSignals[i].semaphore->_priv;
    result = fence
               ? queue->commandQueue->lpVtbl->Signal(
                   queue->commandQueue,
                   fence,
                   info->pSignals[i].value
                 )
               : E_INVALIDARG;
    if (FAILED(result)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  return GPU_OK;
}

GPU_HIDE
void
dx12_initMemory(GPUApiMemory *api) {
  api->getBufferRequirements        = dx12_getBufferMemoryRequirements;
  api->getTextureRequirements       = dx12_getTextureMemoryRequirements;
  api->getSparseBufferRequirements  = dx12_getSparseBufferRequirements;
  api->getSparseTextureRequirements = dx12_getSparseTextureRequirements;
  api->createHeap                   = dx12_createHeap;
  api->destroyHeap                  = dx12_destroyHeap;
  api->createPlacedBuffer           = dx12_createPlacedBuffer;
  api->createPlacedTexture          = dx12_createPlacedTexture;
  api->createSparseBuffer           = dx12_createSparseBuffer;
  api->createSparseTexture          = dx12_createSparseTexture;
  api->submitSparse                 = dx12_submitSparse;
}
