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
  GPUDeviceMT *first, *second;

  if (!interop || !interop->firstDevice || !interop->secondDevice ||
      gpuDeviceApi(interop->firstDevice) !=
        gpuDeviceApi(interop->secondDevice) ||
      !outFirst || !outSecond) {
    return false;
  }

  first  = interop->firstDevice->_priv;
  second = interop->secondDevice->_priv;
  if (!first || !second || !first->device ||
      first->device != second->device) {
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
  GPUDeviceMT *first, *second;

  if (!firstDevice || !secondDevice || !interop) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  first  = firstDevice->_priv;
  second = secondDevice->_priv;
  if (gpuDeviceApi(firstDevice) != gpuDeviceApi(secondDevice) ||
      !first || !second || !first->device ||
      first->device != second->device) {
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
mt_getSharedBufferRequirements(
  GPUDeviceInteropEXT       *interop,
  const GPUBufferCreateInfo *firstInfo,
  const GPUBufferCreateInfo *secondInfo,
  GPUMemoryRequirements     *outRequirements
) {
  GPUDeviceMT   *first, *second;
  MTLSizeAndAlign sizeAndAlign;

  if (!mt_interopDevices(interop, &first, &second) ||
      !firstInfo || !secondInfo || !outRequirements ||
      firstInfo->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(second);

  if (@available(macOS 10.15, iOS 13.0, *)) {
    sizeAndAlign = [first->device
      heapBufferSizeAndAlignWithLength:(NSUInteger)firstInfo->sizeBytes
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
  GPUDeviceMT        *first, *second;
  GPUBufferCreateInfo firstWrapInfo, secondWrapInfo;
  id<MTLBuffer>       nativeBuffer;
  GPUResult           result;

  if (!mt_interopDevices(interop, &first, &second) ||
      !firstInfo || !secondInfo ||
      !outFirstBuffer || !outSecondBuffer ||
      firstInfo->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(second);

  nativeBuffer = [first->device
    newBufferWithLength:(NSUInteger)firstInfo->sizeBytes
                options:MTLResourceStorageModeShared];
  if (!nativeBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  firstWrapInfo       = *firstInfo;
  firstWrapInfo.label = firstInfo->label ? firstInfo->label : secondInfo->label;
  result = mt_wrapBuffer(interop->firstDevice,
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
mt_getSharedTextureRequirements(
  GPUDeviceInteropEXT        *interop,
  const GPUTextureCreateInfo *firstInfo,
  const GPUTextureCreateInfo *secondInfo,
  GPUMemoryRequirements      *outRequirements
) {
  GPUTextureCreateInfo  mergedInfo;
  GPUDeviceMT          *first, *second;
  MTLTextureDescriptor *desc;
  MTLPixelFormat        stencilCopyFormat;
  MTLSizeAndAlign       sizeAndAlign;
  GPUResult             result;

  if (!mt_interopDevices(interop, &first, &second) ||
      !firstInfo || !secondInfo || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(second);

  mergedInfo       = *firstInfo;
  mergedInfo.usage = firstInfo->usage | secondInfo->usage;
  result = mt_createTextureDescriptor(interop->firstDevice,
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
  GPUTextureCreateInfo mergedInfo, secondWrapInfo;
  GPUDeviceMT         *first, *second;
  GPUTextureMT        *native;
  MTLPixelFormat       stencilCopyFormat;
  GPUResult            result;

  if (!mt_interopDevices(interop, &first, &second) ||
      !firstInfo || !secondInfo ||
      !outFirstTexture || !outSecondTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(first);
  GPU__UNUSED(second);

  mergedInfo       = *firstInfo;
  mergedInfo.label = firstInfo->label ? firstInfo->label : secondInfo->label;
  mergedInfo.usage = firstInfo->usage | secondInfo->usage;
  result = mt_createTexture(interop->firstDevice,
                            &mergedInfo,
                            outFirstTexture);
  if (result != GPU_OK) {
    return result;
  }

  (*outFirstTexture)->usage = firstInfo->usage;
  native                    = (*outFirstTexture)->_priv;
  if (!native || !native->texture) {
    mt_destroyTexture(*outFirstTexture);
    *outFirstTexture = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  stencilCopyFormat = native->stencilCopyView
                        ? native->stencilCopyView.pixelFormat
                        : MTLPixelFormatInvalid;
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
  GPUDeviceMT       *first, *second;
  id<MTLSharedEvent> event;

  if (!mt_interopDevices(interop, &first, &second) ||
      !firstSemaphore || !secondSemaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(second);

  if (@available(macOS 10.14, iOS 12.0, *)) {
    event = [first->device newSharedEvent];
    if (!event) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    event.signaledValue = info ? info->initialValue : 0u;
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (gpuDeviceDebugMarkersEnabled(interop->firstDevice) &&
        info && info->label && info->label[0] != '\0') {
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
mt_encodeSharedBarriers(GPUDeviceInteropEXT           *interop,
                        GPUCommandBuffer               *cmdb,
                        const GPUSharedBarrierBatchEXT *barriers,
                        bool                            acquire) {
  GPUDeviceMT *first, *second;
  uint32_t     bufferOffset, textureOffset;

  if (!mt_interopDevices(interop, &first, &second) || !cmdb || !barriers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(first);
  GPU__UNUSED(second);

  bufferOffset  = 0u;
  textureOffset = 0u;
  while (bufferOffset < barriers->bufferBarrierCount ||
         textureOffset < barriers->textureBarrierCount) {
    GPUBufferBarrier  bufferBarriers[MT_SHARED_BARRIER_CHUNK_SIZE];
    GPUTextureBarrier textureBarriers[MT_SHARED_BARRIER_CHUNK_SIZE];
    GPUBarrierBatch   batch = {0};
    uint32_t          bufferCount, textureCount;

    bufferCount = barriers->bufferBarrierCount - bufferOffset;
    if (bufferCount > MT_SHARED_BARRIER_CHUNK_SIZE) {
      bufferCount = MT_SHARED_BARRIER_CHUNK_SIZE;
    }
    textureCount = barriers->textureBarrierCount - textureOffset;
    if (textureCount > MT_SHARED_BARRIER_CHUNK_SIZE) {
      textureCount = MT_SHARED_BARRIER_CHUNK_SIZE;
    }

    for (uint32_t i = 0u; i < bufferCount; i++) {
      const GPUSharedBufferBarrierEXT *shared;
      GPUBufferBarrier                *barrier;

      shared             = &barriers->pBufferBarriers[bufferOffset + i];
      barrier            = &bufferBarriers[i];
      barrier->buffer    = acquire
                             ? shared->destinationBuffer
                             : shared->sourceBuffer;
      barrier->srcAccess = acquire ? GPU_ACCESS_NONE : shared->srcAccess;
      barrier->dstAccess = acquire ? shared->dstAccess : GPU_ACCESS_NONE;
      barrier->offset    = shared->offset;
      barrier->sizeBytes = shared->sizeBytes;
    }
    for (uint32_t i = 0u; i < textureCount; i++) {
      const GPUSharedTextureBarrierEXT *shared;
      GPUTextureBarrier                *barrier;

      shared              = &barriers->pTextureBarriers[textureOffset + i];
      barrier             = &textureBarriers[i];
      barrier->texture    = acquire
                              ? shared->destinationTexture
                              : shared->sourceTexture;
      barrier->srcAccess  = acquire ? GPU_ACCESS_NONE : shared->srcAccess;
      barrier->dstAccess  = acquire ? shared->dstAccess : GPU_ACCESS_NONE;
      barrier->baseMip    = shared->baseMip;
      barrier->mipCount   = shared->mipCount;
      barrier->baseLayer  = shared->baseLayer;
      barrier->layerCount = shared->layerCount;
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
mt_encodeSharedRelease(GPUDeviceInteropEXT           *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers) {
  return mt_encodeSharedBarriers(interop, cmdb, barriers, false);
}

static GPUResult
mt_encodeSharedAcquire(GPUDeviceInteropEXT           *interop,
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
