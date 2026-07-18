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

#ifndef gpu_gpudef_cmdque_h
#define gpu_gpudef_cmdque_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>

typedef struct GPUApiCommandQueue {
  GPUQueue*
  (*newCommandQueue)(GPUDevice * __restrict device);

  GPUQueue*
  (*getCommandQueue)(struct GPUDevice * __restrict device,
                     GPUQueueFlagBits              bits,
                     uint32_t                      index);

  GPUResult
  (*getTimestampPeriod)(GPUQueue *queue,
                        double   *outNanosecondsPerTick);

  GPUCommandBuffer*
  (*newCommandBuffer)(GPUQueue                  * __restrict cmdb,
                      const char                * __restrict label,
                      void                      * __restrict sender,
                      GPUCommandBufferCompletionFn  oncomplete);
  
  void
  (*commandBufferOnComplete)(GPUCommandBuffer * __restrict cmdb,
                             void             * __restrict sender,
                             GPUCommandBufferCompletionFn  oncomplete);
  
  /* Every prepared buffer must reach gpuFinishCommandBuffer exactly once. */
  GPUResult
  (*commit)(GPUCommandBuffer * __restrict cmdb);

  GPUResult
  (*submit)(GPUQueue                  * __restrict queue,
            uint32_t                               count,
            GPUCommandBuffer * const * __restrict buffers);

  GPUResult
  (*createSemaphore)(GPUDevice                       *device,
                     const GPUSemaphoreCreateInfo    *info,
                     GPUSemaphore                    *semaphore);

  void
  (*destroySemaphore)(GPUSemaphore *semaphore);

  GPUResult
  (*submitEx)(GPUQueue                   *queue,
              const GPUQueueSubmitExInfo *info);
} GPUApiCommandQueue;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_cmdque_h */
