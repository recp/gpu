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

typedef struct GPUCommandQueueCreateInfo {
  GPUQueueFlagBits flags;
  GPUQueueFlagBits optionalFlags;
  uint32_t         count;
} GPUCommandQueueCreateInfo;

typedef struct GPUCommandQueue {
  void *_priv;
} GPUCommandQueue;

typedef struct GPUCommandBuffer {
  void *_priv;
} GPUCommandBuffer;

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
 */
GPU_EXPORT
GPUCommandQueue*
GPUGetCommandQueue(struct GPUDevice * __restrict device, GPUQueueFlagBits bits);

GPU_EXPORT
GPUQueueFlagBits
GPUGetAvailQueueBits(struct GPUDevice * __restrict device);

GPU_EXPORT
GPUCommandBuffer*
GPUNewCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
void
gpuCommandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferOnCompleteFn  oncomplete);

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb);

#ifdef __cplusplus
}
#endif
#endif /* gpu_cmdqueue_h */
