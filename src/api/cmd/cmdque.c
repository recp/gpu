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

enum {
  GPU_SUBMIT_EX_MAX_COMMAND_BUFFERS = 64u,
  GPU_SUBMIT_EX_MAX_SEMAPHORES      = 64u
};

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

GPU_HIDE
void
gpuFinishCommandBuffer(GPUCommandBuffer          *cmdb,
                       GPUCommandBufferRecycleFn  recycle) {
  GPUFence *fence;
  GPUFence *transientFence;
  GPUCommandBufferCompletionFn onComplete;
  void *sender;

  if (!cmdb) {
    return;
  }

  fence          = cmdb->_submitFence;
  transientFence = cmdb->_transientFence;
  sender         = cmdb->_onCompleteSender;
  onComplete     = cmdb->_onComplete;
  cmdb->_submitFence      = NULL;
  cmdb->_transientFence   = NULL;
  cmdb->_onCompleteSender = NULL;
  cmdb->_onComplete       = NULL;

  gpu_signalFence(transientFence);

  if (onComplete) {
    onComplete(sender, cmdb);
  }
  if (recycle) {
    recycle(cmdb);
  }
  gpu_signalFence(fence);
}

GPU_HIDE
void
gpuDiscardCommandBufferState(GPUCommandBuffer          *cmdb,
                             GPUCommandBufferRecycleFn  recycle) {
  if (!cmdb) {
    return;
  }

  cmdb->_submitFence      = NULL;
  cmdb->_transientFence   = NULL;
  cmdb->_onCompleteSender = NULL;
  cmdb->_onComplete       = NULL;

  if (recycle) {
    recycle(cmdb);
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
gpu_newCommandBuffer(GPUQueue                  * __restrict cmdq,
                     const char                * __restrict label,
                     void                      * __restrict sender,
                     GPUCommandBufferCompletionFn  oncomplete) {
  GPUApi *api;
  GPUCommandBuffer *cmdb;

  if (!cmdq)
    return NULL;
  if (!(api = gpuCommandQueueApi(cmdq)))
    return NULL;
  if (!api->cmdque.newCommandBuffer)
    return NULL;

  label = gpuDeviceDebugLabel(cmdq->_device, label);
  cmdb = api->cmdque.newCommandBuffer(cmdq, label, sender, oncomplete);
  if (cmdb) {
    cmdb->_queue = cmdq;
  }

  return cmdb;
}

GPU_EXPORT
GPUQueue*
GPUGetCommandQueue(GPUDevice * __restrict device, GPUQueueFlagBits bits) {
  return GPUGetQueue(device, bits, 0);
}

GPU_EXPORT
GPUQueue*
GPUGetQueue(GPUDevice * __restrict device,
            GPUQueueFlagBits       bits,
            uint32_t               index) {
  GPUApi *api;

  if (!device || bits == 0) {
    return NULL;
  }
  if (!(api = gpuDeviceApi(device)))
    return NULL;
  if (!api->cmdque.getCommandQueue)
    return NULL;

  return api->cmdque.getCommandQueue(device, bits, index);
}

GPU_EXPORT
GPUResult
GPUAcquireCommandBuffer(GPUQueue          * __restrict cmdq,
                        const char        * __restrict label,
                        GPUCommandBuffer ** __restrict outCmdb) {
  if (!outCmdb) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = NULL;
  if (!cmdq) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCmdb = gpu_newCommandBuffer(cmdq, label, NULL, NULL);
  return *outCmdb ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
GPUResult
GPUDiscardCommandBuffer(GPUCommandBuffer * __restrict cmdb) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      cmdb->_pipelineStatsQuery) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!(api = gpuCommandBufferApi(cmdb)) || !api->cmdque.discard) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cmdb->_submitted = true;
  return api->cmdque.discard(cmdb);
}

GPU_EXPORT
void
GPUSetCommandBufferCompletionHandler(GPUCommandBuffer * __restrict cmdb,
                                     void             * __restrict sender,
                                     GPUCommandBufferCompletionFn  oncomplete) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted)
    return;
  if (!(api = gpuCommandBufferApi(cmdb)))
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

