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

typedef enum GPUResult {
  GPU_OK                     = 0,
  GPU_ERROR_INVALID_ARGUMENT = -1,
  GPU_ERROR_BACKEND_FAILURE  = -2
} GPUResult;

typedef enum GPUQueueFlagBits {
  GPU_QUEUE_GRAPHICS_BIT         = 0x00000001,
  GPU_QUEUE_COMPUTE_BIT          = 0x00000002,
  GPU_QUEUE_TRANSFER_BIT         = 0x00000004,
  GPU_QUEUE_SPARSE_BINDING_BIT   = 0x00000008,
  GPU_QUEUE_PROTECTED_BIT        = 0x00000010,

  /* TODO: */
  GPU_QUEUE_VIDEO_DECODE_BIT_KHR = 0x00000020,
  GPU_QUEUE_VIDEO_ENCODE_BIT_KHR = 0x00000040,
  GPU_QUEUE_OPTICAL_FLOW_BIT_NV  = 0x00000100,
} GPUQueueFlagBits;

#define GPU_QUEUE_GRAPHICS GPU_QUEUE_GRAPHICS_BIT
#define GPU_QUEUE_COMPUTE  GPU_QUEUE_COMPUTE_BIT
#define GPU_QUEUE_TRANSFER GPU_QUEUE_TRANSFER_BIT

typedef struct GPUCommandQueueCreateInfo {
  GPUQueueFlagBits flags;
  GPUQueueFlagBits optionalFlags;
  uint32_t         count;
} GPUCommandQueueCreateInfo;

typedef struct GPUCommandQueue {
  void           *_priv;
  GPUQueueFlagBits bits;
} GPUCommandQueue;

typedef struct GPUCommandBuffer {
  void *_priv;
} GPUCommandBuffer;

typedef struct GPUQueueSubmitInfo {
  uint32_t commandBufferCount;
  GPUCommandBuffer * const *ppCommandBuffers;
} GPUQueueSubmitInfo;

typedef void (*GPUCommandBufferOnCompleteFn)(void            *__restrict sender,
                                             GPUCommandBuffer*__restrict cmdb);

GPU_EXPORT
GPUCommandQueue*
GPUNewCommandQueue(struct GPUDevice * __restrict device);

/*!
 * @brief get command queue created created with logical device creation.
 *
 * @param[in]  device init params, NULL to default.
 * @param[in]  bits   command queue type bits
 *
 * Convenience alias:
 * - `GPUGetCommandQueue(device, bits)` is a convenience wrapper over
 *   `GPUGetQueue(device, bits, 0)`.
 */
GPU_EXPORT
GPUCommandQueue*
GPUGetCommandQueue(struct GPUDevice * __restrict device, GPUQueueFlagBits bits);

GPU_EXPORT
GPUCommandQueue*
GPUGetQueue(struct GPUDevice * __restrict device,
            GPUQueueFlagBits              bits,
            uint32_t                      index);

GPU_EXPORT
GPUQueueFlagBits
GPUGetAvailQueueBits(struct GPUDevice * __restrict device);

GPU_EXPORT
GPUCommandBuffer*
GPUNewCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
GPUResult
GPUAcquireCommandBuffer(GPUCommandQueue   * __restrict cmdq,
                        const char        * __restrict label,
                        GPUCommandBuffer ** __restrict outCmdb);

GPU_EXPORT
void
gpuCommandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb);

GPU_EXPORT
GPUResult
GPUQueueSubmit(GPUCommandQueue           * __restrict cmdq,
               const GPUQueueSubmitInfo  * __restrict info);

#ifdef __cplusplus
}
#endif
#endif /* gpu_cmdqueue_h */
