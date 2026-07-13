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
#include "../impl.h"

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
vk__queueBroadcast(GPUCommandQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&queue->pendingCondition);
#else
  pthread_cond_broadcast(&queue->pendingCondition);
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
vk__waitQueueIdle(GPUCommandQueueVk *queue) {
  vk__queueLock(queue);
  while (queue->inFlightCount > 0u) {
    vk__queueWait(queue);
  }
  vk__queueUnlock(queue);
}

static void
vk__reportQueueError(GPUCommandBuffer *cmdb,
                     const char       *operation,
                     VkResult          result) {
  GPUDeviceErrorType type;
  GPUDevice          *device;
  GPUResult           gpuResult;
  char                message[96];

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!device) {
    return;
  }

  type      = GPU_DEVICE_ERROR_BACKEND;
  gpuResult = GPU_ERROR_BACKEND_FAILURE;
  if (result == VK_ERROR_DEVICE_LOST) {
    type = GPU_DEVICE_ERROR_LOST;
  } else if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
             result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
    type      = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
    gpuResult = GPU_ERROR_OUT_OF_MEMORY;
  }

  snprintf(message,
           sizeof(message),
           "Vulkan %s failed: %d",
           operation,
           result);
  gpuDeviceReportError(device,
                       type,
                       GPU_DEVICE_LOST_REASON_UNKNOWN,
                       gpuResult,
                       message);
}

static void
vk__recycleCommandBuffer(GPUCommandBuffer *cmdb) {
  GPUCommandBufferVk *native;
  GPUCommandQueueVk  *queue;
#ifdef __APPLE__
  GPUTransferChunkVk *chunk;
#endif

  native = cmdb ? cmdb->_priv : NULL;
  queue  = native ? native->owner : NULL;
  if (!native || !queue) {
    return;
  }

#ifdef __APPLE__
  for (chunk = native->transferChunks; chunk; chunk = chunk->next) {
    chunk->offset = 0u;
  }
#endif
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
  GPUSwapChainVk     *swapchain;
  VkFence             waitFence;
  VkResult            result;

  deviceVk = queue->queue->_device->_priv;
  while ((native = vk__takePendingCommand(queue))) {
    cmdb      = &native->commandBuffer;
    waitFence = native->submitFence ? native->submitFence : native->fence;
    result = vkWaitForFences(deviceVk->device,
                             1u,
                             &waitFence,
                             VK_TRUE,
                             UINT64_MAX);
    if (result != VK_SUCCESS) {
      vk__reportQueueError(cmdb, "fence wait", result);
    }

    swapchain = native->presentSwapchain;
    if (swapchain) {
      vk__queueLock(queue);
      if (swapchain->inFlightCommandCount > 0u) {
        swapchain->inFlightCommandCount--;
      }
      vk__queueBroadcast(queue);
      vk__queueUnlock(queue);
    }
    native->submitFence       = native->fence;
    native->presentSwapchain  = NULL;
    native->presentImageIndex = 0u;
    native->presentFrameIndex = 0u;
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);

    vk__queueLock(queue);
    if (queue->inFlightCount > 0u) {
      queue->inFlightCount--;
    }
    vk__queueBroadcast(queue);
    vk__queueUnlock(queue);
  }
}

GPU_HIDE
void
vk_waitSwapChainIdle(GPUSwapChainVk *swapchain) {
  GPUCommandQueueVk *queue;

  queue = swapchain ? swapchain->queue : NULL;
  if (!queue) {
    return;
  }

  vk__queueLock(queue);
  while (swapchain->inFlightCommandCount > 0u) {
    vk__queueWait(queue);
  }
  vk__queueUnlock(queue);
}

GPU_HIDE
GPUResult
vk_waitDeviceIdle(GPUDevice * __restrict device) {
  GPUDeviceVk *deviceVk;
  VkResult     result;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vkDeviceWaitIdle(deviceVk->device);
  for (uint32_t i = 0u; i < deviceVk->nCreatedQueues; i++) {
    GPUCommandQueueVk *queue;

    queue = deviceVk->createdQueues[i] ?
      deviceVk->createdQueues[i]->_priv : NULL;
    if (queue) {
      vk__waitQueueIdle(queue);
    }
  }

  if (result == VK_SUCCESS) {
    return GPU_OK;
  }
  if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
      result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  return GPU_ERROR_BACKEND_FAILURE;
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

static uint64_t
vk__transferCapacity(uint64_t sizeBytes) {
  uint64_t capacity;

  capacity = 64u * 1024u;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }
    capacity *= 2u;
  }
  return capacity;
}

