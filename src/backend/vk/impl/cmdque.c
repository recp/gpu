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

#include "../common.h"

enum {
  VK_COMPLETION_STACK_SIZE = 64u * 1024u
};

static void
vk__queueLock(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&queue->poolLock);
#else
  pthread_mutex_lock(&queue->poolLock);
#endif
}

static void
vk__queueUnlock(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&queue->poolLock);
#else
  pthread_mutex_unlock(&queue->poolLock);
#endif
}

static void
vk__queueSignal(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  WakeConditionVariable(&queue->pendingCondition);
#else
  pthread_cond_signal(&queue->pendingCondition);
#endif
}

static void
vk__queueWait(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  SleepConditionVariableCS(&queue->pendingCondition,
                           &queue->poolLock,
                           INFINITE);
#else
  pthread_cond_wait(&queue->pendingCondition, &queue->poolLock);
#endif
}

static void
vk__logSubmitError(GPUCommandBuffer *cmdb, VkResult result) {
  GPUDevice *device;

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (device && device->runtimeConfig.enableVerboseLogs) {
    fprintf(stderr, "GPU Vulkan command submission failed: %d\n", result);
  }
}

static void
vk__recycleCommandBuffer(GPUCommandBuffer *cmdb) {
  GPUCommandBufferVk *native;
  GPUCommandQueueVk  *queue;

  native = cmdb ? cmdb->_priv : NULL;
  queue  = native ? native->owner : NULL;
  if (!native || !queue) {
    return;
  }

  vk__queueLock(queue);
  native->poolNext = queue->freeCommands;
  queue->freeCommands = native;
  vk__queueUnlock(queue);
}

static GPUCommandBufferVk*
vk__takePendingCommand(GPUCommandQueueVk *queue) {
  GPUCommandBufferVk *native;

  vk__queueLock(queue);
  while (!queue->stopping && !queue->pendingHead) {
    vk__queueWait(queue);
  }

  native = queue->pendingHead;
  if (native) {
    queue->pendingHead  = native->pendingNext;
    if (!queue->pendingHead) {
      queue->pendingTail = NULL;
    }
    native->pendingNext = NULL;
  }
  vk__queueUnlock(queue);
  return native;
}

