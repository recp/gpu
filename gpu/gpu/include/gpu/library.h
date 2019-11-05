/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef gpu_library_h
#define gpu_library_h

#include "common.h"
#include "pixelformat.h"

typedef struct GPULibrary {
  void *priv;
} GPULibrary;

typedef struct GPUFunction {
  void *priv;
} GPUFunction;

GPU_EXPORT
GPULibrary*
gpu_library_default(GPUDevice *device);

GPU_EXPORT
GPUFunction*
gpu_func_create(GPULibrary *lib, const char *name);

#endif /* gpu_library_h */
