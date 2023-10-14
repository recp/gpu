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

typedef struct GPUCallback {
  void                        *sender;
  void                        *param;
  GPUCommandBufferOnCompleteFn onComplete;
} GPUCallback;

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb);

GPU_HIDE
GPUCommandQueue*
mt_newCommandQueue(GPUDevice * __restrict device) {
  GPUDeviceMT        *deviceMT;
  GPUCommandQueue    *que;
  id<MTLCommandQueue> queMT;

  deviceMT   = device->_priv;
  queMT      = [deviceMT->device newCommandQueue];
  que        = calloc(1, sizeof(*que));
  que->_priv = queMT;

  return que;
}

GPU_HIDE
GPUCommandQueue*
mt_getCommandQueue(GPUDevice * __restrict device,
                   GPUQueueFlagBits       bits) {
  GPUCommandQueue *que;
  GPUDeviceMT     *deviceMT;

  deviceMT = device->_priv;

  return que;
}

GPU_HIDE
GPUCommandBuffer*
mt_newCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCommandBuffer *cb;
  MtCommandBuffer  *mcb;
  
  mcb      = mtNewCommandBuffer(cmdb->_priv);
  cb       = calloc(1, sizeof(*cb));
  cb->_priv = mcb;
  
  if (oncomplete)
    gpuCommandBufferOnComplete(cb, sender, oncomplete);
  
  return cb;
}

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer * __restrict cmdb,
                     void             * __restrict sender,
                     GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCallback *cb;
  
  /* TODO: provide release when needed */
  cb             = calloc(1, sizeof(*cb));
  cb->sender     = sender;
  cb->param      = cmdb;
  cb->onComplete = oncomplete;
  
  mtCommandBufferOnComplete(cmdb->_priv, cb, gpu_cmdoncomplete);
}

GPU_HIDE
void
mt_cmdbufCommit(GPUCommandBuffer * __restrict cmdb) {
  mtCommandBufferCommit(cmdb->_priv);
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

GPU_HIDE
void
mt_initCmdQue(GPUApiCommandQueue *api) {
  api->newCommandQueue         = mt_newCommandQueue;
  api->getCommandQueue         = mt_getCommandQueue;
  api->newCommandBuffer        = mt_newCommandBuffer;
  api->commandBufferOnComplete = mt_ccmdbufOnComplete;
  api->commit                  = mt_cmdbufCommit;
}
