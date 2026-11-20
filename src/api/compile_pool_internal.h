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
 * pipelines compiled on a device's background threads
 *
 * GPUSetPipelineCompileThreads starts a few threads; a task queued there
 * runs on the first free one. a thread that needs a task's result and finds
 * it not started yet runs it itself, so it never waits behind others' work.
 * once the queue drains, a pool thread writes the device's pipeline caches
 * that keep a file (storeDevicePipelineCaches).
 */

#ifndef gpu_compile_pool_internal_h
#define gpu_compile_pool_internal_h

#include "../common.h"

typedef struct GPUCompileTask GPUCompileTask;

struct GPUCompileTask {
  void          (*run)(GPUCompileTask *task);
  GPUCompileTask *next;
  uint32_t        done; /* compileTaskDone: what run wrote is seen */
  bool            started;
  bool            held; /* in the held list (compilePoolHold) */
};

static inline bool
compileTaskDone(const GPUCompileTask *task) {
#if defined(_MSC_VER) && !defined(__clang__)
  return InterlockedCompareExchange((volatile LONG *)&task->done, 0, 0) != 0;
#else
  return __atomic_load_n(&task->done, __ATOMIC_ACQUIRE) != 0u;
#endif
}

static inline void
compileTaskFinish(GPUCompileTask *task) {
#if defined(_MSC_VER) && !defined(__clang__)
  InterlockedExchange((volatile LONG *)&task->done, 1);
#else
  __atomic_store_n(&task->done, 1u, __ATOMIC_RELEASE);
#endif
}

/* threadCount 0 stops the pool once its queue is empty */
GPU_HIDE
GPUResult
compilePoolConfigure(GPUDevice *device, uint32_t threadCount);

/* background compiles asked for (GPUSetPipelineCompileThreads) */
GPU_HIDE
bool
compilePoolActive(GPUDevice *device);

/* false: the device compiles on the creating thread, or in the background
 * by its backend's own means (callerThreadPipelines) */
GPU_HIDE
bool
compilePoolThreaded(GPUDevice *device);

GPU_HIDE
void
compilePoolSubmit(GPUDevice *device, GPUCompileTask *task);

/* tasks submitted from now on held (hold false: those after not; the held
 * stay held): a thread starts one only while no other compile runs that
 * came from the queue or a wait, so those a frame waits for compile alone;
 * a wait runs one at once as any other not started. no-op without threads */
GPU_HIDE
void
compilePoolHold(GPUDevice *device, bool hold);

/* the held tasks queued after the others, as if never held */
GPU_HIDE
void
compilePoolRelease(GPUDevice *device);

/* the task done: run here when no thread started it */
GPU_HIDE
void
compilePoolWait(GPUDevice *device, GPUCompileTask *task);

/* every queued, held and running task done */
GPU_HIDE
void
compilePoolDrain(GPUDevice *device);

/* no pool thread writing a cache: one taken off the device's list (its
 * caches the threads write) can be destroyed after this */
GPU_HIDE
void
compilePoolWaitStores(GPUDevice *device);

GPU_HIDE
void
compilePoolDestroy(GPUDevice *device);

#endif /* gpu_compile_pool_internal_h */
