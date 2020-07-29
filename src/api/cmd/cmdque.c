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

#include "../../common.h"

GPU_EXPORT
GPUCommandQueue*
GPUNewCommandQueue(GPUDevice * __restrict device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  
  return api->cmdque.newCommandQueue(device);
}

GPU_EXPORT
GPUCommandBuffer*
GPUNewCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  
  return api->cmdque.newCommandBuffer(cmdb, sender, oncomplete);
}

GPU_EXPORT
void
gpuCommandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->cmdque.commandBufferOnComplete(cmdb, sender, oncomplete);
}

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->cmdque.commit(cmdb);
}
