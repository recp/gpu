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
  VK_COMPLETION_STACK_SIZE     = 64u * 1024u,
  VK_SUBMIT_STACK_COUNT        = 64u,
  VK_TRANSFER_OFFSET_ALIGNMENT = 512u
};

static GPUResult
vk__flushTransfers(GPUQueue *queue, bool wait);

static void
vk__queueLock(GPUQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&queue->poolLock);
#else
  pthread_mutex_lock(&queue->poolLock);
#endif
}

static void
vk__queueUnlock(GPUQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&queue->poolLock);
#else
  pthread_mutex_unlock(&queue->poolLock);
#endif
}

static void
vk__queueSignal(GPUQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  WakeConditionVariable(&queue->pendingCondition);
#else
  pthread_cond_signal(&queue->pendingCondition);
#endif
}

static void
vk__queueBroadcast(GPUQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&queue->pendingCondition);
#else
  pthread_cond_broadcast(&queue->pendingCondition);
#endif
}

static void
vk__queueWait(GPUQueueVk *queue) {
#if defined(_WIN32) || defined(WIN32)
  SleepConditionVariableCS(&queue->pendingCondition,
                           &queue->poolLock,
                           INFINITE);
#else
  pthread_cond_wait(&queue->pendingCondition, &queue->poolLock);
#endif
}

