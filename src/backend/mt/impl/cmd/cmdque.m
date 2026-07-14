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

enum {
  MT_TRANSFER_STAGING_CAPACITY = 4u * 1024u * 1024u,
  MT_SUBMIT_STACK_COUNT        = 64u,
  MT_TRANSFER_OFFSET_ALIGNMENT = 256u
};

static uint64_t
mt_transferCapacity(uint64_t sizeBytes) {
  uint64_t capacity;

  capacity = MT_TRANSFER_STAGING_CAPACITY;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }
    capacity *= 2u;
  }
  return capacity;
}

static GPUResult
mt_waitTransfer(GPUQueue *queue,
                MTTransferSlot *slot,
                bool            countStall) {
  MTLCommandBufferStatus status;

  if (!slot || !slot->pending) {
    return GPU_OK;
  }
  if (!slot->command) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  status = slot->command.status;
  if (status != MTLCommandBufferStatusCompleted &&
      status != MTLCommandBufferStatusError) {
    if (countStall && queue && queue->_device) {
      queue->_device->allocatorStats.uploadStallCount++;
    }
    [slot->command waitUntilCompleted];
    status = slot->command.status;
  }

  [slot->command release];
  slot->command = nil;
  slot->used    = 0u;
  slot->pending = false;
  return status == MTLCommandBufferStatusCompleted
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

static bool
mt_ensureTransferStaging(GPUQueue *queue,
                         MTTransferSlot *slot,
                         uint64_t        sizeBytes) {
  GPUDeviceMT *device;
  id<MTLBuffer> staging;
  uint64_t      capacity;

  if (!queue || !queue->_device || !slot || sizeBytes == 0u) {
    return false;
  }
  if (slot->staging && slot->capacity >= sizeBytes) {
    return true;
  }

  device   = queue->_device->_priv;
  capacity = mt_transferCapacity(sizeBytes);
  if (!device || !device->device || capacity > NSUIntegerMax) {
    return false;
  }
  staging = [device->device newBufferWithLength:(NSUInteger)capacity
                                        options:MTLResourceStorageModeShared];
  if (!staging) {
    return false;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(queue->_device)) {
    staging.label = @"gpu-queue-upload";
  }
#endif

  [slot->staging release];
  slot->staging  = staging;
  slot->capacity = capacity;
  slot->used     = 0u;
  return true;
}

GPU_HIDE
GPUResult
mt_beginTransfer(GPUQueue           *queue,
                 uint64_t                   sizeBytes,
                 id<MTLBlitCommandEncoder> *outBlit,
                 id<MTLBuffer>             *outStaging,
                 uint64_t                  *outOffset) {
  MTCommandQueue      *native;
  MTTransferSlot      *slot;
  id<MTLCommandQueue>  commandQueue;
  uint64_t             offset;
  GPUResult            result;

  if (!queue || !queue->_device || !outBlit || !outStaging || !outOffset ||
      sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBlit    = nil;
  *outStaging = nil;
  *outOffset  = 0u;
  native      = mt_commandQueue(queue);
  if (!native || (!native->classic && !native->upload)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (native->transferOpen) {
    slot = &native->transferSlots[native->activeTransferSlot];
    if (!slot->blit || !slot->staging) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (slot->used <= UINT64_MAX - (MT_TRANSFER_OFFSET_ALIGNMENT - 1u)) {
      offset = (slot->used + MT_TRANSFER_OFFSET_ALIGNMENT - 1u) &
               ~(uint64_t)(MT_TRANSFER_OFFSET_ALIGNMENT - 1u);
      if (offset <= slot->capacity && sizeBytes <= slot->capacity - offset) {
        slot->used  = offset + sizeBytes;
        *outBlit    = slot->blit;
        *outStaging = slot->staging;
        *outOffset  = offset;
        return GPU_OK;
      }
    }
    result = mt_flushTransfers(queue, false);
    if (result != GPU_OK) {
      return result;
    }
  }

  slot   = &native->transferSlots[native->nextTransferSlot];
  result = mt_waitTransfer(queue, slot, true);
  if (result != GPU_OK ||
      !mt_ensureTransferStaging(queue, slot, sizeBytes)) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  commandQueue = native->classic ? native->classic : native->upload;
  slot->command = [[commandQueue commandBuffer] retain];
  slot->blit    = [[slot->command blitCommandEncoder] retain];
  if (!slot->command || !slot->blit) {
    [slot->blit release];
    [slot->command release];
    slot->blit    = nil;
    slot->command = nil;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  slot->used                  = sizeBytes;
  native->activeTransferSlot  = native->nextTransferSlot;
  native->transferOpen        = true;
  *outBlit                    = slot->blit;
  *outStaging                 = slot->staging;
  *outOffset                  = 0u;
  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_flushTransfers(GPUQueue *queue, bool wait) {
  MTCommandQueue *native;
  MTTransferSlot *slot;
  GPUResult       flushResult;

  native = mt_commandQueue(queue);
  if (!native) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  flushResult = GPU_OK;

  if (native->transferOpen) {
    slot = &native->transferSlots[native->activeTransferSlot];
    if (!slot->command || !slot->blit) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    [slot->blit endEncoding];
    [slot->blit release];
    slot->blit = nil;
    [slot->command commit];
    slot->pending              = true;
    native->transferOpen       = false;
    native->nextTransferSlot   =
      (native->activeTransferSlot + 1u) % MT_TRANSFER_SLOT_COUNT;
  }

  if (wait) {
    for (uint32_t i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
      GPUResult result;

      result = mt_waitTransfer(queue, &native->transferSlots[i], false);
      if (flushResult == GPU_OK && result != GPU_OK) {
        flushResult = result;
      }
    }
  }
  return flushResult;
}

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

static void
mt_recordGPUFrameTime(GPUCommandBuffer *cmdb,
                      CFTimeInterval    start,
                      CFTimeInterval    end) {
  GPUDevice *device;

  if (!cmdb || !cmdb->_recordsGPUFrameTime || !(end > start)) {
    return;
  }

  device = gpuCommandBufferDevice(cmdb);
  gpuDeviceRecordGPUFrameTime(device, (end - start) * 1000.0);
}

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer * __restrict cmdb,
                     void             * __restrict sender,
                     GPUCommandBufferCompletionFn  oncomplete);

GPU_HIDE
GPUQueue*
mt_newCommandQueue(GPUDevice * __restrict device) {
  GPUDeviceMT    *deviceMT;
  GPUQueue       *que;
  MTCommandQueue *native;

  deviceMT = device->_priv;
  que = calloc(1, sizeof(*que));
  native = calloc(1, sizeof(*native));
  if (!que || !native) {
    free(native);
    free(que);
    return NULL;
  }

  native->poolLock = OS_UNFAIR_LOCK_INIT;
  native->mode     = deviceMT->commandMode;
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      native->modern = [deviceMT->device newMTL4CommandQueue];
      native->upload = [deviceMT->device newCommandQueue];
    }
  } else {
    native->classic = [deviceMT->device newCommandQueue];
  }
  native->inFlightGroup = dispatch_group_create();
  if ((!native->classic && (!native->modern || !native->upload)) ||
      !native->inFlightGroup) {
    [native->classic release];
    [native->upload release];
    [native->modern release];
    if (native->inFlightGroup) {
      dispatch_release(native->inFlightGroup);
    }
    free(native);
    free(que);
    return NULL;
  }

  que->_priv   = native;
  que->_device = device;

  return que;
}

GPU_HIDE
void
mt_destroyCommandQueue(GPUQueue * __restrict queue) {
  MTCommandBuffer *command;
  MTCommandBuffer *next;

  if (!queue) {
    return;
  }

  if (queue->_priv) {
    MTCommandQueue *native = mt_commandQueue(queue);

    (void)mt_flushTransfers(queue, true);
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
    for (uint32_t i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
      [native->transferSlots[i].blit release];
      [native->transferSlots[i].command release];
      [native->transferSlots[i].staging release];
    }
    dispatch_release(native->inFlightGroup);
    free(native);
  }
  free(queue);
}

GPU_HIDE
GPUQueue*
mt_getCommandQueue(GPUDevice * __restrict device,
                   GPUQueueFlagBits       bits,
                   uint32_t               index) {
  GPUQueue    *que;
  GPUDeviceMT *deviceMT;
  uint32_t     matchIndex;
  uint32_t     i;

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
mt_getTimestampPeriod(GPUQueue *queue,
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
mt_createCommandBufferState(GPUQueue *cmdb, MTCommandQueue *queue) {
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
      residencyDesc.initialCapacity = MT_RESIDENCY_CACHE_SIZE;
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
mt_takeCommandBufferState(GPUQueue *cmdb, MTCommandQueue *queue) {
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
mt_newCommandBuffer(GPUQueue  * __restrict cmdb,
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

  if (mt_flushTransfers(cmdb->_queue,
                        native->mode == MTCommandMode4) != GPU_OK) {
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

static GPUResult
mt__commitCommandBuffers(uint32_t                  count,
                         GPUCommandBuffer * const *buffers) {
  GPUResult result;

  result = GPU_OK;
  for (uint32_t i = 0u; i < count; i++) {
    GPUResult commitResult;

    commitResult = mt_cmdbufCommit(buffers[i]);
    if (result == GPU_OK && commitResult != GPU_OK) {
      result = commitResult;
    }
  }
  return result;
}

GPU_HIDE
GPUResult
mt_submitCommandBuffers(GPUQueue                  * __restrict queueHandle,
                        uint32_t                                count,
                        GPUCommandBuffer * const * __restrict buffers) {
  MTCommandBuffer      *natives[MT_SUBMIT_STACK_COUNT];
  id<MTL4CommandBuffer> modern[MT_SUBMIT_STACK_COUNT];
  MTCommandQueue       *queue;
  MTL4CommitOptions    *options;

  queue = mt_commandQueue(queueHandle);
  if (!queue || !buffers || count < 2u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (count > MT_SUBMIT_STACK_COUNT) {
    return mt__commitCommandBuffers(count, buffers);
  }

  for (uint32_t i = 0u; i < count; i++) {
    natives[i] = mt_commandBuffer(buffers[i]);
    if (!natives[i] || natives[i]->owner != queue ||
        natives[i]->mode != MTCommandMode4 || !natives[i]->modern ||
        natives[i]->drawable) {
      return mt__commitCommandBuffers(count, buffers);
    }
    modern[i] = natives[i]->modern;
  }

  if (mt_flushTransfers(queueHandle, true) != GPU_OK) {
    for (uint32_t i = 0u; i < count; i++) {
      gpuFinishCommandBuffer(buffers[i], mt_recycleCommandBuffer);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    options = [MTL4CommitOptions new];
    if (!options) {
      return mt__commitCommandBuffers(count, buffers);
    }

    for (uint32_t i = 0u; i < count; i++) {
      GPUCommandBuffer *cmdb;
      MTCommandBuffer  *native;

      cmdb   = buffers[i];
      native = natives[i];
      [native->residency commit];
      [native->modern endCommandBuffer];
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        gpu_cmdoncomplete4(cmdb, native, queue, feedback);
      }];
      dispatch_group_enter(queue->inFlightGroup);
      atomic_store_explicit(&native->completionReady,
                            true,
                            memory_order_release);
    }
    [queue->modern commit:modern count:count options:options];
    [options release];
    return GPU_OK;
  }

  return mt__commitCommandBuffers(count, buffers);
}

static GPUResult
mt_createSemaphore(GPUDevice                    *device,
                   const GPUSemaphoreCreateInfo *info,
                   GPUSemaphore                 *semaphore) {
  GPUDeviceMT       *deviceMT;
  id<MTLSharedEvent> event;

  deviceMT = device ? device->_priv : NULL;
  if (!deviceMT || !deviceMT->device || !semaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (@available(macOS 10.14, iOS 12.0, *)) {
    event = [deviceMT->device newSharedEvent];
    if (!event) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    event.signaledValue = info ? info->initialValue : 0u;
#if GPU_BUILD_WITH_DEBUG_MARKERS
    const char *label;

    label = info ? gpuDeviceDebugLabel(device, info->label) : NULL;
    if (label && label[0] != '\0') {
      event.label = [NSString stringWithUTF8String:label];
    }
#endif
    semaphore->_priv = event;
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_destroySemaphore(GPUSemaphore *semaphore) {
  id<MTLSharedEvent> event;

  event = semaphore ? (id<MTLSharedEvent>)semaphore->_priv : nil;
  [event release];
  if (semaphore) {
    semaphore->_priv = NULL;
  }
}

static GPUResult
mt_submitEx(GPUQueue                   *queueHandle,
            const GPUQueueSubmitExInfo *info) {
  MTCommandQueue  *queue;
  MTCommandBuffer *last;
  GPUResult        result;

  queue = mt_commandQueue(queueHandle);
  last  = info && info->commandBufferCount > 0u
            ? mt_commandBuffer(
                info->ppCommandBuffers[info->commandBufferCount - 1u]
              )
            : NULL;
  if (!queue || !last || !info) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  for (uint32_t i = 0u; i < info->waitCount; i++) {
    if (!info->pWaits[i].semaphore->_priv) {
      for (uint32_t j = 0u; j < info->commandBufferCount; j++) {
        gpuFinishCommandBuffer(info->ppCommandBuffers[j],
                               mt_recycleCommandBuffer);
      }
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  for (uint32_t i = 0u; i < info->signalCount; i++) {
    if (!info->pSignals[i].semaphore->_priv) {
      for (uint32_t j = 0u; j < info->commandBufferCount; j++) {
        gpuFinishCommandBuffer(info->ppCommandBuffers[j],
                               mt_recycleCommandBuffer);
      }
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  if (mt_flushTransfers(queueHandle,
                        queue->mode == MTCommandMode4) != GPU_OK) {
    for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
      gpuFinishCommandBuffer(info->ppCommandBuffers[i],
                             mt_recycleCommandBuffer);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (queue->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      for (uint32_t i = 0u; i < info->waitCount; i++) {
        id<MTLEvent> event;

        event = (id<MTLEvent>)info->pWaits[i].semaphore->_priv;
        [queue->modern waitForEvent:event value:info->pWaits[i].value];
      }

      result = info->commandBufferCount > 1u
                 ? mt_submitCommandBuffers(queueHandle,
                                           info->commandBufferCount,
                                           info->ppCommandBuffers)
                 : mt_cmdbufCommit(info->ppCommandBuffers[0]);
      if (result != GPU_OK) {
        return result;
      }

      for (uint32_t i = 0u; i < info->signalCount; i++) {
        id<MTLEvent> event;

        event = (id<MTLEvent>)info->pSignals[i].semaphore->_priv;
        [queue->modern signalEvent:event value:info->pSignals[i].value];
      }
      return GPU_OK;
    }
    for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
      gpuFinishCommandBuffer(info->ppCommandBuffers[i],
                             mt_recycleCommandBuffer);
    }
    return GPU_ERROR_UNSUPPORTED;
  }

  if (info->waitCount > 0u) {
    id<MTLCommandBuffer> waitBuffer;

    waitBuffer = [queue->classic commandBuffer];
    if (!waitBuffer) {
      for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
        gpuFinishCommandBuffer(info->ppCommandBuffers[i],
                               mt_recycleCommandBuffer);
      }
      return GPU_ERROR_BACKEND_FAILURE;
    }
    for (uint32_t i = 0u; i < info->waitCount; i++) {
      id<MTLEvent> event;

      event = (id<MTLEvent>)info->pWaits[i].semaphore->_priv;
      [waitBuffer encodeWaitForEvent:event value:info->pWaits[i].value];
    }
    [waitBuffer commit];
  }

  if (!last->classic) {
    for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
      gpuFinishCommandBuffer(info->ppCommandBuffers[i],
                             mt_recycleCommandBuffer);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }
  for (uint32_t i = 0u; i < info->signalCount; i++) {
    id<MTLEvent> event;

    event = (id<MTLEvent>)info->pSignals[i].semaphore->_priv;
    [last->classic encodeSignalEvent:event value:info->pSignals[i].value];
  }

  return info->commandBufferCount > 1u
           ? mt_submitCommandBuffers(queueHandle,
                                     info->commandBufferCount,
                                     info->ppCommandBuffers)
           : mt_cmdbufCommit(info->ppCommandBuffers[0]);
}

static
GPU_HIDE
void
mt_reportCommandBufferError(GPUCommandBuffer * __restrict cmdb,
                            NSError          * __restrict error) {
  GPUDeviceErrorType  type;
  GPUDeviceLostReason lostReason;
  GPUQueue           *queue;
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
  if (mtlCmdb) {
    if (@available(macOS 10.15, iOS 10.3, *)) {
      mt_recordGPUFrameTime(cmdb,
                            mtlCmdb.GPUStartTime,
                            mtlCmdb.GPUEndTime);
    }
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
    mt_recordGPUFrameTime(cmdb,
                          modernFeedback.GPUStartTime,
                          modernFeedback.GPUEndTime);
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
  api->submit                  = mt_submitCommandBuffers;
  api->createSemaphore         = mt_createSemaphore;
  api->destroySemaphore        = mt_destroySemaphore;
  api->submitEx                = mt_submitEx;
}
