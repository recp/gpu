/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_cmdqueue_h
#define gpu_cmdqueue_h

#include "common.h"
#include "device.h"

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
gpuCmdQueNew(GPUDevice * __restrict device);

GPU_EXPORT
GPUCommandBuffer*
gpuCmdBufNew(GPUCommandQueue  * __restrict cmdb,
             void             * __restrict sender,
             GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
void
gpuCmdBufOnComplete(GPUCommandBuffer * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
void
gpuCommit(GPUCommandBuffer * __restrict cmdb);

#endif /* gpu_cmdqueue_h */
