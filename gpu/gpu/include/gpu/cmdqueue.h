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

typedef struct GPUCommandBuffer {
  void *priv;
} GPUCommandBuffer;

typedef void (*GPUCommandBufferOnCompleteFn)(void            *__restrict sender,
                                             GPUCommandBuffer*__restrict cmdb);

GPU_EXPORT
GPUCommandQueue*
gpu_cmdqueue_new(GPUDevice * __restrict device);

GPU_EXPORT
GPUCommandBuffer*
gpu_cmdbuf_new(GPUCommandQueue * __restrict cmdb);

GPU_EXPORT
void
gpu_cmdbuf_oncomplete(GPUCommandBuffer * __restrict cmdb,
                      void             * __restrict sender,
                      GPUCommandBufferOnCompleteFn  oncomplete);

#endif /* gpu_cmdqueue_h */