static void
vk__completionLoop(GPUCommandQueueVk *queue) {
  GPUCommandBufferVk *native;
  GPUCommandBuffer   *cmdb;
  GPUDeviceVk        *deviceVk;
  VkResult            result;

  deviceVk = queue->queue->_device->_priv;
  while ((native = vk__takePendingCommand(queue))) {
    cmdb = &native->commandBuffer;
    result = vkWaitForFences(deviceVk->device,
                             1u,
                             &native->fence,
                             VK_TRUE,
                             UINT64_MAX);
    if (result != VK_SUCCESS) {
      vk__logSubmitError(cmdb, result);
    }
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
vk__completionMain(LPVOID context) {
  vk__completionLoop(context);
  return 0;
}
#else
static void*
vk__completionMain(void *context) {
  vk__completionLoop(context);
  return NULL;
}
#endif

static bool
vk__startWorker(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&queue->poolLock);
  InitializeConditionVariable(&queue->pendingCondition);
  queue->worker = CreateThread(NULL,
                               VK_COMPLETION_STACK_SIZE,
                               vk__completionMain,
                               queue,
                               0,
                               NULL);
  queue->workerStarted = queue->worker != NULL;
  if (!queue->workerStarted) {
    DeleteCriticalSection(&queue->poolLock);
  }
#else
  pthread_attr_t attr;

  if (pthread_mutex_init(&queue->poolLock, NULL) != 0) {
    return false;
  }
  if (pthread_cond_init(&queue->pendingCondition, NULL) != 0) {
    pthread_mutex_destroy(&queue->poolLock);
    return false;
  }
  if (pthread_attr_init(&attr) != 0) {
    pthread_cond_destroy(&queue->pendingCondition);
    pthread_mutex_destroy(&queue->poolLock);
    return false;
  }
  (void)pthread_attr_setstacksize(&attr, VK_COMPLETION_STACK_SIZE);
  queue->workerStarted = pthread_create(&queue->worker,
                                        &attr,
                                        vk__completionMain,
                                        queue) == 0;
  pthread_attr_destroy(&attr);
  if (!queue->workerStarted) {
    pthread_cond_destroy(&queue->pendingCondition);
    pthread_mutex_destroy(&queue->poolLock);
  }
#endif
  return queue->workerStarted;
}

static void
vk__stopWorker(GPUCommandQueueVk *queue) {
  if (!queue || !queue->workerStarted) {
    return;
  }

  vk__queueLock(queue);
  queue->stopping = true;
  vk__queueSignal(queue);
  vk__queueUnlock(queue);

#if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(queue->worker, INFINITE);
  CloseHandle(queue->worker);
  DeleteCriticalSection(&queue->poolLock);
#else
  pthread_join(queue->worker, NULL);
  pthread_cond_destroy(&queue->pendingCondition);
  pthread_mutex_destroy(&queue->poolLock);
#endif
  queue->workerStarted = false;
}

GPU_HIDE
GPUCommandQueue*
vk_createCommandQueue(GPUDevice       *device,
                      uint32_t         familyIndex,
                      uint32_t         queueIndex,
                      GPUQueueFlagBits bits) {
  GPUCommandQueue   *queue;
  GPUCommandQueueVk *native;
  GPUDeviceVk       *deviceVk;
  VkCommandPoolCreateInfo poolInfo = {0};

  if (!device || !device->_priv || bits == 0u) {
    return NULL;
  }

  queue  = calloc(1, sizeof(*queue));
  native = calloc(1, sizeof(*native));
  if (!queue || !native) {
    free(native);
    free(queue);
    return NULL;
  }

  deviceVk            = device->_priv;
  queue->_priv        = native;
  queue->_device      = device;
  queue->bits         = bits;
  native->queue       = queue;
  native->familyIndex = familyIndex;
  native->queueIndex  = queueIndex;
  vkGetDeviceQueue(deviceVk->device, familyIndex, queueIndex, &native->queRaw);
  if (!native->queRaw) {
    free(native);
    free(queue);
    return NULL;
  }

  poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = familyIndex;
  if (vkCreateCommandPool(deviceVk->device,
                          &poolInfo,
                          NULL,
                          &native->commandPool) != VK_SUCCESS ||
      !vk__startWorker(native)) {
    if (native->commandPool) {
      vkDestroyCommandPool(deviceVk->device, native->commandPool, NULL);
    }
    free(native);
    free(queue);
    return NULL;
  }

  return queue;
}

GPU_HIDE
void
vk_destroyCommandQueue(GPUCommandQueue *queue) {
  GPUCommandBufferVk *command;
  GPUCommandBufferVk *next;
  GPUCommandQueueVk  *native;
  GPUDeviceVk        *deviceVk;

  if (!queue) {
    return;
  }

  native   = queue->_priv;
  deviceVk = queue->_device ? queue->_device->_priv : NULL;
  if (native) {
    vk__stopWorker(native);
    command = native->commands;
    while (command) {
      next = command->next;
      if (deviceVk && command->fence) {
        vkDestroyFence(deviceVk->device, command->fence, NULL);
      }
      free(command);
      command = next;
    }
    if (deviceVk && native->commandPool) {
      vkDestroyCommandPool(deviceVk->device, native->commandPool, NULL);
    }
    free(native);
  }
  free(queue);
}

GPU_HIDE
GPUCommandQueue*
vk_getCommandQueue(GPUDevice *device, GPUQueueFlagBits bits, uint32_t index) {
  GPUCommandQueue   *queue;
  GPUDeviceVk       *deviceVk;
  uint32_t           matchIndex;
  uint32_t           i;

  deviceVk   = device->_priv;
  matchIndex = 0;

  for (i = 0; i < deviceVk->nCreatedQueues; i++) {
    queue = deviceVk->createdQueues[i];

    if (queue && (queue->bits & bits) == bits) {
      if (matchIndex == index) {
        return queue;
      }
      matchIndex++;
    }
  }

  return NULL;
}

GPU_HIDE
GPUCommandQueue*
vk_newCommandQueue(GPUDevice * __restrict device) {
  return vk_getCommandQueue(
    device,
    GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT,
    0
  );
}

static GPUCommandBufferVk*
vk__createCommandBufferState(GPUCommandQueue *queue) {
  GPUCommandQueueVk  *queueVk;
  GPUCommandBufferVk *native;
  GPUCommandBuffer   *cmdb;
  GPUDeviceVk        *deviceVk;
  VkCommandBufferAllocateInfo allocInfo = {0};
  VkFenceCreateInfo fenceInfo = {0};

  queueVk  = queue->_priv;
  deviceVk = queue->_device->_priv;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  gpuDeviceRecordHotPathAlloc(queue->_device, sizeof(*native));

  allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool        = queueVk->commandPool;
  allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1u;
  if (vkAllocateCommandBuffers(deviceVk->device,
                               &allocInfo,
                               &native->command) != VK_SUCCESS) {
    gpuDeviceRecordHotPathFree(queue->_device, sizeof(*native));
    free(native);
    return NULL;
  }

  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (vkCreateFence(deviceVk->device,
                    &fenceInfo,
                    NULL,
                    &native->fence) != VK_SUCCESS) {
    vkFreeCommandBuffers(deviceVk->device,
                         queueVk->commandPool,
                         1u,
                         &native->command);
    gpuDeviceRecordHotPathFree(queue->_device, sizeof(*native));
    free(native);
    return NULL;
  }

  cmdb          = &native->commandBuffer;
  native->owner = queueVk;
  cmdb->_priv   = native;
  cmdb->_queue  = queue;

  vk__queueLock(queueVk);
  native->next     = queueVk->commands;
  queueVk->commands = native;
  vk__queueUnlock(queueVk);
  return native;
}

static GPUCommandBufferVk*
vk__takeCommandBufferState(GPUCommandQueue *queue) {
  GPUCommandQueueVk  *queueVk;
  GPUCommandBufferVk *native;

  queueVk = queue->_priv;
  vk__queueLock(queueVk);
  native = queueVk->freeCommands;
  if (native) {
    queueVk->freeCommands = native->poolNext;
    native->poolNext      = NULL;
  }
  vk__queueUnlock(queueVk);

  return native ? native : vk__createCommandBufferState(queue);
}

GPU_HIDE
GPUCommandBuffer*
vk_newCommandBuffer(GPUCommandQueue  * __restrict queue,
                    const char       * __restrict label,
                    void             * __restrict sender,
                    GPUCommandBufferCompletionFn  oncomplete) {
  GPUCommandBufferVk *native;
  GPUCommandBuffer   *cmdb;
  VkCommandBufferBeginInfo beginInfo = {0};

  GPU__UNUSED(label);

  if (!queue || !queue->_priv || !queue->_device) {
    return NULL;
  }

  native = vk__takeCommandBufferState(queue);
  if (!native) {
    return NULL;
  }

  cmdb = &native->commandBuffer;
  memset(cmdb, 0, sizeof(*cmdb));
  cmdb->_priv  = native;
  cmdb->_queue = queue;

  if (vkResetCommandBuffer(native->command, 0u) != VK_SUCCESS) {
    vk__recycleCommandBuffer(cmdb);
    return NULL;
  }

  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(native->command, &beginInfo) != VK_SUCCESS) {
    vk__recycleCommandBuffer(cmdb);
    return NULL;
  }

  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = oncomplete;
  return cmdb;
}

