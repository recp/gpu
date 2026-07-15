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
#include "buffer_internal.h"
#include "device_internal.h"
#include "render/rce_internal.h"
#include "vrs_internal.h"

#include <math.h>

static bool
gpu_validShadingRate(GPUShadingRateEXT rate) {
  switch (rate) {
    case GPU_SHADING_RATE_1X1_EXT:
    case GPU_SHADING_RATE_1X2_EXT:
    case GPU_SHADING_RATE_2X1_EXT:
    case GPU_SHADING_RATE_2X2_EXT:
    case GPU_SHADING_RATE_2X4_EXT:
    case GPU_SHADING_RATE_4X2_EXT:
    case GPU_SHADING_RATE_4X4_EXT:
      return true;
    default:
      return false;
  }
}

static bool
gpu_validRateMapInfo(const GPURasterizationRateMapCreateInfoEXT *info) {
  if (!info ||
      info->chain.sType !=
        GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_CREATE_INFO_EXT ||
      (info->chain.structSize != 0u &&
       info->chain.structSize < sizeof(*info)) ||
      info->chain.pNext ||
      info->screenSize.width == 0u || info->screenSize.height == 0u ||
      info->layerCount == 0u || !info->pLayers) {
    return false;
  }

  for (uint32_t i = 0u; i < info->layerCount; i++) {
    const GPURasterizationRateLayerEXT *layer;

    layer = &info->pLayers[i];
    if (!layer->pHorizontal || !layer->pVertical ||
        layer->horizontalCount < 2u || layer->verticalCount < 2u) {
      return false;
    }
    for (uint32_t j = 0u; j < layer->horizontalCount; j++) {
      if (!(layer->pHorizontal[j] > 0.0f &&
            layer->pHorizontal[j] <= 1.0f)) {
        return false;
      }
    }
    for (uint32_t j = 0u; j < layer->verticalCount; j++) {
      if (!(layer->pVertical[j] > 0.0f &&
            layer->pVertical[j] <= 1.0f)) {
        return false;
      }
    }
  }
  return true;
}

