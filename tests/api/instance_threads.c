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

/*
 * instances made on 8 threads at once as the process's first GPU-C calls:
 * the backend's one-time setup must run once with the others waiting for it
 * (backend/once.h). Later rounds make instances and devices concurrently.
 * Run under TSan to see a race; plainly it checks every thread gets its
 * instances. Exits 77 when this machine has no device for the backend
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L /* threads and sched_yield under strict C11 */
#endif

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sched.h>
#endif

enum {
  INSTANCE_THREADS = 8,
  INSTANCE_ROUNDS  = 3
};

typedef struct InstanceWorker {
  uint32_t made;    /* instances made */
  uint32_t devices; /* devices made */
  bool     failed;  /* something failed once an instance was there */
} InstanceWorker;

static int instanceGate;

static bool
gateOpen(void) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedCompareExchange((volatile LONG *)&instanceGate, 0, 0) != 0;
#else
  return __atomic_load_n(&instanceGate, __ATOMIC_ACQUIRE) != 0;
#endif
}

static void
openGate(void) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange((volatile LONG *)&instanceGate, 1);
#else
  __atomic_store_n(&instanceGate, 1, __ATOMIC_RELEASE);
#endif
}

static void
instanceWork(InstanceWorker *worker) {
  GPUInstanceCreateInfo info = {0};
  GPUInstance          *instance;
  GPUAdapter          **adapters;
  GPUDevice            *device;
  uint32_t              count;
  uint32_t              round;

  while (!gateOpen()) {
#if defined(_WIN32) || defined(WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "instance-threads";
  info.preferredBackend = GPU_BACKEND_DEFAULT;

  for (round = 0u; round < INSTANCE_ROUNDS; round++) {
    instance = NULL;

    if (GPUCreateInstance(&info, &instance) != GPU_OK || !instance)
      continue;

    worker->made++;
    count = 0u;

    /* devices from the second round: the first is the setup's race alone */
    if (round > 0u && GPUEnumerateAdapters(instance, &count, NULL) == GPU_OK && count) {
      if (!(adapters = calloc(count, sizeof(*adapters)))
          || GPUEnumerateAdapters(instance, &count, adapters) != GPU_OK || !count) {
        worker->failed = true;
      } else if ((device = GPUCreateDeviceWithDefaultQueues(adapters[0]))) {
        worker->devices++;
        GPUDestroyDevice(device);
      } else {
        worker->failed = true;
      }

      free(adapters);
    }

    GPUDestroyInstance(instance);
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
instanceThread(void *context) {
  instanceWork(context);
  return 0;
}
#else
static void*
instanceThread(void *context) {
  instanceWork(context);
  return NULL;
}
#endif

int
main(void) {
  InstanceWorker workers[INSTANCE_THREADS] = {{0}};
#if defined(_WIN32) || defined(WIN32)
  HANDLE         threads[INSTANCE_THREADS];
#else
  pthread_t      threads[INSTANCE_THREADS];
#endif
  uint32_t       started;
  uint32_t       made;
  uint32_t       devices;
  uint32_t       i;
  bool           ok;

  for (started = 0u; started < INSTANCE_THREADS; started++) {
#if defined(_WIN32) || defined(WIN32)
    if (!(threads[started] = CreateThread(NULL, 0u, instanceThread, &workers[started], 0u, NULL)))
      break;
#else
    if (pthread_create(&threads[started], NULL, instanceThread, &workers[started]) != 0)
      break;
#endif
  }

  openGate();

  for (i = 0u; i < started; i++) {
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
#else
    pthread_join(threads[i], NULL);
#endif
  }

  if (started != INSTANCE_THREADS) {
    fprintf(stderr, "instance threads: started %u of %u\n", started, (unsigned)INSTANCE_THREADS);
    return 1;
  }

  made    = 0u;
  devices = 0u;
  ok      = true;

  for (i = 0u; i < INSTANCE_THREADS; i++) {
    made    += workers[i].made;
    devices += workers[i].devices;
    ok       = ok && !workers[i].failed;
  }

  if (made == 0u) {
    printf("instance threads: no instance for the default backend here, skipped\n");
    return 77;
  }

  /* all or nothing: a thread that lost the setup's race gets none */
  for (i = 0u; i < INSTANCE_THREADS; i++) {
    if (workers[i].made != INSTANCE_ROUNDS) {
      fprintf(stderr, "instance threads: thread %u made %u of %u instances\n",
              i, workers[i].made, (unsigned)INSTANCE_ROUNDS);
      ok = false;
    }
  }

  if (!ok) {
    fprintf(stderr, "instance threads: concurrent instances or devices failed\n");
    return 1;
  }

  printf("instance threads: %u threads, %u instances, %u devices\n",
         (unsigned)INSTANCE_THREADS, made, devices);
  return 0;
}
