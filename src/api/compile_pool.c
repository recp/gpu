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
#include "pipeline_cache_internal.h"

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
  GPUDevice         *device;
  GPUCompileTask    *head;
  GPUCompileTask    *tail;
  GPUCompileTask    *heldHead; /* queued while holding (compilePoolHold): */
  GPUCompileTask    *heldTail; /* started only with no other compile running */
  uint32_t           threadCount;
  uint32_t           running;
  uint32_t           urgent;   /* running from the queue or for a waiter */
  uint32_t           storing;  /* threads writing the device's caches */
  bool               compiled; /* a task ran since they were written */
  bool               holding;
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

/* the queue's next task, else a held one while no other compile runs: a
 * held compile never slows those something waits for */

static GPUCompileTask*
compilePool__take(GPUCompilePool *pool) {
  GPUCompileTask *task;

  if ((task = pool->head)) {
    if (!(pool->head = task->next))
      pool->tail = NULL;
    task->held = false;
    pool->urgent++;
  } else if ((task = pool->heldHead) && !pool->urgent) {
    if (!(pool->heldHead = task->next))
      pool->heldTail = NULL;
  } else {
    return NULL;
  }

  task->next    = NULL;
  task->started = true;
  pool->running++;

  return task;
}

/* the held tasks after those queued, as any other */

static void
compilePool__release(GPUCompilePool *pool) {
  GPUCompileTask *task;

  if (!pool->heldHead)
    return;

  for (task = pool->heldHead; task; task = task->next)
    task->held = false;

  if (pool->tail)
    pool->tail->next = pool->heldHead;
  else
    pool->head = pool->heldHead;
  pool->tail     = pool->heldTail;
  pool->heldHead = NULL;
  pool->heldTail = NULL;
  POOL_BROADCAST(pool, work);
}

/* the task's result is published under the lock. the last one run on the
 * asking thread wakes a pool thread to write the caches; a pool thread that
 * ran it writes them itself, next round */

static void
compilePool__run(GPUCompilePool *pool, GPUCompileTask *task, bool urgent, bool asking) {
  task->run(task);

  POOL_LOCK(pool);
  compileTaskFinish(task);
  pool->running--;
  pool->compiled = true;
  POOL_BROADCAST(pool, done);
  /* the last urgent one done: the held may start */
  if (urgent && !--pool->urgent && pool->heldHead && !pool->head)
    POOL_BROADCAST(pool, work);
  else if (asking && !pool->head && !pool->running)
    POOL_SIGNAL(pool, work);
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
    bool urgent;

    POOL_LOCK(pool);
    while (!pool->head && !(pool->heldHead && !pool->urgent) && !pool->stop
           && !(pool->compiled && !pool->running))
      POOL_WAIT(pool, work);

    urgent = pool->head != NULL;
    if ((task = compilePool__take(pool))) {
      POOL_UNLOCK(pool);
      compilePool__run(pool, task, urgent, false);
      continue;
    }

    if (pool->stop) {
      POOL_UNLOCK(pool);
      break;
    }

    /* the queue drained: what it compiled kept on disk now, not only when
     * the app flushes or destroys its cache (a process ended before then
     * would compile it all again). a stopping pool leaves it to them */
    pool->compiled = false;
    pool->storing++;
    POOL_UNLOCK(pool);

    storeDevicePipelineCaches(pool->device);

    POOL_LOCK(pool);
    pool->storing--;
    POOL_BROADCAST(pool, done);
    POOL_UNLOCK(pool);
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
  compilePool__release(pool);
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

  pool->device = device;

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
  task->held    = false;
  task->done    = 0u;

  if (!device || !(pool = device->_compilePool) || !pool->threadCount) {
    task->started = true;
    task->run(task);
    compileTaskFinish(task);
    return;
  }

  POOL_LOCK(pool);
  if ((task->held = pool->holding)) {
    if (pool->heldTail)
      pool->heldTail->next = task;
    else
      pool->heldHead = task;
    pool->heldTail = task;
    if (!pool->urgent && !pool->head)
      POOL_SIGNAL(pool, work);
  } else {
    if (pool->tail)
      pool->tail->next = task;
    else
      pool->head = task;
    pool->tail = task;
    POOL_SIGNAL(pool, work);
  }
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolHold(GPUDevice *device, bool hold) {
  GPUCompilePool *pool;

  if (!device || !(pool = device->_compilePool) || !pool->threadCount)
    return;

  POOL_LOCK(pool);
  pool->holding = hold;
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolRelease(GPUDevice *device) {
  GPUCompilePool *pool;

  if (!device || !(pool = device->_compilePool))
    return;

  POOL_LOCK(pool);
  compilePool__release(pool);
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
    GPUCompileTask **head = task->held ? &pool->heldHead : &pool->head;
    GPUCompileTask **tail = task->held ? &pool->heldTail : &pool->tail;

    for (link = head; *link && *link != task; link = &(*link)->next) {}

    if (*link) {
      *link = task->next;
      if (*tail == task) {
        *tail = NULL;
        for (GPUCompileTask *last = *head; last; last = last->next)
          *tail = last;
      }
      task->next    = NULL;
      task->held    = false;
      task->started = true;
      pool->running++;
      pool->urgent++; /* held ones wait while it runs */
      POOL_UNLOCK(pool);

      compilePool__run(pool, task, true, true);
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
  compilePool__release(pool);
  while (pool->head || pool->running)
    POOL_WAIT(pool, done);
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolWaitStores(GPUDevice *device) {
  GPUCompilePool *pool;

  if (!device || !(pool = device->_compilePool))
    return;

  POOL_LOCK(pool);
  while (pool->storing)
    POOL_WAIT(pool, done);
  POOL_UNLOCK(pool);
}

GPU_HIDE
void
compilePoolDestroy(GPUDevice *device) {
  (void)compilePoolConfigure(device, 0u);
}