GPU_EXPORT
GPUResult
GPUGetVRSCapabilitiesEXT(const GPUAdapter      *adapter,
                         GPUVRSCapabilitiesEXT *outCaps) {
  GPUApi *api;

  if (!adapter || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  api = gpuAdapterApi(adapter);
  if (!api || !api->vrs.getCapabilities) {
    return GPU_ERROR_UNSUPPORTED;
  }

  api->vrs.getCapabilities(adapter, outCaps);
  return outCaps->modes != 0u ? GPU_OK : GPU_ERROR_UNSUPPORTED;
}

GPU_EXPORT
GPUResult
GPUCreateRasterizationRateMapEXT(
  GPUDevice                                  *device,
  const GPURasterizationRateMapCreateInfoEXT *info,
  GPURasterizationRateMapEXT                **outMap) {
  GPUApi    *api;
  GPUResult  result;

  if (!outMap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outMap = NULL;
  if (!device || !gpu_validRateMapInfo(info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_VARIABLE_RATE_SHADING)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  api = gpuDeviceApi(device);
  if (!api || !api->vrs.createRateMap) {
    return GPU_ERROR_UNSUPPORTED;
  }
  result = api->vrs.createRateMap(device, info, outMap);
  if (result == GPU_OK && !*outMap) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return result;
}

GPU_EXPORT
void
GPUDestroyRasterizationRateMapEXT(GPURasterizationRateMapEXT *map) {
  GPUApi *api;

  if (!map || !map->device) {
    return;
  }
  api = gpuDeviceApi(map->device);
  if (api && api->vrs.destroyRateMap) {
    api->vrs.destroyRateMap(map);
  }
}

GPU_EXPORT
GPUResult
GPUGetRasterizationRateMapPhysicalSizeEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                           layer,
  GPUExtent2D                       *outSize) {
  GPUApi *api;

  if (!map || !map->device || !outSize || layer >= map->layerCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(map->device);
  if (!api || !api->vrs.getRateMapPhysicalSize) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return api->vrs.getRateMapPhysicalSize(map, layer, outSize);
}

static bool
gpu_validRateMapCoordinate(GPUCoordinate2D coordinate) {
  return isfinite(coordinate.x) && isfinite(coordinate.y) &&
         coordinate.x >= 0.0f && coordinate.y >= 0.0f;
}

GPU_EXPORT
GPUResult
GPUMapRasterizationRateScreenToPhysicalEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                           layer,
  GPUCoordinate2D                    screen,
  GPUCoordinate2D                   *outPhysical) {
  GPUApi *api;

  if (!map || !map->device || !outPhysical ||
      layer >= map->layerCount || !gpu_validRateMapCoordinate(screen) ||
      screen.x > (float)map->screenSize.width ||
      screen.y > (float)map->screenSize.height) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(map->device);
  if (!api || !api->vrs.mapRateMapScreenToPhysical) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return api->vrs.mapRateMapScreenToPhysical(map,
                                             layer,
                                             screen,
                                             outPhysical);
}

GPU_EXPORT
GPUResult
GPUMapRasterizationRatePhysicalToScreenEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                           layer,
  GPUCoordinate2D                    physical,
  GPUCoordinate2D                   *outScreen) {
  GPUExtent2D physicalSize;
  GPUApi     *api;
  GPUResult   result;

  if (!map || !map->device || !outScreen ||
      layer >= map->layerCount || !gpu_validRateMapCoordinate(physical)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = GPUGetRasterizationRateMapPhysicalSizeEXT(map,
                                                      layer,
                                                      &physicalSize);
  if (result != GPU_OK) {
    return result;
  }
  if (physical.x > (float)physicalSize.width ||
      physical.y > (float)physicalSize.height) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(map->device);
  if (!api || !api->vrs.mapRateMapPhysicalToScreen) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return api->vrs.mapRateMapPhysicalToScreen(map,
                                             layer,
                                             physical,
                                             outScreen);
}

GPU_EXPORT
GPUResult
GPUGetRasterizationRateMapParameterInfoEXT(
  const GPURasterizationRateMapEXT         *map,
  GPURasterizationRateMapParameterInfoEXT  *outInfo) {
  GPUApi    *api;
  GPUResult  result;

  if (!map || !map->device || !outInfo) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outInfo, 0, sizeof(*outInfo));
  api = gpuDeviceApi(map->device);
  if (!api || !api->vrs.getRateMapParameterInfo) {
    return GPU_ERROR_UNSUPPORTED;
  }
  result = api->vrs.getRateMapParameterInfo(map, outInfo);
  if (result == GPU_OK &&
      (outInfo->sizeBytes == 0u || outInfo->alignment == 0u ||
       (outInfo->alignment & (outInfo->alignment - 1u)) != 0u)) {
    memset(outInfo, 0, sizeof(*outInfo));
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return result;
}

GPU_EXPORT
GPUResult
GPUCopyRasterizationRateMapParametersEXT(
  const GPURasterizationRateMapEXT *map,
  GPUBuffer                        *buffer,
  uint64_t                          offset) {
  GPURasterizationRateMapParameterInfoEXT info;
  GPUApi                                  *api;
  GPUResult                                result;

  if (!map || !map->device || !buffer || buffer->device != map->device ||
      !gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_UNIFORM)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = GPUGetRasterizationRateMapParameterInfoEXT(map, &info);
  if (result != GPU_OK) {
    return result;
  }
  if ((offset & (info.alignment - 1u)) != 0u ||
      !gpuBufferRangeValid(buffer, offset, info.sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(map->device);
  if (!api || !api->vrs.copyRateMapParameters) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return api->vrs.copyRateMapParameters(map, buffer, offset);
}

GPU_EXPORT
void
GPUSetFragmentShadingRateEXT(
  GPURenderPassEncoder      *pass,
  GPUShadingRateEXT          rate,
  GPUShadingRateCombinerEXT  primitiveCombiner,
  GPUShadingRateCombinerEXT  attachmentCombiner) {
  GPUDevice *device;
  GPUApi    *api;

  if (!pass || pass->_ended || !gpu_validShadingRate(rate) ||
      primitiveCombiner < GPU_SHADING_RATE_COMBINER_KEEP_EXT ||
      primitiveCombiner > GPU_SHADING_RATE_COMBINER_MAX_EXT ||
      attachmentCombiner < GPU_SHADING_RATE_COMBINER_KEEP_EXT ||
      attachmentCombiner > GPU_SHADING_RATE_COMBINER_MAX_EXT) {
    return;
  }
  device = pass->_device;
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_VARIABLE_RATE_SHADING)) {
    return;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->rce.setFragmentShadingRate) {
    return;
  }
  api->rce.setFragmentShadingRate(pass,
                                  rate,
                                  primitiveCombiner,
                                  attachmentCombiner);
}
