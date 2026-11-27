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
#include "../../../../api/cmdqueue_internal.h"
#include "../../../../api/frame_feedback_internal.h"

/* staging a slot starts with and doubles up to MT_TRANSFER_STAGING_KEEP; a
 * larger transfer gets staging of about its own size, kept while such
 * transfers keep coming (making it again costs: 125 MB into kept staging
 * 1.4-1.6 ms, into new staging 8-20 ms) and given back once
 * MT_TRANSFER_LARGE_IDLE batches went by without one (a 125 MB page image
 * otherwise pinned 128 MB for good) */
enum {
  MT_TRANSFER_STAGING_CAPACITY = 4u * 1024u * 1024u,
  MT_TRANSFER_STAGING_KEEP     = 16u * 1024u * 1024u,
  MT_TRANSFER_STAGING_LARGE    = 1024u * 1024u, /* large staging rounds up to this */
  MT_TRANSFER_LARGE_IDLE       = 32u,
  MT_SUBMIT_STACK_COUNT        = 64u,
  MT_TRANSFER_OFFSET_ALIGNMENT = 256u
};

#if MT_HAS_METAL4
@interface MTUploadReady : NSObject {
@public
  GPUFenceObserver completion;
@private
  GPUUploaderReadyFn _ready;
  void              *_user;
  os_unfair_lock     _lock;
}
- (instancetype)initWithCallback:(GPUUploaderReadyFn)ready user:(void *)user;
- (void)notify;
@end
#endif

@interface GPUFrameFeedbackTarget : NSObject {
@public
  GPUFrameFeedback *feedback;
}
@end

static
GPU_HIDE
void
cmdoncomplete(GPUCommandBuffer *__restrict cmdb,
              MTCommandBuffer  *__restrict native,
              MTCommandQueue   *__restrict queue,
              id<MTLCommandBuffer>         mtlCmdb);

#if MT_HAS_METAL4
static
GPU_HIDE
void
cmdoncomplete4(GPUCommandBuffer *__restrict cmdb,
               MTCommandBuffer  *__restrict native,
               MTCommandQueue   *__restrict queue,
               id                           feedback);
#endif

GPU_HIDE
void
mt_ccmdbufOnComplete(GPUCommandBuffer *__restrict cmdb,
                     void             *__restrict sender,
                     GPUCommandBufferCompletionFn oncomplete);

GPU_HIDE
GPUResult
mt_cmdbufCommit(GPUCommandBuffer *__restrict cmdb);

GPU_HIDE
GPUResult
mt_submitCommandBuffers(GPUQueue                *__restrict queueHandle,
                        uint32_t                            count,
                        GPUCommandBuffer *const *__restrict buffers);

static void
mt_loseQueue(GPUQueue *queue);

static void
mt_watchedDone(MTCommandQueue *native);

@implementation GPUFrameFeedbackTarget
- (void)dealloc {
  frameFeedbackRelease(feedback);
  [super dealloc];
}
@end

static void
mt_observeFrameFeedback(GPUFrameFeedback *feedback, id<MTLDrawable> drawable) {
#if !TARGET_OS_SIMULATOR
  GPUFrameFeedbackTarget *target;

  /* attach only at submission; abandoned presents can retain native handlers. */
  if (@available(macOS 10.15.4, iOS 10.3, *)) {
    if (drawable && (target = [GPUFrameFeedbackTarget new])) {
      frameFeedbackRetain(feedback);
      target->feedback = feedback;
      [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
        frameFeedbackPresent(target->feedback, presented.presentedTime);
      }];
      [target release];
      return;
    }
  }
#else
  GPU__UNUSED(drawable);
#endif
  frameFeedbackPresent(feedback, 0.0);
}

static uint64_t
mt_transferCapacity(uint64_t sizeBytes) {
  uint64_t capacity;

  if (sizeBytes > MT_TRANSFER_STAGING_KEEP) {
    capacity = (sizeBytes + MT_TRANSFER_STAGING_LARGE - 1u) & ~(uint64_t)(MT_TRANSFER_STAGING_LARGE - 1u);
    return capacity >= sizeBytes ? capacity : sizeBytes;
  }

  capacity = MT_TRANSFER_STAGING_CAPACITY;

  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }

    capacity *= 2u;
  }

  return capacity;
}

/* a batch's end: its handler's, or the queue's loss (then Metal may still
 * hold it, and its staging is never written again) */
static GPUResult
mt_waitTransfer(GPUQueue       *queue,
                MTTransferSlot *slot,
                bool            countStall) {
  GPUResult result;

  if (!slot || !slot->pending) {
    return GPU_OK;
  }

  if (!slot->command) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (countStall && queue && queue->_device
      && atomic_load_explicit(&slot->inFlight, memory_order_acquire)) {
    queue->_device->allocatorStats.uploadStallCount++;
  }

  dispatch_semaphore_wait(slot->done, DISPATCH_TIME_FOREVER);
  result = slot->result;

  [slot->command release];
  slot->command = nil;
  slot->used    = 0u;
  slot->pending = false;

  return result;
}

/* a batch's handler; one the queue's loss took first does nothing more: the
 * queue may be gone by then */
static void
mt_transferDone(GPUQueue *queue, MTTransferSlot *slot, id<MTLCommandBuffer> batch) {
  MTCommandQueue *native;
  GPUResult       result;

  if (!atomic_exchange_explicit(&slot->inFlight, false, memory_order_acq_rel))
    return;

  native = mt_commandQueue(queue);
  if (slot->watched)
    mt_watchedDone(native);
  result = batch.status == MTLCommandBufferStatusCompleted ? GPU_OK : mt_commandFailed(queue, batch.error);
#if MT_HAS_METAL4
  /* a failed batch signals nothing, and later commits wait for its value:
   * given from here, unless the queue is lost (what waits is abandoned) */
  if (result != GPU_OK && result != GPU_ERROR_DEVICE_LOST && native->mode == MTCommandMode4)
    native->transferEvent.signaledValue = slot->signal;
#endif
  slot->result = result;
  dispatch_semaphore_signal(slot->done);
  dispatch_group_leave(native->inFlightGroup);
}

