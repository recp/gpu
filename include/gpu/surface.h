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

#include "common.h"

typedef enum GPUSurfaceType {
  GPU_SURFACE_WINDOWS_HWND,
  GPU_SURFACE_WINDOWS_COREWINDOW,
  GPU_SURFACE_APPLE_NSVIEW,
  GPU_SURFACE_APPLE_UIVIEW,
} GPUSurfaceType;

typedef struct GPUSurface {
  void         * _priv;
  GPUSurfaceType type;
  float          scale;
} GPUSurface;

GPU_EXPORT
GPUSurface*
GPUCreateSurface(GPUInstance * __restrict inst,
                 void        * __restrict nativeHandle,
                 GPUSurfaceType           type,
                 float                    scale);

#endif /* gpu_surface */
