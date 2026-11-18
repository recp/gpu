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
 * a backend's one-time setup (its function tables): the first thread to get
 * there runs it, any other arriving meanwhile yields until it is done, and
 * "done" is published with release, so every caller sees what it wrote.
 * Instances may be made on several threads at once. Afterwards one acquire
 * load. An atomic flag rather than pthread_once / InitOnceExecuteOnce, so
 * nothing depends on the Windows version a build declares
 */

#ifndef backend_once_h
#define backend_once_h

#include "../common.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <sched.h>
#endif

typedef uint32_t GPUBackendOnce; /* 0 not run, 1 running, 2 done */
#define GPU_BACKEND_ONCE_INIT 0u

static inline uint32_t
gpuBackendOnceState(GPUBackendOnce *once) {
#if defined(_MSC_VER) && !defined(__clang__)
  return (uint32_t)InterlockedCompareExchange((volatile LONG *)once, 0, 0);
#else
  return __atomic_load_n(once, __ATOMIC_ACQUIRE);
#endif
}

static GPU_NOINLINE void
gpuBackendOnceSlow(GPUBackendOnce *once, void (*setup)(void)) {
  uint32_t expected;

#if defined(_MSC_VER) && !defined(__clang__)
  expected = (uint32_t)InterlockedCompareExchange((volatile LONG *)once, 1, 0);
  if (expected == 0u) {
    setup();
    InterlockedExchange((volatile LONG *)once, 2);
    return;
  }
#else
  expected = 0u;
  if (__atomic_compare_exchange_n(once, &expected, 1u, false,
                                  __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
    setup();
    __atomic_store_n(once, 2u, __ATOMIC_RELEASE);
    return;
  }
#endif

  /* another thread is running it: the setup is a few table fills */
  while (gpuBackendOnceState(once) != 2u) {
#if defined(_WIN32) || defined(WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
  }
}

static inline void
gpuBackendOnce(GPUBackendOnce *once, void (*setup)(void)) {
  if (gpuBackendOnceState(once) != 2u) {
    gpuBackendOnceSlow(once, setup);
  }
}

#endif /* backend_once_h */
