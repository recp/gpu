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

#ifndef gpu_geometry_h
#define gpu_geometry_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct GPUExtent2D {
  uint32_t width;
  uint32_t height;
} GPUExtent2D;

typedef struct GPUExtent3D {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} GPUExtent3D;

typedef struct GPUOffset2D {
  int32_t x;
  int32_t y;
} GPUOffset2D;

typedef struct GPUOffset3D {
  int32_t x;
  int32_t y;
  int32_t z;
} GPUOffset3D;

typedef struct GPURect2D {
  GPUOffset2D offset;
  GPUExtent2D extent;
} GPURect2D;

#ifdef __cplusplus
}
#endif
#endif /* gpu_geometry_h */
