/*
 * Copyright (C) 2026 Recep Aslantas
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

/* destroyed objects the GPU may still use, over the device's queues
 * (api/retire_internal.h has the scheme) */

#include "../common.h"
#include "../impl.h"

GPU_HIDE
bool
vk_initRetire(GPUDeviceVk *deviceVk) {
  return gpuRetireInit(&deviceVk->retire, deviceVk);
}

GPU_HIDE
void
vk_finishRetire(GPUDeviceVk *deviceVk) {
  gpuRetireFinish(&deviceVk->retire);
}

GPU_HIDE
void
vk_destroyRetire(GPUDeviceVk *deviceVk) {
  gpuRetireDestroy(&deviceVk->retire);
}

GPU_HIDE
void
vk_retire(GPUDeviceVk       *deviceVk,
          GPURetired        *node,
          void              *owner,
          GPURetireReleaseFn release) {
  if (!deviceVk) {
    release(NULL, owner);
    return;
  }

  gpuRetire(&deviceVk->retire,
            deviceVk->createdQueues,
            deviceVk->nCreatedQueues,
            node,
            owner,
            release);
}

GPU_HIDE
void
vk_queueDone(GPUDeviceVk *deviceVk, GPUQueueVk *queue, uint64_t serial) {
  if (!deviceVk || !queue)
    return;

  gpuRetireQueueDone(&deviceVk->retire,
                     deviceVk->createdQueues,
                     deviceVk->nCreatedQueues,
                     queue->queue,
                     serial);
}
