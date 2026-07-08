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
GPUGetCommandQueue(GPUDevice * __restrict device, GPUQueueFlagBits bits) {
  return GPUGetQueue(device, bits, 0);
}

GPU_EXPORT
GPUCommandQueue*
GPUGetQueue(GPUDevice * __restrict device,
            GPUQueueFlagBits       bits,
            uint32_t               index) {
  GPUApi *api;

  if (!device || bits == 0) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->cmdque.getCommandQueue)
    return NULL;

  return api->cmdque.getCommandQueue(device, bits, index);
}

GPU_EXPORT
GPUCommandQueue*
GPUNewCommandQueue(GPUDevice * __restrict device) {
  GPUApi *api;

  if (!device)
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->cmdque.newCommandQueue)
    return NULL;
  
  return api->cmdque.newCommandQueue(device);
}

GPU_EXPORT
GPUCommandBuffer*
GPUNewCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUApi *api;

  if (!cmdb)
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->cmdque.newCommandBuffer)
    return NULL;
  
  return api->cmdque.newCommandBuffer(cmdb, sender, oncomplete);
}

GPU_EXPORT
GPUResult
GPUAcquireCommandBuffer(GPUCommandQueue   * __restrict cmdq,
                        const char        * __restrict label,
                        GPUCommandBuffer ** __restrict outCmdb) {
  (void)label;

  if (!outCmdb) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = NULL;
  if (!cmdq) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = GPUNewCommandBuffer(cmdq, NULL, NULL);
  return *outCmdb ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
void
gpuCommandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUApi *api;

  if (!cmdb)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->cmdque.commandBufferOnComplete)
    return;
  
  api->cmdque.commandBufferOnComplete(cmdb, sender, oncomplete);
}

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb) {
  GPUApi *api;

  if (!cmdb)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->cmdque.commit)
    return;
  
  api->cmdque.commit(cmdb);
}

GPU_EXPORT
GPUResult
GPUQueueSubmit(GPUCommandQueue          * __restrict cmdq,
               const GPUQueueSubmitInfo * __restrict info) {
  GPUApi *api;

  if (!cmdq || !info || info->commandBufferCount == 0 || !info->ppCommandBuffers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()))
    return GPU_ERROR_BACKEND_FAILURE;
  if (!api->cmdque.commit)
    return GPU_ERROR_BACKEND_FAILURE;

  for (uint32_t i = 0; i < info->commandBufferCount; i++) {
    GPUCommandBuffer *cmdb = info->ppCommandBuffers[i];
    if (!cmdb) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    api->cmdque.commit(cmdb);
  }

  return GPU_OK;
}
