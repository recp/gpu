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
 * Destroyed objects the GPU may still use. Every submit, and every transfer
 * batch when it opens, takes the device's next serial. A queue's work goes out
 * in serial order (a submit flushes the queue's open batch first, both under
 * the queue lock), and a fence's signal covers the queue's earlier submits, so
 * a queue that finished serial S finished all of its work up to S.
 *
 * An object destroyed when the device's last serial was G was given to queues
 * only in work up to G. It may go once every queue has finished either G or
 * all it was given; with nothing pending (as after a wait) that holds at the
 * destroy, and the object goes at once, as before. Otherwise it waits in a
 * list, oldest first, freed as queues finish: on the completion thread before
 * the command buffer's completion is reported, so a fence waited on means its
 * work's destroyed objects are gone, and after transfer and idle waits.
 */

#include "../common.h"
#include "../impl.h"

#if defined(_WIN32) || defined(WIN32)
#  define VK_RETIRE_LOCK(d)   EnterCriticalSection(&(d)->retireLock)
#  define VK_RETIRE_UNLOCK(d) LeaveCriticalSection(&(d)->retireLock)
#else
#  define VK_RETIRE_LOCK(d)   pthread_mutex_lock(&(d)->retireLock)
#  define VK_RETIRE_UNLOCK(d) pthread_mutex_unlock(&(d)->retireLock)
#endif

/* the newest serial whose objects may go: every queue with work pending
 * finished its work up to it */
static uint64_t
vk__retireLimit(GPUDeviceVk *deviceVk) {
  GPUQueueVk *queue;
  uint64_t    limit;
  uint64_t    given;
  uint64_t    done;
  uint32_t    i;

  limit = UINT64_MAX;

  for (i = 0u; i < deviceVk->nCreatedQueues; i++) {
    if (!deviceVk->createdQueues[i] || !(queue = deviceVk->createdQueues[i]->_priv))
      continue;

    /* given first: work given after it is newer than any destroy before */
    given = __atomic_load_n(&queue->givenSerial, __ATOMIC_ACQUIRE);
    done  = __atomic_load_n(&queue->doneSerial, __ATOMIC_ACQUIRE);

    if (done < given && done < limit)
      limit = done;
  }

  return limit;
}

GPU_HIDE
bool
vk_initRetire(GPUDeviceVk *deviceVk) {
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&deviceVk->retireLock);
#else
  if (pthread_mutex_init(&deviceVk->retireLock, NULL) != 0)
    return false;
#endif
  deviceVk->retireLockInitialized = true;

  return true;
}

GPU_HIDE
void
vk_finishRetire(GPUDeviceVk *deviceVk) {
  VKRetired *node;
  VKRetired *next;

  if (!deviceVk || !deviceVk->retireLockInitialized)
    return;

  /* under the lock: a completion thread may still be reclaiming */
  VK_RETIRE_LOCK(deviceVk);
  node = deviceVk->retiredHead;
  __atomic_store_n(&deviceVk->retiredHead, NULL, __ATOMIC_RELEASE);
  deviceVk->retiredTail = NULL;
  VK_RETIRE_UNLOCK(deviceVk);

  for (; node; node = next) {
    next = node->next;
    node->release(deviceVk, node->owner);
  }
}

GPU_HIDE
void
vk_destroyRetire(GPUDeviceVk *deviceVk) {
  if (!deviceVk || !deviceVk->retireLockInitialized)
    return;

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&deviceVk->retireLock);
#else
  pthread_mutex_destroy(&deviceVk->retireLock);
#endif
  deviceVk->retireLockInitialized = false;
}

GPU_HIDE
uint64_t
vk_nextSerial(GPUDeviceVk *deviceVk) {
  return __atomic_add_fetch(&deviceVk->serial, 1u, __ATOMIC_ACQ_REL);
}

GPU_HIDE
void
vk_reclaimRetired(GPUDeviceVk *deviceVk) {
  VKRetired *ready;
  VKRetired *last;
  VKRetired *node;
  uint64_t   limit;

  if (!deviceVk || !__atomic_load_n(&deviceVk->retiredHead, __ATOMIC_ACQUIRE))
    return;

  VK_RETIRE_LOCK(deviceVk);
  limit = vk__retireLimit(deviceVk);
  ready = deviceVk->retiredHead;
  last  = NULL;

  for (node = ready; node && node->serial <= limit; node = node->next)
    last = node;

  if (last) {
    last->next = NULL;
    __atomic_store_n(&deviceVk->retiredHead, node, __ATOMIC_RELEASE);
    if (!node)
      deviceVk->retiredTail = NULL;
  } else {
    ready = NULL;
  }
  VK_RETIRE_UNLOCK(deviceVk);

  /* outside the lock: releases take other locks (the pool's, render targets') */
  for (node = ready; node; node = last) {
    last = node->next;
    node->release(deviceVk, node->owner);
  }
}

GPU_HIDE
void
vk_retire(GPUDeviceVk *deviceVk,
          VKRetired   *node,
          void        *owner,
          void       (*release)(GPUDeviceVk *deviceVk, void *owner)) {
  uint64_t serial;

  if (!deviceVk || !deviceVk->retireLockInitialized) {
    release(deviceVk, owner);
    return;
  }

  serial = __atomic_load_n(&deviceVk->serial, __ATOMIC_ACQUIRE);

  /* nothing waits and every queue finished what it was given: at once */
  if (!__atomic_load_n(&deviceVk->retiredHead, __ATOMIC_ACQUIRE)
      && serial <= vk__retireLimit(deviceVk)) {
    release(deviceVk, owner);
    return;
  }

  node->next    = NULL;
  node->owner   = owner;
  node->release = release;

  VK_RETIRE_LOCK(deviceVk);
  /* under the lock: the list's serials stay in order */
  node->serial = __atomic_load_n(&deviceVk->serial, __ATOMIC_ACQUIRE);

  if (deviceVk->retiredTail)
    deviceVk->retiredTail->next = node;
  else
    __atomic_store_n(&deviceVk->retiredHead, node, __ATOMIC_RELEASE);

  deviceVk->retiredTail = node;
  VK_RETIRE_UNLOCK(deviceVk);

  /* older ones may be ready, and work may have finished meanwhile */
  vk_reclaimRetired(deviceVk);
}

GPU_HIDE
void
vk_queueDone(GPUDeviceVk *deviceVk, GPUQueueVk *queue, uint64_t serial) {
  uint64_t done;

  if (!queue || !serial)
    return;

  done = __atomic_load_n(&queue->doneSerial, __ATOMIC_RELAXED);

  while (done < serial
         && !__atomic_compare_exchange_n(&queue->doneSerial,
                                         &done,
                                         serial,
                                         true,
                                         __ATOMIC_ACQ_REL,
                                         __ATOMIC_RELAXED)) {
  }

  vk_reclaimRetired(deviceVk);
}
