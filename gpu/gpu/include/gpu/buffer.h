/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_buffer_h
#define gpu_buffer_h

#include "common.h"
#include "resource.h"

typedef struct GPUBuffer {
  void *priv;
} GPUBuffer;

GPU_EXPORT
GPUBuffer*
gpu_buffer_new(GPUDevice * __restrict device,
               size_t                 len,
               GPUResourceOptions     options);

#endif /* gpu_buffer_h */
