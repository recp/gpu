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

#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "multigpu_internal.h"
#include "texture_internal.h"

static bool
gpuSharedMemoryRequirementsValid(const GPUMemoryRequirements *requirements) {
  return requirements &&
         requirements->sizeBytes > 0u &&
         requirements->alignmentBytes > 0u &&
         (requirements->alignmentBytes &
          (requirements->alignmentBytes - 1u)) == 0u &&
         requirements->compatibilityMask != 0u;
}

static bool
gpuSharedStageMaskValid(GPUPipelineStageMask stages) {
  const uint32_t known = GPU_STAGE_TOP |
                         GPU_STAGE_VERTEX |
                         GPU_STAGE_FRAGMENT |
                         GPU_STAGE_COMPUTE |
                         GPU_STAGE_TRANSFER |
                         GPU_STAGE_BOTTOM;

  return stages != 0u && (((uint32_t)stages & ~known) == 0u);
}

static bool
gpuSharedAccessMaskValid(GPUAccessMask access) {
  const uint32_t known = GPU_ACCESS_SHADER_READ |
                         GPU_ACCESS_SHADER_WRITE |
                         GPU_ACCESS_COLOR_READ |
                         GPU_ACCESS_COLOR_WRITE |
                         GPU_ACCESS_DEPTH_READ |
                         GPU_ACCESS_DEPTH_WRITE |
                         GPU_ACCESS_TRANSFER_READ |
                         GPU_ACCESS_TRANSFER_WRITE |
                         GPU_ACCESS_INDIRECT_READ;

  return (((uint32_t)access & ~known) == 0u);
}

