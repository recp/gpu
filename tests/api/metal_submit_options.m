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

#include "../../src/backend/mt/common.h"
#import <objc/runtime.h>
#include <pthread.h>

/* cover the native batch path and its larger-than-stack fallback together. */
enum { THREADS = 4, ROUNDS = 10, COMMANDS = 65 };

/* classic acquisition must stay below its native queue's outstanding limit. */
static const uint32_t counts[2][5] = {{1, 2, 7, 4, 5}, {1, 2, 7, 64, 65}};
static dispatch_semaphore_t releasedSignal;
static atomic_uint released;
static atomic_uint allocated;
static id (*originalNew)(id, SEL);
static char releaseKey;

typedef struct Completion {
  dispatch_group_t done;
  atomic_uint      calls;
} Completion;

typedef struct Worker {
  GPUDevice *device;
  GPUQueue  *queue;
  uint32_t   completed;
  bool       modern;
  Completion completions[ROUNDS][COMMANDS];
} Worker;

@interface OptionsObserver : NSObject
@end

@implementation OptionsObserver
- (void)dealloc {
  atomic_fetch_add_explicit(&released, 1u, memory_order_release);
  dispatch_semaphore_signal(releasedSignal);
  [super dealloc];
}
@end

static id
observe_options(id type, SEL selector) {
  OptionsObserver *observer;
  id               options;

  options = originalNew(type, selector);
  if (options) {
    observer = [OptionsObserver new];
    objc_setAssociatedObject(options, &releaseKey, observer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [observer release];
    atomic_fetch_add_explicit(&allocated, 1u, memory_order_release);
  }
  return options;
}

static void
fail(const char *message) {
  fprintf(stderr, "submit options: %s\n", message);
  _Exit(1);
}

static void
completed(void *sender, GPUCommandBuffer *command) {
  Completion *completion;

  GPU__UNUSED(command);
  completion = sender;
  if (atomic_fetch_add_explicit(&completion->calls, 1u, memory_order_release) == 0u)
    dispatch_group_leave(completion->done);
}

static void*
run_worker(void *data) {
  GPUQueueSubmitInfo submit = {0};
  GPUCommandBuffer *commands[COMMANDS];
  Worker           *worker;
  GPUFence         *fence = NULL;
  dispatch_group_t  done;
  uint32_t          round, count, i;

  worker = data;
  done   = dispatch_group_create();
  if (!done || GPUCreateFence(worker->device, NULL, &fence) != GPU_OK)
    fail("worker setup");

  submit.chain.sType      = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize = sizeof(submit);
  submit.ppCommandBuffers = commands;
  submit.fence            = fence;

  @autoreleasepool {
    for (round = 0; round < ROUNDS; round++) {
      count = counts[worker->modern ? 1 : 0][round % GPU_ARRAY_LEN(counts[0])];
      for (i = 0; i < count; i++) {
        worker->completions[round][i].done = done;
        atomic_init(&worker->completions[round][i].calls, 0u);
        if (GPUAcquireCommandBuffer(worker->queue, "options-reuse", &commands[i]) != GPU_OK)
          fail("acquire");

        GPUSetCommandBufferCompletionHandler(commands[i], &worker->completions[round][i], completed);
        dispatch_group_enter(done);
      }
      submit.commandBufferCount = count;
      if (GPUQueueSubmit(worker->queue, &submit) != GPU_OK
          || GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK
          || dispatch_group_wait(done, dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))))
        fail("submit/completion");

      worker->completed += count;
    }
    GPUDestroyFence(fence);
  }
  dispatch_release(done);
  return NULL;
}

int
main(void) {
  GPUDeviceCreateInfo  deviceInfo = {0};
  GPUAdapterProperties properties = {0};
  Worker               workers[THREADS] = {0};
  pthread_t            threads[THREADS];
  GPUInstance         *instance = NULL;
  GPUDevice           *device = NULL;
  GPUAdapter          *adapter;
  GPUQueue            *queue;
  MTCommandQueue      *native;
  Method               method = NULL;
  Class                type;
  const char          *requested;
  dispatch_time_t      deadline;
  GPUResult            result;
  GPUFeature           required = GPU_FEATURE_THREADED_UPLOADS;
  uint32_t             i, round, command, count, expected, total = 0;
  int                  status = 0;
  bool                 modern;

  requested = getenv("GPU_METAL_MODE");
  atomic_init(&released, 0u);
  atomic_init(&allocated, 0u);
  releasedSignal = dispatch_semaphore_create(0);

  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);

  deviceInfo.required.pFeatures    = &required;
  deviceInfo.required.featureCount = 1u;

  @autoreleasepool {
    type = NSClassFromString(@"MTL4CommitOptions");
    if (type) {
      method      = class_getClassMethod(type, @selector(new));
      originalNew = (id (*)(id, SEL))method_getImplementation(method);
      class_replaceMethod(object_getClass(type), @selector(new), (IMP)observe_options,
                          method_getTypeEncoding(method));
    }
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance))
        || GPUGetAdapterProperties(adapter, &properties) != GPU_OK
        || properties.backend != GPU_BACKEND_METAL)
      fail("adapter setup");

    result = GPUCreateDevice(adapter, &deviceInfo, &device);
    if (result == GPU_ERROR_UNSUPPORTED) {
      status = 77;
      goto cleanup;
    }
    if (result != GPU_OK)
      fail("device setup");

    queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
    native = mt_commandQueue(queue);
    if (!native)
      fail("queue setup");

    modern = native->mode == MTCommandMode4;
    if (requested && !strcmp(requested, "metal4") && !modern) {
      status = 77;
      goto cleanup;
    }
    if (requested && !strcmp(requested, "classic") && modern)
      fail("unexpected native mode");

    for (i = 0; i < THREADS; i++) {
      workers[i].device = device;
      workers[i].queue  = queue;
      workers[i].modern = modern;
      if (pthread_create(&threads[i], NULL, run_worker, &workers[i]))
        fail("thread creation");
    }
    for (i = 0; i < THREADS; i++) {
      if (pthread_join(threads[i], NULL))
        fail("thread join");
      total += workers[i].completed;
    }
cleanup:
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    if (method)
      class_replaceMethod(object_getClass(type), @selector(new), (IMP)originalNew,
                          method_getTypeEncoding(method));
  }

  if (status == 77) {
    dispatch_release(releasedSignal);
    return status;
  }

  for (i = 0; i < THREADS; i++) {
    for (round = 0; round < ROUNDS; round++) {
      count = counts[modern ? 1 : 0][round % GPU_ARRAY_LEN(counts[0])];
      for (command = 0; command < count; command++) {
        if (atomic_load_explicit(&workers[i].completions[round][command].calls,
                                 memory_order_acquire) != 1u)
          fail("completion must run exactly once");
      }
    }
  }
  expected = atomic_load_explicit(&allocated, memory_order_acquire);
  deadline = dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000));
  while (atomic_load_explicit(&released, memory_order_acquire) < expected) {
    if (dispatch_semaphore_wait(releasedSignal, deadline))
      fail("options lifetime");
  }
  if ((modern && expected == 0u)
      || atomic_load_explicit(&released, memory_order_acquire) != expected)
    fail("options allocation/release count");

  dispatch_release(releasedSignal);
  printf("submit options: native=%s callbacks=%u options=%u, mixed batches and concurrent completion passed\n",
         modern ? "metal4" : "classic", total, expected);
  return 0;
}
