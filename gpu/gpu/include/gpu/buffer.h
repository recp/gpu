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
#include "cmdqueue.h"

typedef struct GPUBuffer {
  void *priv;
} GPUBuffer;

GPU_EXPORT
GPUBuffer*
gpuBufferNew(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options);

GPU_EXPORT
void
gpuPresent(GPUCommandBuffer *cmdb, void *drawable);

#endif /* gpu_buffer_h */