static GPUResult
gpu_prepareQueueSubmit(GPUQueue                  *cmdq,
                       uint32_t                   commandBufferCount,
                       GPUCommandBuffer * const *commandBuffers,
                       GPUFence                  *fence,
                       GPUApi                   **outApi) {
  GPUApi           *api;
  GPUDevice        *device;
  GPUFence         *transientFence;
  GPUCommandBuffer *lastCmdb;
  uint32_t          transientFrameIndex;
  bool              transientFrameTagged;

  if (!cmdq || commandBufferCount == 0u || !commandBuffers || !outApi) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device = gpuCommandQueueDevice(cmdq);
  if (!(api = gpuCommandQueueApi(cmdq)))
    return GPU_ERROR_BACKEND_FAILURE;
  if (!api->cmdque.commit)
    return GPU_ERROR_BACKEND_FAILURE;

  transientFence       = NULL;
  transientFrameIndex  = 0u;
  transientFrameTagged = false;
  for (uint32_t i = 0; i < commandBufferCount; i++) {
    GPUCommandBuffer *cmdb;

    cmdb = commandBuffers[i];
    if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
        cmdb->_pipelineStatsQuery || cmdb->_queue != cmdq) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t j = 0; j < i; j++) {
      if (commandBuffers[j] == cmdb) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
    if (cmdb->_transientFrameTagged) {
      if (!device || !device->transientConfigured ||
          !device->transientFrameFences ||
          cmdb->_transientFrameIndex >=
            device->transientConfig.framesInFlight ||
          (transientFrameTagged &&
           transientFrameIndex != cmdb->_transientFrameIndex)) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      transientFrameIndex  = cmdb->_transientFrameIndex;
      transientFrameTagged = true;
    }
  }

  lastCmdb = commandBuffers[commandBufferCount - 1u];
  if (transientFrameTagged) {
    if (!api->cmdque.commandBufferOnComplete) {
      return GPU_ERROR_UNSUPPORTED;
    }
    transientFence = device->transientFrameFences[transientFrameIndex];
    if (!transientFence || !GPUIsFenceSignaled(transientFence)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  if (fence) {
    if (!api->cmdque.commandBufferOnComplete) {
      return GPU_ERROR_UNSUPPORTED;
    }

    gpu_resetFenceInternal(fence);
    lastCmdb->_submitFence = fence;
  }
  if (transientFence) {
    GPUResetFence(transientFence);
    lastCmdb->_transientFence = transientFence;
  }

  for (uint32_t i = 0; i < commandBufferCount; i++) {
    commandBuffers[i]->_submitted = true;
  }

  *outApi = api;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUQueueSubmit(GPUQueue                 * __restrict cmdq,
               const GPUQueueSubmitInfo * __restrict info) {
  GPUApi    *api;
  GPUResult  result;

  if (!cmdq || !info || info->commandBufferCount == 0u ||
      !info->ppCommandBuffers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0u &&
      info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = gpu_prepareQueueSubmit(cmdq,
                                  info->commandBufferCount,
                                  info->ppCommandBuffers,
                                  info->fence,
                                  &api);
  if (result != GPU_OK) {
    return result;
  }

  if (info->commandBufferCount > 1u && api->cmdque.submit) {
    return api->cmdque.submit(cmdq,
                              info->commandBufferCount,
                              info->ppCommandBuffers);
  }

  result = GPU_OK;
  for (uint32_t i = 0; i < info->commandBufferCount; i++) {
    GPUResult commitResult;

    commitResult = api->cmdque.commit(info->ppCommandBuffers[i]);
    if (result == GPU_OK && commitResult != GPU_OK) {
      result = commitResult;
    }
  }

  return result;
}

GPU_EXPORT
GPUResult
GPUQueueSubmitEx(GPUQueue                   * __restrict cmdq,
                 const GPUQueueSubmitExInfo * __restrict info) {
  GPUApi            *api;
  GPUPipelineStageMask validStages;
  GPUResult          result;

  if (!cmdq || !info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->waitCount > 0u && !info->pWaits) ||
      (info->signalCount > 0u && !info->pSignals)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->waitCount == 0u && info->signalCount == 0u) {
    GPUQueueSubmitInfo baseInfo = {0};

    baseInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    baseInfo.chain.structSize   = sizeof(baseInfo);
    baseInfo.commandBufferCount = info->commandBufferCount;
    baseInfo.ppCommandBuffers   = info->ppCommandBuffers;
    baseInfo.fence              = info->fence;
    return GPUQueueSubmit(cmdq, &baseInfo);
  }
  if (info->commandBufferCount > GPU_SUBMIT_EX_MAX_COMMAND_BUFFERS ||
      info->waitCount > GPU_SUBMIT_EX_MAX_SEMAPHORES ||
      info->signalCount > GPU_SUBMIT_EX_MAX_SEMAPHORES) {
    return GPU_ERROR_UNSUPPORTED;
  }

  validStages = GPU_STAGE_TOP | GPU_STAGE_VERTEX | GPU_STAGE_FRAGMENT |
                GPU_STAGE_COMPUTE | GPU_STAGE_TRANSFER | GPU_STAGE_BOTTOM;
  for (uint32_t i = 0; i < info->waitCount; i++) {
    GPUSemaphore *semaphore;

    semaphore = info->pWaits[i].semaphore;
    if (!semaphore || semaphore->_device != cmdq->_device ||
        info->pWaits[i].waitStages == 0u ||
        (info->pWaits[i].waitStages & ~validStages) != 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  for (uint32_t i = 0; i < info->signalCount; i++) {
    GPUSemaphore *semaphore;

    semaphore = info->pSignals[i].semaphore;
    if (!semaphore || semaphore->_device != cmdq->_device) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  api = gpuCommandQueueApi(cmdq);
  if (!api || !api->cmdque.submitEx) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = gpu_prepareQueueSubmit(cmdq,
                                  info->commandBufferCount,
                                  info->ppCommandBuffers,
                                  info->fence,
                                  &api);
  return result == GPU_OK ? api->cmdque.submitEx(cmdq, info) : result;
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

GPU_EXPORT
GPUResult
GPUCreateSemaphore(GPUDevice                 * __restrict device,
                   const GPUSemaphoreCreateInfo * __restrict info,
                   GPUSemaphore              ** __restrict outSemaphore) {
  GPUApi       *api;
  GPUSemaphore *semaphore;
  GPUResult     result;

  if (!outSemaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSemaphore = NULL;

  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info &&
      info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info && info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  api = gpuDeviceApi(device);
  if (!api || !api->cmdque.createSemaphore ||
      !api->cmdque.destroySemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }

  semaphore = calloc(1, sizeof(*semaphore));
  if (!semaphore) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  semaphore->_device = device;
  result = api->cmdque.createSemaphore(device, info, semaphore);
  if (result != GPU_OK) {
    free(semaphore);
    return result;
  }
  *outSemaphore = semaphore;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroySemaphore(GPUSemaphore * __restrict semaphore) {
  GPUApi *api;

  if (!semaphore) {
    return;
  }
  api = gpuDeviceApi(semaphore->_device);
  if (api && api->cmdque.destroySemaphore) {
    api->cmdque.destroySemaphore(semaphore);
  }
  free(semaphore);
}
