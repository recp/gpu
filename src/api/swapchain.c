/*
 * Copyright (C) 2020 Recep Aslantas
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
#include "device_internal.h"
#include "surface_internal.h"
#include "swapchain_internal.h"
#include <math.h>

static bool
validScaledSize(uint32_t width, uint32_t height, float scale) {
  double pixelWidth, pixelHeight;

  pixelWidth  = (double)width * scale;
  pixelHeight = (double)height * scale;

  return isfinite(scale) && scale > 0.0f
         && pixelWidth >= 1.0 && pixelWidth <= UINT32_MAX
         && pixelHeight >= 1.0 && pixelHeight <= UINT32_MAX;
}

static bool
isPresentModeValid(GPUPresentMode mode) {
  return mode == GPU_PRESENT_MODE_FIFO
         || mode == GPU_PRESENT_MODE_MAILBOX
         || mode == GPU_PRESENT_MODE_IMMEDIATE;
}

static bool
surfaceSupportsFormat(const GPUSurfaceCapabilities *caps,
                      GPUFormat                     format) {
  uint32_t i;

  for (i = 0u; i < caps->formatCount; i++) {
    if (caps->pFormats[i] == (uint32_t)format) {
      return true;
    }
  }

  return false;
}

static GPUFormat
defaultSwapchainFormat(const GPUSurfaceCapabilities *caps) {
  static const GPUFormat preferred[] = {
    GPU_FORMAT_BGRA8_UNORM,
    GPU_FORMAT_RGBA8_UNORM,
    GPU_FORMAT_BGRA8_UNORM_SRGB,
    GPU_FORMAT_RGBA8_UNORM_SRGB
  };

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(preferred); i++) {
    if (surfaceSupportsFormat(caps, preferred[i])) {
      return preferred[i];
    }
  }

  return (GPUFormat)caps->pFormats[0];
}

static GPUSwapchain*
createSwapchainInternal(GPUDevice                    *__restrict device,
                        struct GPUQueue              *__restrict cmdQue,
                        const GPUSwapchainCreateInfo *__restrict info) {
  GPUApi       *api;
  GPUSwapchain *swapchain;

  if (!(api = deviceApi(device)))
    return NULL;

  if (!api->swapchain.createSwapchain)
    return NULL;

  if ((swapchain = api->swapchain.createSwapchain(api,
                                                  device,
                                                  cmdQue,
                                                  info))) {
    swapchain->device            = device;
    swapchain->width             = info->width;
    swapchain->height            = info->height;
    swapchain->format            = info->format;
    swapchain->backingScaleFactor = info->surface->scale;
    swapchainResetStatus(swapchain);
  }

  return swapchain;
}

GPU_EXPORT
GPUResult
GPUCreateSwapchain(GPUDevice                    *__restrict device,
                   const GPUSwapchainCreateInfo *__restrict info,
                   GPUSwapchain                **__restrict outSwapchain) {
  GPUSurfaceCapabilities    surfaceCaps;
  GPUFormatCapabilities     formatCaps;
  const GPUSwapchainOptions *options;
  GPUQueue                 *queue;
  GPUResult                 result;
  uint32_t                  i;
  bool                      formatSupported;
  bool                      presentModeSupported;

  if (!outSwapchain)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outSwapchain = NULL;

  if (!device || !info || !info->surface || info->width == 0 || info->height == 0)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->chain.sType != GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!device->adapter || !device->inst
      || info->surface->inst != device->inst
      || info->format <= GPU_FORMAT_UNDEFINED
      || info->format >= GPU_FORMAT_COUNT
      || !isPresentModeValid(info->presentMode)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  options = info->chain.pNext;
  if (options) {
    if (options->chain.sType != GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS)
      return GPU_ERROR_UNSUPPORTED;

    if (options->chain.structSize < sizeof(*options)
        || options->chain.pNext
        || options->colorSpace < GPU_COLOR_SPACE_DEFAULT
        || options->colorSpace > GPU_COLOR_SPACE_DISPLAY_P3)
      return GPU_ERROR_INVALID_ARGUMENT;

    if ((options->colorSpace != GPU_COLOR_SPACE_DEFAULT
         && !GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_COLOR_SPACE))
        || (options->presentsWithTransaction
            && !GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_TRANSACTIONS)))
      return GPU_ERROR_UNSUPPORTED;
  }

  if (!validScaledSize(info->width, info->height, info->surface->scale))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = GPUGetFormatCapabilities(device->adapter,
                                    info->format,
                                    &formatCaps);

  if (result != GPU_OK) {
    return result;
  }

  if (!formatCaps.colorAttachment) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = GPUGetSurfaceCapabilities(device->adapter,
                                     info->surface,
                                     &surfaceCaps);

  if (result != GPU_OK) {
    return result;
  }

  formatSupported = surfaceSupportsFormat(&surfaceCaps, info->format);

  if (!formatSupported) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (info->imageCount != 0u
      && (info->imageCount < surfaceCaps.minImageCount
          || info->imageCount > surfaceCaps.maxImageCount)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  presentModeSupported = false;

  for (i = 0u; i < surfaceCaps.presentModeCount; i++) {
    if (surfaceCaps.pPresentModes[i] == (uint32_t)info->presentMode) {
      presentModeSupported = true;
      break;
    }
  }

  if (!presentModeSupported) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0)))
    return GPU_ERROR_BACKEND_FAILURE;

  if (!(*outSwapchain = createSwapchainInternal(device,
                                                queue,
                                                info)))
    return GPU_ERROR_BACKEND_FAILURE;

  return GPU_OK;
}

GPU_EXPORT
GPUSwapchain*
GPUCreateSwapchainDefault(GPUDevice         *__restrict device,
                          struct GPUSurface *__restrict surface,
                          uint32_t                      width,
                          uint32_t                      height) {
  GPUSwapchainCreateInfo info = {0};
  GPUSurfaceCapabilities surfaceCaps;
  GPUSwapchain          *swapchain;

  if (!device || !device->adapter || !surface || width == 0u || height == 0u
      || GPUGetSurfaceCapabilities(device->adapter, surface, &surfaceCaps) != GPU_OK) {
    return NULL;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.surface          = surface;
  info.width            = width;
  info.height           = height;
  info.format           = defaultSwapchainFormat(&surfaceCaps);
  info.imageCount       = 0;
  info.presentMode      = GPU_PRESENT_MODE_FIFO;

  if (GPUCreateSwapchain(device, &info, &swapchain) != GPU_OK)
    return NULL;

  return swapchain;
}

GPU_EXPORT
GPUFormat
GPUGetSwapchainFormat(GPUSwapchain *__restrict swapchain) {
  return swapchain ? swapchain->format : GPU_FORMAT_UNDEFINED;
}

GPU_EXPORT
GPUSwapchainStatus
GPUGetSwapchainStatus(GPUSwapchain *__restrict swapchain) {
  return swapchain ? swapchain->status : GPU_SWAPCHAIN_STATUS_UNAVAILABLE;
}

GPU_EXPORT
void
GPUDestroySwapchain(GPUSwapchain *__restrict swapchain) {
  GPUApi *api;

  if (!swapchain) {
    return;
  }

  GPUDestroyFramePacer(swapchain->pacer);

  if (!(api = deviceApi(swapchain->device))) {
    return;
  }

  if (api->swapchain.destroySwapchain) {
    api->swapchain.destroySwapchain(swapchain);
  }
}

GPU_EXPORT
GPUResult
GPUResizeSwapchain(GPUSwapchain *__restrict swapchain,
                   uint32_t                 width,
                   uint32_t                 height) {
  GPUExtent2D size;
  GPUApi     *api;
  GPUResult   result;

  if (!swapchain || width == 0u || height == 0u
      || !validScaledSize(width, height, swapchain->backingScaleFactor))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!(api = deviceApi(swapchain->device)))
    return GPU_ERROR_BACKEND_FAILURE;

  if (!api->swapchain.resizeSwapchain)
    return GPU_ERROR_UNSUPPORTED;

  size.width  = width;
  size.height = height;

  result = api->swapchain.resizeSwapchain(swapchain, size);

  if (result == GPU_OK) {
    swapchain->width  = width;
    swapchain->height = height;
    swapchainResetStatus(swapchain);
  }

  return result;
}

GPU_EXPORT
GPUResult
GPUSetSwapchainScale(GPUSwapchain *swapchain, float scale) {
  GPUResult result;
  float     previous;

  if (!swapchain || !isfinite(scale) || scale <= 0.0f
      || !validScaledSize(swapchain->width, swapchain->height, scale))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!GPUIsFeatureEnabled(swapchain->device, GPU_FEATURE_SWAPCHAIN_SCALE))
    return GPU_ERROR_UNSUPPORTED;

  previous                     = swapchain->backingScaleFactor;
  swapchain->backingScaleFactor = scale;
  result = GPUResizeSwapchain(swapchain, swapchain->width, swapchain->height);
  if (result != GPU_OK)
    swapchain->backingScaleFactor = previous;

  return result;
}