GPU_HIDE
void
vk_commandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
                           void             * __restrict sender,
                           GPUCommandBufferCompletionFn  oncomplete) {
  if (!cmdb || cmdb->_submitted) {
    return;
  }

  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = oncomplete;
}

GPU_HIDE
void
vk_commitCommandBuffer(GPUCommandBuffer * __restrict cmdb) {
  GPUCommandBufferVk *native;
  GPUCommandQueueVk  *queue;
  GPUDeviceVk        *deviceVk;
  VkSubmitInfo        submitInfo = {0};
  VkResult            result;

  native   = cmdb ? cmdb->_priv : NULL;
  queue    = native ? native->owner : NULL;
  deviceVk = cmdb && cmdb->_queue && cmdb->_queue->_device ?
    cmdb->_queue->_device->_priv : NULL;
  if (!native || !queue || !deviceVk) {
    return;
  }

  result = vkEndCommandBuffer(native->command);
  if (result == VK_SUCCESS) {
    result = vkResetFences(deviceVk->device, 1u, &native->fence);
  }
  if (result == VK_SUCCESS) {
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers    = &native->command;
    result = vkQueueSubmit(queue->queRaw,
                           1u,
                           &submitInfo,
                           native->fence);
  }
  if (result != VK_SUCCESS) {
    vk__logSubmitError(cmdb, result);
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
    return;
  }

  vk__queueLock(queue);
  if (queue->pendingTail) {
    queue->pendingTail->pendingNext = native;
  } else {
    queue->pendingHead = native;
  }
  queue->pendingTail = native;
  vk__queueSignal(queue);
  vk__queueUnlock(queue);
}

GPU_HIDE
void
vk_initCmdQue(GPUApiCommandQueue *apiQue) {
  apiQue->getCommandQueue         = vk_getCommandQueue;
  apiQue->newCommandQueue         = vk_newCommandQueue;
  apiQue->newCommandBuffer        = vk_newCommandBuffer;
  apiQue->commandBufferOnComplete = vk_commandBufferOnComplete;
  apiQue->commit                  = vk_commitCommandBuffer;
}
