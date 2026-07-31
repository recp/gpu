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
