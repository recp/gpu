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
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "device_internal.h"
#include "memory_internal.h"
#include "texture_internal.h"

static bool
gpuValidMemoryRequirements(const GPUMemoryRequirements *requirements) {
  return requirements &&
         requirements->sizeBytes > 0u &&
         requirements->alignmentBytes > 0u &&
         (requirements->alignmentBytes &
          (requirements->alignmentBytes - 1u)) == 0u &&
         requirements->compatibilityMask != 0u;
}

static bool
gpuValidSparseTextureRequirements(
  const GPUTextureCreateInfo       *info,
  const GPUSparseTextureRequirements *requirements
) {
  uint32_t mipLevelCount;

  if (!info || !requirements ||
      requirements->compatibilityMask == 0u ||
      requirements->pageSizeBytes == 0u ||
      (requirements->pageSizeBytes &
       (requirements->pageSizeBytes - 1u)) != 0u ||
      requirements->tileWidth == 0u ||
      requirements->tileHeight == 0u ||
      requirements->tileDepth == 0u) {
    return false;
  }

  mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  if (requirements->firstMipInTail > mipLevelCount ||
      (requirements->firstMipInTail < mipLevelCount &&
       requirements->mipTailTileCount == 0u) ||
      (requirements->firstMipInTail == mipLevelCount &&
       requirements->mipTailTileCount != 0u)) {
    return false;
  }
  return true;
}

static bool
gpuValidSparseBufferRequirements(
  const GPUBufferCreateInfo         *info,
  const GPUSparseBufferRequirements *requirements
) {
  uint64_t minimumTileCount;

  if (!info || !requirements ||
      requirements->compatibilityMask == 0u ||
      requirements->pageSizeBytes == 0u ||
      (requirements->pageSizeBytes &
       (requirements->pageSizeBytes - 1u)) != 0u ||
      requirements->tileCount == 0u ||
      requirements->tileCount >
        UINT64_MAX / requirements->pageSizeBytes) {
    return false;
  }

  minimumTileCount = info->sizeBytes / requirements->pageSizeBytes +
                     (info->sizeBytes % requirements->pageSizeBytes != 0u);
  return requirements->tileCount >= minimumTileCount;
}

static uint32_t
gpuSparseMipExtent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel < 32u ? mipLevel : 31u;
  return extent ? extent : 1u;
}

static uint32_t
gpuDivideRoundUpU32(uint32_t value, uint32_t divisor) {
  return value / divisor + (value % divisor != 0u);
}

static bool
gpuSparseBufferMappingValid(const GPUQueue               *queue,
                            const GPUSparseBufferMapping *mapping) {
  const GPUSparseBufferRequirements *requirements;
  GPUBuffer                         *buffer;
  uint64_t                           heapTileCount;

  buffer = mapping ? mapping->buffer : NULL;
  if (!queue || !mapping || !buffer || !mapping->heap ||
      buffer->device != queue->_device ||
      mapping->heap->device != queue->_device ||
      !buffer->_sparse || buffer->_heap != mapping->heap ||
      mapping->heap->usage != GPU_HEAP_USAGE_SPARSE ||
      (mapping->mode != GPU_SPARSE_MAPPING_MAP &&
       mapping->mode != GPU_SPARSE_MAPPING_UNMAP) ||
      mapping->tileCount == 0u ||
      mapping->heapTileOffset == GPU_SPARSE_HEAP_TILE_AUTO) {
    return false;
  }

  requirements = &buffer->_sparseRequirements;
  if (mapping->bufferTileOffset > requirements->tileCount ||
      mapping->tileCount >
        requirements->tileCount - mapping->bufferTileOffset) {
    return false;
  }
  if (mapping->mode == GPU_SPARSE_MAPPING_UNMAP) {
    return true;
  }

  heapTileCount = mapping->heap->sizeBytes / mapping->heap->pageSizeBytes;
  return mapping->heapTileOffset <= heapTileCount &&
         mapping->tileCount <= heapTileCount - mapping->heapTileOffset;
}

