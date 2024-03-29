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

#ifndef gpu_gpudef_surface_h
#define gpu_gpudef_surface_h
#ifdef __cplusplus
extern "C" {
#endif

#include "../common.h"
#include "../gpu.h"

struct GPUApi;

typedef struct GPUApiSurface {
  GPUSurface *(*createSurface)(struct GPUApi            * __restrict api,
                               struct GPUInstance       * __restrict inst,
                               struct GPUPhysicalDevice * __restrict phyDevice,
                               void                     * __restrict nativeHandle,
                               GPUSurfaceType                        type,
                               float                                 scale);
} GPUApiSurface;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_surface_h */