static bool
mt_ensureTransferStaging(GPUQueue       *queue,
                         MTTransferSlot *slot,
                         uint64_t        sizeBytes) {
  GPUDeviceMT  *device;
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

  if (!(staging = [device->device newBufferWithLength:(NSUInteger)capacity
                                              options:MTLResourceStorageModeShared])) {
    return false;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (deviceDebugMarkersEnabled(queue->_device)) {
    staging.label = @"gpu-queue-upload";
  }
#endif

  [slot->staging release];
  slot->staging  = staging;
  slot->capacity = capacity;
  slot->used     = 0u;

  return true;
}

static void
mt_recordGPUFrameTime(GPUCommandBuffer *cmdb,
                      CFTimeInterval    start,
                      CFTimeInterval    end) {
  GPUDevice *device;

  if (!cmdb || !cmdb->_recordsGPUFrameTime || !(end > start)) {
    return;
  }

  device = commandBufferDevice(cmdb);
  deviceRecordGPUFrameTime(device, (end - start) * 1000.0);
}

static MTCommandBuffer*
mt_createCommandBufferState(GPUQueue *cmdb, MTCommandQueue *queue) {
  GPUCommandBuffer          *cb;
  MTCommandBuffer           *native;
#if MT_HAS_METAL4
  GPUDeviceMT               *deviceMT;
  MTLResidencySetDescriptor *residencyDesc;
  NSError                   *error;
#endif

  if (!(native = calloc(1, sizeof(*native)))) {
    return NULL;
  }

  atomic_init(&native->completionReady, false);
  deviceRecordHotPathAlloc(cmdb->_device, sizeof(*native));

  cb            = &native->commandBuffer;
  native->owner = queue;
  native->mode  = queue->mode;
  cb->_priv     = native;
  cb->_queue    = cmdb;

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      deviceMT          = cmdb->_device->_priv;
      native->modern    = [deviceMT->device newCommandBuffer];
      native->allocator = [deviceMT->device newCommandAllocator];
      residencyDesc     = [MTLResidencySetDescriptor new];
#if GPU_BUILD_WITH_DEBUG_MARKERS
      if (deviceDebugMarkersEnabled(cmdb->_device)) {
        residencyDesc.label = @"gpu-command-residency";
      }
#endif
      residencyDesc.initialCapacity = MT_RESIDENCY_CACHE_SIZE;
      error                         = nil;
      native->residency             = [deviceMT->device
        newResidencySetWithDescriptor:residencyDesc
                                error:&error];
      [residencyDesc release];
    }

    if (!native->modern || !native->allocator
        || !native->residency) {
      mt_destroyCommandBufferState(native);
      return NULL;
    }
  }
#endif

  os_unfair_lock_lock(&queue->poolLock);
  native->next    = queue->commands;
  queue->commands = native;
  os_unfair_lock_unlock(&queue->poolLock);

  return native;
}

static MTCommandBuffer*
mt_takeCommandBufferState(GPUQueue *cmdb, MTCommandQueue *queue) {
  MTCommandBuffer *native;

  os_unfair_lock_lock(&queue->poolLock);
  native = queue->freeCommands;

  if (native) {
    queue->freeCommands = native->poolNext;
    native->poolNext    = NULL;
  }

  os_unfair_lock_unlock(&queue->poolLock);

  return native ? native : mt_createCommandBufferState(cmdb, queue);
}

/* semaphore values a submission signals, kept on its last buffer: a CPU
 * signal stands in for them when that buffer fails (a failed one signals
 * nothing), so other queues waiting on them go on */
static bool
mt_keepSignals(MTCommandBuffer *native, const GPUQueueSemaphoreSignal *signals, uint32_t count) {
  MTSignal *grown;
  uint32_t  i;

  if (count > native->signalCapacity) {
    if (!(grown = realloc(native->signals, count * sizeof(*grown))))
      return false;

    native->signals        = grown;
    native->signalCapacity = count;
  }

  for (i = 0u; i < count; i++) {
    native->signals[i].event = [(id<MTLSharedEvent>)signals[i].semaphore->_priv retain];
    native->signals[i].value = signals[i].value;
  }

  native->signalCount = count;
  return true;
}

static void
mt_settleSignals(MTCommandBuffer *native, bool failed) {
  uint32_t i;

  for (i = 0u; i < native->signalCount; i++) {
    /* a value below the event's is ignored: one the GPU gave stays */
    if (failed)
      native->signals[i].event.signaledValue = native->signals[i].value;
    [native->signals[i].event release];
    native->signals[i].event = nil;
  }

  native->signalCount = 0u;
}

/* a command buffer that won't reach the GPU: its recording ended so its state
 * can be used again, what it would signal given, its waiters told */
static GPUResult
mt_abortCommandBuffer(GPUCommandBuffer *cmdb, GPUResult result) {
  MTCommandBuffer *native;

  if ((native = mt_commandBuffer(cmdb))) {
#if MT_HAS_METAL4
    if (native->mode == MTCommandMode4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        mt_closeOpenEncoder(native);
        [native->modern endCommandBuffer];
      }
    }
#endif
    mt_settleSignals(native, true);
  }

  finishCommandBuffer(cmdb, mt_recycleCommandBuffer, result);
  return result;
}

static bool
mt_queueLost(MTCommandQueue *native) {
  return atomic_load_explicit(&native->lost, memory_order_acquire);
}

/* the hang limit (GPURuntimeConfig.hangLimitNs): work submitted under it is
 * watched; a timer, set only while watched work is in flight, loses the
 * queue once the oldest has made no progress for the limit. Off, a submit
 * reads the limit and nothing more */

static uint64_t
mt_hangLimit(GPUQueue *queue) {
  return atomic_load_explicit(&((GPUDeviceMT *)queue->_device->_priv)->hangLimitNs, memory_order_relaxed);
}

static void
mt_setHangLimit(GPUDevice *device, uint64_t limitNs) {
  atomic_store_explicit(&((GPUDeviceMT *)device->_priv)->hangLimitNs, limitNs, memory_order_relaxed);
}

