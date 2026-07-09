/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../../common.h"
#include "../cmdqueue_internal.h"

#if defined(_WIN32) || defined(WIN32)
#else
#  include <pthread.h>
#  include <time.h>
#endif

struct GPUFence {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION lock;
  CONDITION_VARIABLE cond;
#else
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
#endif
  bool signaled;
};

typedef struct GPUFenceSubmitCompletion {
  GPUFence                    *fence;
  void                        *sender;
  GPUCommandBufferCompletionFn onComplete;
} GPUFenceSubmitCompletion;

static void
gpu_signalFence(GPUFence *fence) {
  if (!fence) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&fence->lock);
  fence->signaled = true;
  WakeAllConditionVariable(&fence->cond);
  LeaveCriticalSection(&fence->lock);
#else
  pthread_mutex_lock(&fence->mutex);
  fence->signaled = true;
  pthread_cond_broadcast(&fence->cond);
  pthread_mutex_unlock(&fence->mutex);
#endif
}

static void
gpu_resetFenceInternal(GPUFence *fence) {
  if (!fence) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&fence->lock);
  fence->signaled = false;
  LeaveCriticalSection(&fence->lock);
#else
  pthread_mutex_lock(&fence->mutex);
  fence->signaled = false;
  pthread_mutex_unlock(&fence->mutex);
#endif
}

static void
gpu_submitFenceComplete(void *sender, GPUCommandBuffer *cmdb) {
  GPUFenceSubmitCompletion *completion;
  GPUCommandBufferCompletionFn onComplete;
  void *userSender;

  completion = sender;
  if (!completion) {
    return;
  }

  onComplete = completion->onComplete;
  userSender = completion->sender;
  gpu_signalFence(completion->fence);
  free(completion);

  if (onComplete) {
    onComplete(userSender, cmdb);
  }
}

#if !defined(_WIN32) && !defined(WIN32)
static void
gpu_timeoutFromNow(uint64_t timeoutNs, struct timespec *outTime) {
  uint64_t sec;
  uint64_t nsec;

  clock_gettime(CLOCK_REALTIME, outTime);
  sec = timeoutNs / 1000000000ull;
  nsec = timeoutNs % 1000000000ull;

  outTime->tv_sec += (time_t)sec;
  outTime->tv_nsec += (long)nsec;
  if (outTime->tv_nsec >= 1000000000l) {
    outTime->tv_sec++;
    outTime->tv_nsec -= 1000000000l;
  }
}
#endif

static GPUCommandBuffer*
gpu_newCommandBuffer(GPUCommandQueue  * __restrict cmdq,
                     void             * __restrict sender,
                     GPUCommandBufferCompletionFn  oncomplete) {
  GPUApi *api;
  GPUCommandBuffer *cmdb;

  if (!cmdq)
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->cmdque.newCommandBuffer)
    return NULL;

  cmdb = api->cmdque.newCommandBuffer(cmdq, sender, oncomplete);
  if (cmdb) {
    cmdb->_queue = cmdq;
  }

  return cmdb;
}

GPU_EXPORT
GPUCommandQueue*
GPUGetCommandQueue(GPUDevice * __restrict device, GPUQueueFlagBits bits) {
  return GPUGetQueue(device, bits, 0);
}

GPU_EXPORT
GPUCommandQueue*
GPUGetQueue(GPUDevice * __restrict device,
            GPUQueueFlagBits       bits,
            uint32_t               index) {
  GPUApi *api;

  if (!device || bits == 0) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->cmdque.getCommandQueue)
    return NULL;

  return api->cmdque.getCommandQueue(device, bits, index);
}

GPU_EXPORT
GPUResult
GPUAcquireCommandBuffer(GPUCommandQueue   * __restrict cmdq,
                        const char        * __restrict label,
                        GPUCommandBuffer ** __restrict outCmdb) {
  (void)label;

  if (!outCmdb) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = NULL;
  if (!cmdq) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = gpu_newCommandBuffer(cmdq, NULL, NULL);
  return *outCmdb ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
void
GPUSetCommandBufferCompletionHandler(GPUCommandBuffer * __restrict cmdb,
                                     void             * __restrict sender,
                                     GPUCommandBufferCompletionFn  oncomplete) {
  GPUApi *api;

  if (!cmdb)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->cmdque.commandBufferOnComplete)
    return;
  
  api->cmdque.commandBufferOnComplete(cmdb, sender, oncomplete);
}

GPU_EXPORT
void
GPUCommit(GPUCommandBuffer * __restrict cmdb) {
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};

  if (!cmdb || !cmdb->_queue || cmdb->_submitted)
    return;

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1;
  submitInfo.ppCommandBuffers = buffers;
  (void)GPUQueueSubmit(cmdb->_queue, &submitInfo);
}

