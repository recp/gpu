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

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/vertex.h"
#include "../../../include/gpu/pipeline.h"
#include "../../../include/gpu/depthstencil.h"
#include "../../../include/gpu/cmdqueue.h"
#include <cmt/cmt.h>

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
gpuNewCmdQue(GPUDevice * __restrict device) {
  GPUCommandQueue *cq;
  MtCommandQueue  *mcq;
  
  mcq      = mtCommandQueueCreate(device->priv);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;
  
  return cq;
}

GPU_EXPORT
GPUCommandBuffer*
gpuNewCmdBuf(GPUCommandQueue  * __restrict cmdb,
             void             * __restrict sender,
             GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCommandBuffer *cb;
  MtCommandBuffer  *mcb;
  
  mcb      = mtCommandBufferCreate(cmdb->priv);
  cb       = calloc(1, sizeof(*cb));
  cb->priv = mcb;
  
  if (oncomplete)
    gpuCmdBufOnComplete(cb, sender, oncomplete);
  
  return cb;
}

GPU_EXPORT
void
gpuCmdBufOnComplete(GPUCommandBuffer * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCallback cb;
  cb.sender     = sender;
  cb.param      = cmdb;
  cb.onComplete = oncomplete;
  
  mtCommandBufferOnComplete(cmdb->priv, &cb, gpu_cmdoncomplete);
}

GPU_EXPORT
void
gpuCommit(GPUCommandBuffer * __restrict cmdb) {
  mtCommit(cmdb->priv);
}

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb) {
  GPUCallback *cb;
  
  cb = sender;
  cb->onComplete(cb->sender, cb->param);
}