static uint64_t
mt_nowNs(void) {
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

/* under poolLock: once, delayNs from now (leeway an eighth of the limit) */
static void
mt_setHangTimer(MTCommandQueue *native, uint64_t delayNs, uint64_t limit) {
  dispatch_source_set_timer(native->hangTimer,
                            dispatch_time(DISPATCH_TIME_NOW, (int64_t)MIN(delayNs, (uint64_t)INT64_MAX)),
                            DISPATCH_TIME_FOREVER,
                            limit / 8u);
  native->hangArmed = true;
}

/* the timer: watched work in flight with no progress for the limit loses the
 * queue; otherwise set again for the oldest one's deadline, or, with nothing
 * watched in flight, left off till the next watched submit */
static void
mt_hangCheck(GPUQueue *queue) {
  MTCommandQueue *native;
  uint64_t        limit, since, now, ran;
  bool            hung;

  native = mt_commandQueue(queue);
  limit  = mt_hangLimit(queue);
  hung   = false;

  os_unfair_lock_lock(&native->poolLock);
  native->hangArmed = false;
  if (limit != 0u && !mt_queueLost(native)
      && atomic_load_explicit(&native->watched, memory_order_acquire) != 0u) {
    since = atomic_load_explicit(&native->progressNs, memory_order_acquire);
    now   = mt_nowNs();
    ran   = now > since ? now - since : 0u;
    if (!(hung = ran >= limit))
      mt_setHangTimer(native, limit - ran, limit);
  }
  os_unfair_lock_unlock(&native->poolLock);

  /* reported first: a wait the loss ends finds the device lost */
  if (hung && !mt_queueLost(native)) {
    deviceReportQueueHang(queue->_device, "Metal", limit);
    mt_loseQueue(queue);
  }
}

/* under poolLock, made at the queue's first watched submit; its cancel
 * handler runs after the last check, for the queue's destruction */
static bool
mt_makeHangTimer(GPUQueue *queue, MTCommandQueue *native) {
  dispatch_source_t    timer;
  dispatch_semaphore_t gone;

  if (native->hangTimer)
    return true;

  timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
                                 0u,
                                 0u,
                                 dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
  gone  = dispatch_semaphore_create(0);
  if (!timer || !gone) {
    if (timer)
      dispatch_release(timer);
    if (gone)
      dispatch_release(gone);
    return false;
  }

  dispatch_source_set_event_handler(timer, ^{
    mt_hangCheck(queue);
  });
  dispatch_source_set_cancel_handler(timer, ^{
    dispatch_semaphore_signal(gone);
  });
  native->hangTimer     = timer;
  native->hangTimerGone = gone;
  dispatch_source_set_timer(timer, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 0u);
  dispatch_activate(timer);
  return true;
}

/* under poolLock: count more watched work in flight; with none before, the
 * clock starts now. The timer is set unless it is */
static void
mt_watchSubmits(GPUQueue *queue, MTCommandQueue *native, uint32_t count, uint64_t limit) {
  uint64_t since, now;

  if (atomic_fetch_add_explicit(&native->watched, count, memory_order_acq_rel) == 0u)
    atomic_store_explicit(&native->progressNs, mt_nowNs(), memory_order_release);

  if (!native->hangArmed && mt_makeHangTimer(queue, native)) {
    since = atomic_load_explicit(&native->progressNs, memory_order_acquire);
    now   = mt_nowNs();
    mt_setHangTimer(native, now - since < limit ? limit - (now - since) : 0u, limit);
  }
}

/* watched work finished: the queue made progress */
static void
mt_watchedDone(MTCommandQueue *native) {
  atomic_store_explicit(&native->progressNs, mt_nowNs(), memory_order_release);
  atomic_fetch_sub_explicit(&native->watched, 1u, memory_order_acq_rel);
}

/* in flight from here, unless the queue is lost: marked under the lock the
 * loss sweeps under, so each one either fails here or is let go by the loss */
static GPUResult
mt_beginCommits(GPUQueue *handle, MTCommandQueue *queue, MTCommandBuffer *const *natives, uint32_t count) {
  uint64_t limit;
  uint32_t i;
  bool     lost;

  limit = mt_hangLimit(handle);
  os_unfair_lock_lock(&queue->poolLock);
  if (!(lost = mt_queueLost(queue))) {
    for (i = 0u; i < count; i++) {
      dispatch_group_enter(queue->inFlightGroup);
      natives[i]->watched = limit != 0u;
      atomic_store_explicit(&natives[i]->completionReady, true, memory_order_release);
    }
    if (limit != 0u)
      mt_watchSubmits(handle, queue, count, limit);
  }
  os_unfair_lock_unlock(&queue->poolLock);

  return lost ? GPU_ERROR_DEVICE_LOST : GPU_OK;
}

/* one marked in flight that won't reach the GPU after all; one the queue's
 * loss took meanwhile was let go then (and is never used again) */
static GPUResult
mt_cancelCommit(GPUCommandBuffer *cmdb, MTCommandBuffer *native, MTCommandQueue *queue, GPUResult result) {
  if (!atomic_exchange_explicit(&native->completionReady, false, memory_order_acq_rel))
    return GPU_ERROR_DEVICE_LOST;

  if (native->watched)
    atomic_fetch_sub_explicit(&queue->watched, 1u, memory_order_acq_rel);
  dispatch_group_leave(queue->inFlightGroup);
  return mt_abortCommandBuffer(cmdb, result);
}

static GPUResult
mt__commitCommandBuffers(uint32_t                 count,
                         GPUCommandBuffer *const *buffers) {
  GPUResult result;
  uint32_t  i;
  GPUResult commitResult;

  result = GPU_OK;

  for (i = 0u; i < count; i++) {
    commitResult = mt_cmdbufCommit(buffers[i]);

    if (result == GPU_OK && commitResult != GPU_OK) {
      result = commitResult;
    }
  }

  return result;
}

static GPUResult
mt_createSemaphore(GPUDevice                    *device,
                   const GPUSemaphoreCreateInfo *info,
                   GPUSemaphore                 *semaphore) {
  GPUDeviceMT       *deviceMT;
  id<MTLSharedEvent> event;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  const char        *label;
#endif

  deviceMT = device ? device->_priv : NULL;

  if (!deviceMT || !deviceMT->device || !semaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (@available(macOS 10.14, iOS 12.0, *)) {
    if (!(event = [deviceMT->device newSharedEvent])) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    event.signaledValue = info ? info->initialValue : 0u;
#if GPU_BUILD_WITH_DEBUG_MARKERS
    label = info ? deviceDebugLabel(device, info->label) : NULL;

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
            ? mt_commandBuffer(info->ppCommandBuffers[info->commandBufferCount - 1u])
            : NULL;

  if (!queue || !last || !info) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = GPU_OK;

  for (uint32_t i = 0u; i < info->waitCount; i++) {
    if (!info->pWaits[i].semaphore->_priv)
      result = GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t i = 0u; i < info->signalCount; i++) {
    if (!info->pSignals[i].semaphore->_priv)
      result = GPU_ERROR_BACKEND_FAILURE;
  }

  /* kept before anything can fail: a failure gives them from the CPU */
  if (result == GPU_OK && !mt_keepSignals(last, info->pSignals, info->signalCount)) {
    for (uint32_t i = 0u; i < info->signalCount; i++)
      ((id<MTLSharedEvent>)info->pSignals[i].semaphore->_priv).signaledValue = info->pSignals[i].value;
    result = GPU_ERROR_OUT_OF_MEMORY;
  }

  if (result == GPU_OK && mt_queueLost(queue))
    result = GPU_ERROR_DEVICE_LOST;

  if (result == GPU_OK)
    result = mt_flushTransfers(queueHandle, false);

  if (result != GPU_OK) {
    for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
      (void)mt_abortCommandBuffer(info->ppCommandBuffers[i], result);
    }

    return result;
  }

#if MT_HAS_METAL4
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
      (void)mt_abortCommandBuffer(info->ppCommandBuffers[i], GPU_ERROR_UNSUPPORTED);
    }

    return GPU_ERROR_UNSUPPORTED;
  }
#endif

  if (info->waitCount > 0u) {
    id<MTLCommandBuffer> waitBuffer;

    @autoreleasepool {
      waitBuffer = [[queue->classic commandBuffer] retain];
    }

    if (!waitBuffer) {
      for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
        (void)mt_abortCommandBuffer(info->ppCommandBuffers[i], GPU_ERROR_BACKEND_FAILURE);
      }

      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t i = 0u; i < info->waitCount; i++) {
      id<MTLEvent> event;

      event = (id<MTLEvent>)info->pWaits[i].semaphore->_priv;
      [waitBuffer encodeWaitForEvent:event value:info->pWaits[i].value];
    }

    [waitBuffer commit];
    [waitBuffer release];
  }

  if (!last->classic) {
    for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
      (void)mt_abortCommandBuffer(info->ppCommandBuffers[i], GPU_ERROR_BACKEND_FAILURE);
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

/* the system gave up on the queue: what's in flight won't finish, so its
 * waiters go now with GPU_ERROR_DEVICE_LOST and nothing submitted after runs.
 * Metal may still hold that work: its states and batches are never used again
 * (nor freed), and their handlers, if they come, do nothing */
static void
mt_loseQueue(GPUQueue *queue) {
  MTCommandQueue  *native;
  MTCommandBuffer *state, *abandoned, *next;
  MTTransferSlot  *slot;
  uint32_t         i, lostSlots;

  if (!(native = mt_commandQueue(queue))
      || atomic_exchange_explicit(&native->lost, true, memory_order_acq_rel))
    return;

  abandoned = NULL;
  lostSlots = 0u;
  os_unfair_lock_lock(&native->poolLock);
  for (state = native->commands; state; state = state->next) {
    if (atomic_exchange_explicit(&state->completionReady, false, memory_order_acq_rel)) {
      state->abandoned     = true;
      state->abandonedNext = abandoned;
      abandoned            = state;
    }
  }

  for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
    if (atomic_exchange_explicit(&native->transferSlots[i].inFlight, false, memory_order_acq_rel))
      lostSlots |= 1u << i;
  }
  os_unfair_lock_unlock(&native->poolLock);

  for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
    if (lostSlots & (1u << i)) {
      slot         = &native->transferSlots[i];
      slot->result = GPU_ERROR_DEVICE_LOST;
      dispatch_semaphore_signal(slot->done);
      dispatch_group_leave(native->inFlightGroup);
    }
  }

  for (state = abandoned; state; state = next) {
    next = state->abandonedNext;
    mt_settleSignals(state, true);
    finishCommandBuffer(&state->commandBuffer, NULL, GPU_ERROR_DEVICE_LOST);
    dispatch_group_leave(native->inFlightGroup);
  }
}