static bool
gpuSparseTextureMappingValid(const GPUQueue                *queue,
                             const GPUSparseTextureMapping *mapping) {
  const GPUSparseTextureRequirements *requirements;
  GPUTexture                          *texture;
  uint64_t                             heapTileCount;
  uint64_t                             mappingTileCount;
  uint32_t                             tileCountX;
  uint32_t                             tileCountY;
  uint32_t                             tileCountZ;

  texture = mapping ? mapping->texture : NULL;
  if (!queue || !mapping || !texture || !mapping->heap ||
      texture->device != queue->_device ||
      mapping->heap->device != queue->_device ||
      !texture->_sparse || texture->_heap != mapping->heap ||
      mapping->heap->usage != GPU_HEAP_USAGE_SPARSE ||
      (mapping->mode != GPU_SPARSE_MAPPING_MAP &&
       mapping->mode != GPU_SPARSE_MAPPING_UNMAP) ||
      mapping->tileWidth == 0u || mapping->tileHeight == 0u ||
      mapping->tileDepth == 0u ||
      mapping->mipLevel >= texture->mipLevelCount ||
      mapping->arrayLayer >= gpuTextureArrayLayerCount(texture)) {
    return false;
  }

  requirements = &texture->_sparseRequirements;
  if (mapping->mipLevel > requirements->firstMipInTail) {
    return false;
  }
  if (mapping->mipLevel == requirements->firstMipInTail) {
    if (requirements->firstMipInTail >= texture->mipLevelCount ||
        (requirements->mipTailLayerStrideTiles == 0u &&
         mapping->arrayLayer != 0u) ||
        mapping->tileX != 0u || mapping->tileY != 0u ||
        mapping->tileZ != 0u ||
        mapping->tileWidth != requirements->mipTailTileCount ||
        mapping->tileHeight != 1u || mapping->tileDepth != 1u) {
      return false;
    }
  } else {
    tileCountX = gpuDivideRoundUpU32(
      gpuSparseMipExtent(texture->width, mapping->mipLevel),
      requirements->tileWidth
    );
    tileCountY = gpuDivideRoundUpU32(
      gpuSparseMipExtent(texture->height, mapping->mipLevel),
      requirements->tileHeight
    );
    tileCountZ = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                   ? gpuDivideRoundUpU32(
                       gpuSparseMipExtent(texture->depthOrLayers,
                                          mapping->mipLevel),
                       requirements->tileDepth
                     )
                   : 1u;
    if (mapping->tileX >= tileCountX ||
        mapping->tileWidth > tileCountX - mapping->tileX ||
        mapping->tileY >= tileCountY ||
        mapping->tileHeight > tileCountY - mapping->tileY ||
        mapping->tileZ >= tileCountZ ||
        mapping->tileDepth > tileCountZ - mapping->tileZ) {
      return false;
    }
  }

  if (mapping->mode == GPU_SPARSE_MAPPING_UNMAP) {
    return true;
  }
  if (GPUIsFeatureEnabled(queue->_device,
                          GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)) {
    if (mapping->heapTileOffset == GPU_SPARSE_HEAP_TILE_AUTO) {
      return false;
    }
  } else if (mapping->heapTileOffset != GPU_SPARSE_HEAP_TILE_AUTO) {
    return false;
  }
  if (mapping->heapTileOffset == GPU_SPARSE_HEAP_TILE_AUTO) {
    return true;
  }
  if (mapping->tileWidth > UINT64_MAX / mapping->tileHeight ||
      (uint64_t)mapping->tileWidth * mapping->tileHeight >
        UINT64_MAX / mapping->tileDepth) {
    return false;
  }
  mappingTileCount = (uint64_t)mapping->tileWidth *
                     mapping->tileHeight * mapping->tileDepth;
  heapTileCount = mapping->heap->sizeBytes / mapping->heap->pageSizeBytes;
  return mapping->heapTileOffset <= heapTileCount &&
         mappingTileCount <= heapTileCount - mapping->heapTileOffset;
}

