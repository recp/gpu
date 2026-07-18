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
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

struct GPUDevice;
typedef struct GPUFence        GPUFence;
typedef struct GPUSemaphore    GPUSemaphore;
typedef struct GPUQueue         GPUQueue;
typedef struct GPUCommandBuffer GPUCommandBuffer;

typedef enum GPUQueueFlagBits {
  GPU_QUEUE_GRAPHICS_BIT = 0x00000001,
  GPU_QUEUE_COMPUTE_BIT  = 0x00000002,
  GPU_QUEUE_TRANSFER_BIT = 0x00000004
} GPUQueueFlagBits;

#define GPU_QUEUE_GRAPHICS GPU_QUEUE_GRAPHICS_BIT
#define GPU_QUEUE_COMPUTE  GPU_QUEUE_COMPUTE_BIT
#define GPU_QUEUE_TRANSFER GPU_QUEUE_TRANSFER_BIT

typedef void (*GPUCommandBufferCompletionFn)(void            *__restrict sender,
                                             GPUCommandBuffer*__restrict cmdb);

typedef struct GPUQueueSubmitInfo {
  GPUChainedStruct  chain;
  GPUCommandBuffer *const *ppCommandBuffers;
  GPUFence         *fence; /* optional; signaled after submitted buffers complete */
  uint32_t          commandBufferCount;
} GPUQueueSubmitInfo;

typedef struct GPUQueueSemaphoreWait {
  GPUSemaphore        *semaphore;
  uint64_t             value;
  GPUPipelineStageMask waitStages;
} GPUQueueSemaphoreWait;

typedef struct GPUQueueSemaphoreSignal {
  GPUSemaphore *semaphore;
  uint64_t      value;
} GPUQueueSemaphoreSignal;

typedef struct GPUQueueSubmitExInfo {
  GPUChainedStruct               chain;
  GPUCommandBuffer              *const *ppCommandBuffers;
  const GPUQueueSemaphoreWait   *pWaits;
  const GPUQueueSemaphoreSignal *pSignals;
  GPUFence                      *fence;
  uint32_t                       commandBufferCount;
  uint32_t                       waitCount;
  uint32_t                       signalCount;
} GPUQueueSubmitExInfo;

typedef struct GPUFenceCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  bool             signaled;
} GPUFenceCreateInfo;

typedef struct GPUSemaphoreCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  uint64_t         initialValue;
} GPUSemaphoreCreateInfo;

/* convenience alias for GPUGetQueue(device, bits, 0). */
GPU_EXPORT
GPUQueue*
GPUGetCommandQueue(struct GPUDevice * __restrict device, GPUQueueFlagBits bits);

GPU_EXPORT
GPUQueue*
GPUGetQueue(struct GPUDevice * __restrict device,
            GPUQueueFlagBits              bits,
            uint32_t                      index);

GPU_EXPORT
GPUResult
GPUAcquireCommandBuffer(GPUQueue          * __restrict cmdq,
                        const char        * __restrict label,
                        GPUCommandBuffer ** __restrict outCmdb);

/* Discards recorded commands and consumes the command buffer. */
GPU_EXPORT
GPUResult
GPUDiscardCommandBuffer(GPUCommandBuffer * __restrict cmdb);

GPU_EXPORT
void
GPUSetCommandBufferCompletionHandler(GPUCommandBuffer * __restrict cmdb,
                                     void             * __restrict sender,
                                     GPUCommandBufferCompletionFn  oncomplete);

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb);

GPU_EXPORT
GPUResult
GPUQueueSubmit(GPUQueue                 * __restrict cmdq,
               const GPUQueueSubmitInfo * __restrict info);

GPU_EXPORT
GPUResult
GPUQueueSubmitEx(GPUQueue                   * __restrict cmdq,
                 const GPUQueueSubmitExInfo * __restrict info);

GPU_EXPORT
GPUResult
GPUCreateFence(struct GPUDevice          * __restrict device,
               const GPUFenceCreateInfo  * __restrict info,
               GPUFence                 ** __restrict outFence);

GPU_EXPORT
void
GPUDestroyFence(GPUFence * __restrict fence);

GPU_EXPORT
GPUResult
GPUWaitFence(GPUFence * __restrict fence, uint64_t timeoutNs);

GPU_EXPORT
bool
GPUIsFenceSignaled(GPUFence * __restrict fence);

GPU_EXPORT
void
GPUResetFence(GPUFence * __restrict fence);

GPU_EXPORT
GPUResult
GPUCreateSemaphore(struct GPUDevice              * __restrict device,
                   const GPUSemaphoreCreateInfo  * __restrict info,
                   GPUSemaphore                 ** __restrict outSemaphore);

GPU_EXPORT
void
GPUDestroySemaphore(GPUSemaphore * __restrict semaphore);

#ifdef __cplusplus
}
#endif
#endif /* gpu_cmdqueue_h */