/* what a failed command buffer's error comes to, reported; the queue lost
 * when the system gave up on it (a GPU timeout, GPU access taken away, the
 * device gone): Metal then runs nothing behind it in time, or ever */
GPU_HIDE
GPUResult
mt_commandFailed(GPUQueue *__restrict queue, NSError *__restrict error) {
  char                message[256];
  GPUDevice          *device;
  const char         *detail;
  GPUDeviceErrorType  type;
  GPUDeviceLostReason lostReason;
  GPUResult           result;

  type       = GPU_DEVICE_ERROR_BACKEND;
  lostReason = GPU_DEVICE_LOST_REASON_UNKNOWN;
  result     = GPU_ERROR_BACKEND_FAILURE;
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    if ([error.domain isEqualToString:MTL4CommandQueueErrorDomain]) {
      switch ((MTL4CommandQueueError)error.code) {
        case MTL4CommandQueueErrorOutOfMemory:
          type   = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
          result = GPU_ERROR_OUT_OF_MEMORY;
          break;
        case MTL4CommandQueueErrorTimeout:
          type       = GPU_DEVICE_ERROR_LOST;
          lostReason = GPU_DEVICE_LOST_REASON_HUNG;
          break;
        case MTL4CommandQueueErrorDeviceRemoved:
          type       = GPU_DEVICE_ERROR_LOST;
          lostReason = GPU_DEVICE_LOST_REASON_REMOVED;
          break;
        case MTL4CommandQueueErrorNotPermitted:
        case MTL4CommandQueueErrorAccessRevoked:
          type       = GPU_DEVICE_ERROR_LOST;
          lostReason = GPU_DEVICE_LOST_REASON_DRIVER_ERROR;
          break;
        default:
          break;
      }
    }
  }
