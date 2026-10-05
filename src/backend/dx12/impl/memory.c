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

static GPUResult
dx12_createHeap(GPUDevice               *device,
                const GPUHeapCreateInfo *info,
                GPUHeap                **outHeap) {
  D3D12_HEAP_DESC  desc = {0};
  GPUDeviceDX12   *deviceDX12;
  GPUHeap         *heap;
  GPUHeapDX12     *native;
  uint64_t         alignment;
  uint64_t         nativeSize;
  uint64_t         compatibility;
  D3D12_HEAP_FLAGS heapFlags;
  HRESULT          result;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outHeap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->usage == GPU_HEAP_USAGE_SPARSE
      && info->pageSizeBytes != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (deviceDX12->resourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2
      && (info->compatibilityMask & DX12_MEMORY_COMPAT_ALL) != 0u) {
    compatibility = DX12_MEMORY_COMPAT_ALL;
    heapFlags     = D3D12_HEAP_FLAG_NONE;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_BUFFER) {
    compatibility = DX12_MEMORY_COMPAT_BUFFER;
    heapFlags     = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_TEXTURE) {
    compatibility = DX12_MEMORY_COMPAT_TEXTURE;
    heapFlags     = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
  } else if (info->compatibilityMask == DX12_MEMORY_COMPAT_RT_DS) {
    compatibility = DX12_MEMORY_COMPAT_RT_DS;
    heapFlags     = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  } else {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

  if (info->sizeBytes > UINT64_MAX - (alignment - 1u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  nativeSize = (info->sizeBytes + alignment - 1u) & ~(alignment - 1u);

  if (!(heap = calloc(1, sizeof(*heap) + sizeof(*native)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native        = (GPUHeapDX12 *)(heap + 1);
  native->type  = D3D12_HEAP_TYPE_DEFAULT;
  native->flags = heapFlags;

  desc.SizeInBytes                 = nativeSize;
  desc.Properties.Type             = native->type;
  desc.Properties.CreationNodeMask = 1u;
  desc.Properties.VisibleNodeMask  = 1u;
  desc.Flags                       = native->flags;

  result = deviceDX12->d3dDevice->lpVtbl->CreateHeap(deviceDX12->d3dDevice,
                                                     &desc,
                                                     &IID_ID3D12Heap,
                                                     (void **)&native->heap);

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
  GPUQueueDX12                  *queue;
  ID3D12Fence                   *waitFence;
  const GPUSparseBufferMapping  *bufferMapping;
  GPUBufferDX12                 *buffer;
  GPUHeapDX12                   *bufferHeap;
  const GPUSparseTextureMapping *textureMapping;
  GPUTextureDX12                *texture;
  GPUHeapDX12                   *textureHeap;
  ID3D12Fence                   *signalFence;
  uint64_t                       tileCount64;
  HRESULT                        result;
  uint32_t                       waitIndex;
  uint32_t                       bufferIndex;
  uint32_t                       textureIndex;
  uint32_t                       signalIndex;

  queue = queueHandle ? queueHandle->_priv : NULL;

  if (!queue || !queue->commandQueue || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (dx12_flushTransfers(queueHandle) != GPU_OK) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (waitIndex = 0u; waitIndex < info->waitCount; waitIndex++) {
    waitFence = info->pWaits[waitIndex].semaphore->_priv;

    if (!waitFence) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    result = queue->commandQueue->lpVtbl->Wait(queue->commandQueue,
                                               waitFence,
                                               info->pWaits[waitIndex].value);

    if (FAILED(result)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (bufferIndex = 0u; bufferIndex < info->bufferMappingCount; bufferIndex++) {
    D3D12_TILED_RESOURCE_COORDINATE bufferCoordinate = {0};
    D3D12_TILE_REGION_SIZE          bufferRegion     = {0};
    D3D12_TILE_RANGE_FLAGS          bufferRangeFlag;
    UINT                            bufferHeapOffset;
    UINT                            bufferTileCount;

    bufferMapping = &info->pBufferMappings[bufferIndex];
    buffer        = bufferMapping->buffer->_priv;
    bufferHeap    = bufferMapping->heap->_priv;

    if (!buffer || !buffer->sparse || !buffer->resource
        || !bufferHeap || !bufferHeap->heap
        || bufferMapping->bufferTileOffset > UINT_MAX
        || bufferMapping->tileCount > UINT_MAX
        || (bufferMapping->mode == GPU_SPARSE_MAPPING_MAP
            && bufferMapping->heapTileOffset > UINT_MAX)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    bufferCoordinate.X    = (UINT)bufferMapping->bufferTileOffset;
    bufferRegion.UseBox   = FALSE;
    bufferRegion.NumTiles = (UINT)bufferMapping->tileCount;
    bufferRangeFlag       = bufferMapping->mode == GPU_SPARSE_MAPPING_MAP
                              ? D3D12_TILE_RANGE_FLAG_NONE
                              : D3D12_TILE_RANGE_FLAG_NULL;
    bufferHeapOffset      = bufferMapping->mode == GPU_SPARSE_MAPPING_MAP ? (UINT)bufferMapping->heapTileOffset : 0u;
    bufferTileCount       = (UINT)bufferMapping->tileCount;

    queue->commandQueue->lpVtbl->UpdateTileMappings(queue->commandQueue,
                                                    buffer->resource,
                                                    1u,
                                                    &bufferCoordinate,
                                                    &bufferRegion,
                                                    bufferMapping->mode == GPU_SPARSE_MAPPING_MAP ? bufferHeap->heap : NULL,
                                                    1u,
                                                    &bufferRangeFlag,
                                                    &bufferHeapOffset,
                                                    &bufferTileCount,
                                                    D3D12_TILE_MAPPING_FLAG_NONE);
  }

  for (textureIndex = 0u; textureIndex < info->textureMappingCount; textureIndex++) {
    D3D12_TILED_RESOURCE_COORDINATE textureCoordinate = {0};
    D3D12_TILE_REGION_SIZE          textureRegion     = {0};
    D3D12_TILE_RANGE_FLAGS          textureRangeFlag;
    UINT                            textureHeapOffset;
    UINT                            textureTileCount;

    textureMapping = &info->pTextureMappings[textureIndex];
    texture        = textureMapping->texture->_priv;
    textureHeap    = textureMapping->heap->_priv;

    if (!texture || !texture->sparse || !texture->resource || !textureHeap
        || !textureHeap->heap
        || (textureMapping->mode == GPU_SPARSE_MAPPING_MAP
            && textureMapping->heapTileOffset > UINT_MAX)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    textureCoordinate.Subresource = textureMapping->mipLevel +
                                    textureMapping->arrayLayer * texture->mipLevelCount;

    if (textureMapping->mipLevel == textureMapping->texture->_sparseRequirements.firstMipInTail) {
      textureRegion.UseBox   = FALSE;
      textureRegion.NumTiles = texture->packedMipInfo.NumTilesForPackedMips;
      tileCount64            = textureRegion.NumTiles;
    } else {
      textureCoordinate.X    = textureMapping->tileX;
      textureCoordinate.Y    = textureMapping->tileY;
      textureCoordinate.Z    = textureMapping->tileZ;
      textureRegion.UseBox   = TRUE;
      textureRegion.Width    = textureMapping->tileWidth;
      textureRegion.Height   = (UINT16)textureMapping->tileHeight;
      textureRegion.Depth    = (UINT16)textureMapping->tileDepth;
      tileCount64            = (uint64_t)textureMapping->tileWidth *
                               textureMapping->tileHeight * textureMapping->tileDepth;
      textureRegion.NumTiles = (UINT)tileCount64;
    }

    if (tileCount64 == 0u || tileCount64 > UINT_MAX) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    textureRangeFlag  = textureMapping->mode == GPU_SPARSE_MAPPING_MAP
                         ? D3D12_TILE_RANGE_FLAG_NONE
                         : D3D12_TILE_RANGE_FLAG_NULL;
    textureHeapOffset = textureMapping->mode == GPU_SPARSE_MAPPING_MAP ? (UINT)textureMapping->heapTileOffset : 0u;
    textureTileCount  = (UINT)tileCount64;

    queue->commandQueue->lpVtbl->UpdateTileMappings(queue->commandQueue,
                                                    texture->resource,
                                                    1u,
                                                    &textureCoordinate,
                                                    &textureRegion,
                                                    textureMapping->mode == GPU_SPARSE_MAPPING_MAP ? textureHeap->heap : NULL,
                                                    1u,
                                                    &textureRangeFlag,
                                                    &textureHeapOffset,
                                                    &textureTileCount,
                                                    D3D12_TILE_MAPPING_FLAG_NONE);
  }

  for (signalIndex = 0u; signalIndex < info->signalCount; signalIndex++) {
    signalFence = info->pSignals[signalIndex].semaphore->_priv;
    result      = signalFence ? queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                                    signalFence,
                                                                    info->pSignals[signalIndex].value)
                              : E_INVALIDARG;

    if (FAILED(result)) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  return GPU_OK;
}

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
