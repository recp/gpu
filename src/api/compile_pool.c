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

#include "compile_pool_internal.h"
#include "device_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#define GPU_COMPILE_POOL_MAX_THREADS 16u

typedef struct GPUCompilePool {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE work;
  CONDITION_VARIABLE done;
  HANDLE             threads[GPU_COMPILE_POOL_MAX_THREADS];
#else
  pthread_mutex_t    lock;
  pthread_cond_t     work;
  pthread_cond_t     done;
  pthread_t          threads[GPU_COMPILE_POOL_MAX_THREADS];
#endif
  GPUCompileTask    *head;
  GPUCompileTask    *tail;
  uint32_t           threadCount;
  uint32_t           running;
  bool               stop;
} GPUCompilePool;

#if defined(_WIN32) || defined(WIN32)
#  define POOL_LOCK(p)        EnterCriticalSection(&(p)->lock)
#  define POOL_UNLOCK(p)      LeaveCriticalSection(&(p)->lock)
#  define POOL_WAIT(p, c)     SleepConditionVariableCS(&(p)->c, &(p)->lock, INFINITE)
#  define POOL_SIGNAL(p, c)   WakeConditionVariable(&(p)->c)
#  define POOL_BROADCAST(p,c) WakeAllConditionVariable(&(p)->c)
#else
#  define POOL_LOCK(p)        pthread_mutex_lock(&(p)->lock)
#  define POOL_UNLOCK(p)      pthread_mutex_unlock(&(p)->lock)
#  define POOL_WAIT(p, c)     pthread_cond_wait(&(p)->c, &(p)->lock)
#  define POOL_SIGNAL(p, c)   pthread_cond_signal(&(p)->c)
#  define POOL_BROADCAST(p,c) pthread_cond_broadcast(&(p)->c)
#endif

static GPUCompileTask*
compilePool__take(GPUCompilePool *pool) {
  GPUCompileTask *task;

  if ((task = pool->head)) {
    if (!(pool->head = task->next))
      pool->tail = NULL;
    task->next    = NULL;
    task->started = true;
    pool->running++;
  }

  return task;
}

/* the task's result is published under the lock */

