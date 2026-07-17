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
      (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
       info->chain.sType != GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO) ||
      (info->chain.structSize != 0u &&
       info->chain.structSize < sizeof(*info))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)) {
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

  (*outHeap)->sizeBytes = info->sizeBytes;
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
  if (!heap || heap->device != device) {
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
  if (!heap || heap->device != device) {
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
