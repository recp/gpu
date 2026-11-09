/*
 * Copyright (C) 2026 Recep Aslantas
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

#include <gpu/gpu.h>
#include "../../src/api/cmdqueue_internal.h"
#import <Metal/Metal.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define WAIT_NS UINT64_C(2000000000)

static void
wake(void *user) {
  atomic_uint *count = user;

  atomic_fetch_add_explicit(count, 1u, memory_order_release);
}

static void
holdQueue(GPUQueue *queue, GPUSemaphore *gate, GPUFence *fence) {
  GPUQueueSubmitExInfo  submit = {0};
  GPUQueueSemaphoreWait wait   = {0};
  GPUCommandBuffer     *command;

  assert(GPUAcquireCommandBuffer(queue, "uploader ready gate", &command) == GPU_OK);
  wait.semaphore            = gate;
  wait.value                = 1u;
  wait.waitStages           = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers   = &command;
  submit.commandBufferCount = 1u;
  submit.pWaits             = &wait;
  submit.waitCount          = 1u;
  submit.fence              = fence;
  assert(GPUQueueSubmitEx(queue, &submit) == GPU_OK);
}

static void
checkReady(GPUDevice *device, GPUQueue *queue, bool cancel) {
  GPUUploaderCreateInfo info = {0};
  GPUUploaderOptions    options = {0};
  GPUBufferCreateInfo   bufferInfo = {0};
  struct timespec       delay = {0, 1000000l};
  uint8_t               initial[256], suffix[256], actual[512];
  GPUUploader          *uploader;
  GPUUploaderReady     *ready, *duplicate;
  GPUBuffer            *buffer;
  GPUSemaphore         *gate, *laterGate;
  GPUFence             *held, *laterHeld, *prefixFence, *last, *done;
  atomic_uint          *context;
  id<MTLSharedEvent>    event, laterEvent;
  atomic_uint          calls = 0u, rejects = 0u;
  uint64_t             limit;
  GPUResult            result;

  options.chain.sType      = GPU_STRUCTURE_TYPE_UPLOADER_OPTIONS;
  options.chain.structSize = sizeof(options);
  options.deferWhenBusy    = true;
  info.chain.sType         = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  info.chain.structSize    = sizeof(info);
  info.chain.pNext         = &options;
  info.stagingSizeBytes    = sizeof(initial);
  info.stagingBufferCount  = 1u;
  bufferInfo.sizeBytes     = sizeof(actual);
  bufferInfo.usage         = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;

  assert(GPUCreateUploader(queue, &info, &uploader) == GPU_OK);
  assert(GPUUploaderNotifyReady(uploader, wake, &calls, &ready) == GPU_OK);
  assert(atomic_exchange(&calls, 0u) == 1u);
  GPUDestroyUploaderReady(ready);

  assert(GPUCreateBuffer(device, &bufferInfo, &buffer) == GPU_OK);
  assert(GPUCreateSemaphore(device, NULL, &gate) == GPU_OK);
  assert(GPUCreateSemaphore(device, NULL, &laterGate) == GPU_OK);
  assert(GPUCreateFence(device, NULL, &held) == GPU_OK);
  assert(GPUCreateFence(device, NULL, &laterHeld) == GPU_OK);
  event      = (__bridge id<MTLSharedEvent>)gate->_priv;
  laterEvent = (__bridge id<MTLSharedEvent>)laterGate->_priv;

  memset(initial, 0x35, sizeof(initial));
  memset(suffix, 0xc7, sizeof(suffix));
  holdQueue(queue, gate, held);
  assert(GPUUploadBuffer(uploader, buffer, 0u, initial, sizeof(initial)) == GPU_OK);
  assert(GPUFlushUploads(uploader, &prefixFence) == GPU_OK);
  holdQueue(queue, laterGate, laterHeld);
  assert(GPUUploadBuffer(uploader, buffer, sizeof(initial), suffix, sizeof(suffix)) == GPU_ERROR_TIMEOUT);
  assert(GPUFlushUploads(uploader, &last) == GPU_OK && last == prefixFence);

  context = &calls;
  if (cancel) {
    assert((context = malloc(sizeof(*context))));
    atomic_init(context, 0u);
  }

  assert(GPUUploaderNotifyReady(uploader, wake, context, &ready) == GPU_OK);
  assert(GPUUploaderNotifyReady(uploader, wake, &rejects, &duplicate) == GPU_ERROR_INVALID_ARGUMENT);
  assert(!duplicate && !atomic_load(context) && !GPUIsFenceSignaled(last));
  if (cancel) {
    GPUDestroyUploaderReady(ready);
    assert(!GPUIsFenceSignaled(last));
    free(context);
  }

  event.signaledValue = 1u;
  limit = clock_gettime_nsec_np(CLOCK_MONOTONIC) + WAIT_NS;
  if (!cancel) {
    while (!atomic_load(&calls)) {
      assert(clock_gettime_nsec_np(CLOCK_MONOTONIC) < limit);
      nanosleep(&delay, NULL);
    }
    GPUDestroyUploaderReady(ready);
  }

  assert(!GPUIsFenceSignaled(laterHeld));
  memset(suffix, 0x79, sizeof(suffix));
  do {
    result = GPUUploadBuffer(uploader, buffer, sizeof(initial), suffix, sizeof(suffix));
    assert(clock_gettime_nsec_np(CLOCK_MONOTONIC) < limit);
    if (result == GPU_ERROR_TIMEOUT)
      nanosleep(&delay, NULL);
  } while (cancel && result == GPU_ERROR_TIMEOUT);
  assert(result == GPU_OK);
  assert(GPUFlushUploads(uploader, &done) == GPU_OK && done != last);

  laterEvent.signaledValue = 1u;
  assert(GPUWaitFence(done, WAIT_NS) == GPU_OK);
  assert(GPUWaitFence(last, WAIT_NS) == GPU_OK);
  assert(GPUWaitFence(held, WAIT_NS) == GPU_OK);
  assert(GPUWaitFence(laterHeld, WAIT_NS) == GPU_OK);
  assert(GPUQueueReadBuffer(queue, buffer, 0u, actual, sizeof(actual)) == GPU_OK);
  assert(!memcmp(actual, initial, sizeof(initial)));
  assert(!memcmp(actual + sizeof(initial), suffix, sizeof(suffix)));
  assert(atomic_load(&calls) == (cancel ? 0u : 1u) && !atomic_load(&rejects));

  GPUDestroyUploader(uploader);
  GPUDestroyFence(done);
  GPUDestroyFence(last);
  GPUDestroyFence(prefixFence);
  GPUDestroyFence(laterHeld);
  GPUDestroyFence(held);
  GPUDestroySemaphore(laterGate);
  GPUDestroySemaphore(gate);
  GPUDestroyBuffer(buffer);
  printf("uploader %s: exact prefix and retry\n", cancel ? "canceled before progress" : "ready once");
}

int
main(void) {
  @autoreleasepool {
    GPUDeviceCreateInfo info    = {0};
    const GPUFeature    feature = GPU_FEATURE_THREADED_UPLOADS;
    GPUInstance        *instance;
    GPUAdapter         *adapter;
    GPUDevice          *device;
    GPUQueue           *queue;

    if (GPUCreateInstance(NULL, &instance) != GPU_OK)
      return 1;

    adapter                    = GPUGetAutoSelectedAdapter(instance);
    info.required.pFeatures    = &feature;
    info.required.featureCount = 1u;
    if (!adapter || GPUCreateDevice(adapter, &info, &device) != GPU_OK) {
      GPUDestroyInstance(instance);
      return 77;
    }

    queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
    assert(queue);
    checkReady(device, queue, false);
    checkReady(device, queue, true);
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
  }

  return 0;
}
