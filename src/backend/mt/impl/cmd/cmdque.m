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
gpu_cmdoncomplete(GPUCommandBuffer * __restrict cmdb,
                  id<MTLCommandBuffer>        mtlCmdb);

static
GPU_HIDE
void
mt_logCommandBufferError(GPUCommandBuffer * __restrict cmdb,
                         id<MTLCommandBuffer>        mtlCmdb);

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer * __restrict cmdb,
                     void             * __restrict sender,
                     GPUCommandBufferCompletionFn  oncomplete);

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
  que->_device = device;

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
                    const char       * __restrict label,
                    void             * __restrict sender,
                    GPUCommandBufferCompletionFn  oncomplete) {
  GPUCommandBuffer *cb;
  id<MTLCommandBuffer> mcb;
  
  mcb       = [(id<MTLCommandQueue>)cmdb->_priv commandBuffer];
  if (label && label[0] != '\0') {
    mcb.label = [NSString stringWithUTF8String:label];
  }
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
                     GPUCommandBufferCompletionFn  oncomplete) {
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

  if (!cmdb) {
    return;
  }

  mcb = (id<MTLCommandBuffer>)cmdb->_priv;

  [mcb addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
    gpu_cmdoncomplete(cmdb, buffer);
  }];
  [mcb commit];
}

static
GPU_HIDE
void
mt_logCommandBufferError(GPUCommandBuffer * __restrict cmdb,
                         id<MTLCommandBuffer>        mtlCmdb) {
  GPUCommandQueue *queue;
  GPUDevice       *device;

  if (!cmdb || !mtlCmdb || mtlCmdb.status != MTLCommandBufferStatusError) {
    return;
  }

  queue = cmdb->_queue;
  device = queue ? queue->_device : NULL;
  if (!device || !device->runtimeConfig.enableVerboseLogs) {
    return;
  }

  if (mtlCmdb.error) {
    NSLog(@"GPU Metal command buffer failed: %@", mtlCmdb.error);
  } else {
    NSLog(@"GPU Metal command buffer failed");
  }
}

static
GPU_HIDE
void
gpu_cmdoncomplete(GPUCommandBuffer * __restrict cmdb,
                  id<MTLCommandBuffer>        mtlCmdb) {
  if (!cmdb) {
    return;
  }

  mt_logCommandBufferError(cmdb, mtlCmdb);

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