GPU_EXPORT
GPUResult
GPUQueueSubmit(GPUCommandQueue          * __restrict cmdq,
               const GPUQueueSubmitInfo * __restrict info) {
  GPUApi *api;
  GPUCommandBuffer *lastCmdb;
  GPUFenceSubmitCompletion *completion;

  if (!cmdq || !info || info->commandBufferCount == 0 || !info->ppCommandBuffers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()))
    return GPU_ERROR_BACKEND_FAILURE;
  if (!api->cmdque.commit)
    return GPU_ERROR_BACKEND_FAILURE;

  for (uint32_t i = 0; i < info->commandBufferCount; i++) {
    GPUCommandBuffer *cmdb;

    cmdb = info->ppCommandBuffers[i];
    if (!cmdb || cmdb->_submitted || cmdb->_queue != cmdq) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t j = 0; j < i; j++) {
      if (info->ppCommandBuffers[j] == cmdb) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  }

  lastCmdb = info->ppCommandBuffers[info->commandBufferCount - 1u];
  completion = NULL;
  if (info->fence) {
    if (!api->cmdque.commandBufferOnComplete) {
      return GPU_ERROR_UNSUPPORTED;
    }

    completion = calloc(1, sizeof(*completion));
    if (!completion) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    completion->fence = info->fence;
    completion->sender = lastCmdb->_onCompleteSender;
    completion->onComplete = lastCmdb->_onComplete;
    gpu_resetFenceInternal(info->fence);
    api->cmdque.commandBufferOnComplete(lastCmdb, completion, gpu_submitFenceComplete);
  }

  for (uint32_t i = 0; i < info->commandBufferCount; i++) {
    info->ppCommandBuffers[i]->_submitted = true;
  }

  for (uint32_t i = 0; i < info->commandBufferCount; i++) {
    api->cmdque.commit(info->ppCommandBuffers[i]);
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateFence(GPUDevice              * __restrict device,
               const GPUFenceCreateInfo * __restrict info,
               GPUFence              ** __restrict outFence) {
  GPUFence *fence;

  if (!outFence) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFence = NULL;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info &&
      info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info && info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  fence = calloc(1, sizeof(*fence));
  if (!fence) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&fence->lock);
  InitializeConditionVariable(&fence->cond);
#else
  if (pthread_mutex_init(&fence->mutex, NULL) != 0) {
    free(fence);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (pthread_cond_init(&fence->cond, NULL) != 0) {
    pthread_mutex_destroy(&fence->mutex);
    free(fence);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  fence->signaled = info ? info->signaled : false;
  *outFence = fence;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyFence(GPUFence * __restrict fence) {
  if (!fence) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&fence->lock);
#else
  pthread_cond_destroy(&fence->cond);
  pthread_mutex_destroy(&fence->mutex);
#endif
  free(fence);
}

GPU_EXPORT
GPUResult
GPUWaitFence(GPUFence * __restrict fence, uint64_t timeoutNs) {
  if (!fence) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

#if defined(_WIN32) || defined(WIN32)
  DWORD timeoutMs;
  BOOL ok;

  timeoutMs = timeoutNs == UINT64_MAX
                ? INFINITE
                : (DWORD)((timeoutNs + 999999ull) / 1000000ull);
  EnterCriticalSection(&fence->lock);
  while (!fence->signaled) {
    ok = SleepConditionVariableCS(&fence->cond, &fence->lock, timeoutMs);
    if (!ok) {
      DWORD error;

      error = GetLastError();
      LeaveCriticalSection(&fence->lock);
      if (error == ERROR_TIMEOUT) {
        return GPU_ERROR_TIMEOUT;
      }
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  LeaveCriticalSection(&fence->lock);
#else
  struct timespec deadline;
  int rc;

  pthread_mutex_lock(&fence->mutex);
  if (timeoutNs == UINT64_MAX) {
    while (!fence->signaled) {
      rc = pthread_cond_wait(&fence->cond, &fence->mutex);
      if (rc != 0) {
        pthread_mutex_unlock(&fence->mutex);
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
  } else {
    gpu_timeoutFromNow(timeoutNs, &deadline);
    while (!fence->signaled) {
      rc = pthread_cond_timedwait(&fence->cond, &fence->mutex, &deadline);
      if (rc != 0) {
        pthread_mutex_unlock(&fence->mutex);
        if (rc == ETIMEDOUT) {
          return GPU_ERROR_TIMEOUT;
        }
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
  }
  pthread_mutex_unlock(&fence->mutex);
#endif

  return GPU_OK;
}

GPU_EXPORT
bool
GPUIsFenceSignaled(GPUFence * __restrict fence) {
  bool signaled;

  if (!fence) {
    return false;
  }

#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&fence->lock);
  signaled = fence->signaled;
  LeaveCriticalSection(&fence->lock);
#else
  pthread_mutex_lock(&fence->mutex);
  signaled = fence->signaled;
  pthread_mutex_unlock(&fence->mutex);
#endif

  return signaled;
}

GPU_EXPORT
void
GPUResetFence(GPUFence * __restrict fence) {
  gpu_resetFenceInternal(fence);
}
