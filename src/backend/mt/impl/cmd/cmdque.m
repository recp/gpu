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
gpu_cmdoncomplete4(GPUCommandBuffer * __restrict cmdb,
                   id                        feedback);

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
  GPUDeviceMT     *deviceMT;
  GPUCommandQueue *que;
  MTCommandQueue  *native;

  deviceMT = device->_priv;
  que = calloc(1, sizeof(*que));
  native = calloc(1, sizeof(*native));
  if (!que || !native) {
    free(native);
    free(que);
    return NULL;
  }

  native->poolLock = OS_UNFAIR_LOCK_INIT;
  native->mode = deviceMT->commandMode;
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      native->modern = [deviceMT->device newMTL4CommandQueue];
    }
  } else {
    native->classic = [deviceMT->device newCommandQueue];
  }
  if (!native->classic && !native->modern) {
    free(native);
    free(que);
    return NULL;
  }

  que->_priv = native;
  que->_device = device;

  return que;
}

GPU_HIDE
void
mt_destroyCommandQueue(GPUCommandQueue * __restrict queue) {
  MTCommandBuffer *command;
  MTCommandBuffer *next;

  if (!queue) {
    return;
  }

  if (queue->_priv) {
    MTCommandQueue *native = mt_commandQueue(queue);

    command = native->commands;
    while (command) {
      next = command->next;
      mt_destroyCommandBufferState(command);
      command = next;
    }
    [native->classic release];
    [native->modern release];
    free(native);
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
GPUResult
mt_getTimestampPeriod(GPUCommandQueue *queue,
                      double          *outNanosecondsPerTick) {
  GPUDeviceMT    *deviceMT;
  MTCommandQueue *native;
  uint64_t        frequency;

  native   = mt_commandQueue(queue);
  deviceMT = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!native || native->mode != MTCommandMode4 || !deviceMT ||
      !deviceMT->device) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    frequency = [deviceMT->device queryTimestampFrequency];
    if (frequency == 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }

    *outNanosecondsPerTick = 1000000000.0 / (double)frequency;
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

static MTCommandBuffer *
mt_createCommandBufferState(GPUCommandQueue *cmdb, MTCommandQueue *queue) {
  GPUCommandBuffer *cb;
  MTCommandBuffer  *native;
  GPUDeviceMT      *deviceMT;

  native = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  gpuDeviceRecordHotPathAlloc(cmdb->_device, sizeof(*native));

  cb = &native->commandBuffer;
  native->owner = queue;
  native->mode = queue->mode;
  cb->_priv = native;
  cb->_queue = cmdb;

  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTLResidencySetDescriptor *residencyDesc;
      NSError                   *error;

      deviceMT = cmdb->_device->_priv;
      native->modern = [deviceMT->device newCommandBuffer];
      native->allocator = [deviceMT->device newCommandAllocator];
      residencyDesc = [MTLResidencySetDescriptor new];
      residencyDesc.label = @"gpu-command-residency";
      residencyDesc.initialCapacity = 64u;
      error = nil;
      native->residency = [deviceMT->device
        newResidencySetWithDescriptor:residencyDesc
                                 error:&error];
      [residencyDesc release];
    }
    if (!native->modern || !native->allocator ||
        !native->residency) {
      mt_destroyCommandBufferState(native);
      return NULL;
    }
  }

  os_unfair_lock_lock(&queue->poolLock);
  native->next = queue->commands;
  queue->commands = native;
  os_unfair_lock_unlock(&queue->poolLock);
  return native;
}

static MTCommandBuffer *
mt_takeCommandBufferState(GPUCommandQueue *cmdb, MTCommandQueue *queue) {
  MTCommandBuffer *native;

  os_unfair_lock_lock(&queue->poolLock);
  native = queue->freeCommands;
  if (native) {
    queue->freeCommands = native->poolNext;
    native->poolNext = NULL;
  }
  os_unfair_lock_unlock(&queue->poolLock);

  return native ? native : mt_createCommandBufferState(cmdb, queue);
}

