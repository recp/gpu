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

#include "../common.h"

GPU_EXPORT
GPUCommandQueue*
vk_getCommandQueue(GPUDevice *device, GPUQueueFlagBits bits) {
  GPUCommandQueue   *queue;
  GPUCommandQueueVk *queueVk;
  GPUDeviceVk       *deviceVk;
  GPUCommandQueue   *fallbackQueue;
  uint32_t           i;

  deviceVk      = device->priv;
  fallbackQueue = NULL;

  for (i = 0; i < deviceVk->nCreatedQueues; i++) {
    queue   = deviceVk->createdQueues[i];
    queueVk = queue->priv;

    /* check if this queue matches the desired flags. */
    if (queueVk->createCI->flags & bits) {
      return queue;
    }

    /* if it's a graphics queue, keep it as a fallback. */
    if (queueVk->createCI->flags & GPU_QUEUE_GRAPHICS_BIT) {
      fallbackQueue = queue;
    }
  }

  return fallbackQueue;
}

GPU_EXPORT
GPUCommandQueue*
vk_newCommandQueue(GPUDevice * __restrict device) {
  return vk_getCommandQueue(
    device,
    GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT
  );
}

GPU_HIDE
void
vk_initCmdQue(GPUApiCommandQueue * apiQue) {
  apiQue->getCommandQueue = vk_getCommandQueue;
  apiQue->newCommandQueue = vk_newCommandQueue;
}
