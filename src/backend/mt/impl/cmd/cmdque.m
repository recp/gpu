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
                  MTCommandBuffer  * __restrict native,
                  MTCommandQueue   * __restrict queue,
                  id<MTLCommandBuffer>        mtlCmdb);

static
GPU_HIDE
void
gpu_cmdoncomplete4(GPUCommandBuffer * __restrict cmdb,
                   MTCommandBuffer  * __restrict native,
                   MTCommandQueue   * __restrict queue,
                   id                        feedback);

static
GPU_HIDE
void
mt_reportCommandBufferError(GPUCommandBuffer * __restrict cmdb,
                            NSError          * __restrict error);

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
  native->inFlightGroup = dispatch_group_create();
  if ((!native->classic && !native->modern) || !native->inFlightGroup) {
    [native->classic release];
    [native->modern release];
    if (native->inFlightGroup) {
      dispatch_release(native->inFlightGroup);
    }
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

    dispatch_group_wait(native->inFlightGroup, DISPATCH_TIME_FOREVER);
    command = native->commands;
    while (command) {
      next = command->next;
      mt_destroyCommandBufferState(command);
      command = next;
    }
    [native->classic release];
    [native->upload release];
    [native->modern release];
    dispatch_release(native->inFlightGroup);
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
  atomic_init(&native->completionReady, false);
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
#if GPU_BUILD_WITH_DEBUG_MARKERS
      if (gpuDeviceDebugMarkersEnabled(cmdb->_device)) {
        residencyDesc.label = @"gpu-command-residency";
      }
#endif
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
  atomic_store_explicit(&native->completionReady,
                        false,
                        memory_order_relaxed);
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
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    NSString *nativeLabel = [NSString stringWithUTF8String:label];

    native->classic.label = nativeLabel;
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4CommandBuffer>)native->modern setLabel:nativeLabel];
    }
  }
#else
  GPU__UNUSED(label);
#endif
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
GPUResult
mt_cmdbufCommit(GPUCommandBuffer * __restrict cmdb) {
  MTCommandBuffer      *native;
  MTCommandQueue       *queue;
  id<MTLCommandBuffer>  mcb;

  if (!cmdb) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = mt_commandBuffer(cmdb);
  queue  = cmdb->_queue ? mt_commandQueue(cmdb->_queue) : NULL;
  if (!native || !queue) {
    gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
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
        gpu_cmdoncomplete4(cmdb, native, queue, feedback);
      }];
      dispatch_group_enter(queue->inFlightGroup);
      atomic_store_explicit(&native->completionReady,
                            true,
                            memory_order_release);
      [queue->modern commit:buffers
                       count:1u
                     options:options];
      if (drawable) {
        [queue->modern signalDrawable:drawable];
        [drawable present];
        [drawable release];
      }
      [options release];
      return GPU_OK;
    }
    gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  mcb = native->classic;
  if (!mcb) {
    gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  [mcb addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
    gpu_cmdoncomplete(cmdb, native, queue, buffer);
  }];
  dispatch_group_enter(queue->inFlightGroup);
  atomic_store_explicit(&native->completionReady,
                        true,
                        memory_order_release);
  [mcb commit];
  return GPU_OK;
}

static
GPU_HIDE
void
mt_reportCommandBufferError(GPUCommandBuffer * __restrict cmdb,
                            NSError          * __restrict error) {
  GPUDeviceErrorType  type;
  GPUDeviceLostReason lostReason;
  GPUCommandQueue    *queue;
  GPUDevice          *device;
  GPUResult           result;
  const char         *detail;
  char                message[256];

  if (!cmdb) {
    return;
  }

  queue = cmdb->_queue;
  device = queue ? queue->_device : NULL;
  if (!device) {
    return;
  }

  type       = GPU_DEVICE_ERROR_BACKEND;
  lostReason = GPU_DEVICE_LOST_REASON_UNKNOWN;
  result     = GPU_ERROR_BACKEND_FAILURE;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    if ([error.domain isEqualToString:MTL4CommandQueueErrorDomain]) {
      switch ((MTL4CommandQueueError)error.code) {
        case MTL4CommandQueueErrorOutOfMemory:
          type   = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
          result = GPU_ERROR_OUT_OF_MEMORY;
          break;
        case MTL4CommandQueueErrorDeviceRemoved:
          type       = GPU_DEVICE_ERROR_LOST;
          lostReason = GPU_DEVICE_LOST_REASON_REMOVED;
          break;
        case MTL4CommandQueueErrorAccessRevoked:
          type       = GPU_DEVICE_ERROR_LOST;
          lostReason = GPU_DEVICE_LOST_REASON_DRIVER_ERROR;
          break;
        default:
          break;
      }
    }
  }
  if ([error.domain isEqualToString:MTLCommandBufferErrorDomain]) {
    switch ((MTLCommandBufferError)error.code) {
      case MTLCommandBufferErrorOutOfMemory:
        type   = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
        result = GPU_ERROR_OUT_OF_MEMORY;
        break;
      case MTLCommandBufferErrorAccessRevoked:
        type       = GPU_DEVICE_ERROR_LOST;
        lostReason = GPU_DEVICE_LOST_REASON_DRIVER_ERROR;
        break;
#if TARGET_OS_OSX
      case MTLCommandBufferErrorDeviceRemoved:
        type       = GPU_DEVICE_ERROR_LOST;
        lostReason = GPU_DEVICE_LOST_REASON_REMOVED;
        break;
#endif
      default:
        break;
    }
  }

  detail = error ? error.localizedDescription.UTF8String : NULL;
  snprintf(message,
           sizeof(message),
           "Metal command buffer failed%s%s",
           detail ? ": " : "",
           detail ? detail : "");
  gpuDeviceReportError(device, type, lostReason, result, message);
}

static
GPU_HIDE
void
gpu_cmdoncomplete(GPUCommandBuffer * __restrict cmdb,
                  MTCommandBuffer  * __restrict native,
                  MTCommandQueue   * __restrict queue,
                  id<MTLCommandBuffer>        mtlCmdb) {
  if (!cmdb || !native || !queue ||
      !atomic_load_explicit(&native->completionReady,
                            memory_order_acquire)) {
    return;
  }

  if (mtlCmdb && mtlCmdb.status == MTLCommandBufferStatusError) {
    mt_reportCommandBufferError(cmdb, mtlCmdb.error);
  }
  gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
  dispatch_group_leave(queue->inFlightGroup);
}

static
GPU_HIDE
void
gpu_cmdoncomplete4(GPUCommandBuffer * __restrict cmdb,
                   MTCommandBuffer  * __restrict native,
                   MTCommandQueue   * __restrict queue,
                   id                        feedback) {
  GPUDevice       *device;

  if (!cmdb || !native || !queue ||
      !atomic_load_explicit(&native->completionReady,
                            memory_order_acquire)) {
    return;
  }

  device = cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    id<MTL4CommitFeedback> modernFeedback = feedback;

    if (modernFeedback.error && device) {
      mt_reportCommandBufferError(cmdb, modernFeedback.error);
    }
  }
  gpuFinishCommandBuffer(cmdb, mt_recycleCommandBuffer);
  dispatch_group_leave(queue->inFlightGroup);
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