GPU_HIDE
GPUCommandBuffer*
mt_newCommandBuffer(GPUCommandQueue  * __restrict cmdb,
                    const char       * __restrict label,
                    void             * __restrict sender,
                    GPUCommandBufferCompletionFn  oncomplete) {
  MTCommandQueue  *queue;
  GPUCommandBuffer *cb;
  MTCommandBuffer *native;
  MTUploadChunk   *upload;

  queue = mt_commandQueue(cmdb);
  if (!queue || (!queue->classic && !queue->modern)) {
    return NULL;
  }

  native = mt_takeCommandBufferState(cmdb, queue);
  if (!native) {
    return NULL;
  }

  cb = &native->commandBuffer;
  memset(cb, 0, sizeof(*cb));
  cb->_priv = native;
  cb->_queue = cmdb;
  native->pendingAfterStages = 0u;
  native->pendingBeforeStages = 0u;
  native->pendingVisibility = 0u;
  for (upload = native->uploads; upload; upload = upload->next) {
    upload->offset = 0u;
  }

  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern beginCommandBufferWithAllocator:native->allocator];
      [native->modern useResidencySet:native->residency];
    }
  } else {
    native->classic = [queue->classic commandBuffer];
  }
  if (!native->classic && !native->modern) {
    mt_recycleCommandBuffer(cb);
    return NULL;
  }
  if (label && label[0] != '\0') {
    NSString *nativeLabel = [NSString stringWithUTF8String:label];

    native->classic.label = nativeLabel;
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4CommandBuffer>)native->modern setLabel:nativeLabel];
    }
  }
  if (oncomplete) {
    mt_ccmdbufOnComplete(cb, sender, oncomplete);
  }

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
  MTCommandBuffer *native;
  MTCommandQueue  *queue;
  id<MTLCommandBuffer> mcb;

  if (!cmdb) {
    return;
  }

  native = mt_commandBuffer(cmdb);
  queue = cmdb->_queue ? mt_commandQueue(cmdb->_queue) : NULL;
  if (!native || !queue) {
    return;
  }

  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      id<CAMetalDrawable> drawable;
      id<MTL4CommandBuffer> buffers[1];
      MTL4CommitOptions *options;

      [native->residency commit];
      [native->modern endCommandBuffer];
      buffers[0] = native->modern;
      drawable = native->drawable;
      native->drawable = nil;

      options = [MTL4CommitOptions new];
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        gpu_cmdoncomplete4(cmdb, feedback);
      }];
      [queue->modern commit:buffers
                       count:1u
                     options:options];
      if (drawable) {
        [queue->modern signalDrawable:drawable];
        [drawable present];
        [drawable release];
      }
      [options release];
    }
    return;
  }

  mcb = native->classic;
  if (!mcb) {
    return;
  }

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
  gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
}

static
GPU_HIDE
void
gpu_cmdoncomplete4(GPUCommandBuffer * __restrict cmdb,
                   id                        feedback) {
  GPUDevice *device;

  if (!cmdb) {
    return;
  }

  device = cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    id<MTL4CommitFeedback> modernFeedback = feedback;

    if (modernFeedback.error && device && device->runtimeConfig.enableVerboseLogs) {
      NSLog(@"GPU Metal 4 command buffer failed: %@", modernFeedback.error);
    }
  }
  gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
}

GPU_HIDE
void
mt_initCmdQue(GPUApiCommandQueue *api) {
  api->newCommandQueue         = mt_newCommandQueue;
  api->getCommandQueue         = mt_getCommandQueue;
  api->getTimestampPeriod      = mt_getTimestampPeriod;
  api->newCommandBuffer        = mt_newCommandBuffer;
  api->commandBufferOnComplete = mt_ccmdbufOnComplete;
  api->commit                  = mt_cmdbufCommit;
}
