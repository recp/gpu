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

typedef struct GPUCallback {
  void                        *sender;
  void                        *param;
  GPUCommandBufferOnCompleteFn onComplete;
} GPUCallback;

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb);

GPU_EXPORT
GPUCommandQueue*
gpuNewCommandQueue(GPUDevice * __restrict device) {
  GPUCommandQueue *cq;
  MtCommandQueue  *mcq;
  
  mcq      = mtNewCommandQueue(device->priv);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;
  
  return cq;
}

GPU_EXPORT
GPUCommandBuffer*
gpuNewCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCommandBuffer *cb;
  MtCommandBuffer  *mcb;
  
  mcb      = mtNewCommandBuffer(cmdb->priv);
  cb       = calloc(1, sizeof(*cb));
  cb->priv = mcb;
  
  if (oncomplete)
    gpuCommandBufferOnComplete(cb, sender, oncomplete);
  
  return cb;
}

GPU_EXPORT
void
gpuCommandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCallback *cb;
  
  /* TODO: provide release when needed */
  cb             = calloc(1, sizeof(*cb));
  cb->sender     = sender;
  cb->param      = cmdb;
  cb->onComplete = oncomplete;
  
  mtCommandBufferOnComplete(cmdb->priv, cb, gpu_cmdoncomplete);
}

GPU_EXPORT
void
gpuCommit(GPUCommandBuffer * __restrict cmdb) {
  mtCommandBufferCommit(cmdb->priv);
}

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb) {
  GPUCallback *cb;
  
  cb = sender;
  cb->onComplete(cb->sender, cb->param);

  free(cb);
}
