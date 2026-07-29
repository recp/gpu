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
#include <TargetConditionals.h>

static const uint32_t mt_surfaceFormats[] = {
  GPU_FORMAT_BGRA8_UNORM,
  GPU_FORMAT_BGRA8_UNORM_SRGB
};

#if TARGET_OS_OSX
static const uint32_t mt_presentModes[] = {
  GPU_PRESENT_MODE_FIFO,
  GPU_PRESENT_MODE_IMMEDIATE
};
#else
static const uint32_t mt_presentModes[] = {
  GPU_PRESENT_MODE_FIFO
};
#endif

GPUSurface*
mt_createSurface(GPUApi                    * __restrict api,
                 GPUInstance               * __restrict inst,
                 const GPUSurfaceNativeInfo * __restrict info) {
  GPUSurface *surface;

  GPU__UNUSED(api);
  GPU__UNUSED(inst);

  if (!info || !info->nativeHandle ||
      (info->type != GPU_SURFACE_APPLE_NSVIEW &&
       info->type != GPU_SURFACE_APPLE_UIVIEW)) {
    return NULL;
  }

  surface        = calloc(1, sizeof(*surface));
  if (!surface) {
    return NULL;
  }

  surface->_priv = info->nativeHandle;
  surface->type  = info->type;
  surface->scale = info->scale;

  return surface;
}

static GPUResult
mt_getSurfaceCapabilities(const GPUAdapter       * __restrict adapter,
                          GPUSurface             * __restrict surface,
                          GPUSurfaceCapabilities * __restrict outCaps) {
  GPU__UNUSED(adapter);

  if (!surface || !outCaps ||
      (surface->type != GPU_SURFACE_APPLE_NSVIEW &&
       surface->type != GPU_SURFACE_APPLE_UIVIEW)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outCaps->pFormats         = mt_surfaceFormats;
  outCaps->pPresentModes    = mt_presentModes;
  outCaps->minImageCount    = 2u;
  outCaps->maxImageCount    = 3u;
  outCaps->formatCount      = (uint32_t)GPU_ARRAY_LEN(mt_surfaceFormats);
  outCaps->presentModeCount = (uint32_t)GPU_ARRAY_LEN(mt_presentModes);
  return GPU_OK;
}

GPU_HIDE
void
mt_destroySurface(GPUSurface * __restrict surface) {
  free(surface);
}

GPU_HIDE
void
mt_initSurface(GPUApiSurface * apiDevice) {
  apiDevice->createSurface   = mt_createSurface;
  apiDevice->getCapabilities = mt_getSurfaceCapabilities;
  apiDevice->destroySurface  = mt_destroySurface;
}
