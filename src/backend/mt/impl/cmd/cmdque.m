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

static
GPU_HIDE
void
gpu_cmdoncomplete(GPUCommandBuffer * __restrict cmdb, void *mtlCmdb);

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer * __restrict cmdb,
                     void             * __restrict sender,
                     GPUCommandBufferOnCompleteFn  oncomplete);

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
void
mt_destroyCommandQueue(GPUCommandQueue * __restrict queue) {
  if (!queue) {
    return;
  }

  if (queue->_priv) {
    [(id<MTLCommandQueue>)queue->_priv release];
  }
  free(queue);
}

GPU_HIDE
GPUCommandQueue*
mt_getCommandQueue(GPUDevice * __restrict device,
                   GPUQueueFlagBits       bits,
                   uint32_t               index) {
  GPUCommandQueue *que;
  GPUDeviceMT     *deviceMT;
  uint32_t          matchIndex;
  uint32_t          i;

  deviceMT = device->_priv;
  matchIndex = 0;

  for (i = 0; i < deviceMT->nCreatedQueues; i++) {
    que = deviceMT->createdQueues[i];
    if (que && (que->bits & bits) == bits) {
      if (matchIndex == index) {
        return que;
      }
      matchIndex++;
    }
  }

  return NULL;
}

GPU_HIDE
GPUCommandBuffer*
mt_newCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    void             * __restrict sender,
                    GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCommandBuffer *cb;
  id<MTLCommandBuffer> mcb;
  
  mcb       = [(id<MTLCommandQueue>)cmdb->_priv commandBuffer];
  cb       = calloc(1, sizeof(*cb));
  cb->_priv = mcb;
  
  if (oncomplete)
    mt_ccmdbufOnComplete(cb, sender, oncomplete);
  
  return cb;
}

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer * __restrict cmdb,
                     void             * __restrict sender,
                     GPUCommandBufferOnCompleteFn  oncomplete) {
  if (!cmdb || cmdb->_submitted) {
    return;
  }

  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = oncomplete;
}

GPU_HIDE
void
mt_cmdbufCommit(GPUCommandBuffer * __restrict cmdb) {
  id<MTLCommandBuffer> mcb;

  if (!cmdb || cmdb->_submitted) {
    return;
  }

  mcb = (id<MTLCommandBuffer>)cmdb->_priv;
  cmdb->_submitted = true;

  [mcb addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
    gpu_cmdoncomplete(cmdb, (__bridge void *)buffer);
  }];
  [mcb commit];
}

static
GPU_HIDE
void
gpu_cmdoncomplete(GPUCommandBuffer * __restrict cmdb, void *mtlCmdb) {
  GPU__UNUSED(mtlCmdb);

  if (!cmdb) {
    return;
  }

  if (cmdb->_onComplete) {
    cmdb->_onComplete(cmdb->_onCompleteSender, cmdb);
  }

  free(cmdb);
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