#endif
  if ([error.domain isEqualToString:MTLCommandBufferErrorDomain]) {
    switch ((MTLCommandBufferError)error.code) {
      case MTLCommandBufferErrorOutOfMemory:
        type   = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
        result = GPU_ERROR_OUT_OF_MEMORY;
        break;
      case MTLCommandBufferErrorTimeout:
        type       = GPU_DEVICE_ERROR_LOST;
        lostReason = GPU_DEVICE_LOST_REASON_HUNG;
        break;
      case MTLCommandBufferErrorNotPermitted:
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

  if (type == GPU_DEVICE_ERROR_LOST)
    result = GPU_ERROR_DEVICE_LOST;

  if ((device = queue ? queue->_device : NULL)) {
    detail = error ? error.localizedDescription.UTF8String : NULL;
    snprintf(message,
             sizeof(message),
             "Metal command buffer failed%s%s",
             detail ? ": " : "",
             detail ? detail : "");
    deviceReportError(device, type, lostReason, result, message);
  }

  if (result == GPU_ERROR_DEVICE_LOST)
    mt_loseQueue(queue);

  return result;
}

static
GPU_HIDE
void
cmdoncomplete(GPUCommandBuffer *__restrict cmdb,
              MTCommandBuffer  *__restrict native,
              MTCommandQueue   *__restrict queue,
              id<MTLCommandBuffer>         mtlCmdb) {
  CFTimeInterval start = 0.0, end = 0.0;
  GPUResult      result;

  /* taken by the queue's loss: its waiters went then, the queue may be gone */
  if (!cmdb || !native || !queue
      || !atomic_exchange_explicit(&native->completionReady,
                                   false,
                                   memory_order_acq_rel)) {
    return;
  }

  if (native->watched)
    mt_watchedDone(queue);

  result = !mtlCmdb ? GPU_ERROR_BACKEND_FAILURE
           : mtlCmdb.status == MTLCommandBufferStatusError ? mt_commandFailed(cmdb->_queue, mtlCmdb.error)
           : GPU_OK;
  mt_settleSignals(native, result != GPU_OK);

  if (mtlCmdb && (cmdb->_recordsGPUFrameTime || cmdb->_frameFeedback)) {
    if (@available(macOS 10.15, iOS 10.3, *)) {
      start = mtlCmdb.GPUStartTime;
      end   = mtlCmdb.GPUEndTime;
      mt_recordGPUFrameTime(cmdb, start, end);
    }
  }

  if (cmdb->_frameFeedback)
    frameFeedbackComplete(cmdb->_frameFeedback, result,
                          result == GPU_OK ? start : 0.0,
                          result == GPU_OK ? end : 0.0);

  finishCommandBuffer(cmdb, mt_recycleCommandBuffer, result);
  dispatch_group_leave(queue->inFlightGroup);
}

#if MT_HAS_METAL4
static
GPU_HIDE
void
cmdoncomplete4(GPUCommandBuffer *__restrict cmdb,
               MTCommandBuffer  *__restrict native,
               MTCommandQueue   *__restrict queue,
               id                           feedback) {
  id<MTL4CommitFeedback> modernFeedback;
  NSError               *error;
  CFTimeInterval         start = 0.0, end = 0.0;
  GPUResult              result = GPU_ERROR_BACKEND_FAILURE;

  /* taken by the queue's loss: its waiters went then, the queue may be gone */
  if (!cmdb || !native || !queue
      || !atomic_exchange_explicit(&native->completionReady,
                                   false,
                                   memory_order_acq_rel)) {
    return;
  }

  if (native->watched)
    mt_watchedDone(queue);

  if (@available(macOS 26.0, iOS 26.0, *)) {
    modernFeedback = feedback;
    error  = modernFeedback.error;
    result = !modernFeedback ? GPU_ERROR_BACKEND_FAILURE
             : error         ? mt_commandFailed(cmdb->_queue, error)
             : GPU_OK;

    if (cmdb->_recordsGPUFrameTime || cmdb->_frameFeedback) {
      start = modernFeedback.GPUStartTime;
      end   = modernFeedback.GPUEndTime;
      mt_recordGPUFrameTime(cmdb, start, end);
    }
  }

  mt_settleSignals(native, result != GPU_OK);
  if (cmdb->_frameFeedback)
    frameFeedbackComplete(cmdb->_frameFeedback, result,
                          result == GPU_OK ? start : 0.0,
                          result == GPU_OK ? end : 0.0);

  finishCommandBuffer(cmdb, mt_recycleCommandBuffer, result);
  dispatch_group_leave(queue->inFlightGroup);
}
#endif

/* a slot with no upload in flight (none, or one the GPU has finished), looked for without waiting, the first ones
 * first; with every one in flight, the next in turn (its upload waited for) */
static MTTransferSlot*
mt_freeTransferSlot(MTCommandQueue *native) {
  MTTransferSlot *slot;
  uint32_t        i;

  for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
    slot = &native->transferSlots[i];
    if (!slot->pending || !atomic_load_explicit(&slot->inFlight, memory_order_acquire)) {
      return slot;
    }
  }

  return &native->transferSlots[native->nextTransferSlot];
}

/* large staging of slots the GPU is done with goes back */
static void
mt_trimTransferStaging(GPUQueue *queue, MTCommandQueue *native) {
  MTTransferSlot *slot;
  uint32_t        i;

  for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
    slot = &native->transferSlots[i];

    if (!slot->staging || slot->capacity <= MT_TRANSFER_STAGING_KEEP)
      continue;

    if (slot->pending) {
      if (atomic_load_explicit(&slot->inFlight, memory_order_acquire))
        continue;
      (void)mt_waitTransfer(queue, slot, false);
    }

    [slot->staging release];
    slot->staging  = nil;
    slot->capacity = 0u;
    slot->used     = 0u;
  }
}

