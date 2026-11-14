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

#include "retire_internal.h"
#include "cmdqueue_internal.h"

#if defined(_WIN32) || defined(WIN32)
#  define RETIRE_LOCK(l)   AcquireSRWLockExclusive(&(l)->lock)
#  define RETIRE_UNLOCK(l) ReleaseSRWLockExclusive(&(l)->lock)
#else
#  define RETIRE_LOCK(l)   pthread_mutex_lock(&(l)->lock)
#  define RETIRE_UNLOCK(l) pthread_mutex_unlock(&(l)->lock)
#endif

static inline GPURetired*
retire__head(GPURetireList *list) {
#if defined(_MSC_VER) && !defined(__clang__)
  return InterlockedCompareExchangePointer((PVOID volatile *)&list->head, NULL, NULL);
#else
  return __atomic_load_n(&list->head, __ATOMIC_ACQUIRE);
#endif
}

static inline void
retire__setHead(GPURetireList *list, GPURetired *head) {
#if defined(_MSC_VER) && !defined(__clang__)
  InterlockedExchangePointer((PVOID volatile *)&list->head, head);
#else
  __atomic_store_n(&list->head, head, __ATOMIC_RELEASE);
#endif
}

/* the newest serial whose objects may go: every queue with work pending
 * finished its work up to it */
static uint64_t
retire__limit(GPUQueue *const *queues, uint32_t queueCount) {
  uint64_t limit;
  uint64_t given;
  uint64_t done;
  uint32_t i;

  limit = UINT64_MAX;

  for (i = 0u; i < queueCount; i++) {
    if (!queues[i])
      continue;

    /* given first: work given after it is newer than any destroy before */
    given = gpuRetireLoad(&queues[i]->_retire.given);
    done  = gpuRetireLoad(&queues[i]->_retire.done);

    if (done < given && done < limit)
      limit = done;
  }

  return limit;
}

GPU_HIDE
bool
gpuRetireInit(GPURetireList *list, void *context) {
  list->head    = NULL;
  list->tail    = NULL;
  list->context = context;
  list->serial  = 0u;
#if defined(_WIN32) || defined(WIN32)
  InitializeSRWLock(&list->lock);
#else
  if (pthread_mutex_init(&list->lock, NULL) != 0)
    return false;
#endif
  list->ready = true;

  return true;
}

GPU_HIDE
void
gpuRetireFinish(GPURetireList *list) {
  GPURetired *node;
  GPURetired *next;

  if (!list || !list->ready)
    return;

  RETIRE_LOCK(list);
  node = list->head;
  retire__setHead(list, NULL);
  list->tail = NULL;
  RETIRE_UNLOCK(list);

  for (; node; node = next) {
    next = node->next;
    node->release(list->context, node->owner);
  }
}

GPU_HIDE
void
gpuRetireDestroy(GPURetireList *list) {
  if (!list || !list->ready)
    return;

#if !defined(_WIN32) && !defined(WIN32)
  pthread_mutex_destroy(&list->lock);
#endif
  list->ready = false;
}

GPU_HIDE
uint64_t
gpuRetireNextSerial(GPURetireList *list) {
#if defined(_MSC_VER) && !defined(__clang__)
  return (uint64_t)InterlockedIncrement64((volatile LONG64 *)&list->serial);
#else
  return __atomic_add_fetch(&list->serial, 1u, __ATOMIC_ACQ_REL);
#endif
}

GPU_HIDE
void
gpuRetireReclaim(GPURetireList *list, GPUQueue *const *queues, uint32_t queueCount) {
  GPURetired *ready;
  GPURetired *last;
  GPURetired *node;
  uint64_t    limit;

  if (!list || !list->ready || !retire__head(list))
    return;

  RETIRE_LOCK(list);
  limit = retire__limit(queues, queueCount);
  ready = list->head;
  last  = NULL;

  for (node = ready; node && node->serial <= limit; node = node->next)
    last = node;

  if (last) {
    last->next = NULL;
    retire__setHead(list, node);
    if (!node)
      list->tail = NULL;
  } else {
    ready = NULL;
  }
  RETIRE_UNLOCK(list);

  /* outside the lock: releases take other locks (memory pools, render targets) */
  for (node = ready; node; node = last) {
    last = node->next;
    node->release(list->context, node->owner);
  }
}

GPU_HIDE
void
gpuRetire(GPURetireList     *list,
          GPUQueue *const   *queues,
          uint32_t           queueCount,
          GPURetired        *node,
          void              *owner,
          GPURetireReleaseFn release) {
  if (!list || !list->ready) {
    release(list ? list->context : NULL, owner);
    return;
  }

  /* nothing waits and every queue finished what it was given: at once */
  if (!retire__head(list) && gpuRetireLoad(&list->serial) <= retire__limit(queues, queueCount)) {
    release(list->context, owner);
    return;
  }

  node->next    = NULL;
  node->owner   = owner;
  node->release = release;

  RETIRE_LOCK(list);
  /* under the lock: the list's serials stay in order */
  node->serial = gpuRetireLoad(&list->serial);

  if (list->tail)
    list->tail->next = node;
  else
    retire__setHead(list, node);

  list->tail = node;
  RETIRE_UNLOCK(list);

  /* older ones may be ready, and work may have finished meanwhile */
  gpuRetireReclaim(list, queues, queueCount);
}

GPU_HIDE
void
gpuRetireQueueDone(GPURetireList   *list,
                   GPUQueue *const *queues,
                   uint32_t         queueCount,
                   GPUQueue        *queue,
                   uint64_t         serial) {
  uint64_t done;

  if (!queue || !serial)
    return;

#if defined(_MSC_VER) && !defined(__clang__)
  done = gpuRetireLoad(&queue->_retire.done);

  while (done < serial) {
    uint64_t seen;

    seen = (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)&queue->_retire.done,
                                                  (LONG64)serial,
                                                  (LONG64)done);
    if (seen == done)
      break;
    done = seen;
  }
#else
  done = __atomic_load_n(&queue->_retire.done, __ATOMIC_RELAXED);

  while (done < serial
         && !__atomic_compare_exchange_n(&queue->_retire.done,
                                         &done,
                                         serial,
                                         true,
                                         __ATOMIC_ACQ_REL,
                                         __ATOMIC_RELAXED)) {
  }
#endif

  gpuRetireReclaim(list, queues, queueCount);
}

GPU_HIDE
void
gpuRetireSubmitted(GPURetireList *list, GPUQueue *queue, uint64_t *serial) {
  if (!*serial) {
    *serial = gpuRetireNextSerial(list);
    gpuRetireStore(&queue->_retire.given, *serial);
  }

  gpuRetireStore(&queue->_retire.submitted, *serial);
}

GPU_HIDE
void
gpuRetireOpenBatch(GPURetireList *list,
                   GPUQueue      *queue,
                   uint64_t      *serial,
                   uint64_t      *givenBefore) {
  *givenBefore = gpuRetireLoad(&queue->_retire.given);
  *serial      = gpuRetireNextSerial(list);
  gpuRetireStore(&queue->_retire.given, *serial);
}

GPU_HIDE
void
gpuRetireDropBatch(GPUQueue *queue, uint64_t *serial, uint64_t givenBefore) {
  if (*serial) {
    gpuRetireStore(&queue->_retire.given, givenBefore);
    *serial = 0u;
  }
}
