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
