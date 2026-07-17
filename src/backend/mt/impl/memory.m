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
mt_createHeap(GPUDevice               *device,
              const GPUHeapCreateInfo *info,
              GPUHeap                **outHeap) {
  GPUDeviceMT       *deviceMT;
  MTLHeapDescriptor *desc;
  id<MTLHeap>        nativeHeap;
  GPUHeap           *heap;
  GPUHeapMT         *native;
  uint64_t           compatibility;

  if (!device || !(deviceMT = device->_priv) || !info || !outHeap ||
      info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  compatibility = UINT64_C(1);
  if ((info->compatibilityMask & compatibility) == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15, iOS 13.0, *)) {
    desc                    = [MTLHeapDescriptor new];
    desc.size               = (NSUInteger)info->sizeBytes;
    desc.storageMode        = MTLStorageModePrivate;
    desc.type               = MTLHeapTypePlacement;
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

GPU_HIDE
void
mt_initMemory(GPUApiMemory *api) {
  api->getBufferRequirements  = mt_getBufferMemoryRequirements;
  api->getTextureRequirements = mt_getTextureMemoryRequirements;
  api->createHeap             = mt_createHeap;
  api->destroyHeap            = mt_destroyHeap;
  api->createPlacedBuffer     = mt_createPlacedBuffer;
  api->createPlacedTexture    = mt_createPlacedTexture;
}