GPU_HIDE
GPUResult
mt_beginTransfer(GPUQueue                  *queue,
                 uint64_t                   sizeBytes,
                 id<MTLBlitCommandEncoder> *outBlit,
                 id<MTLBuffer>             *outStaging,
                 uint64_t                  *outOffset) {
  MTCommandQueue     *native;
  MTTransferSlot     *slot;
  id<MTLCommandQueue> commandQueue;
  uint64_t            offset;
  GPUResult           result;

  if (!queue || !queue->_device || !outBlit || !outStaging || !outOffset
      || sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBlit    = nil;
  *outStaging = nil;
  *outOffset  = 0u;
  native      = mt_commandQueue(queue);

  if (!native || (!native->classic && !native->upload)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (mt_queueLost(native)) {
    return GPU_ERROR_DEVICE_LOST;
  }

  if (sizeBytes > MT_TRANSFER_STAGING_KEEP)
    native->transfersSinceLarge = 0u;

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

  slot   = mt_freeTransferSlot(native);
  result = mt_waitTransfer(queue, slot, true);

  /* a batch: past MT_TRANSFER_LARGE_IDLE without large staging, the large
   * staging left goes, this slot's too (done with: it's set up small below) */
  if (sizeBytes <= MT_TRANSFER_STAGING_KEEP && native->transfersSinceLarge < MT_TRANSFER_LARGE_IDLE)
    native->transfersSinceLarge++;
  if (result == GPU_OK && native->transfersSinceLarge >= MT_TRANSFER_LARGE_IDLE)
    mt_trimTransferStaging(queue, native);

  if (result != GPU_OK
      || !mt_ensureTransferStaging(queue, slot, sizeBytes)) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  commandQueue = native->classic ? native->classic : native->upload;

  @autoreleasepool {
    slot->command = [[commandQueue commandBuffer] retain];
#if MT_HAS_METAL4
    if (native->mode == MTCommandMode4) {
      [slot->command encodeWaitForEvent:native->transferEvent
                                 value:native->transferValue];
    }
#endif
    slot->blit    = [[slot->command blitCommandEncoder] retain];
  }

  if (!slot->command || !slot->blit) {
    [slot->blit release];
    [slot->command release];
    slot->blit    = nil;
    slot->command = nil;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  slot->used                 = sizeBytes;
  native->activeTransferSlot = (uint32_t)(slot - native->transferSlots);
  native->transferOpen       = true;
  *outBlit                   = slot->blit;
  *outStaging                = slot->staging;
  *outOffset                 = 0u;

  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_flushTransfers(GPUQueue *queue, bool wait) {
  MTCommandQueue *native;
  MTTransferSlot *slot;
  GPUResult       flushResult;
  uint64_t        limit;
  uint32_t        i;
  GPUResult       result;
  bool            lost;

  native = mt_commandQueue(queue);

  if (!native) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4 && native->transferValue >= UINT64_MAX - 1u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  flushResult = GPU_OK;

  if (native->transferOpen) {
    slot = &native->transferSlots[native->activeTransferSlot];

    if (!slot->command || !slot->blit) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    [slot->blit endEncoding];
    [slot->blit release];
    slot->blit           = nil;
    native->transferOpen = false;

    /* in flight unless lost, under the lock the loss sweeps under */
    limit = mt_hangLimit(queue);
    os_unfair_lock_lock(&native->poolLock);
    if (!(lost = mt_queueLost(native))) {
      slot->watched = limit != 0u;
      atomic_store_explicit(&slot->inFlight, true, memory_order_release);
      dispatch_group_enter(native->inFlightGroup);
      if (limit != 0u)
        mt_watchSubmits(queue, native, 1u, limit);
    }
    os_unfair_lock_unlock(&native->poolLock);

    if (lost) {
      /* nothing runs on a lost queue */
      [slot->command release];
      slot->command = nil;
      slot->used    = 0u;
      flushResult   = GPU_ERROR_DEVICE_LOST;
    } else {
#if MT_HAS_METAL4
      if (native->mode == MTCommandMode4) {
        slot->signal = ++native->transferValue;
        [slot->command encodeSignalEvent:native->transferEvent value:slot->signal];
      }
#endif
      [slot->command addCompletedHandler:^(id<MTLCommandBuffer> batch) {
        mt_transferDone(queue, slot, batch);
      }];
      [slot->command commit];
#if MT_HAS_METAL4
      if (native->mode == MTCommandMode4) {
        if (@available(macOS 26.0, iOS 26.0, *)) {
          [native->modern waitForEvent:native->transferEvent
                                value:native->transferValue];
        }
      }
#endif
      slot->pending            = true;
      native->nextTransferSlot = (native->activeTransferSlot + 1u) % MT_TRANSFER_SLOT_COUNT;
    }
  }

  if (wait) {
    for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
      result = mt_waitTransfer(queue, &native->transferSlots[i], false);

      if (flushResult == GPU_OK && result != GPU_OK) {
        flushResult = result;
      }
    }
  }

  return flushResult;
}

GPU_HIDE
GPUResult
mt_waitCommandQueueIdle(GPUQueue *queue) {
  MTCommandQueue *native;
  GPUResult       result;

  native = mt_commandQueue(queue);

  if (!native || !native->inFlightGroup) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = mt_flushTransfers(queue, true);

  if (result != GPU_OK) {
    return result;
  }

  /* a lost queue's in-flight work was let go: this returns */
  dispatch_group_wait(native->inFlightGroup, DISPATCH_TIME_FOREVER);
  if (mt_queueLost(native))
    return GPU_ERROR_DEVICE_LOST;
  if (native->transfersSinceLarge >= MT_TRANSFER_LARGE_IDLE)
    mt_trimTransferStaging(queue, native);

  return GPU_OK;
}

GPU_HIDE
GPUQueue*
mt_newCommandQueue(GPUDevice *__restrict device) {
  GPUDeviceMT    *deviceMT;
  GPUQueue       *que;
  MTCommandQueue *native;
  uint32_t        i;
  bool            slotsReady;
#if MT_HAS_METAL4
  const char     *shaderValidation;
#endif

  deviceMT = device->_priv;
  que      = calloc(1, sizeof(*que));
  native   = calloc(1, sizeof(*native));

  if (!que || !native) {
    free(native);
    free(que);
    return NULL;
  }

  native->poolLock   = OS_UNFAIR_LOCK_INIT;
  native->submitLock = OS_UNFAIR_LOCK_INIT;
  native->mode       = deviceMT->commandMode;
#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      shaderValidation         = getenv("MTL_SHADER_VALIDATION");
      native->shaderValidation = shaderValidation && strcmp(shaderValidation, "1") == 0;

      native->modern        = [deviceMT->device newMTL4CommandQueue];
      native->upload        = [deviceMT->device newCommandQueue];
      native->transferEvent = [deviceMT->device newSharedEvent];
      native->commitOptions = [MTL4CommitOptions new];
    }
  } else
#endif
  {
    native->classic = [deviceMT->device newCommandQueue];
  }

  native->inFlightGroup = dispatch_group_create();
  slotsReady            = true;
  atomic_init(&native->progressNs, 0u);
  atomic_init(&native->watched, 0u);

  for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
    atomic_init(&native->transferSlots[i].inFlight, false);
    slotsReady = (native->transferSlots[i].done = dispatch_semaphore_create(0)) && slotsReady;
  }
#if MT_HAS_METAL4
  if ((!native->classic
       && (!native->modern || !native->upload || !native->transferEvent
           || !native->commitOptions))
      || !native->inFlightGroup || !slotsReady) {
#else
  if (!native->classic || !native->inFlightGroup || !slotsReady) {
#endif
    [native->classic release];
    [native->upload release];
    [native->modern release];
    [native->transferEvent release];
    [native->commitOptions release];

    if (native->inFlightGroup) {
      dispatch_release(native->inFlightGroup);
    }

    for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
      if (native->transferSlots[i].done)
        dispatch_release(native->transferSlots[i].done);
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
mt_destroyCommandQueue(GPUQueue *__restrict queue) {
  MTCommandBuffer *command;
  MTCommandBuffer *next;
  MTCommandQueue  *native;
  uint32_t         i;
  bool             lost;

  if (!queue) {
    return;
  }

  if (queue->_priv) {
    native = mt_commandQueue(queue);

    (void)mt_flushTransfers(queue, true);
    dispatch_group_wait(native->inFlightGroup, DISPATCH_TIME_FOREVER);
    lost    = mt_queueLost(native);

    /* no hang check runs past its cancel handler */
    if (native->hangTimer) {
      dispatch_source_cancel(native->hangTimer);
      dispatch_semaphore_wait(native->hangTimerGone, DISPATCH_TIME_FOREVER);
      dispatch_release(native->hangTimer);
      dispatch_release(native->hangTimerGone);
    }
    command = native->commands;

    while (command) {
      next = command->next;

      /* abandoned with the queue: Metal may still hold it and call back */
      if (!command->abandoned) {
        mt_settleSignals(command, false);
        free(command->signals);
        mt_destroyCommandBufferState(command);
      }

      command = next;
    }

    [native->classic release];
    [native->upload release];
    [native->modern release];
    [native->transferEvent release];
    [native->commitOptions release];

    for (i = 0u; i < MT_TRANSFER_SLOT_COUNT; i++) {
      [native->transferSlots[i].blit release];
      [native->transferSlots[i].command release];
      [native->transferSlots[i].staging release];
      dispatch_release(native->transferSlots[i].done);
    }

    dispatch_release(native->inFlightGroup);

    /* a lost queue's late handlers still read its batches' flags */
    if (!lost)
      free(native);
  }

  free(queue);
}

GPU_HIDE
GPUQueue*
mt_getCommandQueue(GPUDevice *__restrict device,
                   GPUQueueFlagBits      bits,
                   uint32_t              index) {
  GPUQueue    *que;
  GPUDeviceMT *deviceMT;
  uint32_t     matchIndex;
  uint32_t     i;

  deviceMT   = device->_priv;
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
                      double   *outNanosecondsPerTick) {
#if MT_HAS_METAL4
  GPUDeviceMT    *deviceMT;
  MTCommandQueue *native;
  uint64_t        frequency;

  native   = mt_commandQueue(queue);
  deviceMT = queue && queue->_device ? queue->_device->_priv : NULL;

  if (!native || native->mode != MTCommandMode4 || !deviceMT
      || !deviceMT->device) {
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
#else
  GPU__UNUSED(queue);
  GPU__UNUSED(outNanosecondsPerTick);
  return GPU_ERROR_UNSUPPORTED;
#endif
}

GPU_HIDE
GPUCommandBuffer*
mt_newCommandBuffer(GPUQueue         *__restrict cmdb,
                    const char       *__restrict label,
                    void             *__restrict sender,
                    GPUCommandBufferCompletionFn oncomplete) {
  MTCommandQueue   *queue;
  GPUCommandBuffer *cb;
  MTCommandBuffer  *native;
  MTUploadChunk    *upload;
#if MT_HAS_METAL4
  GPUDeviceMT      *deviceMT;
#endif
#if GPU_BUILD_WITH_DEBUG_MARKERS
  NSString         *nativeLabel;
#endif

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
  cb->_priv                   = native;
  cb->_queue                  = cmdb;
  native->pendingAfterStages  = 0u;
  native->pendingBeforeStages = 0u;
  native->pendingVisibility   = 0u;
  native->groupAfterStages    = 0u;
  native->groupBeforeStages   = 0u;
  native->groupVisibility     = 0u;

  for (upload = native->uploads; upload; upload = upload->next) {
    upload->offset = 0u;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      if (!native->modern) {
        deviceMT       = cmdb->_device->_priv;
        native->modern = [deviceMT->device newCommandBuffer];
      }

      [native->modern beginCommandBufferWithAllocator:native->allocator];
      [native->modern useResidencySet:native->residency];
    }
  } else
#endif
  {
    @autoreleasepool {
      native->classic = [[queue->classic commandBuffer] retain];
    }
  }

  if (!native->classic && !native->modern) {
    mt_recycleCommandBuffer(cb);
    return NULL;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    @autoreleasepool {
      nativeLabel = [NSString stringWithUTF8String:label];

      native->classic.label = nativeLabel;
#if MT_HAS_METAL4
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4CommandBuffer>)native->modern setLabel:nativeLabel];
      }
#endif
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
mt_ccmdbufOnComplete(GPUCommandBuffer *__restrict cmdb,
                     void             *__restrict sender,
                     GPUCommandBufferCompletionFn oncomplete) {
  if (!cmdb || cmdb->_submitted) {
    return;
  }

  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = oncomplete;
}

GPU_HIDE
GPUResult
mt_discardCommandBuffer(GPUCommandBuffer *__restrict cmdb) {
  MTCommandBuffer *native;

  native = mt_commandBuffer(cmdb);

  if (!native) {
    discardCommandBufferState(cmdb, NULL);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_closeOpenEncoder(native);
      [native->modern endCommandBuffer];
    } else {
      discardCommandBufferState(cmdb, NULL);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
#endif

  discardCommandBufferState(cmdb, mt_recycleCommandBuffer);

  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_cmdbufCommit(GPUCommandBuffer *__restrict cmdb) {
#if MT_HAS_METAL4
  id<MTL4CommandBuffer> buffers[1];
#endif
  MTCommandBuffer      *native;
  MTCommandQueue       *queue;
  id<MTLCommandBuffer>  mcb;
  id<CAMetalDrawable>   drawable;
  GPUResult             result;

  if (!cmdb) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = mt_commandBuffer(cmdb);
  queue  = cmdb->_queue ? mt_commandQueue(cmdb->_queue) : NULL;

  if (!native || !queue) {
    return mt_abortCommandBuffer(cmdb, GPU_ERROR_BACKEND_FAILURE);
  }

  if ((result = mt_beginCommits(cmdb->_queue, queue, &native, 1u)) != GPU_OK) {
    return mt_abortCommandBuffer(cmdb, result);
  }

  if ((result = mt_flushTransfers(cmdb->_queue, false)) != GPU_OK) {
    return mt_cancelCommit(cmdb, native, queue, result);
  }

  /* barriers with no encoder after them here: Metal 4's on one of their own
   * now, classic's wait on the queue's next encoder */
  mt_flushPendingBarrier(cmdb);
  mt_carryPendingBarrier(cmdb);

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4CommitOptions *options;

      drawable = native->drawable;

      if (drawable) {
        [native->modern useResidencySet:drawable.layer.residencySet];
      }

      [native->residency commit];
      [native->modern endCommandBuffer];
      buffers[0]       = native->modern;
      native->drawable = nil;

      /* serialized submission rearms the handlers consumed by each commit. */
      options = queue->commitOptions;
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        cmdoncomplete4(cmdb, native, queue, feedback);
      }];
      if (cmdb->_frameFeedback)
        mt_observeFrameFeedback(cmdb->_frameFeedback, drawable);

      if (drawable) {
        [queue->modern waitForDrawable:drawable];
      }

      [queue->modern commit:buffers
                      count:1u
                    options:options];
      [queue->modern signalEvent:queue->transferEvent
                          value:++queue->transferValue];

      if (drawable) {
        [queue->modern signalDrawable:drawable];
        [drawable present];
        [drawable release];
      }

      return GPU_OK;
    }

    return mt_cancelCommit(cmdb, native, queue, GPU_ERROR_BACKEND_FAILURE);
  }
#endif

  mcb = native->classic;

  if (!mcb) {
    return mt_cancelCommit(cmdb, native, queue, GPU_ERROR_BACKEND_FAILURE);
  }

  drawable         = native->drawable;
  native->drawable = nil;
  if (drawable)
    [mcb retain];

  [mcb addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
    cmdoncomplete(cmdb, native, queue, buffer);
  }];
  if (cmdb->_frameFeedback) {
    mt_observeFrameFeedback(cmdb->_frameFeedback, drawable ? drawable : native->feedbackDrawable);
    [native->feedbackDrawable release];
    native->feedbackDrawable = nil;
  }

  [mcb commit];

  if (drawable) {
    /* Core Animation needs presentation on the caller's transaction thread. */
    [mcb waitUntilScheduled];
    [drawable present];
    [drawable release];
    [mcb release];
  }

  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_submitCommandBuffers(GPUQueue                *__restrict queueHandle,
                        uint32_t                            count,
                        GPUCommandBuffer *const *__restrict buffers) {
#if MT_HAS_METAL4
  MTCommandBuffer      *natives[MT_SUBMIT_STACK_COUNT];
  id<MTL4CommandBuffer> modern[MT_SUBMIT_STACK_COUNT];
  MTCommandQueue       *queue;
  GPUCommandBuffer     *cmdb;
  MTCommandBuffer      *native;
  GPUResult             flushResult;
  uint32_t              validateIndex;
  uint32_t              cleanupIndex;
  uint32_t              submitIndex;

  queue = mt_commandQueue(queueHandle);

  if (!queue || !buffers || count < 2u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (count > MT_SUBMIT_STACK_COUNT) {
    return mt__commitCommandBuffers(count, buffers);
  }

  for (validateIndex = 0u; validateIndex < count; validateIndex++) {
    natives[validateIndex] = mt_commandBuffer(buffers[validateIndex]);

    if (!natives[validateIndex] || natives[validateIndex]->owner != queue
        || natives[validateIndex]->mode != MTCommandMode4 || !natives[validateIndex]->modern
        || natives[validateIndex]->drawable) {
      return mt__commitCommandBuffers(count, buffers);
    }

    modern[validateIndex] = natives[validateIndex]->modern;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTL4CommitOptions *options;

    if ((flushResult = mt_beginCommits(queueHandle, queue, natives, count)) != GPU_OK) {
      for (cleanupIndex = 0u; cleanupIndex < count; cleanupIndex++) {
        (void)mt_abortCommandBuffer(buffers[cleanupIndex], flushResult);
      }

      return flushResult;
    }

    if ((flushResult = mt_flushTransfers(queueHandle, false)) != GPU_OK) {
      for (cleanupIndex = 0u; cleanupIndex < count; cleanupIndex++) {
        (void)mt_cancelCommit(buffers[cleanupIndex], natives[cleanupIndex], queue, flushResult);
      }

      return flushResult;
    }

    options = queue->commitOptions;

    for (submitIndex = 0u; submitIndex < count; submitIndex++) {
      cmdb   = buffers[submitIndex];
      native = natives[submitIndex];
      mt_flushPendingBarrier(cmdb);
      mt_carryPendingBarrier(cmdb);
      [native->residency commit];
      [native->modern endCommandBuffer];
      [options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
        cmdoncomplete4(cmdb, native, queue, feedback);
      }];
    }

    [queue->modern commit:modern count:count options:options];
    [queue->modern signalEvent:queue->transferEvent
                        value:++queue->transferValue];
    return GPU_OK;
  }

  return mt__commitCommandBuffers(count, buffers);
#else
  if (!mt_commandQueue(queueHandle) || !buffers || count < 2u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return mt__commitCommandBuffers(count, buffers);
#endif
}

static void
mt_lockQueue(GPUQueue *queue) {
  os_unfair_lock_lock(&mt_commandQueue(queue)->submitLock);
}

static void
mt_unlockQueue(GPUQueue *queue) {
  os_unfair_lock_unlock(&mt_commandQueue(queue)->submitLock);
}

#if MT_HAS_METAL4
@implementation MTUploadReady

- (instancetype)initWithCallback:(GPUUploaderReadyFn)ready user:(void *)user {
  if ((self = [super init])) {
    _ready = ready;
    _user  = user;
    _lock  = (os_unfair_lock)OS_UNFAIR_LOCK_INIT;
  }

  return self;
}

- (void)notify {
  GPUUploaderReadyFn ready;
  void              *user;

  os_unfair_lock_lock(&_lock);
  ready  = _ready;
  user   = _user;
  _ready = NULL;
  _user  = NULL;
  os_unfair_lock_unlock(&_lock);

  if (ready)
    ready(user);
}

@end

static void
uploadFenceReady(void *user) {
  MTUploadReady *ready = user;

  [ready notify];
  [ready release];
}

static uint64_t
mt_uploadProgress(GPUQueue *queue, bool completed) {
  MTCommandQueue *native = mt_commandQueue(queue);

  if (native->mode != MTCommandMode4)
    return 0u;

  return completed ? native->transferEvent.signaledValue : native->transferValue;
}

static GPUResult
mt_observeUploadProgress(GPUQueue           *queue,
                         uint64_t            value,
                         GPUFence           *fence,
                         GPUUploaderReadyFn callback,
                         void               *user) {
  MTCommandQueue         *native = mt_commandQueue(queue);
  MTLSharedEventListener *listener;
  MTUploadReady          *ready;
  GPUResult               result;

  if (native->mode != MTCommandMode4)
    return GPU_ERROR_UNSUPPORTED;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if (native->transferEvent.signaledValue >= value) {
      callback(user);
      return GPU_OK;
    }
    if (!(listener = [MTLSharedEventListener sharedListener])
        || !(ready = [[MTUploadReady alloc] initWithCallback:callback user:user]))
      return GPU_ERROR_UNSUPPORTED;

    /* completion also consumes the observer if a failed submit leaves the event behind */
    ready->completion.ready = uploadFenceReady;
    ready->completion.user  = ready;
    [ready retain];
    result = observeFenceCompletion(fence, &ready->completion);
    if (result != GPU_OK) {
      [ready release];
    } else if (!GPUIsFenceSignaled(fence)) {
      [native->transferEvent notifyListener:listener
                                    atValue:value
                                      block:^(id<MTLSharedEvent> event, uint64_t completed) {
        GPU__UNUSED(event);
        GPU__UNUSED(completed);
        [ready notify];
      }];
    }
    [ready release];
    return result;
  }
  return GPU_ERROR_UNSUPPORTED;
}
#endif

GPU_HIDE
void
mt_initCmdQue(GPUCommandQueueApi *api) {
  api->lock                    = mt_lockQueue;
  api->unlock                  = mt_unlockQueue;
  api->waitIdle                = mt_waitCommandQueueIdle;
  api->newCommandQueue         = mt_newCommandQueue;
  api->getCommandQueue         = mt_getCommandQueue;
  api->getTimestampPeriod      = mt_getTimestampPeriod;
  api->newCommandBuffer        = mt_newCommandBuffer;
  api->commandBufferOnComplete = mt_ccmdbufOnComplete;
  api->discard                 = mt_discardCommandBuffer;
  api->commit                  = mt_cmdbufCommit;
  api->submit                  = mt_submitCommandBuffers;
  api->createSemaphore         = mt_createSemaphore;
  api->destroySemaphore        = mt_destroySemaphore;
  api->submitEx                = mt_submitEx;
  api->setHangLimit            = mt_setHangLimit;
#if MT_HAS_METAL4
  api->uploadProgress          = mt_uploadProgress;
  api->observeUploadProgress   = mt_observeUploadProgress;
#endif
}
