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

static const uint32_t mt_surfaceFormats[] = {
  GPU_FORMAT_BGRA8_UNORM,
  GPU_FORMAT_BGRA8_UNORM_SRGB
};

GPUSurface*
mt_createSurface(GPUApi            * __restrict api,
                 GPUInstance       * __restrict inst,
                 GPUAdapter        * __restrict adapter,
                 void              * __restrict nativeHandle,
                 GPUSurfaceType                 type,
                 float                          scale) {
  GPUSurface *surface;

  surface        = calloc(1, sizeof(*surface));
  if (!surface) {
    return NULL;
  }

  surface->_priv = nativeHandle;
  surface->type  = type;
  surface->scale = scale;

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

  outCaps->minImageCount = 2u;
  outCaps->maxImageCount = 3u;
  outCaps->formatCount   = (uint32_t)GPU_ARRAY_LEN(mt_surfaceFormats);
  outCaps->pFormats      = mt_surfaceFormats;
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
