/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_cmdqueue_h
#define gpu_cmdqueue_h

#include "common.h"

typedef struct GPUCommandQueue {
  void *priv;
} GPUCommandQueue;

GPU_EXPORT
GPUCommandQueue*
gpu_cmdqueue_new(GPUDevice * __restrict device);

#endif /* gpu_cmdqueue_h */
