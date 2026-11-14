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
 * objects destroyed while queues may still use them
 *
 * for backends whose command buffers don't keep what they use alive
 * (Vulkan, D3D12): a buffer, texture or view destroyed while work given to a
 * queue may still use it goes once that work finishes. Every submit, and every
 * transfer batch when it opens, takes the list's next serial. A queue's work
 * goes out in serial order (a submit flushes the queue's open batch first,
 * both under the queue lock) and a fence covers the queue's earlier submits,
 * so a queue that finished serial S finished all of its work up to S.
 *
 * An object destroyed when the list's last serial was G was given to queues
 * only in work up to G: it may go once every queue has finished either G or
 * all it was given. With nothing pending (as after a wait) that holds at the
 * destroy and the object goes at once. Otherwise it waits, oldest first, and
 * goes as queues finish: gpuRetireQueueDone runs on a backend's completion
 * thread before the completion is reported (a fence waited on means its work's
 * destroyed objects are gone), after transfer and idle waits, and
 * gpuRetireFinish frees the rest when the device goes. The entries live in the
 * objects: nothing is allocated.
 */

#ifndef gpu_retire_internal_h
#define gpu_retire_internal_h

#include "../common.h"

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* each queue's, in GPUQueue (all atomic) */
typedef struct GPURetireQueue {
  uint64_t given;     /* the newest work given: a submit or an open transfer batch */
  uint64_t submitted; /* the newest submitted */
  uint64_t done;      /* the newest seen finished, and so all before it */
} GPURetireQueue;

typedef void (*GPURetireReleaseFn)(void *context, void *owner);

typedef struct GPURetired {
  struct GPURetired *next;
  void              *owner;
  GPURetireReleaseFn release;
  uint64_t           serial; /* the list's last serial at the destroy */
} GPURetired;

typedef struct GPURetireList {
  GPURetired *head; /* oldest first */
  GPURetired *tail;
  void       *context; /* the backend's device, given to releases */
  uint64_t    serial;  /* the last given to a queue's work (atomic) */
#if defined(_WIN32) || defined(WIN32)
  SRWLOCK     lock;
#else
  pthread_mutex_t lock;
#endif
  bool        ready;
} GPURetireList;

static inline uint64_t
gpuRetireLoad(const uint64_t *value) {
#if defined(_MSC_VER) && !defined(__clang__)
  return (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)value, 0, 0);
#else
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
#endif
}

static inline void
gpuRetireStore(uint64_t *value, uint64_t newValue) {
#if defined(_MSC_VER) && !defined(__clang__)
  InterlockedExchange64((volatile LONG64 *)value, (LONG64)newValue);
#else
  __atomic_store_n(value, newValue, __ATOMIC_RELEASE);
#endif
}

GPU_HIDE
bool
gpuRetireInit(GPURetireList *list, void *context);

/* the device idle: what is left goes now (a completion thread may still be
 * reclaiming, so under the lock) */
GPU_HIDE
void
gpuRetireFinish(GPURetireList *list);

/* last, once no queue is left */
GPU_HIDE
void
gpuRetireDestroy(GPURetireList *list);

GPU_HIDE
uint64_t
gpuRetireNextSerial(GPURetireList *list);

/* queues: every queue that may use the device's objects */
GPU_HIDE
void
gpuRetire(GPURetireList     *list,
          GPUQueue *const   *queues,
          uint32_t           queueCount,
          GPURetired        *node,
          void              *owner,
          GPURetireReleaseFn release);

GPU_HIDE
void
gpuRetireReclaim(GPURetireList *list, GPUQueue *const *queues, uint32_t queueCount);

/* queue finished its work up to serial (and so all before it); what waited
 * on it goes */
GPU_HIDE
void
gpuRetireQueueDone(GPURetireList   *list,
                   GPUQueue *const *queues,
                   uint32_t         queueCount,
                   GPUQueue        *queue,
                   uint64_t         serial);

/* a submit went out: *serial 0 takes the next one, a transfer batch passes
 * the one it took when it opened */
GPU_HIDE
void
gpuRetireSubmitted(GPURetireList *list, GPUQueue *queue, uint64_t *serial);

/* a transfer batch opens: its serial now, so what is destroyed while it
 * records waits for it */
GPU_HIDE
void
gpuRetireOpenBatch(GPURetireList *list,
                   GPUQueue      *queue,
                   uint64_t      *serial,
                   uint64_t      *givenBefore);

/* a transfer batch that won't go out gives its serial back */
GPU_HIDE
void
gpuRetireDropBatch(GPUQueue *queue, uint64_t *serial, uint64_t givenBefore);

#endif /* gpu_retire_internal_h */
