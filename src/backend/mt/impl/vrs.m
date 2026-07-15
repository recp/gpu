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
#include "../../../api/vrs_internal.h"

static void
mt_getVRSCapabilities(const GPUAdapter      *adapter,
                      GPUVRSCapabilitiesEXT *outCaps) {
  const GPUAdapterMT *adapterMT;
  id<MTLDevice>       device;

  adapterMT = adapter ? adapter->_priv : NULL;
  if (!outCaps || !adapterMT || !(device = adapterMT->device)) {
    return;
  }
  memset(outCaps, 0, sizeof(*outCaps));
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    if (![device supportsRasterizationRateMapWithLayerCount:1u]) {
      return;
    }
    outCaps->modes = GPU_VRS_RATE_MAP_BIT_EXT;
    for (uint32_t count = 1u; count <= 32u; count++) {
      if (![device supportsRasterizationRateMapWithLayerCount:count]) {
        break;
      }
      outCaps->maxRateMapLayers = count;
    }
  }
}

static GPUResult
mt_createRateMap(GPUDevice                                  *device,
                 const GPURasterizationRateMapCreateInfoEXT *info,
                 GPURasterizationRateMapEXT                **outMap) {
  MTLRasterizationRateMapDescriptor *descriptor;
  GPURasterizationRateMapEXT         *map;
  GPUDeviceMT                        *deviceMT;
  id<MTLRasterizationRateMap>         native;

  if (!device || !(deviceMT = device->_priv) || !info || !outMap) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    if (![deviceMT->device
          supportsRasterizationRateMapWithLayerCount:info->layerCount]) {
      return GPU_ERROR_UNSUPPORTED;
    }

    descriptor = [MTLRasterizationRateMapDescriptor
      rasterizationRateMapDescriptorWithScreenSize:
        MTLSizeMake(info->screenSize.width, info->screenSize.height, 1u)];
    if (!descriptor) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    descriptor.label = info->label
                         ? [NSString stringWithUTF8String:info->label]
                         : nil;
    for (uint32_t i = 0u; i < info->layerCount; i++) {
      const GPURasterizationRateLayerEXT *source;
      MTLRasterizationRateLayerDescriptor *layer;

      source = &info->pLayers[i];
      layer = [[MTLRasterizationRateLayerDescriptor alloc]
        initWithSampleCount:MTLSizeMake(source->horizontalCount,
                                        source->verticalCount,
                                        1u)
                    horizontal:source->pHorizontal
                      vertical:source->pVertical];
      if (!layer) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
      [descriptor setLayer:layer atIndex:i];
      [layer release];
    }

    native = [deviceMT->device
      newRasterizationRateMapWithDescriptor:descriptor];
    if (!native) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    map = calloc(1, sizeof(*map));
    if (!map) {
      [native release];
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    map->_priv      = native;
    map->device     = device;
    map->screenSize = info->screenSize;
    map->layerCount = info->layerCount;
    *outMap         = map;
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_mapRateMapScreenToPhysical(const GPURasterizationRateMapEXT *map,
                              uint32_t                           layer,
                              GPUCoordinate2D                    screen,
                              GPUCoordinate2D                   *outPhysical) {
  id<MTLRasterizationRateMap> native;
  MTLCoordinate2D             mapped;

  if (!map || !outPhysical || layer >= map->layerCount ||
      !(native = map->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    mapped = [native
      mapScreenToPhysicalCoordinates:MTLCoordinate2DMake(screen.x, screen.y)
                             forLayer:layer];
    outPhysical->x = mapped.x;
    outPhysical->y = mapped.y;
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_mapRateMapPhysicalToScreen(const GPURasterizationRateMapEXT *map,
                              uint32_t                           layer,
                              GPUCoordinate2D                    physical,
                              GPUCoordinate2D                   *outScreen) {
  id<MTLRasterizationRateMap> native;
  MTLCoordinate2D             mapped;

  if (!map || !outScreen || layer >= map->layerCount ||
      !(native = map->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    mapped = [native
      mapPhysicalToScreenCoordinates:
        MTLCoordinate2DMake(physical.x, physical.y)
                             forLayer:layer];
    outScreen->x = mapped.x;
    outScreen->y = mapped.y;
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_getRateMapParameterInfo(
  const GPURasterizationRateMapEXT         *map,
  GPURasterizationRateMapParameterInfoEXT  *outInfo) {
  id<MTLRasterizationRateMap> native;
  MTLSizeAndAlign             nativeInfo;

  if (!map || !outInfo || !(native = map->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    nativeInfo         = native.parameterBufferSizeAndAlign;
    outInfo->sizeBytes = (uint64_t)nativeInfo.size;
    outInfo->alignment = (uint64_t)nativeInfo.align;
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_copyRateMapParameters(const GPURasterizationRateMapEXT *map,
                         GPUBuffer                        *buffer,
                         uint64_t                          offset) {
  id<MTLRasterizationRateMap> native;
  id<MTLBuffer>                nativeBuffer;

  if (!map || !buffer || !(native = map->_priv) ||
      !(nativeBuffer = buffer->_priv) || offset > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    [native copyParameterDataToBuffer:nativeBuffer
                               offset:(NSUInteger)offset];
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_destroyRateMap(GPURasterizationRateMapEXT *map) {
  if (!map) {
    return;
  }
  [(id<MTLRasterizationRateMap>)map->_priv release];
  free(map);
}

static GPUResult
mt_getRateMapPhysicalSize(const GPURasterizationRateMapEXT *map,
                          uint32_t                           layer,
                          GPUExtent2D                       *outSize) {
  id<MTLRasterizationRateMap> native;
  MTLSize                     size;

  if (!map || !outSize || layer >= map->layerCount ||
      !(native = map->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (@available(macOS 10.15.4, iOS 13.0, *)) {
    size            = [native physicalSizeForLayer:layer];
    outSize->width  = (uint32_t)size.width;
    outSize->height = (uint32_t)size.height;
    return GPU_OK;
  }
  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
mt_initVRS(GPUApiVRS *api) {
  api->getCapabilities                 = mt_getVRSCapabilities;
  api->createRateMap                   = mt_createRateMap;
  api->destroyRateMap                  = mt_destroyRateMap;
  api->getRateMapPhysicalSize          = mt_getRateMapPhysicalSize;
  api->mapRateMapScreenToPhysical      = mt_mapRateMapScreenToPhysical;
  api->mapRateMapPhysicalToScreen      = mt_mapRateMapPhysicalToScreen;
  api->getRateMapParameterInfo         = mt_getRateMapParameterInfo;
  api->copyRateMapParameters           = mt_copyRateMapParameters;
}
