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
#include "../../../api/multigpu_internal.h"

enum {
  MT_SHARED_BARRIER_CHUNK_SIZE = 16u
};

static bool
mt_interopDevices(GPUDeviceInteropEXT *interop,
                  GPUDeviceMT        **outFirst,
                  GPUDeviceMT        **outSecond) {
  GPUDeviceMT *first;
  GPUDeviceMT *second;

  if (!interop || !interop->firstDevice || !interop->secondDevice
      || gpuDeviceApi(interop->firstDevice) !=
        gpuDeviceApi(interop->secondDevice)
      || !outFirst || !outSecond) {
    return false;
  }

  first  = interop->firstDevice->_priv;
  second = interop->secondDevice->_priv;

  if (!first || !second || !first->device
      || first->device != second->device) {
    return false;
  }

  *outFirst  = first;
  *outSecond = second;

  return true;
}

static GPUResult
mt_createDeviceInterop(GPUDevice           *firstDevice,
                       GPUDevice           *secondDevice,
                       GPUDeviceInteropEXT *interop) {
  GPUDeviceMT *first;
  GPUDeviceMT *second;

  if (!firstDevice || !secondDevice || !interop) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  first  = firstDevice->_priv;
  second = secondDevice->_priv;

  if (gpuDeviceApi(firstDevice) != gpuDeviceApi(secondDevice)
      || !first || !second || !first->device
      || first->device != second->device) {
    return GPU_ERROR_UNSUPPORTED;
  }

  return GPU_OK;
}

static void
mt_destroyDeviceInterop(GPUDeviceInteropEXT *interop) {
  GPU__UNUSED(interop);
}

static MTLStorageMode
mt_sharedTextureStorageMode(const GPUTextureCreateInfo *info) {
  if (!info || (info->usage & GPU_TEXTURE_USAGE_COPY_DST) == 0u) {
    return MTLStorageModePrivate;
  }
#if TARGET_OS_OSX
  return MTLStorageModeManaged;
#else
  return MTLStorageModeShared;
#endif
}