static void
compilePool__run(GPUCompilePool *pool, GPUCompileTask *task) {
  task->run(task);

  POOL_LOCK(pool);
  compileTaskFinish(task);
  pool->running--;
  POOL_BROADCAST(pool, done);
  POOL_UNLOCK(pool);
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
compilePool__worker(void *context) {
#else
static void*
compilePool__worker(void *context) {
#endif
  GPUCompilePool *pool;
  GPUCompileTask *task;

  pool = context;

  for (;;) {
    POOL_LOCK(pool);
    while (!pool->head && !pool->stop)
      POOL_WAIT(pool, work);

    if (!(task = compilePool__take(pool))) {
      POOL_UNLOCK(pool);
      break;
    }
    POOL_UNLOCK(pool);

    compilePool__run(pool, task);
  }

#if defined(_WIN32) || defined(WIN32)
  return 0;
#else
  return NULL;
#endif
}

static void
compilePool__stop(GPUCompilePool *pool) {
  uint32_t i;

  POOL_LOCK(pool);
  pool->stop = true;
  POOL_BROADCAST(pool, work);
  POOL_UNLOCK(pool);

  for (i = 0u; i < pool->threadCount; i++) {
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(pool->threads[i], INFINITE);
    CloseHandle(pool->threads[i]);
#else
    pthread_join(pool->threads[i], NULL);
#endif
  }

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&pool->lock);
#else
  pthread_cond_destroy(&pool->done);
  pthread_cond_destroy(&pool->work);
  pthread_mutex_destroy(&pool->lock);
#endif
  free(pool);
}

GPU_HIDE
GPUResult
compilePoolConfigure(GPUDevice *device, uint32_t threadCount) {
  GPUCompilePool *pool;
  GPUApi         *api;
  uint32_t        i;
  bool            threaded;

  if (!device)
    return GPU_ERROR_INVALID_ARGUMENT;

  /* the old pool finishes what it holds first */
  if ((pool = device->_compilePool)) {
    device->_compilePool = NULL;
    compilePool__stop(pool);
  }

  if (threadCount == 0u)
    return GPU_OK;

  if (threadCount > GPU_COMPILE_POOL_MAX_THREADS)
    threadCount = GPU_COMPILE_POOL_MAX_THREADS;

  /* a device only its creating thread may call: background compiles are
   * the backend's own (a pool without threads marks them asked for) */
  threaded = !(api = deviceApi(device)) || !api->callerThreadPipelines;

  if (!(pool = calloc(1u, sizeof(*pool))))
    return GPU_ERROR_OUT_OF_MEMORY;

#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&pool->lock);
  InitializeConditionVariable(&pool->work);
  InitializeConditionVariable(&pool->done);
#else
  if (pthread_mutex_init(&pool->lock, NULL) != 0) {
    free(pool);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (pthread_cond_init(&pool->work, NULL) != 0) {
    pthread_mutex_destroy(&pool->lock);
    free(pool);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (pthread_cond_init(&pool->done, NULL) != 0) {
    pthread_cond_destroy(&pool->work);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  for (i = 0u; threaded && i < threadCount; i++) {
#if defined(_WIN32) || defined(WIN32)
    if (!(pool->threads[i] = CreateThread(NULL, 0u, compilePool__worker, pool, 0u, NULL)))
      break;
#else
    if (pthread_create(&pool->threads[i], NULL, compilePool__worker, pool) != 0)
      break;
#endif
    pool->threadCount++;
  }

  if (threaded && pool->threadCount == 0u) {
    compilePool__stop(pool);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  device->_compilePool = pool;

  return GPU_OK;
}

GPU_HIDE
bool
compilePoolActive(GPUDevice *device) {
  return device && device->_compilePool;
}

GPU_HIDE
bool
compilePoolThreaded(GPUDevice *device) {
  GPUCompilePool *pool;

  return device && (pool = device->_compilePool) && pool->threadCount;
}

GPU_HIDE
void
compilePoolSubmit(GPUDevice *device, GPUCompileTask *task) {
  GPUCompilePool *pool;

  if (!task)
    return;

  task->next    = NULL;
  task->started = false;
  task->done    = 0u;

  if (!device || !(pool = device->_compilePool) || !pool->threadCount) {
    task->started = true;
    task->run(task);
    compileTaskFinish(task);
    return;
  }

  POOL_LOCK(pool);
  if (pool->tail)
    pool->tail->next = task;
  else
    pool->head = task;
  pool->tail = task;
  POOL_SIGNAL(pool, work);
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolWait(GPUDevice *device, GPUCompileTask *task) {
  GPUCompilePool  *pool;
  GPUCompileTask **link;

  if (!task || compileTaskDone(task) || !device || !(pool = device->_compilePool))
    return;

  POOL_LOCK(pool);

  if (!task->started) {
    for (link = &pool->head; *link && *link != task; link = &(*link)->next) {}

    if (*link) {
      *link = task->next;
      if (pool->tail == task) {
        pool->tail = NULL;
        for (GPUCompileTask *last = pool->head; last; last = last->next)
          pool->tail = last;
      }
      task->next    = NULL;
      task->started = true;
      pool->running++;
      POOL_UNLOCK(pool);

      compilePool__run(pool, task);
      return;
    }
  }

  while (!compileTaskDone(task))
    POOL_WAIT(pool, done);

  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolDrain(GPUDevice *device) {
  GPUCompilePool *pool;

  if (!device || !(pool = device->_compilePool))
    return;

  POOL_LOCK(pool);
  while (pool->head || pool->running)
    POOL_WAIT(pool, done);
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolDestroy(GPUDevice *device) {
  (void)compilePoolConfigure(device, 0u);
}