static bool
vk__ensureTransferContext(GPUCommandQueueVk *queue, GPUDeviceVk *device) {
  VkCommandBufferAllocateInfo allocationInfo = {0};
  VkFenceCreateInfo           fenceInfo = {0};
  VkCommandBuffer             command;
  VkFence                     fence;

  if (!queue || !device || !device->device || !queue->commandPool) {
    return false;
  }
  if (queue->transferCommand && queue->transferFence) {
    return true;
  }
  if (queue->transferCommand || queue->transferFence) {
    return false;
  }

  command = VK_NULL_HANDLE;
  fence   = VK_NULL_HANDLE;
  allocationInfo.sType              =
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocationInfo.commandPool        = queue->commandPool;
  allocationInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocationInfo.commandBufferCount = 1u;
  if (vkAllocateCommandBuffers(device->device,
                               &allocationInfo,
                               &command) != VK_SUCCESS) {
    return false;
  }

  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (vkCreateFence(device->device, &fenceInfo, NULL, &fence) != VK_SUCCESS) {
    vkFreeCommandBuffers(device->device,
                         queue->commandPool,
                         1u,
                         &command);
    return false;
  }

  queue->transferCommand = command;
  queue->transferFence   = fence;
  return true;
}

static bool
vk__ensureTransferBuffer(GPUCommandQueueVk *queue,
                         bool               upload,
                         uint64_t           sizeBytes) {
  GPUBufferCreateInfo  info = {0};
  GPUBuffer           **slot;
  GPUBuffer            *staging;
  uint64_t             *currentCapacity;
  uint64_t              capacity;

  if (!queue || sizeBytes == 0u) {
    return false;
  }
  slot            = upload ? &queue->uploadStaging : &queue->readbackStaging;
  currentCapacity = upload ? &queue->uploadCapacity
                           : &queue->readbackCapacity;
  if (*slot && *currentCapacity >= sizeBytes) {
    return true;
  }

  capacity = vk__transferCapacity(sizeBytes);
  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = upload ? "vulkan-upload-staging"
                                 : "vulkan-readback-staging";
  info.sizeBytes        = capacity;
  info.usage            = upload ? GPU_BUFFER_USAGE_COPY_SRC
                                 : GPU_BUFFER_USAGE_COPY_DST;
  staging               = NULL;
  if (vk_createHostBuffer(queue->queue->_device, &info, &staging) != GPU_OK ||
      !staging) {
    return false;
  }

  vk_destroyBuffer(*slot);
  *slot            = staging;
  *currentCapacity = capacity;
  return true;
}