static GPUResult
mt_getSharedBufferRequirements(GPUDeviceInteropEXT       *interop,
                               const GPUBufferCreateInfo *firstInfo,
                               const GPUBufferCreateInfo *secondInfo,
                               GPUMemoryRequirements     *outRequirements) {
  MTLSizeAndAlign sizeAndAlign;
  GPUDeviceMT    *first;
  GPUDeviceMT    *second;

  if (!mt_interopDevices(interop, &first, &second)
      || !firstInfo || !secondInfo || !outRequirements
      || firstInfo->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(second);

  if (@available(macOS 10.15, iOS 13.0, *)) {
    sizeAndAlign = [first->device heapBufferSizeAndAlignWithLength:(NSUInteger)firstInfo->sizeBytes
                                                           options:MTLResourceStorageModeShared];

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
mt_createSharedBuffer(GPUDeviceInteropEXT       *interop,
                      const GPUBufferCreateInfo *firstInfo,
                      const GPUBufferCreateInfo *secondInfo,
                      GPUBuffer                **outFirstBuffer,
                      GPUBuffer                **outSecondBuffer) {
  GPUBufferCreateInfo firstWrapInfo;
  GPUBufferCreateInfo secondWrapInfo;
  GPUDeviceMT        *first;
  GPUDeviceMT        *second;
  id<MTLBuffer>       nativeBuffer;
  GPUResult           result;

  if (!mt_interopDevices(interop, &first, &second)
      || !firstInfo || !secondInfo
      || !outFirstBuffer || !outSecondBuffer
      || firstInfo->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(second);

  if (!(nativeBuffer = [first->device newBufferWithLength:(NSUInteger)firstInfo->sizeBytes
                                                  options:MTLResourceStorageModeShared])) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  firstWrapInfo       = *firstInfo;
  firstWrapInfo.label = firstInfo->label ? firstInfo->label : secondInfo->label;
  result              = mt_wrapBuffer(interop->firstDevice,
                                      &firstWrapInfo,
                                      nativeBuffer,
                                      outFirstBuffer);

  if (result != GPU_OK) {
    [nativeBuffer release];
    return result;
  }

  secondWrapInfo       = *secondInfo;
  secondWrapInfo.label = NULL;
  [nativeBuffer retain];
  result = mt_wrapBuffer(interop->secondDevice,
                         &secondWrapInfo,
                         nativeBuffer,
                         outSecondBuffer);

  if (result != GPU_OK) {
    [nativeBuffer release];
    mt_destroyBuffer(*outFirstBuffer);
    *outFirstBuffer = NULL;
    return result;
  }

  return GPU_OK;
}

static GPUResult
mt_getSharedTextureRequirements(GPUDeviceInteropEXT        *interop,
                                const GPUTextureCreateInfo *firstInfo,
                                const GPUTextureCreateInfo *secondInfo,
                                GPUMemoryRequirements      *outRequirements) {
  GPUTextureCreateInfo  mergedInfo;
  MTLSizeAndAlign       sizeAndAlign;
  GPUDeviceMT          *first;
  GPUDeviceMT          *second;
  MTLTextureDescriptor *desc;
  MTLPixelFormat        stencilCopyFormat;
  GPUResult             result;

  if (!mt_interopDevices(interop, &first, &second)
      || !firstInfo || !secondInfo || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(second);

  mergedInfo       = *firstInfo;
  mergedInfo.usage = firstInfo->usage | secondInfo->usage;
  result           = mt_createTextureDescriptor(interop->firstDevice,
                                                &mergedInfo,
                                                mt_sharedTextureStorageMode(&mergedInfo),
                                                &desc,
                                                &stencilCopyFormat);

  if (result != GPU_OK) {
    return result;
  }

  GPU__UNUSED(stencilCopyFormat);

  sizeAndAlign = [first->device heapTextureSizeAndAlignWithDescriptor:desc];
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
mt_createSharedTexture(GPUDeviceInteropEXT        *interop,
                       const GPUTextureCreateInfo *firstInfo,
                       const GPUTextureCreateInfo *secondInfo,
                       GPUTexture                **outFirstTexture,
                       GPUTexture                **outSecondTexture) {
  GPUTextureCreateInfo mergedInfo;
  GPUTextureCreateInfo secondWrapInfo;
  GPUDeviceMT         *first;
  GPUDeviceMT         *second;
  GPUTextureMT        *native;
  MTLPixelFormat       stencilCopyFormat;
  GPUResult            result;

  if (!mt_interopDevices(interop, &first, &second)
      || !firstInfo || !secondInfo
      || !outFirstTexture || !outSecondTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(first);
  GPU__UNUSED(second);

  mergedInfo       = *firstInfo;
  mergedInfo.label = firstInfo->label ? firstInfo->label : secondInfo->label;
  mergedInfo.usage = firstInfo->usage | secondInfo->usage;
  result           = mt_createTexture(interop->firstDevice,
                                      &mergedInfo,
                                      outFirstTexture);

  if (result != GPU_OK) {
    return result;
  }

  (*outFirstTexture)->usage = firstInfo->usage;
  native = (*outFirstTexture)->_priv;

  if (!native || !native->texture) {
    mt_destroyTexture(*outFirstTexture);
    *outFirstTexture = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  stencilCopyFormat    = native->stencilCopyView ? native->stencilCopyView.pixelFormat : MTLPixelFormatInvalid;
  secondWrapInfo       = *secondInfo;
  secondWrapInfo.label = NULL;
  [native->texture retain];
  result = mt_wrapTexture(interop->secondDevice,
                          &secondWrapInfo,
                          native->texture,
                          stencilCopyFormat,
                          outSecondTexture);

  if (result != GPU_OK) {
    [native->texture release];
    mt_destroyTexture(*outFirstTexture);
    *outFirstTexture = NULL;
    return result;
  }

  return GPU_OK;
}

static GPUResult
mt_createSharedSemaphore(GPUDeviceInteropEXT          *interop,
                         const GPUSemaphoreCreateInfo *info,
                         GPUSemaphore                 *firstSemaphore,
                         GPUSemaphore                 *secondSemaphore) {
  GPUDeviceMT       *first;
  GPUDeviceMT       *second;
  id<MTLSharedEvent> event;

  if (!mt_interopDevices(interop, &first, &second)
      || !firstSemaphore || !secondSemaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(second);

  if (@available(macOS 10.14, iOS 12.0, *)) {
    if (!(event = [first->device newSharedEvent])) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    event.signaledValue = info ? info->initialValue : 0u;
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (gpuDeviceDebugMarkersEnabled(interop->firstDevice)
        && info && info->label && info->label[0] != '\0') {
      event.label = [NSString stringWithUTF8String:info->label];
    }
#endif
    firstSemaphore->_priv = event;
    [event retain];
    secondSemaphore->_priv = event;
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_encodeSharedBarriers(GPUDeviceInteropEXT            *interop,
                        GPUCommandBuffer               *cmdb,
                        const GPUSharedBarrierBatchEXT *barriers,
                        bool                            acquire) {
  GPUBufferBarrier                  bufferBarriers[MT_SHARED_BARRIER_CHUNK_SIZE];
  GPUTextureBarrier                 textureBarriers[MT_SHARED_BARRIER_CHUNK_SIZE];
  GPUBarrierBatch                   batch;
  GPUDeviceMT                      *first;
  GPUDeviceMT                      *second;
  const GPUSharedBufferBarrierEXT  *sharedBuffer;
  GPUBufferBarrier                 *bufferBarrier;
  const GPUSharedTextureBarrierEXT *sharedTexture;
  GPUTextureBarrier                *textureBarrier;
  uint32_t                          bufferOffset;
  uint32_t                          textureOffset;
  uint32_t                          bufferCount;
  uint32_t                          textureCount;
  uint32_t                          bufferIndex;
  uint32_t                          textureIndex;

  if (!mt_interopDevices(interop, &first, &second) || !cmdb || !barriers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPU__UNUSED(first);
  GPU__UNUSED(second);

  bufferOffset  = 0u;
  textureOffset = 0u;

  while (bufferOffset < barriers->bufferBarrierCount
         || textureOffset < barriers->textureBarrierCount) {
    batch = (GPUBarrierBatch){0};

    bufferCount = barriers->bufferBarrierCount - bufferOffset;

    if (bufferCount > MT_SHARED_BARRIER_CHUNK_SIZE) {
      bufferCount = MT_SHARED_BARRIER_CHUNK_SIZE;
    }

    textureCount = barriers->textureBarrierCount - textureOffset;

    if (textureCount > MT_SHARED_BARRIER_CHUNK_SIZE) {
      textureCount = MT_SHARED_BARRIER_CHUNK_SIZE;
    }

    for (bufferIndex = 0u; bufferIndex < bufferCount; bufferIndex++) {
      sharedBuffer             = &barriers->pBufferBarriers[bufferOffset + bufferIndex];
      bufferBarrier            = &bufferBarriers[bufferIndex];
      bufferBarrier->buffer    = acquire ? sharedBuffer->destinationBuffer : sharedBuffer->sourceBuffer;
      bufferBarrier->srcAccess = acquire ? GPU_ACCESS_NONE : sharedBuffer->srcAccess;
      bufferBarrier->dstAccess = acquire ? sharedBuffer->dstAccess : GPU_ACCESS_NONE;
      bufferBarrier->offset    = sharedBuffer->offset;
      bufferBarrier->sizeBytes = sharedBuffer->sizeBytes;
    }

    for (textureIndex = 0u; textureIndex < textureCount; textureIndex++) {
      sharedTexture              = &barriers->pTextureBarriers[textureOffset + textureIndex];
      textureBarrier             = &textureBarriers[textureIndex];
      textureBarrier->texture    = acquire ? sharedTexture->destinationTexture : sharedTexture->sourceTexture;
      textureBarrier->srcAccess  = acquire ? GPU_ACCESS_NONE : sharedTexture->srcAccess;
      textureBarrier->dstAccess  = acquire ? sharedTexture->dstAccess : GPU_ACCESS_NONE;
      textureBarrier->baseMip    = sharedTexture->baseMip;
      textureBarrier->mipCount   = sharedTexture->mipCount;
      textureBarrier->baseLayer  = sharedTexture->baseLayer;
      textureBarrier->layerCount = sharedTexture->layerCount;
    }

    batch.pBufferBarriers     = bufferCount > 0u ? bufferBarriers : NULL;
    batch.pTextureBarriers    = textureCount > 0u ? textureBarriers : NULL;
    batch.srcStages           = acquire ? GPU_STAGE_TOP : barriers->srcStages;
    batch.dstStages           = acquire ? barriers->dstStages : GPU_STAGE_BOTTOM;
    batch.bufferBarrierCount  = bufferCount;
    batch.textureBarrierCount = textureCount;
    mt_encodeBarriers(cmdb, &batch);

    bufferOffset  += bufferCount;
    textureOffset += textureCount;
  }

  return GPU_OK;
}

static GPUResult
mt_encodeSharedRelease(GPUDeviceInteropEXT            *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers) {
  return mt_encodeSharedBarriers(interop, cmdb, barriers, false);
}

static GPUResult
mt_encodeSharedAcquire(GPUDeviceInteropEXT            *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers) {
  return mt_encodeSharedBarriers(interop, cmdb, barriers, true);
}

GPU_HIDE
void
mt_initMultiGPU(GPUApiMultiGPU *api) {
  api->createInterop          = mt_createDeviceInterop;
  api->destroyInterop         = mt_destroyDeviceInterop;
  api->getBufferRequirements  = mt_getSharedBufferRequirements;
  api->createBuffer           = mt_createSharedBuffer;
  api->getTextureRequirements = mt_getSharedTextureRequirements;
  api->createTexture          = mt_createSharedTexture;
  api->createSemaphore        = mt_createSharedSemaphore;
  api->encodeRelease          = mt_encodeSharedRelease;
  api->encodeAcquire          = mt_encodeSharedAcquire;
}