static void
vk__waitQueueIdle(GPUQueueVk *queue) {
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

static bool
vk__beginFrameTime(GPUCommandBufferVk *native) {
  GPUQueueVk           *queue;
  GPUDevice            *device;
  GPUDeviceVk          *deviceVk;
  VkQueryPoolCreateInfo queryInfo = {0};

  queue    = native ? native->owner : NULL;
  device   = queue && queue->queue ? queue->queue->_device : NULL;
  deviceVk = device ? device->_priv : NULL;
  if (!native || !queue || !device || !deviceVk ||
      !device->runtimeConfig.enableStats ||
      !(queue->queue->bits & GPU_QUEUE_GRAPHICS_BIT) ||
      queue->timestampValidBits == 0u ||
      !(queue->timestampPeriodNs > 0.0)) {
    return false;
  }

  if (!native->frameTimeQueries) {
    queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    queryInfo.queryCount = 2u;
    if (vkCreateQueryPool(deviceVk->device,
                          &queryInfo,
                          NULL,
                          &native->frameTimeQueries) != VK_SUCCESS) {
      return false;
    }
  }

  vkCmdResetQueryPool(native->command, native->frameTimeQueries, 0u, 2u);
  vkCmdWriteTimestamp(native->command,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      native->frameTimeQueries,
                      0u);
  return true;
}

static void
vk__endFrameTime(GPUCommandBufferVk *native) {
  if (!native || !native->frameTimeActive ||
      !native->commandBuffer._recordsGPUFrameTime) {
    return;
  }

  vkCmdWriteTimestamp(native->command,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      native->frameTimeQueries,
                      1u);
}

static void
vk__recordFrameTime(GPUCommandBufferVk *native) {
  GPUQueueVk *queue;
  GPUDevice  *device;
  uint64_t    timestamps[2];
  uint64_t    elapsed;
  uint64_t    mask;
  double      milliseconds;
  VkResult    result;

  queue  = native ? native->owner : NULL;
  device = queue && queue->queue ? queue->queue->_device : NULL;
  if (!native || !native->frameTimeActive ||
      !native->commandBuffer._recordsGPUFrameTime ||
      !native->frameTimeQueries || !device || !device->_priv) {
    return;
  }

  result = vkGetQueryPoolResults(
    ((GPUDeviceVk *)device->_priv)->device,
    native->frameTimeQueries,
    0u,
    2u,
    sizeof(timestamps),
    timestamps,
    sizeof(timestamps[0]),
    VK_QUERY_RESULT_64_BIT
  );
  if (result != VK_SUCCESS) {
    return;
  }

  if (queue->timestampValidBits < 64u) {
    mask          = (1ull << queue->timestampValidBits) - 1ull;
    timestamps[0] &= mask;
    timestamps[1] &= mask;
    elapsed       = (timestamps[1] - timestamps[0]) & mask;
  } else {
    elapsed = timestamps[1] - timestamps[0];
  }
  milliseconds = (double)elapsed * queue->timestampPeriodNs / 1000000.0;
  gpuDeviceRecordGPUFrameTime(device, milliseconds);
}

static void
vk__recycleCommandBuffer(GPUCommandBuffer *cmdb) {
  GPUCommandBufferVk *native;
  GPUQueueVk         *queue;
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
vk__takePendingCommand(GPUQueueVk *queue) {
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
vk__completionLoop(GPUQueueVk *queue) {
  GPUCommandBufferVk *native;
  GPUCommandBuffer   *cmdb;
  GPUDeviceVk        *deviceVk;
  GPUSwapchainVk     *swapchain;
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
    } else {
      vk__recordFrameTime(native);
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
vk_waitSwapchainIdle(GPUSwapchainVk *swapchain) {
  GPUQueueVk        *queue;

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
  GPUResult    flushResult;
  VkResult     result;

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  flushResult = GPU_OK;
  for (uint32_t i = 0u; i < deviceVk->nCreatedQueues; i++) {
    GPUQueue        *queue;
    GPUResult        queueResult;

    queue = deviceVk->createdQueues[i];
    if (!queue) {
      continue;
    }
    queueResult = vk__flushTransfers(queue, false);
    if (flushResult == GPU_OK && queueResult != GPU_OK) {
      flushResult = queueResult;
    }
  }

  result = vkDeviceWaitIdle(deviceVk->device);
  for (uint32_t i = 0u; i < deviceVk->nCreatedQueues; i++) {
    GPUQueueVk        *queue;

    queue = deviceVk->createdQueues[i] ?
      deviceVk->createdQueues[i]->_priv : NULL;
    if (queue) {
      if (result == VK_SUCCESS) {
        for (uint32_t slot = 0u; slot < GPU_VK_TRANSFER_SLOT_COUNT; slot++) {
          queue->transferSlots[slot].pending = false;
        }
      }
      vk__waitQueueIdle(queue);
    }
  }

  if (flushResult != GPU_OK) {
    return flushResult;
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
vk__startWorker(GPUQueueVk *queue) {
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
vk__stopWorker(GPUQueueVk *queue) {
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
vk__transferCapacity(uint64_t sizeBytes, uint64_t minimumCapacity) {
  uint64_t capacity;

  capacity = minimumCapacity > GPU_VK_BUFFER_TRANSFER_CAPACITY
               ? minimumCapacity
               : GPU_VK_BUFFER_TRANSFER_CAPACITY;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }
    capacity *= 2u;
  }
  return capacity;
}

static bool
vk__ensureTransferContext(GPUQueueVk *queue,
                          GPUDeviceVk       *device,
                          GPUTransferSlotVk *slot) {
  VkCommandBufferAllocateInfo allocationInfo = {0};
  VkFenceCreateInfo           fenceInfo = {0};
  VkCommandBuffer             command;
  VkFence                     fence;

  if (!queue || !device || !device->device || !queue->commandPool || !slot) {
    return false;
  }
  if (slot->command && slot->fence) {
    return true;
  }
  if (slot->command || slot->fence) {
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

  slot->command = command;
  slot->fence   = fence;
  return true;
}

static bool
vk__ensureTransferBuffer(GPUQueueVk *queue,
                         GPUTransferSlotVk  *transfer,
                         bool               upload,
                         uint64_t           sizeBytes,
                         uint64_t           minimumCapacity) {
  GPUBufferCreateInfo  info = {0};
  GPUBuffer           **slot;
  GPUBuffer            *staging;
  uint64_t             *currentCapacity;
  uint64_t              capacity;

  if (!queue || !transfer || sizeBytes == 0u) {
    return false;
  }
  slot            = upload ? &transfer->uploadStaging
                           : &queue->readbackStaging;
  currentCapacity = upload ? &transfer->uploadCapacity
                           : &queue->readbackCapacity;
  if (*slot && *currentCapacity >= sizeBytes &&
      *currentCapacity >= minimumCapacity) {
    return true;
  }

  capacity = vk__transferCapacity(sizeBytes, minimumCapacity);
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

static GPUResult
vk__waitTransfer(GPUQueue   *queue,
                 GPUTransferSlotVk *slot,
                 bool               countStall) {
  GPUQueueVk        *native;
  GPUDeviceVk       *device;
  VkResult           result;

  native = queue ? queue->_priv : NULL;
  device = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!native || !device || !device->device || !slot) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!slot->pending) {
    return GPU_OK;
  }
  if (!slot->fence) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = vkGetFenceStatus(device->device, slot->fence);
  if (result == VK_NOT_READY) {
    if (countStall) {
      queue->_device->allocatorStats.uploadStallCount++;
    }
    result = vkWaitForFences(device->device,
                             1u,
                             &slot->fence,
                             VK_TRUE,
                             UINT64_MAX);
  }
  if (result != VK_SUCCESS) {
    (void)vkDeviceWaitIdle(device->device);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  slot->pending = false;
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_beginTransfer(GPUQueue *queue,
                 bool             upload,
                 uint64_t         sizeBytes,
                 uint64_t         minimumCapacity,
                 VkCommandBuffer *outCommand,
                 GPUBuffer      **outStaging,
                 uint64_t        *outOffset) {
  GPUQueueVk              *native;
  GPUDeviceVk             *device;
  GPUTransferSlotVk       *slot;
  VkCommandBufferBeginInfo beginInfo = {0};
  GPUResult                result;

  if (!queue || !queue->_device || !outCommand || !outStaging || !outOffset ||
      sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCommand = VK_NULL_HANDLE;
  *outStaging = NULL;
  *outOffset  = 0u;
  native      = queue->_priv;
  device      = queue->_device->_priv;
  if (!native || !device || !device->device || !native->queRaw) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (native->transferOpen) {
    uint64_t offset;

    if (!native->transferUpload) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (upload) {
      slot = &native->transferSlots[native->activeTransferSlot];
      if (slot->uploadCapacity >= minimumCapacity &&
          slot->uploadUsed <=
          UINT64_MAX - (VK_TRANSFER_OFFSET_ALIGNMENT - 1u)) {
        offset = (slot->uploadUsed + VK_TRANSFER_OFFSET_ALIGNMENT - 1u) &
                 ~(uint64_t)(VK_TRANSFER_OFFSET_ALIGNMENT - 1u);
        if (offset <= slot->uploadCapacity &&
            sizeBytes <= slot->uploadCapacity - offset) {
          slot->uploadUsed = offset + sizeBytes;
          *outCommand      = slot->command;
          *outStaging      = slot->uploadStaging;
          *outOffset       = offset;
          return GPU_OK;
        }
      }
    }
    result = vk__flushTransfers(queue, false);
    if (result != GPU_OK) {
      return result;
    }
  }
  slot   = &native->transferSlots[native->nextTransferSlot];
  result = vk__waitTransfer(queue, slot, true);
  if (result != GPU_OK) {
    return result;
  }
  if (!vk__ensureTransferContext(native, device, slot) ||
      !vk__ensureTransferBuffer(native,
                                slot,
                                upload,
                                sizeBytes,
                                minimumCapacity)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (vkResetCommandBuffer(slot->command, 0u) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(slot->command, &beginInfo) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->activeTransferSlot = native->nextTransferSlot;
  native->transferOpen       = true;
  native->transferUpload     = upload;
  slot->uploadUsed           = upload ? sizeBytes : 0u;
  *outCommand                = slot->command;
  *outStaging                = upload ? slot->uploadStaging
                                      : native->readbackStaging;
  *outOffset                 = 0u;
  return GPU_OK;
}

static GPUResult
vk__flushTransfers(GPUQueue *queue, bool wait) {
  GPUQueueVk        *native;
  GPUDeviceVk       *device;
  GPUTransferSlotVk *slot;
  VkSubmitInfo       submitInfo = {0};
  VkResult           result;

  native = queue ? queue->_priv : NULL;
  device = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!native || !device || !device->device || !native->queRaw) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (native->transferOpen) {
    slot = native->activeTransferSlot < GPU_VK_TRANSFER_SLOT_COUNT
             ? &native->transferSlots[native->activeTransferSlot]
             : NULL;
    if (!slot || !slot->command || !slot->fence) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    result                 = vkEndCommandBuffer(slot->command);
    native->transferOpen   = false;
    native->transferUpload = false;
    if (result != VK_SUCCESS) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    result = vkResetFences(device->device, 1u, &slot->fence);
    if (result != VK_SUCCESS) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers    = &slot->command;
    result = vkQueueSubmit(native->queRaw, 1u, &submitInfo, slot->fence);
    if (result != VK_SUCCESS) {
      return GPU_ERROR_BACKEND_FAILURE;
    }

    slot->pending            = true;
    native->nextTransferSlot =
      (native->activeTransferSlot + 1u) % GPU_VK_TRANSFER_SLOT_COUNT;
  }

  if (wait) {
    for (uint32_t i = 0u; i < GPU_VK_TRANSFER_SLOT_COUNT; i++) {
      GPUResult waitResult;

      waitResult = vk__waitTransfer(queue, &native->transferSlots[i], false);
      if (waitResult != GPU_OK) {
        return waitResult;
      }
    }
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_submitTransfer(GPUQueue *queue, bool wait) {
  GPUQueueVk        *native;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->transferOpen) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!wait && native->transferUpload) {
    return GPU_OK;
  }
  return vk__flushTransfers(queue, wait);
}

GPU_HIDE
void
vk_abortTransfer(GPUQueue *queue) {
  GPUQueueVk        *native;
  GPUTransferSlotVk *slot;

  native = queue ? queue->_priv : NULL;
  slot = native && native->activeTransferSlot < GPU_VK_TRANSFER_SLOT_COUNT
           ? &native->transferSlots[native->activeTransferSlot]
           : NULL;
  if (!native || !slot || !native->transferOpen || !slot->command) {
    return;
  }

  (void)vkEndCommandBuffer(slot->command);
  slot->uploadUsed         = 0u;
  native->transferOpen     = false;
  native->transferUpload   = false;
}

GPU_HIDE
GPUQueue*
vk_createCommandQueue(GPUDevice       *device,
                      uint32_t         familyIndex,
                      uint32_t         queueIndex,
                      GPUQueueFlagBits bits) {
  GPUQueue            *queue;
  GPUQueueVk          *native;
  GPUDeviceVk         *deviceVk;
  GPUAdapterVk        *adapterVk;
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
  adapterVk           = device->adapter ? device->adapter->_priv : NULL;

  queue->_priv        = native;
  queue->_device      = device;
  queue->bits         = bits;

  native->queue              = queue;
  native->familyIndex        = familyIndex;
  native->queueIndex         = queueIndex;
  native->timestampValidBits = 0u;
  native->timestampPeriodNs  = 0.0;
  if (adapterVk && familyIndex < adapterVk->nQueFamilies) {
    native->timestampValidBits =
      adapterVk->queueFamilyProps[familyIndex].timestampValidBits;
    native->timestampPeriodNs =
      (double)adapterVk->props.limits.timestampPeriod;
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
vk_destroyCommandQueue(GPUQueue *queue) {
  GPUCommandBufferVk *command;
  GPUCommandBufferVk *next;
  GPUQueueVk         *native;
  GPUDeviceVk        *deviceVk;

  if (!queue) {
    return;
  }

  native   = queue->_priv;
  deviceVk = queue->_device ? queue->_device->_priv : NULL;
  if (native) {
    vk__stopWorker(native);
    if (vk__flushTransfers(queue, true) != GPU_OK) {
      vk_abortTransfer(queue);
    }
    for (uint32_t slot = 0u; slot < GPU_VK_TRANSFER_SLOT_COUNT; slot++) {
      (void)vk__waitTransfer(queue, &native->transferSlots[slot], false);
    }
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
      if (deviceVk && command->frameTimeQueries) {
        vkDestroyQueryPool(deviceVk->device,
                           command->frameTimeQueries,
                           NULL);
      }
      free(command);
      command = next;
    }
    for (uint32_t slot = 0u; slot < GPU_VK_TRANSFER_SLOT_COUNT; slot++) {
      GPUTransferSlotVk *transfer;

      transfer = &native->transferSlots[slot];
      if (deviceVk && transfer->fence) {
        vkDestroyFence(deviceVk->device, transfer->fence, NULL);
      }
      vk_destroyBuffer(transfer->uploadStaging);
    }
    vk_destroyBuffer(native->readbackStaging);
    if (deviceVk && native->commandPool) {
      vkDestroyCommandPool(deviceVk->device, native->commandPool, NULL);
    }
    free(native);
  }
  free(queue);
}

GPU_HIDE
GPUQueue*
vk_getCommandQueue(GPUDevice *device, GPUQueueFlagBits bits, uint32_t index) {
  GPUQueue          *queue;
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
vk_getTimestampPeriod(GPUQueue *queue,
                      double   *outNanosecondsPerTick) {
  GPUQueueVk *native;

  native = queue ? queue->_priv : NULL;
  if (!native || native->timestampValidBits == 0u ||
      !(native->timestampPeriodNs > 0.0)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outNanosecondsPerTick = native->timestampPeriodNs;
  return GPU_OK;
}

GPU_HIDE
GPUQueue*
vk_newCommandQueue(GPUDevice * __restrict device) {
  return vk_getCommandQueue(
    device,
    GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT,
    0
  );
}

static GPUCommandBufferVk*
vk__createCommandBufferState(GPUQueue *queue) {
  GPUQueueVk         *queueVk;
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
vk__takeCommandBufferState(GPUQueue *queue) {
  GPUQueueVk         *queueVk;
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
vk_newCommandBuffer(GPUQueue  * __restrict queue,
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
  native->frameTimeActive   = false;
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
  native->frameTimeActive = vk__beginFrameTime(native);

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
  GPUQueueVk         *queue;
  GPUDeviceVk        *deviceVk;
  GPUSwapchainVk     *swapchain;
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

  commitResult = vk__flushTransfers(queue->queue, false);
  if (commitResult != GPU_OK) {
    gpuFinishCommandBuffer(cmdb, vk__recycleCommandBuffer);
    return commitResult;
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

  vk__endFrameTime(native);
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

static GPUResult
vk__commitCommandBuffers(uint32_t                  count,
                         GPUCommandBuffer * const *buffers) {
  GPUResult result;

  result = GPU_OK;
  for (uint32_t i = 0u; i < count; i++) {
    GPUResult commitResult;

    commitResult = vk_commitCommandBuffer(buffers[i]);
    if (result == GPU_OK && commitResult != GPU_OK) {
      result = commitResult;
    }
  }
  return result;
}

static void
vk__finishCommandBuffers(uint32_t                  count,
                         GPUCommandBuffer * const *buffers,
                         bool                      recycle) {
  for (uint32_t i = 0u; i < count; i++) {
    gpuFinishCommandBuffer(buffers[i],
                           recycle ? vk__recycleCommandBuffer : NULL);
  }
}

GPU_HIDE
GPUResult
vk_submitCommandBuffers(GPUQueue                  * __restrict queueHandle,
                        uint32_t                                count,
                        GPUCommandBuffer * const * __restrict buffers) {
  GPUCommandBufferVk *natives[VK_SUBMIT_STACK_COUNT];
  VkCommandBuffer     commands[VK_SUBMIT_STACK_COUNT];
  GPUQueueVk         *queue;
  GPUDeviceVk        *device;
  VkFence             submitFence;
  VkSubmitInfo        submitInfo = {0};
  GPUResult           flushResult;
  VkResult            result;

  queue  = queueHandle ? queueHandle->_priv : NULL;
  device = queueHandle && queueHandle->_device
             ? queueHandle->_device->_priv
             : NULL;
  if (!queue || !device || !buffers || count < 2u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (count > VK_SUBMIT_STACK_COUNT) {
    return vk__commitCommandBuffers(count, buffers);
  }

  for (uint32_t i = 0u; i < count; i++) {
    natives[i] = buffers[i] ? buffers[i]->_priv : NULL;
    if (!natives[i] || natives[i]->owner != queue ||
        !natives[i]->command || natives[i]->presentSwapchain) {
      return vk__commitCommandBuffers(count, buffers);
    }
    commands[i] = natives[i]->command;
  }

  flushResult = vk__flushTransfers(queueHandle, false);
  if (flushResult != GPU_OK) {
    vk__finishCommandBuffers(count, buffers, true);
    return flushResult;
  }

  for (uint32_t i = 0u; i < count; i++) {
    vk__endFrameTime(natives[i]);
    result = vkEndCommandBuffer(commands[i]);
    if (result != VK_SUCCESS) {
      vk__reportQueueError(buffers[i], "command buffer end", result);
      vk__finishCommandBuffers(count, buffers, true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  submitFence = natives[count - 1u]->fence;
  result = vkResetFences(device->device, 1u, &submitFence);
  if (result == VK_SUCCESS) {
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = count;
    submitInfo.pCommandBuffers    = commands;
    result = vkQueueSubmit(queue->queRaw, 1u, &submitInfo, submitFence);
  }
  if (result != VK_SUCCESS) {
    vk__reportQueueError(buffers[0], "queue batch submit", result);
    vk__finishCommandBuffers(count, buffers, true);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk__queueLock(queue);
  for (uint32_t i = 0u; i < count; i++) {
    natives[i]->submitFence = submitFence;
    natives[i]->pendingNext = NULL;
    if (queue->pendingTail) {
      queue->pendingTail->pendingNext = natives[i];
    } else {
      queue->pendingHead = natives[i];
    }
    queue->pendingTail = natives[i];
  }
  queue->inFlightCount += count;
  vk__queueSignal(queue);
  vk__queueUnlock(queue);
  return GPU_OK;
}

static VkPipelineStageFlags
vk_submitWaitStages(GPUPipelineStageMask stages) {
  VkPipelineStageFlags native;

  native = 0u;
  if (stages & GPU_STAGE_TOP)
    native |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (stages & GPU_STAGE_VERTEX)
    native |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  if (stages & GPU_STAGE_FRAGMENT)
    native |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  if (stages & GPU_STAGE_COMPUTE)
    native |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  if (stages & GPU_STAGE_TRANSFER)
    native |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  if (stages & GPU_STAGE_BOTTOM)
    native |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  return native;
}

static GPUResult
vk_createSemaphore(GPUDevice                    *device,
                   const GPUSemaphoreCreateInfo *info,
                   GPUSemaphore                 *semaphore) {
  GPUDeviceVk                       *deviceVk;
  GPUSemaphoreVk                    *native;
  VkSemaphoreTypeCreateInfo          typeInfo = {0};
  VkSemaphoreCreateInfo              createInfo = {0};

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->device || !semaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!deviceVk->timelineSemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  typeInfo.initialValue  = info ? info->initialValue : 0u;
  createInfo.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  createInfo.pNext       = &typeInfo;
  if (vkCreateSemaphore(deviceVk->device,
                        &createInfo,
                        NULL,
                        &native->semaphore) != VK_SUCCESS) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->device  = deviceVk->device;
  semaphore->_priv = native;
  vk_setDebugName(device,
                  VK_OBJECT_TYPE_SEMAPHORE,
                  (uint64_t)(uintptr_t)native->semaphore,
                  info ? gpuDeviceDebugLabel(device, info->label) : NULL);
  return GPU_OK;
}

static void
vk_destroySemaphore(GPUSemaphore *semaphore) {
  GPUSemaphoreVk *native;

  native = semaphore ? semaphore->_priv : NULL;
  if (!native) {
    return;
  }
  if (native->device && native->semaphore) {
    vkDestroySemaphore(native->device, native->semaphore, NULL);
  }
  free(native);
  semaphore->_priv = NULL;
}

static GPUResult
vk_submitEx(GPUQueue                   *queueHandle,
            const GPUQueueSubmitExInfo *info) {
  GPUCommandBufferVk        *natives[VK_SUBMIT_STACK_COUNT];
  VkCommandBuffer            commands[VK_SUBMIT_STACK_COUNT];
  VkSemaphore                waits[VK_SUBMIT_STACK_COUNT + 1u];
  VkSemaphore                signals[VK_SUBMIT_STACK_COUNT + 1u];
  VkPipelineStageFlags       waitStages[VK_SUBMIT_STACK_COUNT + 1u];
  uint64_t                   waitValues[VK_SUBMIT_STACK_COUNT + 1u];
  uint64_t                   signalValues[VK_SUBMIT_STACK_COUNT + 1u];
  GPUQueueVk                *queue;
  GPUDeviceVk               *device;
  GPUSwapchainVk            *swapchain;
  GPUCommandBufferVk        *presentNative;
  GPUFrameSyncVk            *frameSync;
  VkFence                    submitFence;
  VkTimelineSemaphoreSubmitInfo timelineInfo = {0};
  VkSubmitInfo               submitInfo = {0};
  VkPresentInfoKHR           presentInfo = {0};
  GPUResult                  flushResult;
  VkResult                   result;
  VkResult                   presentResult;
  uint32_t                   waitCount;
  uint32_t                   signalCount;
  bool                       frameFenceReset;

  queue  = queueHandle ? queueHandle->_priv : NULL;
  device = queueHandle && queueHandle->_device
             ? queueHandle->_device->_priv
             : NULL;
  if (!queue || !device || !device->timelineSemaphore || !info ||
      info->commandBufferCount == 0u ||
      info->commandBufferCount > VK_SUBMIT_STACK_COUNT ||
      info->waitCount > VK_SUBMIT_STACK_COUNT ||
      info->signalCount > VK_SUBMIT_STACK_COUNT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  swapchain     = NULL;
  presentNative = NULL;
  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    natives[i] = info->ppCommandBuffers[i]
                   ? info->ppCommandBuffers[i]->_priv
                   : NULL;
    if (!natives[i] || natives[i]->owner != queue || !natives[i]->command) {
      vk__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (natives[i]->presentSwapchain) {
      if (swapchain) {
        vk__finishCommandBuffers(info->commandBufferCount,
                                 info->ppCommandBuffers,
                                 true);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      swapchain     = natives[i]->presentSwapchain;
      presentNative = natives[i];
    }
    commands[i] = natives[i]->command;
  }

  flushResult = vk__flushTransfers(queueHandle, false);
  if (flushResult != GPU_OK) {
    vk__finishCommandBuffers(info->commandBufferCount,
                             info->ppCommandBuffers,
                             true);
    return flushResult;
  }

  frameSync       = NULL;
  submitFence     = natives[info->commandBufferCount - 1u]->fence;
  frameFenceReset = false;
  if (swapchain) {
    if (!swapchain->frameActive || !swapchain->frameScheduled ||
        presentNative->presentFrameIndex >= swapchain->imageCount ||
        presentNative->presentImageIndex >= swapchain->imageCount) {
      vk__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    frameSync   = &swapchain->frameSync[presentNative->presentFrameIndex];
    submitFence = frameSync->fence;
  }

  waitCount = info->waitCount;
  for (uint32_t i = 0u; i < info->waitCount; i++) {
    GPUSemaphoreVk *native;

    native = info->pWaits[i].semaphore->_priv;
    if (!native || native->device != device->device || !native->semaphore) {
      vk__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    waits[i]      = native->semaphore;
    waitValues[i] = info->pWaits[i].value;
    waitStages[i] = vk_submitWaitStages(info->pWaits[i].waitStages);
  }
  if (frameSync) {
    waits[waitCount]      = frameSync->imageAvailable;
    waitValues[waitCount] = 0u;
    waitStages[waitCount] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    waitCount++;
  }

  signalCount = info->signalCount;
  for (uint32_t i = 0u; i < info->signalCount; i++) {
    GPUSemaphoreVk *native;

    native = info->pSignals[i].semaphore->_priv;
    if (!native || native->device != device->device || !native->semaphore) {
      vk__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    signals[i]      = native->semaphore;
    signalValues[i] = info->pSignals[i].value;
  }
  if (frameSync) {
    signals[signalCount]      = frameSync->renderFinished;
    signalValues[signalCount] = 0u;
    signalCount++;
  }

  result = VK_SUCCESS;
  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    vk__endFrameTime(natives[i]);
    result = vkEndCommandBuffer(commands[i]);
    if (result != VK_SUCCESS) {
      break;
    }
  }
  if (result == VK_SUCCESS) {
    result = vkResetFences(device->device, 1u, &submitFence);
    frameFenceReset = result == VK_SUCCESS && frameSync != NULL;
  }
  if (result == VK_SUCCESS) {
    timelineInfo.sType =
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount   = waitCount;
    timelineInfo.pWaitSemaphoreValues      = waitValues;
    timelineInfo.signalSemaphoreValueCount = signalCount;
    timelineInfo.pSignalSemaphoreValues    = signalValues;

    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext                = &timelineInfo;
    submitInfo.waitSemaphoreCount   = waitCount;
    submitInfo.pWaitSemaphores      = waits;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = info->commandBufferCount;
    submitInfo.pCommandBuffers      = commands;
    submitInfo.signalSemaphoreCount = signalCount;
    submitInfo.pSignalSemaphores    = signals;
    result = vkQueueSubmit(queue->queRaw, 1u, &submitInfo, submitFence);
  }
  if (result != VK_SUCCESS) {
    if (frameFenceReset) {
      (void)vk_restoreFrameFence(swapchain, frameSync);
    }
    vk__reportQueueError(info->ppCommandBuffers[0],
                         "advanced queue submit",
                         result);
    vk__finishCommandBuffers(info->commandBufferCount,
                             info->ppCommandBuffers,
                             true);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (swapchain) {
    swapchain->frameSubmitted = true;
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores    = &frameSync->renderFinished;
    presentInfo.swapchainCount     = 1u;
    presentInfo.pSwapchains        = &swapchain->swapchain;
    presentInfo.pImageIndices      = &presentNative->presentImageIndex;
    presentResult = vkQueuePresentKHR(queue->queRaw, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
      vk__reportQueueError(info->ppCommandBuffers[0],
                           "advanced present",
                           presentResult);
    }
  } else {
    presentResult = VK_SUCCESS;
  }

  vk__queueLock(queue);
  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    natives[i]->submitFence = submitFence;
    natives[i]->pendingNext = NULL;
    if (queue->pendingTail) {
      queue->pendingTail->pendingNext = natives[i];
    } else {
      queue->pendingHead = natives[i];
    }
    queue->pendingTail = natives[i];
  }
  queue->inFlightCount += info->commandBufferCount;
  if (swapchain) {
    swapchain->inFlightCommandCount++;
  }
  vk__queueSignal(queue);
  vk__queueUnlock(queue);
  return presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
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
  apiQue->submit                  = vk_submitCommandBuffers;
  apiQue->createSemaphore         = vk_createSemaphore;
  apiQue->destroySemaphore        = vk_destroySemaphore;
  apiQue->submitEx                = vk_submitEx;
}