GPU_HIDE
GPUResult
vk_beginTransfer(GPUCommandQueue *queue,
                 bool             upload,
                 uint64_t         sizeBytes,
                 VkCommandBuffer *outCommand,
                 GPUBuffer      **outStaging) {
  GPUCommandQueueVk       *native;
  GPUDeviceVk             *device;
  VkCommandBufferBeginInfo beginInfo = {0};

  if (!queue || !queue->_device || !outCommand || !outStaging ||
      sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCommand = VK_NULL_HANDLE;
  *outStaging = NULL;
  native      = queue->_priv;
  device      = queue->_device->_priv;
  if (!native || !device || !device->device || !native->queRaw) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (native->transferOpen) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!vk__ensureTransferContext(native, device) ||
      !vk__ensureTransferBuffer(native, upload, sizeBytes)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (vkResetCommandBuffer(native->transferCommand, 0u) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(native->transferCommand, &beginInfo) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->transferOpen = true;
  *outCommand          = native->transferCommand;
  *outStaging          = upload ? native->uploadStaging
                                : native->readbackStaging;
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_submitTransfer(GPUCommandQueue *queue) {
  GPUCommandQueueVk *native;
  GPUDeviceVk       *device;
  VkSubmitInfo       submitInfo = {0};
  VkResult           result;
  bool               submitted;

  native = queue ? queue->_priv : NULL;
  device = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!native || !device || !device->device || !native->queRaw ||
      !native->transferOpen || !native->transferCommand ||
      !native->transferFence) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result               = vkEndCommandBuffer(native->transferCommand);
  native->transferOpen = false;
  if (result != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = vkResetFences(device->device, 1u, &native->transferFence);
  if (result != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1u;
  submitInfo.pCommandBuffers    = &native->transferCommand;
  result    = vkQueueSubmit(native->queRaw,
                            1u,
                            &submitInfo,
                            native->transferFence);
  submitted = result == VK_SUCCESS;
  if (submitted) {
    result = vkWaitForFences(device->device,
                             1u,
                             &native->transferFence,
                             VK_TRUE,
                             UINT64_MAX);
  }
  if (submitted && result != VK_SUCCESS) {
    (void)vkDeviceWaitIdle(device->device);
  }
  return result == VK_SUCCESS ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
void
vk_abortTransfer(GPUCommandQueue *queue) {
  GPUCommandQueueVk *native;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->transferOpen || !native->transferCommand) {
    return;
  }

  (void)vkEndCommandBuffer(native->transferCommand);
  native->transferOpen = false;
}

GPU_HIDE
GPUCommandQueue*
vk_createCommandQueue(GPUDevice       *device,
                      uint32_t         familyIndex,
                      uint32_t         queueIndex,
                      GPUQueueFlagBits bits) {
  GPUCommandQueue     *queue;
  GPUCommandQueueVk   *native;
  GPUDeviceVk         *deviceVk;
  GPUPhysicalDeviceVk *physical;
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
  physical            = device->phyDevice ? device->phyDevice->_priv : NULL;
  queue->_priv        = native;
  queue->_device      = device;
  queue->bits         = bits;
  native->queue       = queue;
  native->familyIndex = familyIndex;
  native->queueIndex  = queueIndex;
  native->timestampValidBits = 0u;
  if (physical && familyIndex < physical->nQueFamilies) {
    native->timestampValidBits =
      physical->queueFamilyProps[familyIndex].timestampValidBits;
  }
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
    vk_abortTransfer(queue);
    command = native->commands;
    while (command) {
      next = command->next;
#ifdef __APPLE__
      while (command->transferChunks) {
        GPUTransferChunkVk *chunk;

        chunk = command->transferChunks;
        command->transferChunks = chunk->next;
        GPUDestroyBuffer(chunk->buffer);
        free(chunk);
      }
#endif
      if (deviceVk && command->fence) {
        vkDestroyFence(deviceVk->device, command->fence, NULL);
      }
      free(command);
      command = next;
    }
    if (deviceVk && native->transferFence) {
      vkDestroyFence(deviceVk->device, native->transferFence, NULL);
    }
    vk_destroyBuffer(native->uploadStaging);
    vk_destroyBuffer(native->readbackStaging);
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
GPUResult
vk_getTimestampPeriod(GPUCommandQueue *queue,
                      double          *outNanosecondsPerTick) {
  GPUPhysicalDeviceVk *physical;
  GPUCommandQueueVk   *native;
  GPUDevice           *device;

  native   = queue ? queue->_priv : NULL;
  device   = queue ? queue->_device : NULL;
  physical = device && device->phyDevice ? device->phyDevice->_priv : NULL;
  if (!native || native->timestampValidBits == 0u || !physical ||
      !(physical->props.limits.timestampPeriod > 0.0f)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outNanosecondsPerTick = (double)physical->props.limits.timestampPeriod;
  return GPU_OK;
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
  native->submitFence = native->fence;
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

  if (!queue || !queue->_priv || !queue->_device) {
    return NULL;
  }

  native = vk__takeCommandBufferState(queue);
  if (!native) {
    return NULL;
  }

  cmdb = &native->commandBuffer;
  memset(cmdb, 0, sizeof(*cmdb));
  native->presentSwapchain  = NULL;
  native->submitFence       = native->fence;
  native->presentImageIndex = 0u;
  native->presentFrameIndex = 0u;
  native->copyDebugLabelActive = false;
  cmdb->_priv               = native;
  cmdb->_queue              = queue;

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

  vk_setDebugName(queue->_device,
                  VK_OBJECT_TYPE_COMMAND_BUFFER,
                  (uint64_t)(uintptr_t)native->command,
                  label);

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
GPUResult
vk_commitCommandBuffer(GPUCommandBuffer * __restrict cmdb) {
  GPUCommandBufferVk *native;
  GPUCommandQueueVk  *queue;
  GPUDeviceVk        *deviceVk;
  GPUSwapChainVk     *swapchain;
  GPUFrameSyncVk     *frameSync;
  VkFence             submitFence;
  VkPipelineStageFlags waitStage;
  VkSubmitInfo        submitInfo = {0};
  VkPresentInfoKHR    presentInfo = {0};
  VkResult            result;
  VkResult            presentResult;
  GPUResult           commitResult;
  bool                frameFenceReset;

  native   = cmdb ? cmdb->_priv : NULL;
  queue    = native ? native->owner : NULL;
  deviceVk = cmdb && cmdb->_queue && cmdb->_queue->_device ?
    cmdb->_queue->_device->_priv : NULL;
  if (!native || !queue || !deviceVk) {
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  swapchain       = native->presentSwapchain;
  frameSync       = NULL;
  submitFence     = native->fence;
  waitStage       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  frameFenceReset = false;
  if (swapchain) {
    if (!swapchain->frameActive || !swapchain->frameScheduled ||
        native->presentFrameIndex >= swapchain->imageCount ||
        native->presentImageIndex >= swapchain->imageCount) {
      gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    frameSync   = &swapchain->frameSync[native->presentFrameIndex];
    submitFence = frameSync->fence;
  }

  result = vkEndCommandBuffer(native->command);
  if (result == VK_SUCCESS) {
    result = vkResetFences(deviceVk->device, 1u, &submitFence);
    frameFenceReset = result == VK_SUCCESS && frameSync != NULL;
  }
  if (result == VK_SUCCESS) {
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers    = &native->command;
    if (frameSync) {
      submitInfo.waitSemaphoreCount   = 1u;
      submitInfo.pWaitSemaphores      = &frameSync->imageAvailable;
      submitInfo.pWaitDstStageMask    = &waitStage;
      submitInfo.signalSemaphoreCount = 1u;
      submitInfo.pSignalSemaphores    = &frameSync->renderFinished;
    }
    result = vkQueueSubmit(queue->queRaw,
                           1u,
                           &submitInfo,
                           submitFence);
  }
  if (result != VK_SUCCESS) {
    if (frameFenceReset) {
      (void)vk_restoreFrameFence(swapchain, frameSync);
    }
    vk__reportQueueError(cmdb, "queue submit", result);
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  commitResult        = GPU_OK;
  native->submitFence = submitFence;
  if (swapchain) {
    swapchain->frameSubmitted = true;

    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores    = &frameSync->renderFinished;
    presentInfo.swapchainCount     = 1u;
    presentInfo.pSwapchains        = &swapchain->swapchain;
    presentInfo.pImageIndices      = &native->presentImageIndex;
    presentResult = vkQueuePresentKHR(queue->queRaw, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
      vk__reportQueueError(cmdb, "present", presentResult);
      commitResult = GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk__queueLock(queue);
  queue->inFlightCount++;
  if (swapchain) {
    swapchain->inFlightCommandCount++;
  }
  if (queue->pendingTail) {
    queue->pendingTail->pendingNext = native;
  } else {
    queue->pendingHead = native;
  }
  queue->pendingTail = native;
  vk__queueSignal(queue);
  vk__queueUnlock(queue);
  return commitResult;
}

GPU_HIDE
void
vk_initCmdQue(GPUApiCommandQueue *apiQue) {
  apiQue->getCommandQueue         = vk_getCommandQueue;
  apiQue->getTimestampPeriod      = vk_getTimestampPeriod;
  apiQue->newCommandQueue         = vk_newCommandQueue;
  apiQue->newCommandBuffer        = vk_newCommandBuffer;
  apiQue->commandBufferOnComplete = vk_commandBufferOnComplete;
  apiQue->commit                  = vk_commitCommandBuffer;
}