static GPUResult
gpuSubmitSparseFenceMarker(GPUQueue *queue, GPUFence *fence) {
  GPUCommandBuffer  *commandBuffer;
  GPUQueueSubmitInfo submitInfo = {0};
  GPUResult          result;

  commandBuffer = NULL;
  result = GPUAcquireCommandBuffer(queue,
                                   "sparse completion",
                                   &commandBuffer);
  if (result != GPU_OK) {
    return result;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &commandBuffer;
  submitInfo.fence              = fence;
  submitInfo.commandBufferCount = 1u;
  return GPUQueueSubmit(queue, &submitInfo);
}

GPU_EXPORT
GPUResult
GPUGetBufferMemoryRequirements(GPUDevice                 * __restrict device,
                               const GPUBufferCreateInfo * __restrict info,
                               GPUMemoryRequirements     * __restrict outRequirements) {
  GPUApi    *api;
  GPUResult  result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateBufferCreateInfo(device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.getBufferRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.getBufferRequirements(device,
                                              info,
                                              outRequirements);
  if (result != GPU_OK) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result;
  }
  if (!gpuValidMemoryRequirements(outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetTextureMemoryRequirements(GPUDevice                  * __restrict device,
                                const GPUTextureCreateInfo * __restrict info,
                                GPUMemoryRequirements      * __restrict outRequirements) {
  GPUApi    *api;
  GPUResult  result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateTextureCreateInfo(device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.getTextureRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.getTextureRequirements(device,
                                               info,
                                               outRequirements);
  if (result != GPU_OK) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result;
  }
  if (!gpuValidMemoryRequirements(outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetSparseBufferRequirements(GPUDevice                   * __restrict device,
                               const GPUBufferCreateInfo   * __restrict info,
                               GPUSparseBufferRequirements * __restrict outRequirements) {
  GPUApi    *api;
  GPUResult  result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateBufferCreateInfo(device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_SPARSE_BUFFERS) ||
      !GPUIsFeatureEnabled(device,
                           GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.getSparseBufferRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.getSparseBufferRequirements(device,
                                                    info,
                                                    outRequirements);
  if (result != GPU_OK) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result;
  }
  if (!gpuValidSparseBufferRequirements(info, outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetSparseTextureRequirements(GPUDevice                    * __restrict device,
                                const GPUTextureCreateInfo   * __restrict info,
                                GPUSparseTextureRequirements * __restrict outRequirements) {
  GPUApi    *api;
  GPUResult  result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateTextureCreateInfo(device, info);
  if (result != GPU_OK) {
    return result;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_SPARSE_TEXTURES)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.getSparseTextureRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.getSparseTextureRequirements(device,
                                                     info,
                                                     outRequirements);
  if (result != GPU_OK) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result;
  }
  if (!gpuValidSparseTextureRequirements(info, outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateHeap(GPUDevice               * __restrict device,
              const GPUHeapCreateInfo * __restrict info,
              GPUHeap                ** __restrict outHeap) {
  GPUApi    *api;
  GPUResult  result;

  if (!outHeap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outHeap = NULL;

  if (!device || !info || info->sizeBytes == 0u ||
      info->compatibilityMask == 0u ||
      (info->usage != GPU_HEAP_USAGE_PLACED &&
       info->usage != GPU_HEAP_USAGE_SPARSE) ||
      (info->usage == GPU_HEAP_USAGE_SPARSE &&
       (info->pageSizeBytes == 0u ||
        (info->pageSizeBytes & (info->pageSizeBytes - 1u)) != 0u ||
        info->sizeBytes % info->pageSizeBytes != 0u)) ||
      (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
       info->chain.sType != GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO) ||
      (info->chain.structSize != 0u &&
       info->chain.structSize < sizeof(*info))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->usage == GPU_HEAP_USAGE_PLACED &&
       !GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)) ||
      (info->usage == GPU_HEAP_USAGE_SPARSE &&
       !GPUIsFeatureEnabled(device, GPU_FEATURE_SPARSE_TEXTURES) &&
       !GPUIsFeatureEnabled(device, GPU_FEATURE_SPARSE_BUFFERS))) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.createHeap) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.createHeap(device, info, outHeap);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outHeap) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if ((*outHeap)->device != device ||
      (*outHeap)->compatibilityMask == 0u ||
      ((*outHeap)->compatibilityMask & info->compatibilityMask) == 0u) {
    if (api->memory.destroyHeap) {
      api->memory.destroyHeap(*outHeap);
    }
    *outHeap = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outHeap)->sizeBytes     = info->sizeBytes;
  (*outHeap)->pageSizeBytes = info->pageSizeBytes;
  (*outHeap)->usage         = info->usage;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyHeap(GPUHeap * __restrict heap) {
  GPUApi *api;

  if (!heap || !(api = gpuDeviceApi(heap->device))) {
    return;
  }
  if (api->memory.destroyHeap) {
    api->memory.destroyHeap(heap);
  }
}

GPU_EXPORT
GPUResult
GPUCreatePlacedBuffer(GPUDevice                 * __restrict device,
                      const GPUBufferCreateInfo * __restrict info,
                      GPUHeap                   * __restrict heap,
                      uint64_t                               heapOffset,
                      GPUBuffer                ** __restrict outBuffer) {
  GPUMemoryRequirements requirements;
  GPUApi              *api;
  GPUResult            result;

  if (!outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;
  if (!heap || heap->device != device ||
      heap->usage != GPU_HEAP_USAGE_PLACED) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUGetBufferMemoryRequirements(device,
                                          info,
                                          &requirements);
  if (result != GPU_OK) {
    return result;
  }
  if (!gpuHeapRangeValid(heap, &requirements, heapOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.createPlacedBuffer) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.createPlacedBuffer(device,
                                          info,
                                          heap,
                                          heapOffset,
                                          outBuffer);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outBuffer)->device          = device;
  (*outBuffer)->_heap           = heap;
  (*outBuffer)->_heapOffset     = heapOffset;
  (*outBuffer)->_allocationSize = requirements.sizeBytes;
  (*outBuffer)->sizeBytes       = info->sizeBytes;
  (*outBuffer)->usage           = info->usage;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreatePlacedTexture(GPUDevice                  * __restrict device,
                       const GPUTextureCreateInfo * __restrict info,
                       GPUHeap                    * __restrict heap,
                       uint64_t                                heapOffset,
                       GPUTexture                ** __restrict outTexture) {
  GPUMemoryRequirements requirements;
  GPUApi              *api;
  GPUResult            result;

  if (!outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  if (!heap || heap->device != device ||
      heap->usage != GPU_HEAP_USAGE_PLACED) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUGetTextureMemoryRequirements(device,
                                           info,
                                           &requirements);
  if (result != GPU_OK) {
    return result;
  }
  if (!gpuHeapRangeValid(heap, &requirements, heapOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.createPlacedTexture) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.createPlacedTexture(device,
                                           info,
                                           heap,
                                           heapOffset,
                                           outTexture);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outTexture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outTexture)->device          = device;
  (*outTexture)->_heap           = heap;
  (*outTexture)->_heapOffset     = heapOffset;
  (*outTexture)->_allocationSize = requirements.sizeBytes;
  (*outTexture)->format          = info->format;
  (*outTexture)->dimension       = info->dimension;
  (*outTexture)->width           = info->width;
  (*outTexture)->height          = info->height;
  (*outTexture)->depthOrLayers   = info->depthOrLayers;
  (*outTexture)->mipLevelCount   = info->mipLevelCount
                                      ? info->mipLevelCount
                                      : 1u;
  (*outTexture)->sampleCount     = info->sampleCount ? info->sampleCount : 1u;
  (*outTexture)->usage           = info->usage;
  (*outTexture)->_ownsNative     = true;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateSparseBuffer(GPUDevice                 * __restrict device,
                      const GPUBufferCreateInfo * __restrict info,
                      GPUHeap                   * __restrict heap,
                      GPUBuffer                ** __restrict outBuffer) {
  GPUSparseBufferRequirements requirements;
  GPUApi                     *api;
  GPUResult                   result;

  if (!outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;
  if (!heap || heap->device != device ||
      heap->usage != GPU_HEAP_USAGE_SPARSE) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUGetSparseBufferRequirements(device, info, &requirements);
  if (result != GPU_OK) {
    return result;
  }
  if (heap->pageSizeBytes != requirements.pageSizeBytes ||
      (heap->compatibilityMask & requirements.compatibilityMask) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.createSparseBuffer) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.createSparseBuffer(device, info, heap, outBuffer);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outBuffer)->device              = device;
  (*outBuffer)->_heap               = heap;
  (*outBuffer)->_sparseRequirements = requirements;
  (*outBuffer)->sizeBytes           = info->sizeBytes;
  (*outBuffer)->usage               = info->usage;
  (*outBuffer)->_sparse             = true;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateSparseTexture(GPUDevice                  * __restrict device,
                       const GPUTextureCreateInfo * __restrict info,
                       GPUHeap                    * __restrict heap,
                       GPUTexture                ** __restrict outTexture) {
  GPUApi                       *api;
  GPUSparseTextureRequirements  requirements;
  GPUResult                     result;

  if (!outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  if (!heap || heap->device != device ||
      heap->usage != GPU_HEAP_USAGE_SPARSE) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = GPUGetSparseTextureRequirements(device, info, &requirements);
  if (result != GPU_OK) {
    return result;
  }
  if (heap->pageSizeBytes != requirements.pageSizeBytes ||
      (heap->compatibilityMask & requirements.compatibilityMask) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->memory.createSparseTexture) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->memory.createSparseTexture(device, info, heap, outTexture);
  if (result != GPU_OK) {
    return result;
  }
  if (!*outTexture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  (*outTexture)->device              = device;
  (*outTexture)->_heap               = heap;
  (*outTexture)->_sparseRequirements = requirements;
  (*outTexture)->format              = info->format;
  (*outTexture)->dimension           = info->dimension;
  (*outTexture)->width               = info->width;
  (*outTexture)->height              = info->height;
  (*outTexture)->depthOrLayers       = info->depthOrLayers;
  (*outTexture)->mipLevelCount       = info->mipLevelCount
                                          ? info->mipLevelCount
                                          : 1u;
  (*outTexture)->sampleCount         = info->sampleCount
                                          ? info->sampleCount
                                          : 1u;
  (*outTexture)->usage               = info->usage;
  (*outTexture)->_sparse             = true;
  (*outTexture)->_ownsNative         = true;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUQueueSubmitSparse(GPUQueue                       * __restrict queue,
                     const GPUQueueSparseSubmitInfo * __restrict info) {
  GPUQueueSparseSubmitInfo backendInfo;
  GPUPipelineStageMask     validStages;
  GPUApi                  *api;
  GPUResult                result;

  if (!queue || !info ||
      (info->bufferMappingCount == 0u &&
       info->textureMappingCount == 0u) ||
      (info->bufferMappingCount > 0u && !info->pBufferMappings) ||
      (info->textureMappingCount > 0u && !info->pTextureMappings) ||
      (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
       info->chain.sType != GPU_STRUCTURE_TYPE_QUEUE_SPARSE_SUBMIT_INFO) ||
      (info->chain.structSize != 0u &&
       info->chain.structSize < sizeof(*info)) ||
      (info->waitCount > 0u && !info->pWaits) ||
      (info->signalCount > 0u && !info->pSignals)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->bufferMappingCount > 0u &&
       !GPUIsFeatureEnabled(queue->_device, GPU_FEATURE_SPARSE_BUFFERS)) ||
      (info->textureMappingCount > 0u &&
       !GPUIsFeatureEnabled(queue->_device, GPU_FEATURE_SPARSE_TEXTURES))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  for (uint32_t i = 0u; i < info->bufferMappingCount; i++) {
    if (!gpuSparseBufferMappingValid(queue, &info->pBufferMappings[i])) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  for (uint32_t i = 0u; i < info->textureMappingCount; i++) {
    if (!gpuSparseTextureMappingValid(queue, &info->pTextureMappings[i])) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  validStages = GPU_STAGE_TOP | GPU_STAGE_VERTEX | GPU_STAGE_FRAGMENT |
                GPU_STAGE_COMPUTE | GPU_STAGE_TRANSFER | GPU_STAGE_BOTTOM;
  for (uint32_t i = 0u; i < info->waitCount; i++) {
    if (!info->pWaits[i].semaphore ||
        info->pWaits[i].semaphore->_device != queue->_device ||
        info->pWaits[i].waitStages == 0u ||
        (info->pWaits[i].waitStages & ~validStages) != 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  for (uint32_t i = 0u; i < info->signalCount; i++) {
    if (!info->pSignals[i].semaphore ||
        info->pSignals[i].semaphore->_device != queue->_device) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  api = gpuCommandQueueApi(queue);
  if (!api || !api->memory.submitSparse) {
    return GPU_ERROR_UNSUPPORTED;
  }
  backendInfo       = *info;
  backendInfo.fence = NULL;
  result = api->memory.submitSparse(queue, &backendInfo);
  return result == GPU_OK
           ? gpuSubmitSparseFenceMarker(queue, info->fence)
           : result;
}
