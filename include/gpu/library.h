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

#ifndef gpu_library_h
#define gpu_library_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "pixelformat.h"
#include "device.h"
//#include <us/us.h>

typedef struct GPULibrary {
  void *_priv;
} GPULibrary;

typedef struct GPUFunction {
  void *_priv;
} GPUFunction;

GPU_EXPORT
GPULibrary*
GPUDefaultLibrary(GPUDevice *device);

GPU_EXPORT
GPUFunction*
GPUShaderFunction(GPULibrary *lib, const char *name);

#ifdef __cplusplus
}
#endif
#endif /* gpu_library_h */
