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

static bool
mt_sparsePageSize(uint64_t pageSizeBytes, MTLSparsePageSize *outPageSize) {
  if (!outPageSize) {
    return false;
  }
  switch (pageSizeBytes) {
    case 16u * 1024u:
      *outPageSize = MTLSparsePageSize16;
      return true;
    case 64u * 1024u:
      *outPageSize = MTLSparsePageSize64;
      return true;
    case 256u * 1024u:
      *outPageSize = MTLSparsePageSize256;
      return true;
    default:
      return false;
  }
}

static GPUResult
mt_newSparseTexture(GPUDevice                  *device,
                    const GPUTextureCreateInfo *info,
                    GPUHeap                    *heap,
                    id<MTLTexture>             *outTexture,
                    MTLPixelFormat             *outStencilCopyFormat) {
  GPUDeviceMT          *deviceMT;
  GPUHeapMT            *heapMT;
  MTLTextureDescriptor *textureDesc;
  MTLHeapDescriptor    *heapDesc;
  id<MTLHeap>           temporaryHeap;
  id<MTLTexture>        texture;
  MTLSparsePageSize     pageSize;
  uint64_t              pageSizeBytes;
  GPUResult             result;

  if (!device || !(deviceMT = device->_priv) || !info || !outTexture ||
      !outStencilCopyFormat) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = nil;
  heapMT       = heap ? heap->_priv : NULL;
  pageSizeBytes = heap ? heap->pageSizeBytes : 64u * 1024u;
  if (!mt_sparsePageSize(pageSizeBytes, &pageSize)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = mt_createTextureDescriptor(device,
                                      info,
                                      MTLStorageModePrivate,
                                      &textureDesc,
                                      outStencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }

  temporaryHeap = nil;
  texture       = nil;
#if MT_HAS_METAL4
  if (deviceMT->commandMode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      textureDesc.placementSparsePageSize = pageSize;
      texture = [deviceMT->device newTextureWithDescriptor:textureDesc];
    }
  } else
#endif
  {
#if TARGET_OS_OSX
    if (@available(macOS 11.0, *)) {
      if (heapMT && heapMT->heap) {
        texture = [heapMT->heap newTextureWithDescriptor:textureDesc];
      } else {
        heapDesc             = [MTLHeapDescriptor new];
        heapDesc.size        = (NSUInteger)pageSizeBytes;
        heapDesc.storageMode = MTLStorageModePrivate;
        heapDesc.type        = MTLHeapTypeSparse;
        if (@available(macOS 13.0, *)) {
          heapDesc.sparsePageSize = pageSize;
        }
        temporaryHeap = [deviceMT->device newHeapWithDescriptor:heapDesc];
        [heapDesc release];
        texture = [temporaryHeap newTextureWithDescriptor:textureDesc];
      }
    }
#endif
  }
  [textureDesc release];
  [temporaryHeap release];
  if (!texture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  *outTexture = texture;
  return GPU_OK;
}

static GPUResult
mt_getBufferMemoryRequirements(GPUDevice                 *device,
                               const GPUBufferCreateInfo *info,
                               GPUMemoryRequirements     *outRequirements) {
  GPUDeviceMT    *deviceMT;
  MTLSizeAndAlign sizeAndAlign;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements ||
      info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15, iOS 13.0, *)) {
    sizeAndAlign = [deviceMT->device
      heapBufferSizeAndAlignWithLength:(NSUInteger)info->sizeBytes
                               options:MTLResourceStorageModePrivate];
    if (sizeAndAlign.size == 0u || sizeAndAlign.align == 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
    outRequirements->sizeBytes         = sizeAndAlign.size;
    outRequirements->alignmentBytes    = sizeAndAlign.align;
    outRequirements->compatibilityMask = UINT64_C(1);
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_getTextureMemoryRequirements(GPUDevice                  *device,
                                const GPUTextureCreateInfo *info,
                                GPUMemoryRequirements      *outRequirements) {
  GPUDeviceMT          *deviceMT;
  MTLTextureDescriptor *desc;
  MTLPixelFormat        stencilCopyFormat;
  MTLSizeAndAlign       sizeAndAlign;
  GPUResult             result;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = mt_createTextureDescriptor(device,
                                      info,
                                      MTLStorageModePrivate,
                                      &desc,
                                      &stencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(stencilCopyFormat);
  sizeAndAlign = [deviceMT->device heapTextureSizeAndAlignWithDescriptor:desc];
  [desc release];
  if (sizeAndAlign.size == 0u || sizeAndAlign.align == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->sizeBytes         = sizeAndAlign.size;
  outRequirements->alignmentBytes    = sizeAndAlign.align;
  outRequirements->compatibilityMask = UINT64_C(1);
  return GPU_OK;
}

static GPUResult
mt_getSparseTextureRequirements(
  GPUDevice                    *device,
  const GPUTextureCreateInfo   *info,
  GPUSparseTextureRequirements *outRequirements
) {
  GPUDeviceMT      *deviceMT;
  id<MTLTexture>    texture;
  MTLPixelFormat    stencilCopyFormat;
  MTLSparsePageSize pageSize;
  MTLSize           tileSize;
  NSUInteger        firstMipInTail;
  NSUInteger        tailSizeBytes;
  uint64_t          pageSizeBytes;
  uint32_t          mipLevelCount;
  GPUResult         result;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements ||
      !mt_sparsePageSize(64u * 1024u, &pageSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = mt_newSparseTexture(device,
                               info,
                               NULL,
                               &texture,
                               &stencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(stencilCopyFormat);

  pageSizeBytes = 0u;
  tileSize      = MTLSizeMake(0u, 0u, 0u);
  if (@available(macOS 13.0, iOS 16.0, *)) {
    pageSizeBytes = [deviceMT->device
      sparseTileSizeInBytesForSparsePageSize:pageSize];
    tileSize = [deviceMT->device
      sparseTileSizeWithTextureType:texture.textureType
                         pixelFormat:texture.pixelFormat
                         sampleCount:texture.sampleCount
                      sparsePageSize:pageSize];
  } else if (@available(macOS 11.0, iOS 13.0, *)) {
    pageSizeBytes = deviceMT->device.sparseTileSizeInBytes;
    tileSize = [deviceMT->device
      sparseTileSizeWithTextureType:texture.textureType
                         pixelFormat:texture.pixelFormat
                         sampleCount:texture.sampleCount];
  }
  firstMipInTail = texture.firstMipmapInTail;
  tailSizeBytes  = texture.tailSizeInBytes;
  [texture release];
  if (pageSizeBytes == 0u || tileSize.width == 0u ||
      tileSize.height == 0u || tileSize.depth == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  if (firstMipInTail >= mipLevelCount || tailSizeBytes == 0u) {
    firstMipInTail = mipLevelCount;
    tailSizeBytes  = 0u;
  }
  outRequirements->compatibilityMask       = UINT64_C(1);
  outRequirements->pageSizeBytes           = pageSizeBytes;
  outRequirements->mipTailTileCount        =
    ((uint64_t)tailSizeBytes + pageSizeBytes - 1u) / pageSizeBytes;
  outRequirements->mipTailLayerStrideTiles =
    outRequirements->mipTailTileCount;
  outRequirements->tileWidth                = (uint32_t)tileSize.width;
  outRequirements->tileHeight               = (uint32_t)tileSize.height;
  outRequirements->tileDepth                = (uint32_t)tileSize.depth;
  outRequirements->firstMipInTail           = (uint32_t)firstMipInTail;
  return GPU_OK;
}

static GPUResult
mt_createHeap(GPUDevice               *device,
              const GPUHeapCreateInfo *info,
              GPUHeap                **outHeap) {
  GPUDeviceMT       *deviceMT;
  MTLHeapDescriptor *desc;
  id<MTLHeap>        nativeHeap;
  GPUHeap           *heap;
  GPUHeapMT         *native;
  uint64_t           compatibility;
  MTLSparsePageSize  sparsePageSize;

  if (!device || !(deviceMT = device->_priv) || !info || !outHeap ||
      info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  compatibility = UINT64_C(1);
  if ((info->compatibilityMask & compatibility) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->usage == GPU_HEAP_USAGE_SPARSE &&
      !mt_sparsePageSize(info->pageSizeBytes, &sparsePageSize)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (@available(macOS 10.15, iOS 13.0, *)) {
    desc                    = [MTLHeapDescriptor new];
    desc.size               = (NSUInteger)info->sizeBytes;
    desc.storageMode        = MTLStorageModePrivate;
    desc.type               = MTLHeapTypePlacement;
#if MT_HAS_METAL4
    if (info->usage == GPU_HEAP_USAGE_SPARSE &&
        deviceMT->commandMode == MTCommandMode4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        desc.maxCompatiblePlacementSparsePageSize = sparsePageSize;
      }
    } else
#endif
    if (info->usage == GPU_HEAP_USAGE_SPARSE) {
#if TARGET_OS_OSX
      if (@available(macOS 11.0, *)) {
        desc.type = MTLHeapTypeSparse;
        if (@available(macOS 13.0, *)) {
          desc.sparsePageSize = sparsePageSize;
        }
      } else {
        [desc release];
        return GPU_ERROR_UNSUPPORTED;
      }
#else
      [desc release];
      return GPU_ERROR_UNSUPPORTED;
#endif
    }
    desc.hazardTrackingMode = deviceMT->commandMode == MTCommandMode4
                                ? MTLHazardTrackingModeUntracked
                                : MTLHazardTrackingModeTracked;
    nativeHeap              = [deviceMT->device newHeapWithDescriptor:desc];
    [desc release];
  } else {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!nativeHeap) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    nativeHeap.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  heap = calloc(1, sizeof(*heap) + sizeof(*native));
  if (!heap) {
    [nativeHeap release];
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native                  = (GPUHeapMT *)(heap + 1);
  native->heap            = nativeHeap;
  heap->_priv             = native;
  heap->device            = device;
  heap->compatibilityMask = compatibility;
  *outHeap                = heap;
  return GPU_OK;
}

static void
mt_destroyHeap(GPUHeap *heap) {
  GPUHeapMT *native;

  if (!heap) {
    return;
  }
  native = heap->_priv;
  [native->heap release];
  free(heap);
}

static GPUResult
mt_createPlacedBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      uint64_t                   heapOffset,
                      GPUBuffer                **outBuffer) {
  GPUHeapMT     *nativeHeap;
  id<MTLBuffer> nativeBuffer;
  GPUResult     result;

  if (!device || !info || !heap || !(nativeHeap = heap->_priv) ||
      !outBuffer || heapOffset > NSUIntegerMax ||
      info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  nativeBuffer = [nativeHeap->heap
    newBufferWithLength:(NSUInteger)info->sizeBytes
                options:MTLResourceStorageModePrivate
                 offset:(NSUInteger)heapOffset];
  if (!nativeBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = mt_wrapBuffer(device, info, nativeBuffer, outBuffer);
  if (result != GPU_OK) {
    [nativeBuffer release];
  }
  return result;
}

static GPUResult
mt_createPlacedTexture(GPUDevice                  *device,
                       const GPUTextureCreateInfo *info,
                       GPUHeap                    *heap,
                       uint64_t                    heapOffset,
                       GPUTexture                **outTexture) {
  GPUHeapMT            *nativeHeap;
  MTLTextureDescriptor *desc;
  id<MTLTexture>        nativeTexture;
  MTLPixelFormat        stencilCopyFormat;
  GPUResult             result;

  if (!device || !info || !heap || !(nativeHeap = heap->_priv) ||
      !outTexture || heapOffset > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = mt_createTextureDescriptor(device,
                                      info,
                                      MTLStorageModePrivate,
                                      &desc,
                                      &stencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }
  nativeTexture = [nativeHeap->heap newTextureWithDescriptor:desc
                                                      offset:(NSUInteger)heapOffset];
  [desc release];
  if (!nativeTexture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = mt_wrapTexture(device,
                          info,
                          nativeTexture,
                          stencilCopyFormat,
                          outTexture);
  if (result != GPU_OK) {
    [nativeTexture release];
  }
  return result;
}

static GPUResult
mt_createSparseTexture(GPUDevice                  *device,
                       const GPUTextureCreateInfo *info,
                       GPUHeap                    *heap,
                       GPUTexture                **outTexture) {
  id<MTLTexture> nativeTexture;
  MTLPixelFormat stencilCopyFormat;
  GPUResult      result;

  if (!device || !info || !heap || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = mt_newSparseTexture(device,
                               info,
                               heap,
                               &nativeTexture,
                               &stencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }
  result = mt_wrapTexture(device,
                          info,
                          nativeTexture,
                          stencilCopyFormat,
                          outTexture);
  if (result != GPU_OK) {
    [nativeTexture release];
  }
  return result;
}

static GPUResult
mt_submitSparseClassic(GPUQueue                       *queueHandle,
                       const GPUQueueSparseSubmitInfo *info) {
  MTCommandQueue             *queue;
  id<MTLCommandBuffer>        commandBuffer;
  id<MTLResourceStateCommandEncoder> encoder;

  queue = mt_commandQueue(queueHandle);
  if (!queue || !queue->classic || !info) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  @autoreleasepool {
    commandBuffer = [[queue->classic commandBuffer] retain];
  }
  if (!commandBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  for (uint32_t i = 0u; i < info->waitCount; i++) {
    id<MTLEvent> event;

    event = (id<MTLEvent>)info->pWaits[i].semaphore->_priv;
    if (!event) {
      [commandBuffer release];
      return GPU_ERROR_BACKEND_FAILURE;
    }
    [commandBuffer encodeWaitForEvent:event value:info->pWaits[i].value];
  }

  encoder = [commandBuffer resourceStateCommandEncoder];
  if (!encoder) {
    [commandBuffer release];
    return GPU_ERROR_BACKEND_FAILURE;
  }
  for (uint32_t i = 0u; i < info->textureMappingCount; i++) {
    const GPUSparseTextureMapping *mapping;
    id<MTLTexture>                 texture;
    MTLRegion                      region;
    MTLSparseTextureMappingMode    mode;

    mapping = &info->pTextureMappings[i];
    texture = mt_nativeTexture(mapping->texture);
    region  = MTLRegionMake3D(mapping->tileX,
                              mapping->tileY,
                              mapping->tileZ,
                              mapping->tileWidth,
                              mapping->tileHeight,
                              mapping->tileDepth);
    mode = mapping->mode == GPU_SPARSE_MAPPING_MAP
             ? MTLSparseTextureMappingModeMap
             : MTLSparseTextureMappingModeUnmap;
    [encoder updateTextureMapping:texture
                             mode:mode
                           region:region
                         mipLevel:mapping->mipLevel
                            slice:mapping->arrayLayer];
  }
  [encoder endEncoding];

  for (uint32_t i = 0u; i < info->signalCount; i++) {
    id<MTLEvent> event;

    event = (id<MTLEvent>)info->pSignals[i].semaphore->_priv;
    if (!event) {
      [commandBuffer release];
      return GPU_ERROR_BACKEND_FAILURE;
    }
    [commandBuffer encodeSignalEvent:event value:info->pSignals[i].value];
  }

  dispatch_group_enter(queue->inFlightGroup);
  [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    GPU__UNUSED(completed);
    dispatch_group_leave(queue->inFlightGroup);
  }];
  [commandBuffer commit];
  [commandBuffer release];
  return GPU_OK;
}

static GPUResult
mt_submitSparse(GPUQueue                       *queueHandle,
                const GPUQueueSparseSubmitInfo *info) {
  MTCommandQueue *queue;

  queue = mt_commandQueue(queueHandle);
  if (!queue || !info ||
      mt_flushTransfers(queueHandle, false) != GPU_OK) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
#if MT_HAS_METAL4
  if (queue->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      for (uint32_t i = 0u; i < info->waitCount; i++) {
        id<MTLEvent> event;

        event = (id<MTLEvent>)info->pWaits[i].semaphore->_priv;
        if (!event) {
          return GPU_ERROR_BACKEND_FAILURE;
        }
        [queue->modern waitForEvent:event value:info->pWaits[i].value];
      }
      for (uint32_t i = 0u; i < info->textureMappingCount; i++) {
        const GPUSparseTextureMapping *mapping;
        GPUHeapMT                     *heap;
        MTL4UpdateSparseTextureMappingOperation operation = {0};

        mapping = &info->pTextureMappings[i];
        heap    = mapping->heap->_priv;
        if (!heap || !heap->heap ||
            (mapping->mode == GPU_SPARSE_MAPPING_MAP &&
             mapping->heapTileOffset > NSUIntegerMax)) {
          return GPU_ERROR_BACKEND_FAILURE;
        }
        operation.mode = mapping->mode == GPU_SPARSE_MAPPING_MAP
                           ? MTLSparseTextureMappingModeMap
                           : MTLSparseTextureMappingModeUnmap;
        operation.textureRegion = MTLRegionMake3D(mapping->tileX,
                                                   mapping->tileY,
                                                   mapping->tileZ,
                                                   mapping->tileWidth,
                                                   mapping->tileHeight,
                                                   mapping->tileDepth);
        operation.textureLevel = mapping->mipLevel;
        operation.textureSlice = mapping->arrayLayer;
        operation.heapOffset   = mapping->mode == GPU_SPARSE_MAPPING_MAP
                                   ? (NSUInteger)mapping->heapTileOffset
                                   : 0u;
        [queue->modern
          updateTextureMappings:mt_nativeTexture(mapping->texture)
                           heap:mapping->mode == GPU_SPARSE_MAPPING_MAP
                                  ? heap->heap
                                  : nil
                     operations:&operation
                          count:1u];
      }
      for (uint32_t i = 0u; i < info->signalCount; i++) {
        id<MTLEvent> event;

        event = (id<MTLEvent>)info->pSignals[i].semaphore->_priv;
        if (!event) {
          return GPU_ERROR_BACKEND_FAILURE;
        }
        [queue->modern signalEvent:event value:info->pSignals[i].value];
      }
      os_unfair_lock_lock(&queue->poolLock);
      queue->pendingSparseBarrier = true;
      os_unfair_lock_unlock(&queue->poolLock);
      return GPU_OK;
    }
    return GPU_ERROR_UNSUPPORTED;
  }
#endif
  return mt_submitSparseClassic(queueHandle, info);
}

GPU_HIDE
void
mt_initMemory(GPUApiMemory *api) {
  api->getBufferRequirements  = mt_getBufferMemoryRequirements;
  api->getTextureRequirements = mt_getTextureMemoryRequirements;
  api->getSparseTextureRequirements = mt_getSparseTextureRequirements;
  api->createHeap             = mt_createHeap;
  api->destroyHeap            = mt_destroyHeap;
  api->createPlacedBuffer     = mt_createPlacedBuffer;
  api->createPlacedTexture    = mt_createPlacedTexture;
  api->createSparseTexture    = mt_createSparseTexture;
  api->submitSparse           = mt_submitSparse;
}
