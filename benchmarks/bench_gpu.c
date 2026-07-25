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

#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bench.h"

#include <stdatomic.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <time.h>
#endif

enum {
  BENCH_REQUEST_WAIT_STEP_MS = 1
};

typedef struct BenchAdapterRequest {
  GPUAdapter  *adapter;
  GPUResult    result;
  atomic_bool  done;
} BenchAdapterRequest;

typedef struct BenchDeviceRequest {
  GPUDevice   *device;
  GPUResult    result;
  atomic_bool  done;
} BenchDeviceRequest;

static void
bench_sleepMillis(uint32_t milliseconds) {
#if defined(_WIN32) || defined(WIN32)
  Sleep(milliseconds);
#else
  struct timespec duration;

  duration.tv_sec  = (time_t)(milliseconds / 1000u);
  duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000l;
  nanosleep(&duration, NULL);
#endif
}

static void
bench_wait(atomic_bool *done) {
  while (!atomic_load_explicit(done, memory_order_acquire)) {
    bench_sleepMillis(BENCH_REQUEST_WAIT_STEP_MS);
  }
}

static void
bench_adapterReady(GPUResult result,
                   GPUAdapter *adapter,
                   void       *userData) {
  BenchAdapterRequest *request;

  request          = userData;
  request->adapter = adapter;
  request->result  = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

static void
bench_deviceReady(GPUResult result,
                  GPUDevice *device,
                  void      *userData) {
  BenchDeviceRequest *request;

  request         = userData;
  request->device = device;
  request->result = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

GPUAdapter *
bench_createAdapter(GPUInstance *instance) {
  BenchAdapterRequest request;
  GPUAdapter         *adapter;
  GPUResult           result;
  uint32_t            count;

  if (!instance) {
    return NULL;
  }

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result == GPU_OK || result == GPU_ERROR_INSUFFICIENT_CAPACITY) &&
      adapter) {
    return adapter;
  }

  request.adapter = NULL;
  request.result  = GPU_ERROR_BACKEND_FAILURE;
  atomic_init(&request.done, false);
  result          = GPURequestAdapter(instance, bench_adapterReady, &request);
  if (result != GPU_OK) {
    return NULL;
  }
  bench_wait(&request.done);
  if (request.result != GPU_OK) {
    return NULL;
  }
  return request.adapter;
}

GPUDevice *
bench_createDevice(GPUAdapter                *adapter,
                   const GPUDeviceCreateInfo *info) {
  BenchDeviceRequest request;
  GPUDevice         *device;
  GPUResult          result;

  if (!adapter) {
    return NULL;
  }

  device = NULL;
  if (info) {
    result = GPUCreateDevice(adapter, info, &device);
  } else {
    device = GPUCreateDeviceWithDefaultQueues(adapter);
    result = device ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
  }
  if (result == GPU_OK && device) {
    return device;
  }

  request.device = NULL;
  request.result = GPU_ERROR_BACKEND_FAILURE;
  atomic_init(&request.done, false);
  result         = GPURequestDevice(adapter, info, bench_deviceReady, &request);
  if (result != GPU_OK) {
    return NULL;
  }
  bench_wait(&request.done);
  if (request.result != GPU_OK) {
    return NULL;
  }
  return request.device;
}