static bool
gpuSharedTextureAccessValid(const GPUTexture *texture,
                            GPUAccessMask     access) {
  GPUTextureUsageFlags usage;

  if (!texture || !gpuSharedAccessMaskValid(access) ||
      (access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    return false;
  }

  usage = texture->usage;
  return ((access & GPU_ACCESS_SHADER_READ) == 0u ||
          (usage & (GPU_TEXTURE_USAGE_SAMPLED |
                    GPU_TEXTURE_USAGE_STORAGE)) != 0u) &&
         ((access & GPU_ACCESS_SHADER_WRITE) == 0u ||
          (usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) &&
         ((access & (GPU_ACCESS_COLOR_READ |
                     GPU_ACCESS_COLOR_WRITE)) == 0u ||
          (usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) &&
         ((access & (GPU_ACCESS_DEPTH_READ |
                     GPU_ACCESS_DEPTH_WRITE)) == 0u ||
          (usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) &&
         ((access & GPU_ACCESS_TRANSFER_READ) == 0u ||
          (usage & GPU_TEXTURE_USAGE_COPY_SRC) != 0u) &&
         ((access & GPU_ACCESS_TRANSFER_WRITE) == 0u ||
          (usage & GPU_TEXTURE_USAGE_COPY_DST) != 0u);
}

static bool
gpuSharedResourceDevicesValid(const GPUDeviceInteropEXT *interop,
                              const GPUDevice           *source,
                              const GPUDevice           *destination) {
  return interop && source && destination &&
         ((source == interop->firstDevice &&
           destination == interop->secondDevice) ||
          (source == interop->secondDevice &&
           destination == interop->firstDevice));
}

static bool
gpuSharedBarrierBatchValid(GPUDeviceInteropEXT           *interop,
                           GPUCommandBuffer               *cmdb,
                           const GPUSharedBarrierBatchEXT *barriers,
                           bool                            acquire) {
  GPUDevice *commandDevice;

  if (!interop || !cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !barriers ||
      !gpuSharedStageMaskValid(barriers->srcStages) ||
      !gpuSharedStageMaskValid(barriers->dstStages) ||
      (barriers->bufferBarrierCount > 0u && !barriers->pBufferBarriers) ||
      (barriers->textureBarrierCount > 0u && !barriers->pTextureBarriers) ||
      (barriers->bufferBarrierCount == 0u &&
       barriers->textureBarrierCount == 0u)) {
    return false;
  }

  commandDevice = gpuCommandBufferDevice(cmdb);
  for (uint32_t i = 0u; i < barriers->bufferBarrierCount; i++) {
    const GPUSharedBufferBarrierEXT *barrier;
    GPUDevice                       *sourceDevice, *destinationDevice;

    barrier           = &barriers->pBufferBarriers[i];
    sourceDevice      = barrier->sourceBuffer
                          ? barrier->sourceBuffer->device
                          : NULL;
    destinationDevice = barrier->destinationBuffer
                          ? barrier->destinationBuffer->device
                          : NULL;
    if (!gpuSharedResourceDevicesValid(interop,
                                       sourceDevice,
                                       destinationDevice) ||
        barrier->sourceBuffer->_sharedPeer != barrier->destinationBuffer ||
        barrier->destinationBuffer->_sharedPeer != barrier->sourceBuffer ||
        commandDevice != (acquire ? destinationDevice : sourceDevice) ||
        barrier->sourceBuffer->sizeBytes !=
          barrier->destinationBuffer->sizeBytes ||
        !gpuBufferRangeValid(barrier->sourceBuffer,
                             barrier->offset,
                             barrier->sizeBytes) ||
        !gpuBufferRangeValid(barrier->destinationBuffer,
                             barrier->offset,
                             barrier->sizeBytes) ||
        !gpuSharedAccessMaskValid(barrier->srcAccess) ||
        !gpuSharedAccessMaskValid(barrier->dstAccess)) {
      return false;
    }
  }

  for (uint32_t i = 0u; i < barriers->textureBarrierCount; i++) {
    const GPUSharedTextureBarrierEXT *barrier;
    GPUDevice                        *sourceDevice, *destinationDevice;

    barrier           = &barriers->pTextureBarriers[i];
    sourceDevice      = barrier->sourceTexture
                          ? barrier->sourceTexture->device
                          : NULL;
    destinationDevice = barrier->destinationTexture
                          ? barrier->destinationTexture->device
                          : NULL;
    if (!gpuSharedResourceDevicesValid(interop,
                                       sourceDevice,
                                       destinationDevice) ||
        barrier->sourceTexture->_sharedPeer != barrier->destinationTexture ||
        barrier->destinationTexture->_sharedPeer != barrier->sourceTexture ||
        commandDevice != (acquire ? destinationDevice : sourceDevice) ||
        barrier->sourceTexture->format != barrier->destinationTexture->format ||
        barrier->sourceTexture->dimension !=
          barrier->destinationTexture->dimension ||
        barrier->sourceTexture->width != barrier->destinationTexture->width ||
        barrier->sourceTexture->height != barrier->destinationTexture->height ||
        barrier->sourceTexture->depthOrLayers !=
          barrier->destinationTexture->depthOrLayers ||
        barrier->sourceTexture->mipLevelCount !=
          barrier->destinationTexture->mipLevelCount ||
        barrier->sourceTexture->sampleCount !=
          barrier->destinationTexture->sampleCount ||
        !gpuTextureSubresourceRangeValid(barrier->sourceTexture,
                                         barrier->baseMip,
                                         barrier->mipCount,
                                         barrier->baseLayer,
                                         barrier->layerCount) ||
        !gpuTextureSubresourceRangeValid(barrier->destinationTexture,
                                         barrier->baseMip,
                                         barrier->mipCount,
                                         barrier->baseLayer,
                                         barrier->layerCount) ||
        !gpuSharedTextureAccessValid(barrier->sourceTexture,
                                     barrier->srcAccess) ||
        !gpuSharedTextureAccessValid(barrier->destinationTexture,
                                     barrier->dstAccess)) {
      return false;
    }
  }
  return true;
}

static bool
gpuSharedTextureShapeEqual(const GPUTextureCreateInfo *first,
                           const GPUTextureCreateInfo *second) {
  uint32_t firstMipCount, secondMipCount;
  uint32_t firstSampleCount, secondSampleCount;

  if (!first || !second) {
    return false;
  }

  firstMipCount     = first->mipLevelCount ? first->mipLevelCount : 1u;
  secondMipCount    = second->mipLevelCount ? second->mipLevelCount : 1u;
  firstSampleCount  = first->sampleCount ? first->sampleCount : 1u;
  secondSampleCount = second->sampleCount ? second->sampleCount : 1u;
  return first->dimension == second->dimension &&
         first->format == second->format &&
         first->width == second->width &&
         first->height == second->height &&
         first->depthOrLayers == second->depthOrLayers &&
         firstMipCount == secondMipCount &&
         firstSampleCount == secondSampleCount;
}

static GPUResult
gpuValidateSharedBufferInfo(GPUDeviceInteropEXT       *interop,
                            const GPUBufferCreateInfo *firstInfo,
                            const GPUBufferCreateInfo *secondInfo) {
  GPUResult result;

  if (!interop || !interop->firstDevice || !interop->secondDevice ||
      !interop->api || !firstInfo || !secondInfo ||
      firstInfo->sizeBytes != secondInfo->sizeBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = gpuValidateBufferCreateInfo(interop->firstDevice, firstInfo);
  if (result != GPU_OK) {
    return result;
  }
  return gpuValidateBufferCreateInfo(interop->secondDevice, secondInfo);
}

static GPUResult
gpuValidateSharedTextureInfo(GPUDeviceInteropEXT        *interop,
                             const GPUTextureCreateInfo *firstInfo,
                             const GPUTextureCreateInfo *secondInfo) {
  GPUResult result;

  if (!interop || !interop->firstDevice || !interop->secondDevice ||
      !interop->api || !gpuSharedTextureShapeEqual(firstInfo, secondInfo)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = gpuValidateTextureCreateInfo(interop->firstDevice, firstInfo);
  if (result != GPU_OK) {
    return result;
  }
  return gpuValidateTextureCreateInfo(interop->secondDevice, secondInfo);
}

GPU_EXPORT
GPUResult
GPUCreateDeviceInteropEXT(GPUDevice            *firstDevice,
                          GPUDevice            *secondDevice,
                          GPUDeviceInteropEXT **outInterop) {
  GPUDeviceInteropEXT *interop;
  GPUApi              *firstApi, *secondApi;
  GPUResult            result;

  if (!outInterop) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outInterop = NULL;

  if (!firstDevice || !secondDevice || firstDevice == secondDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  firstApi  = gpuDeviceApi(firstDevice);
  secondApi = gpuDeviceApi(secondDevice);
  if (!firstApi || !secondApi ||
      ((!firstApi->multigpu.createInterop ||
        !firstApi->multigpu.destroyInterop) &&
       (secondApi == firstApi ||
        !secondApi->multigpu.createInterop ||
        !secondApi->multigpu.destroyInterop))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  interop = calloc(1, sizeof(*interop));
  if (!interop) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  interop->firstDevice  = firstDevice;
  interop->secondDevice = secondDevice;

  result = GPU_ERROR_UNSUPPORTED;
  if (firstApi->multigpu.createInterop &&
      firstApi->multigpu.destroyInterop) {
    interop->api = firstApi;
    result = firstApi->multigpu.createInterop(firstDevice,
                                               secondDevice,
                                               interop);
    if (result != GPU_OK) {
      firstApi->multigpu.destroyInterop(interop);
      interop->_priv = NULL;
    }
  }
  if (result == GPU_ERROR_UNSUPPORTED && secondApi != firstApi &&
      secondApi->multigpu.createInterop &&
      secondApi->multigpu.destroyInterop) {
    interop->api = secondApi;
    result = secondApi->multigpu.createInterop(firstDevice,
                                                secondDevice,
                                                interop);
    if (result != GPU_OK) {
      secondApi->multigpu.destroyInterop(interop);
      interop->_priv = NULL;
    }
  }
  if (result != GPU_OK) {
    free(interop);
    return result;
  }

  *outInterop = interop;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyDeviceInteropEXT(GPUDeviceInteropEXT *interop) {
  if (!interop) {
    return;
  }
  if (interop->api && interop->api->multigpu.destroyInterop) {
    interop->api->multigpu.destroyInterop(interop);
  }
  free(interop);
}

GPU_EXPORT
GPUResult
GPUGetSharedBufferMemoryRequirementsEXT(
  GPUDeviceInteropEXT       *interop,
  const GPUBufferCreateInfo *firstInfo,
  const GPUBufferCreateInfo *secondInfo,
  GPUMemoryRequirements     *outRequirements
) {
  GPUResult result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateSharedBufferInfo(interop, firstInfo, secondInfo);
  if (result != GPU_OK) {
    return result;
  }
  if (!interop->api->multigpu.getBufferRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = interop->api->multigpu.getBufferRequirements(interop,
                                                         firstInfo,
                                                         secondInfo,
                                                         outRequirements);
  if (result != GPU_OK ||
      !gpuSharedMemoryRequirementsValid(outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateSharedBufferEXT(GPUDeviceInteropEXT       *interop,
                         const GPUBufferCreateInfo *firstInfo,
                         const GPUBufferCreateInfo *secondInfo,
                         GPUBuffer                **outFirstBuffer,
                         GPUBuffer                **outSecondBuffer) {
  GPUResult result;

  if (!outFirstBuffer || !outSecondBuffer ||
      outFirstBuffer == outSecondBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstBuffer  = NULL;
  *outSecondBuffer = NULL;

  result = gpuValidateSharedBufferInfo(interop, firstInfo, secondInfo);
  if (result != GPU_OK) {
    return result;
  }
  if (!interop->api->multigpu.createBuffer) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = interop->api->multigpu.createBuffer(interop,
                                                firstInfo,
                                                secondInfo,
                                                outFirstBuffer,
                                                outSecondBuffer);
  if (result != GPU_OK) {
    GPUDestroyBuffer(*outSecondBuffer);
    GPUDestroyBuffer(*outFirstBuffer);
    *outFirstBuffer  = NULL;
    *outSecondBuffer = NULL;
    return result;
  }
  if (!*outFirstBuffer || !*outSecondBuffer ||
      (*outFirstBuffer)->device != interop->firstDevice ||
      (*outSecondBuffer)->device != interop->secondDevice) {
    GPUDestroyBuffer(*outSecondBuffer);
    GPUDestroyBuffer(*outFirstBuffer);
    *outFirstBuffer  = NULL;
    *outSecondBuffer = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  (*outFirstBuffer)->_sharedPeer  = *outSecondBuffer;
  (*outSecondBuffer)->_sharedPeer = *outFirstBuffer;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetSharedTextureMemoryRequirementsEXT(
  GPUDeviceInteropEXT        *interop,
  const GPUTextureCreateInfo *firstInfo,
  const GPUTextureCreateInfo *secondInfo,
  GPUMemoryRequirements      *outRequirements
) {
  GPUResult result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outRequirements, 0, sizeof(*outRequirements));

  result = gpuValidateSharedTextureInfo(interop, firstInfo, secondInfo);
  if (result != GPU_OK) {
    return result;
  }
  if (!interop->api->multigpu.getTextureRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = interop->api->multigpu.getTextureRequirements(interop,
                                                          firstInfo,
                                                          secondInfo,
                                                          outRequirements);
  if (result != GPU_OK ||
      !gpuSharedMemoryRequirementsValid(outRequirements)) {
    memset(outRequirements, 0, sizeof(*outRequirements));
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateSharedTextureEXT(GPUDeviceInteropEXT        *interop,
                          const GPUTextureCreateInfo *firstInfo,
                          const GPUTextureCreateInfo *secondInfo,
                          GPUTexture                **outFirstTexture,
                          GPUTexture                **outSecondTexture) {
  GPUResult result;

  if (!outFirstTexture || !outSecondTexture ||
      outFirstTexture == outSecondTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstTexture  = NULL;
  *outSecondTexture = NULL;

  result = gpuValidateSharedTextureInfo(interop, firstInfo, secondInfo);
  if (result != GPU_OK) {
    return result;
  }
  if (!interop->api->multigpu.createTexture) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = interop->api->multigpu.createTexture(interop,
                                                 firstInfo,
                                                 secondInfo,
                                                 outFirstTexture,
                                                 outSecondTexture);
  if (result != GPU_OK) {
    GPUDestroyTexture(*outSecondTexture);
    GPUDestroyTexture(*outFirstTexture);
    *outFirstTexture  = NULL;
    *outSecondTexture = NULL;
    return result;
  }
  if (!*outFirstTexture || !*outSecondTexture ||
      (*outFirstTexture)->device != interop->firstDevice ||
      (*outSecondTexture)->device != interop->secondDevice) {
    GPUDestroyTexture(*outSecondTexture);
    GPUDestroyTexture(*outFirstTexture);
    *outFirstTexture  = NULL;
    *outSecondTexture = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  (*outFirstTexture)->_sharedPeer  = *outSecondTexture;
  (*outSecondTexture)->_sharedPeer = *outFirstTexture;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateSharedSemaphoreEXT(
  GPUDeviceInteropEXT          *interop,
  const GPUSemaphoreCreateInfo *info,
  GPUSemaphore                **outFirstSemaphore,
  GPUSemaphore                **outSecondSemaphore
) {
  GPUSemaphore *firstSemaphore, *secondSemaphore;
  GPUResult     result;

  if (!interop || !interop->api ||
      !outFirstSemaphore || !outSecondSemaphore ||
      outFirstSemaphore == outSecondSemaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstSemaphore  = NULL;
  *outSecondSemaphore = NULL;

  if (info &&
      ((info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
        info->chain.sType != GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO) ||
       (info->chain.structSize != 0u &&
        info->chain.structSize < sizeof(*info)))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!interop->api->multigpu.createSemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }

  firstSemaphore  = calloc(1, sizeof(*firstSemaphore));
  secondSemaphore = calloc(1, sizeof(*secondSemaphore));
  if (!firstSemaphore || !secondSemaphore) {
    free(secondSemaphore);
    free(firstSemaphore);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  firstSemaphore->_device  = interop->firstDevice;
  secondSemaphore->_device = interop->secondDevice;

  result = interop->api->multigpu.createSemaphore(interop,
                                                   info,
                                                   firstSemaphore,
                                                   secondSemaphore);
  if (result != GPU_OK) {
    if (secondSemaphore->_priv) {
      GPUDestroySemaphore(secondSemaphore);
    } else {
      free(secondSemaphore);
    }
    if (firstSemaphore->_priv) {
      GPUDestroySemaphore(firstSemaphore);
    } else {
      free(firstSemaphore);
    }
    return result;
  }
  if (!firstSemaphore->_priv || !secondSemaphore->_priv) {
    GPUDestroySemaphore(secondSemaphore);
    GPUDestroySemaphore(firstSemaphore);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outFirstSemaphore  = firstSemaphore;
  *outSecondSemaphore = secondSemaphore;
  return GPU_OK;
}

static GPUResult
gpuEncodeSharedBarrier(GPUDeviceInteropEXT           *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers,
                       bool                            acquire) {
  if (!gpuSharedBarrierBatchValid(interop, cmdb, barriers, acquire)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (acquire) {
    return interop->api->multigpu.encodeAcquire
             ? interop->api->multigpu.encodeAcquire(interop, cmdb, barriers)
             : GPU_ERROR_UNSUPPORTED;
  }
  return interop->api->multigpu.encodeRelease
           ? interop->api->multigpu.encodeRelease(interop, cmdb, barriers)
           : GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUEncodeSharedReleaseEXT(GPUDeviceInteropEXT           *interop,
                          GPUCommandBuffer               *cmdb,
                          const GPUSharedBarrierBatchEXT *barriers) {
  return gpuEncodeSharedBarrier(interop, cmdb, barriers, false);
}

GPU_EXPORT
GPUResult
GPUEncodeSharedAcquireEXT(GPUDeviceInteropEXT           *interop,
                          GPUCommandBuffer               *cmdb,
                          const GPUSharedBarrierBatchEXT *barriers) {
  return gpuEncodeSharedBarrier(interop, cmdb, barriers, true);
}
