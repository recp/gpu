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
  DeviceMT             *deviceMT;
  AdapterMT            *adapterMT;
  HeapMT               *heapMT;
  MTLTextureDescriptor *textureDesc;
  MTLHeapDescriptor    *heapDesc;
  id<MTLHeap>           temporaryHeap;
  id<MTLTexture>        texture;
  MTLSparsePageSize     pageSize;
  uint64_t              pageSizeBytes;
  GPUResult             result;

  if (!device || !(deviceMT = device->_priv) || !info || !outTexture
      || !outStencilCopyFormat) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outTexture = nil;
  adapterMT   = device->adapter ? device->adapter->_priv : NULL;

  if (deviceMT->commandMode != MTCommandMode4
      && (!adapterMT || !adapterMT->sparseTextures)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  heapMT        = heap ? heap->_priv : NULL;
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
      texture                             = [deviceMT->device newTextureWithDescriptor:textureDesc];
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
  MTLSizeAndAlign sizeAndAlign;
  DeviceMT       *deviceMT;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements
      || info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (@available(macOS 10.15, iOS 13.0, *)) {
    sizeAndAlign = [deviceMT->device heapBufferSizeAndAlignWithLength:(NSUInteger)info->sizeBytes
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
  MTLSizeAndAlign       sizeAndAlign;
  DeviceMT             *deviceMT;
  MTLTextureDescriptor *desc;
  MTLPixelFormat        stencilCopyFormat;
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
mt_getSparseBufferRequirements(GPUDevice                   *device,
                               const GPUBufferCreateInfo   *info,
                               GPUSparseBufferRequirements *outRequirements) {
  DeviceMT         *deviceMT;
  MTLSparsePageSize pageSize;
  uint64_t          pageSizeBytes;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements
      || info->sizeBytes > NSUIntegerMax
      || deviceMT->commandMode != MTCommandMode4
      || !mt_sparsePageSize(64u * 1024u, &pageSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    pageSizeBytes = [deviceMT->device sparseTileSizeInBytesForSparsePageSize:pageSize];

    if (pageSizeBytes == 0u) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    outRequirements->compatibilityMask = UINT64_C(1);
    outRequirements->pageSizeBytes     = pageSizeBytes;
    outRequirements->tileCount         = info->sizeBytes / pageSizeBytes +
      (info->sizeBytes % pageSizeBytes != 0u);

    return GPU_OK;
  }
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_getSparseTextureRequirements(GPUDevice                    *device,
                                const GPUTextureCreateInfo   *info,
                                GPUSparseTextureRequirements *outRequirements) {
  MTLSize           tileSize;
  DeviceMT         *deviceMT;
  id<MTLTexture>    texture;
  MTLPixelFormat    stencilCopyFormat;
  MTLSparsePageSize pageSize;
  NSUInteger        firstMipInTail;
  NSUInteger        tailSizeBytes;
  uint64_t          pageSizeBytes;
  uint32_t          mipLevelCount;
  GPUResult         result;

  if (!device || !(deviceMT = device->_priv) || !info || !outRequirements
      || !mt_sparsePageSize(64u * 1024u, &pageSize)) {
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
    pageSizeBytes = [deviceMT->device sparseTileSizeInBytesForSparsePageSize:pageSize];
    tileSize      = [deviceMT->device sparseTileSizeWithTextureType:texture.textureType
                                                        pixelFormat:texture.pixelFormat
                                                        sampleCount:texture.sampleCount
                                                     sparsePageSize:pageSize];
  } else if (@available(macOS 11.0, iOS 13.0, *)) {
    pageSizeBytes = deviceMT->device.sparseTileSizeInBytes;
    tileSize      = [deviceMT->device sparseTileSizeWithTextureType:texture.textureType
                                                        pixelFormat:texture.pixelFormat
                                                        sampleCount:texture.sampleCount];
  }

  firstMipInTail = texture.firstMipmapInTail;
  tailSizeBytes  = texture.tailSizeInBytes;
  [texture release];

  if (pageSizeBytes == 0u || tileSize.width == 0u
      || tileSize.height == 0u || tileSize.depth == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;

  if (firstMipInTail >= mipLevelCount || tailSizeBytes == 0u) {
    firstMipInTail = mipLevelCount;
    tailSizeBytes  = 0u;
  }

  outRequirements->compatibilityMask       = UINT64_C(1);
  outRequirements->pageSizeBytes           = pageSizeBytes;
  outRequirements->mipTailTileCount        = ((uint64_t)tailSizeBytes + pageSizeBytes - 1u) / pageSizeBytes;
  outRequirements->mipTailLayerStrideTiles = outRequirements->mipTailTileCount;
  outRequirements->tileWidth               = (uint32_t)tileSize.width;
  outRequirements->tileHeight              = (uint32_t)tileSize.height;
  outRequirements->tileDepth               = (uint32_t)tileSize.depth;
  outRequirements->firstMipInTail          = (uint32_t)firstMipInTail;

  return GPU_OK;
}

static GPUResult
mt_createHeap(GPUDevice               *device,
              const GPUHeapCreateInfo *info,
              GPUHeap                **outHeap) {
  DeviceMT          *deviceMT;
  AdapterMT         *adapterMT;
  MTLHeapDescriptor *desc;
  id<MTLHeap>        nativeHeap;
  GPUHeap           *heap;
  HeapMT            *native;
  uint64_t           compatibility;
  MTLSparsePageSize  sparsePageSize;

  if (!device || !(deviceMT = device->_priv) || !info || !outHeap
      || info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  adapterMT     = device->adapter ? device->adapter->_priv : NULL;
  compatibility = UINT64_C(1);

  if ((info->compatibilityMask & compatibility) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->usage == GPU_HEAP_USAGE_SPARSE
      && !mt_sparsePageSize(info->pageSizeBytes, &sparsePageSize)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (info->usage == GPU_HEAP_USAGE_SPARSE
      && deviceMT->commandMode != MTCommandMode4
      && (!adapterMT || !adapterMT->sparseTextures)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (@available(macOS 10.15, iOS 13.0, *)) {
    desc             = [MTLHeapDescriptor new];
    desc.size        = (NSUInteger)info->sizeBytes;
    desc.storageMode = MTLStorageModePrivate;
    desc.type        = MTLHeapTypePlacement;
#if MT_HAS_METAL4
    if (info->usage == GPU_HEAP_USAGE_SPARSE
        && deviceMT->commandMode == MTCommandMode4) {
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
  if (deviceDebugMarkersEnabled(device)
      && info->label && info->label[0] != '\0') {
    nativeHeap.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  if (!(heap = calloc(1, sizeof(*heap) + sizeof(*native)))) {
    [nativeHeap release];
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native                  = (HeapMT *)(heap + 1);
  native->heap            = nativeHeap;
  heap->_priv             = native;
  heap->device            = device;
  heap->compatibilityMask = compatibility;
  *outHeap                = heap;

  return GPU_OK;
}

static void
mt_destroyHeap(GPUHeap *heap) {
  HeapMT    *native;

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
  HeapMT       *nativeHeap;
  id<MTLBuffer> nativeBuffer;
  GPUResult     result;

  if (!device || !info || !heap || !(nativeHeap = heap->_priv)
      || !outBuffer || heapOffset > NSUIntegerMax
      || info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(nativeBuffer = [nativeHeap->heap newBufferWithLength:(NSUInteger)info->sizeBytes
                                                     options:MTLResourceStorageModePrivate
                                                      offset:(NSUInteger)heapOffset])) {
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
  HeapMT               *nativeHeap;
  MTLTextureDescriptor *desc;
  id<MTLTexture>        nativeTexture;
  MTLPixelFormat        stencilCopyFormat;
  GPUResult             result;

  if (!device || !info || !heap || !(nativeHeap = heap->_priv)
      || !outTexture || heapOffset > NSUIntegerMax) {
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
mt_createSparseBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      GPUBuffer                **outBuffer) {
  DeviceMT         *deviceMT;
  id<MTLBuffer>     nativeBuffer;
  MTLSparsePageSize pageSize;
  GPUResult         result;

  if (!device || !(deviceMT = device->_priv) || !info || !heap
      || !outBuffer || info->sizeBytes > NSUIntegerMax
      || deviceMT->commandMode != MTCommandMode4
      || !mt_sparsePageSize(heap->pageSizeBytes, &pageSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  nativeBuffer = nil;
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    nativeBuffer = [deviceMT->device newBufferWithLength:(NSUInteger)info->sizeBytes
                                                 options:MTLResourceStorageModePrivate
                                 placementSparsePageSize:pageSize];
  }
#endif
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
  MTLRegion                          region;
  MTCommandQueue                    *queue;
  id<MTLCommandBuffer>               commandBuffer;
  id<MTLResourceStateCommandEncoder> encoder;
  id<MTLEvent>                       waitEvent;
  const GPUSparseTextureMapping     *mapping;
  id<MTLTexture>                     texture;
  id<MTLEvent>                       signalEvent;
  MTLSparseTextureMappingMode        mode;
  uint32_t                           waitIndex;
  uint32_t                           textureIndex;
  uint32_t                           signalIndex;

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

  for (waitIndex = 0u; waitIndex < info->waitCount; waitIndex++) {
    waitEvent = (id<MTLEvent>)info->pWaits[waitIndex].semaphore->_priv;

    if (!waitEvent) {
      [commandBuffer release];
      return GPU_ERROR_BACKEND_FAILURE;
    }

    [commandBuffer encodeWaitForEvent:waitEvent value:info->pWaits[waitIndex].value];
  }

  if (!(encoder = [commandBuffer resourceStateCommandEncoder])) {
    [commandBuffer release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (textureIndex = 0u; textureIndex < info->textureMappingCount; textureIndex++) {
    mapping = &info->pTextureMappings[textureIndex];
    texture = mt_nativeTexture(mapping->texture);
    region  = MTLRegionMake3D(mapping->tileX,
                              mapping->tileY,
                              mapping->tileZ,
                              mapping->tileWidth,
                              mapping->tileHeight,
                              mapping->tileDepth);
    mode    = mapping->mode == GPU_SPARSE_MAPPING_MAP
              ? MTLSparseTextureMappingModeMap
              : MTLSparseTextureMappingModeUnmap;
    [encoder updateTextureMapping:texture
                             mode:mode
                           region:region
                         mipLevel:mapping->mipLevel
                            slice:mapping->arrayLayer];
  }

  [encoder endEncoding];

  for (signalIndex = 0u; signalIndex < info->signalCount; signalIndex++) {
    signalEvent = (id<MTLEvent>)info->pSignals[signalIndex].semaphore->_priv;

    if (!signalEvent) {
      [commandBuffer release];
      return GPU_ERROR_BACKEND_FAILURE;
    }

    [commandBuffer encodeSignalEvent:signalEvent value:info->pSignals[signalIndex].value];
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
  MTCommandQueue                *queue;
#if MT_HAS_METAL4
  id<MTLEvent>                   waitEvent;
  const GPUSparseBufferMapping  *bufferMapping;
  HeapMT                        *bufferHeap;
  const GPUSparseTextureMapping *textureMapping;
  HeapMT                        *textureHeap;
  id<MTLEvent>                   signalEvent;
  uint32_t                       waitIndex;
  uint32_t                       bufferIndex;
  uint32_t                       textureIndex;
  uint32_t                       signalIndex;
#endif

  queue = mt_commandQueue(queueHandle);

  if (!queue || !info
      || mt_flushTransfers(queueHandle, false) != GPU_OK) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
#if MT_HAS_METAL4
  if (queue->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      for (waitIndex = 0u; waitIndex < info->waitCount; waitIndex++) {
        waitEvent = (id<MTLEvent>)info->pWaits[waitIndex].semaphore->_priv;

        if (!waitEvent) {
          return GPU_ERROR_BACKEND_FAILURE;
        }

        [queue->modern waitForEvent:waitEvent value:info->pWaits[waitIndex].value];
      }

      for (bufferIndex = 0u; bufferIndex < info->bufferMappingCount; bufferIndex++) {
        MTL4UpdateSparseBufferMappingOperation bufferOperation = {0};

        bufferMapping = &info->pBufferMappings[bufferIndex];
        bufferHeap    = bufferMapping->heap->_priv;

        if (!bufferHeap || !bufferHeap->heap
            || bufferMapping->bufferTileOffset > NSUIntegerMax
            || bufferMapping->tileCount > NSUIntegerMax
            || (bufferMapping->mode == GPU_SPARSE_MAPPING_MAP
                && bufferMapping->heapTileOffset > NSUIntegerMax)) {
          return GPU_ERROR_BACKEND_FAILURE;
        }

        bufferOperation.mode        = bufferMapping->mode == GPU_SPARSE_MAPPING_MAP
                                      ? MTLSparseTextureMappingModeMap
                                      : MTLSparseTextureMappingModeUnmap;
        bufferOperation.bufferRange = NSMakeRange((NSUInteger)bufferMapping->bufferTileOffset,
                                                  (NSUInteger)bufferMapping->tileCount);
        bufferOperation.heapOffset  = bufferMapping->mode == GPU_SPARSE_MAPPING_MAP
                                      ? (NSUInteger)bufferMapping->heapTileOffset
                                      : 0u;
        [queue->modern updateBufferMappings:(id<MTLBuffer>)bufferMapping->buffer->_priv
                                       heap:bufferMapping->mode == GPU_SPARSE_MAPPING_MAP ? bufferHeap->heap : nil
                                 operations:&bufferOperation
                                      count:1u];
      }

      for (textureIndex = 0u; textureIndex < info->textureMappingCount; textureIndex++) {
        MTL4UpdateSparseTextureMappingOperation textureOperation = {0};

        textureMapping = &info->pTextureMappings[textureIndex];
        textureHeap    = textureMapping->heap->_priv;

        if (!textureHeap || !textureHeap->heap
            || (textureMapping->mode == GPU_SPARSE_MAPPING_MAP
                && textureMapping->heapTileOffset > NSUIntegerMax)) {
          return GPU_ERROR_BACKEND_FAILURE;
        }

        textureOperation.mode          = textureMapping->mode == GPU_SPARSE_MAPPING_MAP
                                         ? MTLSparseTextureMappingModeMap
                                         : MTLSparseTextureMappingModeUnmap;
        textureOperation.textureRegion = MTLRegionMake3D(textureMapping->tileX,
                                                         textureMapping->tileY,
                                                         textureMapping->tileZ,
                                                         textureMapping->tileWidth,
                                                         textureMapping->tileHeight,
                                                         textureMapping->tileDepth);
        textureOperation.textureLevel  = textureMapping->mipLevel;
        textureOperation.textureSlice  = textureMapping->arrayLayer;
        textureOperation.heapOffset    = textureMapping->mode == GPU_SPARSE_MAPPING_MAP
                                         ? (NSUInteger)textureMapping->heapTileOffset
                                         : 0u;
        [queue->modern updateTextureMappings:mt_nativeTexture(textureMapping->texture)
                                        heap:textureMapping->mode == GPU_SPARSE_MAPPING_MAP ? textureHeap->heap : nil
                                  operations:&textureOperation
                                       count:1u];
      }

      for (signalIndex = 0u; signalIndex < info->signalCount; signalIndex++) {
        signalEvent = (id<MTLEvent>)info->pSignals[signalIndex].semaphore->_priv;

        if (!signalEvent) {
          return GPU_ERROR_BACKEND_FAILURE;
        }

        [queue->modern signalEvent:signalEvent value:info->pSignals[signalIndex].value];
      }

      os_unfair_lock_lock(&queue->poolLock);
      queue->pendingSparseBarrier = true;
      os_unfair_lock_unlock(&queue->poolLock);

      return GPU_OK;
    }

    return GPU_ERROR_UNSUPPORTED;
  }
#endif
  if (info->bufferMappingCount > 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  return mt_submitSparseClassic(queueHandle, info);
}

GPU_HIDE
void
mt_initMemory(ApiMemory    *api) {
  api->getBufferRequirements        = mt_getBufferMemoryRequirements;
  api->getTextureRequirements       = mt_getTextureMemoryRequirements;
  api->getSparseBufferRequirements  = mt_getSparseBufferRequirements;
  api->getSparseTextureRequirements = mt_getSparseTextureRequirements;
  api->createHeap                   = mt_createHeap;
  api->destroyHeap                  = mt_destroyHeap;
  api->createPlacedBuffer           = mt_createPlacedBuffer;
  api->createPlacedTexture          = mt_createPlacedTexture;
  api->createSparseBuffer           = mt_createSparseBuffer;
  api->createSparseTexture          = mt_createSparseTexture;
  api->submitSparse                 = mt_submitSparse;
}
