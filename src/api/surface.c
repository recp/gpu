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

static const uint32_t GPU_SURFACE_DEFAULT_FORMATS[] = {
  GPU_FORMAT_BGRA8_UNORM,
  GPU_FORMAT_BGRA8_UNORM_SRGB,
  GPU_FORMAT_RGBA8_UNORM,
  GPU_FORMAT_RGBA8_UNORM_SRGB,
  GPU_FORMAT_RGBA16_FLOAT
};

static bool
gpuIsSurfaceTypeValid(GPUSurfaceType type) {
  return type == GPU_SURFACE_WINDOWS_HWND ||
         type == GPU_SURFACE_WINDOWS_COREWINDOW ||
         type == GPU_SURFACE_APPLE_NSVIEW ||
         type == GPU_SURFACE_APPLE_UIVIEW;
}

GPU_EXPORT
GPUSurface*
GPUCreateSurface(GPUInstance       * __restrict inst,
                 GPUAdapter        * __restrict adapter,
                 void              * __restrict nativeHandle,
                 GPUSurfaceType                 type,
                 float                          scale) {
  GPUApi *api;

  if (!adapter || !nativeHandle || !gpuIsSurfaceTypeValid(type) ||
      !(scale > 0.0f)) {
    return NULL;
  }

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  if (!api->surface.createSurface) {
    return NULL;
  }

  return api->surface.createSurface(api, inst, adapter, nativeHandle, type, scale);
}

GPU_EXPORT
void
GPUDestroySurface(GPUSurface * __restrict surface) {
  GPUApi *api;

  if (!surface) {
    return;
  }

  if (!(api = gpuActiveGPUApi())) {
    return;
  }

  if (api->surface.destroySurface) {
    api->surface.destroySurface(surface);
  }
}

GPU_EXPORT
GPUResult
GPUGetSurfaceCapabilities(const GPUAdapter * __restrict adapter,
                          const GPUSurface * __restrict surface,
                          GPUSurfaceCapabilities * __restrict outCaps) {
  if (!adapter || !surface || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  outCaps->minImageCount = 2;
  outCaps->maxImageCount = 3;
  outCaps->formatCount = (uint32_t)GPU_ARRAY_LEN(GPU_SURFACE_DEFAULT_FORMATS);
  outCaps->pFormats = GPU_SURFACE_DEFAULT_FORMATS;

  return GPU_OK;
}
