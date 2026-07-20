/*
 * Copyright (C) 2023 Recep Aslantas
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

#ifndef gpu_surface_h
#define gpu_surface_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "device.h"

typedef enum GPUSurfaceType {
  GPU_SURFACE_WINDOWS_HWND,
  GPU_SURFACE_WINDOWS_COREWINDOW,
  GPU_SURFACE_APPLE_NSVIEW,
  GPU_SURFACE_APPLE_UIVIEW,
  GPU_SURFACE_WEB_CANVAS,
} GPUSurfaceType;

typedef struct GPUSurface GPUSurface;

typedef struct GPUSurfaceCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
} GPUSurfaceCreateInfo;

typedef struct GPUNativeSurfaceCreateInfo {
  GPUChainedStruct chain;
  GPUAdapter      *adapter;
  void            *nativeHandle;
  GPUSurfaceType   type;
  float            scale;
} GPUNativeSurfaceCreateInfo;

typedef struct GPUSurfaceCapabilities {
  const uint32_t *pFormats;
  const uint32_t *pPresentModes;
  uint32_t        minImageCount;
  uint32_t        maxImageCount;
  uint32_t        formatCount;
  uint32_t        presentModeCount;
} GPUSurfaceCapabilities;

GPU_EXPORT
GPUResult
GPUCreateSurface(GPUInstance                  * __restrict inst,
                 const GPUSurfaceCreateInfo   * __restrict info,
                 GPUSurface                  ** __restrict outSurface);

GPU_EXPORT
GPUSurface*
GPUCreateSurfaceFromNative(GPUInstance       * __restrict inst,
                           GPUAdapter        * __restrict adapter,
                           void              * __restrict nativeHandle,
                           GPUSurfaceType                 type,
                           float                          scale);

GPU_EXPORT
void
GPUDestroySurface(GPUSurface * __restrict surface);

GPU_EXPORT
GPUResult
GPUGetSurfaceCapabilities(const GPUAdapter * __restrict adapter,
                          const GPUSurface * __restrict surface,
                          GPUSurfaceCapabilities * __restrict outCaps);

#ifdef __cplusplus
}
#endif
#endif /* gpu_surface */
