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

static const uint32_t dx12_surfaceFormats[] = {
  GPU_FORMAT_RGBA16_FLOAT,
  GPU_FORMAT_RGBA8_UNORM,
  GPU_FORMAT_BGRA8_UNORM,
  GPU_FORMAT_RGB10A2_UNORM
};

static const uint32_t dx12_fifoPresentMode[] = {
  GPU_PRESENT_MODE_FIFO
};

static const uint32_t dx12_tearingPresentModes[] = {
  GPU_PRESENT_MODE_FIFO,
  GPU_PRESENT_MODE_IMMEDIATE
};

GPUSurface *
dx12_createSurface(GPUApi            * __restrict api,
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
dx12_getSurfaceCapabilities(const GPUAdapter       * __restrict adapter,
                            GPUSurface             * __restrict surface,
                            GPUSurfaceCapabilities * __restrict outCaps) {
  GPUInstanceDX12 *instance;

  if (!adapter || !adapter->inst || !surface || !outCaps ||
      (surface->type != GPU_SURFACE_WINDOWS_HWND &&
       surface->type != GPU_SURFACE_WINDOWS_COREWINDOW)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  instance                  = adapter->inst->_priv;
  outCaps->pFormats         = dx12_surfaceFormats;
  outCaps->pPresentModes    = instance && instance->allowTearing ?
    dx12_tearingPresentModes : dx12_fifoPresentMode;
  outCaps->minImageCount    = 2u;
  outCaps->maxImageCount    = 3u;
  outCaps->formatCount      = (uint32_t)GPU_ARRAY_LEN(dx12_surfaceFormats);
  outCaps->presentModeCount = instance && instance->allowTearing ?
    (uint32_t)GPU_ARRAY_LEN(dx12_tearingPresentModes) :
    (uint32_t)GPU_ARRAY_LEN(dx12_fifoPresentMode);
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroySurface(GPUSurface * __restrict surface) {
  free(surface);
}

GPU_HIDE
void
dx12_initSurface(GPUApiSurface *apiDevice) {
  apiDevice->createSurface   = dx12_createSurface;
  apiDevice->getCapabilities = dx12_getSurfaceCapabilities;
  apiDevice->destroySurface  = dx12_destroySurface;
}
