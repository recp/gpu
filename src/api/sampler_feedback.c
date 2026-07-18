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
#include "adapter_internal.h"
#include "cmdqueue_internal.h"
#include "device_internal.h"
#include "sampler_feedback_internal.h"
#include "texture_internal.h"

static bool
gpu_samplerFeedbackPowerOfTwo(uint32_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool
gpu_samplerFeedbackCreateInfoValid(
  const GPUDevice                           *device,
  const GPUSamplerFeedbackMapCreateInfoEXT *info) {
  const GPUTexture *texture;

  if (!device || !info || !(texture = info->texture) ||
      texture->device != device ||
      (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
       info->chain.sType !=
         GPU_STRUCTURE_TYPE_SAMPLER_FEEDBACK_MAP_CREATE_INFO_EXT) ||
      (info->chain.structSize != 0u &&
       info->chain.structSize < sizeof(*info)) ||
      info->chain.pNext ||
      (info->mode != GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT &&
       info->mode != GPU_SAMPLER_FEEDBACK_MIP_REGION_USED_EXT) ||
      texture->dimension != GPU_TEXTURE_DIMENSION_2D ||
      texture->sampleCount != 1u ||
      !(texture->usage & GPU_TEXTURE_USAGE_SAMPLED) ||
      !gpu_samplerFeedbackPowerOfTwo(info->mipRegionWidth) ||
      !gpu_samplerFeedbackPowerOfTwo(info->mipRegionHeight)) {
    return false;
  }

  return info->mipRegionWidth >= 4u &&
         info->mipRegionHeight >= 4u &&
         info->mipRegionWidth <= texture->width / 2u &&
         info->mipRegionHeight <= texture->height / 2u;
}

static bool
gpu_samplerFeedbackDecodeInfo(
  const GPUSamplerFeedbackMapCreateInfoEXT *info,
  GPUSamplerFeedbackDecodeInfoEXT          *outInfo) {
  const GPUTexture *texture;
  uint32_t          mipPadding;

  texture = info->texture;
  if (texture->mipLevelCount == 0u || texture->mipLevelCount > 31u) {
    return false;
  }

  outInfo->format          = GPU_FORMAT_R8_UINT;
  outInfo->width           = (texture->width + info->mipRegionWidth - 1u) /
                             info->mipRegionWidth;
  outInfo->height          = (texture->height + info->mipRegionHeight - 1u) /
                             info->mipRegionHeight;
  outInfo->mipLevelCount   = info->mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
                               ? 1u
                               : texture->mipLevelCount;
  outInfo->arrayLayerCount = gpuTextureArrayLayerCount(texture);

  if (outInfo->mipLevelCount > 1u) {
    mipPadding = 1u << (outInfo->mipLevelCount - 1u);
    if (outInfo->width < mipPadding) {
      outInfo->width = mipPadding;
    }
  }
  return outInfo->width != 0u && outInfo->height != 0u &&
         outInfo->arrayLayerCount != 0u;
}

static bool
gpu_samplerFeedbackCommandValid(const GPUCommandBuffer         *cmdb,
                                const GPUSamplerFeedbackMapEXT *map) {
  const GPUQueue *queue;

  queue = cmdb ? cmdb->_queue : NULL;
  return queue && !cmdb->_submitted && !cmdb->_activeEncoder && map &&
         map->device == queue->_device &&
         (queue->bits & (GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT));
}

static bool
gpu_samplerFeedbackTextureValid(const GPUSamplerFeedbackMapEXT *map,
                                const GPUTexture               *texture,
                                GPUTextureUsageFlags            usage) {
  const GPUSamplerFeedbackDecodeInfoEXT *info;

  if (!map || !texture || texture->device != map->device) {
    return false;
  }
  info = &map->decodeInfo;
  return texture->dimension == GPU_TEXTURE_DIMENSION_2D &&
         texture->format == info->format &&
         texture->width == info->width &&
         texture->height == info->height &&
         gpuTextureArrayLayerCount(texture) == info->arrayLayerCount &&
         texture->mipLevelCount == info->mipLevelCount &&
         texture->sampleCount == 1u && (texture->usage & usage) == usage;
}

GPU_EXPORT
GPUResult
GPUGetSamplerFeedbackPropertiesEXT(
  const GPUAdapter                 *adapter,
  GPUSamplerFeedbackPropertiesEXT *outProperties) {
  GPUApi *api;

  if (!adapter || !outProperties) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outProperties, 0, sizeof(*outProperties));
  api = gpuAdapterApi(adapter);
  if (!api || !api->samplerFeedback.getProperties) {
    return GPU_ERROR_UNSUPPORTED;
  }

  api->samplerFeedback.getProperties(adapter, outProperties);
  return outProperties->tier != GPU_SAMPLER_FEEDBACK_TIER_NONE_EXT
           ? GPU_OK
           : GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUCreateSamplerFeedbackMapEXT(
  GPUDevice                                *device,
  const GPUSamplerFeedbackMapCreateInfoEXT *info,
  GPUSamplerFeedbackMapEXT                **outMap) {
  GPUSamplerFeedbackMapEXT *map;
  GPUApi                   *api;
  GPUResult                 result;

  if (!outMap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outMap = NULL;
  if (!gpu_samplerFeedbackCreateInfoValid(device, info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_SAMPLER_FEEDBACK)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->samplerFeedback.create) {
    return GPU_ERROR_UNSUPPORTED;
  }

  map = calloc(1, sizeof(*map));
  if (!map) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  map->device          = device;
  map->texture         = info->texture;
  map->mode            = info->mode;
  map->mipRegionWidth  = info->mipRegionWidth;
  map->mipRegionHeight = info->mipRegionHeight;
  if (!gpu_samplerFeedbackDecodeInfo(info, &map->decodeInfo)) {
    free(map);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = api->samplerFeedback.create(device, info, map);
  if (result != GPU_OK) {
    free(map);
    return result;
  }
  if (!map->_priv) {
    if (api->samplerFeedback.destroy) {
      api->samplerFeedback.destroy(map);
    }
    free(map);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outMap = map;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroySamplerFeedbackMapEXT(GPUSamplerFeedbackMapEXT *map) {
  GPUApi *api;

  if (!map) {
    return;
  }
  api = gpuDeviceApi(map->device);
  if (api && api->samplerFeedback.destroy) {
    api->samplerFeedback.destroy(map);
  }
  free(map);
}

GPU_EXPORT
GPUResult
GPUGetSamplerFeedbackDecodeInfoEXT(
  const GPUSamplerFeedbackMapEXT *map,
  GPUSamplerFeedbackDecodeInfoEXT *outInfo) {
  if (!map || !outInfo) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outInfo = map->decodeInfo;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUClearSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                           GPUSamplerFeedbackMapEXT *map) {
  GPUApi *api;

  if (!gpu_samplerFeedbackCommandValid(cmdb, map)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuCommandBufferApi(cmdb);
  return api && api->samplerFeedback.clear
           ? api->samplerFeedback.clear(cmdb, map)
           : GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUDecodeSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                            GPUSamplerFeedbackMapEXT *map,
                            GPUTexture               *decodedTexture) {
  GPUApi *api;

  if (!gpu_samplerFeedbackCommandValid(cmdb, map) ||
      !gpu_samplerFeedbackTextureValid(map,
                                       decodedTexture,
                                       GPU_TEXTURE_USAGE_COPY_DST)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuCommandBufferApi(cmdb);
  return api && api->samplerFeedback.decode
           ? api->samplerFeedback.decode(cmdb, map, decodedTexture)
           : GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUEncodeSamplerFeedbackEXT(GPUCommandBuffer         *cmdb,
                            GPUTexture               *decodedTexture,
                            GPUSamplerFeedbackMapEXT *map) {
  GPUApi *api;

  if (!gpu_samplerFeedbackCommandValid(cmdb, map) ||
      !gpu_samplerFeedbackTextureValid(map,
                                       decodedTexture,
                                       GPU_TEXTURE_USAGE_COPY_SRC)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuCommandBufferApi(cmdb);
  return api && api->samplerFeedback.encode
           ? api->samplerFeedback.encode(cmdb, decodedTexture, map)
           : GPU_ERROR_UNSUPPORTED;
}
