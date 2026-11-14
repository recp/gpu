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

/* destroyed buffers and textures the GPU may still use, over the device's
 * queues (api/retire_internal.h has the scheme) */

#include "../common.h"
#include "../impl.h"

GPU_HIDE
void
dx12_retire(GPUDeviceDX12     *device,
            GPURetired        *node,
            void              *owner,
            GPURetireReleaseFn release) {
  if (!device) {
    release(NULL, owner);
    return;
  }

  gpuRetire(&device->retire,
            device->createdQueues,
            device->nCreatedQueues,
            node,
            owner,
            release);
}

GPU_HIDE
void
dx12_queueDone(GPUDeviceDX12 *device, GPUQueueDX12 *queue, uint64_t serial) {
  if (!device || !queue)
    return;

  gpuRetireQueueDone(&device->retire,
                     device->createdQueues,
                     device->nCreatedQueues,
                     queue->queue,
                     serial);
}
