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

#ifndef gpu_surface_internal_h
#define gpu_surface_internal_h

#include "../common.h"
#include "instance_internal.h"

struct GPUSurface {
  GPUInstance   *inst;
  void          *_priv;
  GPUSurfaceType type;
  float          scale;
};

static inline GPUApi *
gpuSurfaceApi(const GPUSurface *surface) {
  return surface ? gpuInstanceApi(surface->inst) : NULL;
}

#endif /* gpu_surface_internal_h */
