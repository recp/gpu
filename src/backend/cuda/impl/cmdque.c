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

#include "../common.h"

enum {
  CUDA_SUBMIT_STACK_COUNT    = 64u,
  CUDA_SEMAPHORE_BATCH_COUNT = 16u
};

static CUresult
cuda__launchCommand(GPUQueueCuda *queue, GPUCommandCuda *command) {
  void                        *parameters[GPU_SHADER_PTX_MAX_PARAM_COUNT];
  GPUComputePipelineCuda      *pipeline;
  GPUDispatchCuda             *dispatch;
  uint8_t                     *paramData;
  const GPUShaderPTXParamInfo *param;
  CUresult                     result;
  uint32_t                     i;
  uint32_t                     j;
  uint32_t                     size;

  result = CUDA_SUCCESS;

  for (i = 0u; i < command->dispatchCount; i++) {
    dispatch = &command->dispatches[i];
    pipeline = dispatch->pipeline->_state;

    if (!pipeline || pipeline->paramCount > GPU_SHADER_PTX_MAX_PARAM_COUNT
        || dispatch->paramDataSize != pipeline->paramDataSize) {
      return CUDA_ERROR_INVALID_VALUE;
    }

    if (dispatch->paramDataSize <= sizeof(dispatch->inlineParams)) {
      paramData = dispatch->inlineParams;
    } else {
      if (!command->paramData
          || dispatch->paramDataOffset > command->paramDataCount
          || dispatch->paramDataSize > command->paramDataCount - dispatch->paramDataOffset) {
        return CUDA_ERROR_INVALID_VALUE;
      }

      paramData = command->paramData + dispatch->paramDataOffset;
    }

    for (j = 0u; j < pipeline->paramCount; j++) {
      param = &pipeline->params[j];
      size  = cuda_ptxParamSize(param->kind);

      if (size == 0u || dispatch->paramDataSize < size
          || param->dataOffset > dispatch->paramDataSize - size) {
        return CUDA_ERROR_INVALID_VALUE;
      }

      parameters[j] = paramData + param->dataOffset;
    }

    result = queue->driver->launchKernel(pipeline->function,
                                         dispatch->grid[0],
                                         dispatch->grid[1],
                                         dispatch->grid[2],
                                         dispatch->block[0],
                                         dispatch->block[1],
                                         dispatch->block[2],
                                         0u,
                                         queue->stream,
                                         pipeline->paramCount > 0u ? parameters : NULL,
                                         NULL);

    if (result != CUDA_SUCCESS) {
      break;
    }
  }

  return result;
}

static void
cuda__queuePending(GPUQueueCuda *queue, GPUCommandCuda *command) {
  command->next    = NULL;
  command->pending = true;

  if (queue->pendingTail) {
    queue->pendingTail->next = command;
  } else {
    queue->pendingHead = command;
  }

  queue->pendingTail = command;
  queue->pendingCount++;
}

static void
cuda__finishCommands(uint32_t count, GPUCommandBuffer *const *commands) {
  uint32_t i;

  for (i = 0u; i < count; i++) {
    if (commands[i]) {
      finishCommandBuffer(commands[i], cuda_recycleCommand);
    }
  }
}

static bool
cuda__validSemaphores(GPUQueueCuda                  *queue,
                      const GPUQueueSemaphoreWait   *waits,
                      uint32_t                       waitCount,
                      const GPUQueueSemaphoreSignal *signals,
                      uint32_t                       signalCount) {
  GPUSemaphoreCuda *native;
  uint32_t          i;

  for (i = 0u; i < waitCount; i++) {
    native = waits[i].semaphore->_priv;

    if (!native || native->driver != queue->driver || !native->semaphore) {
      return false;
    }
  }

  for (i = 0u; i < signalCount; i++) {
    native = signals[i].semaphore->_priv;

    if (!native || native->driver != queue->driver || !native->semaphore) {
      return false;
    }
  }

  return true;
}

static CUresult
cuda__waitSemaphores(GPUQueueCuda                *queue,
                     const GPUQueueSemaphoreWait *waits,
                     uint32_t                     count) {
  CUDAExternalSemaphoreWaitParams params[CUDA_SEMAPHORE_BATCH_COUNT];
  CUexternalSemaphore             native[CUDA_SEMAPHORE_BATCH_COUNT];
  GPUSemaphoreCuda               *semaphore;
  CUresult                        result;
  uint32_t                        offset;
  uint32_t                        i;

  for (offset = 0u; offset < count;) {
    uint32_t batchCount;

    batchCount = count - offset;

    if (batchCount > CUDA_SEMAPHORE_BATCH_COUNT) {
      batchCount = CUDA_SEMAPHORE_BATCH_COUNT;
    }

    memset(params, 0, batchCount * sizeof(params[0]));

    for (i = 0u; i < batchCount; i++) {
      semaphore                    = waits[offset + i].semaphore->_priv;
      native[i]                    = semaphore->semaphore;
      params[i].params.fence.value = waits[offset + i].value;
    }

    result = queue->driver->waitExternalSemaphoresAsync(native,
                                                        params,
                                                        batchCount,
                                                        queue->stream);

    if (result != CUDA_SUCCESS) {
      return result;
    }

    offset += batchCount;
  }

  return CUDA_SUCCESS;
}

static CUresult
cuda__signalSemaphores(GPUQueueCuda                  *queue,
                       const GPUQueueSemaphoreSignal *signals,
                       uint32_t                       count) {
  CUDAExternalSemaphoreSignalParams params[CUDA_SEMAPHORE_BATCH_COUNT];
  CUexternalSemaphore               native[CUDA_SEMAPHORE_BATCH_COUNT];
  GPUSemaphoreCuda                 *semaphore;
  CUresult                          result;
  uint32_t                          offset;
  uint32_t                          i;

  for (offset = 0u; offset < count;) {
    uint32_t batchCount;

    batchCount = count - offset;

    if (batchCount > CUDA_SEMAPHORE_BATCH_COUNT) {
      batchCount = CUDA_SEMAPHORE_BATCH_COUNT;
    }

    memset(params, 0, batchCount * sizeof(params[0]));

    for (i = 0u; i < batchCount; i++) {
      semaphore                    = signals[offset + i].semaphore->_priv;
      native[i]                    = semaphore->semaphore;
      params[i].params.fence.value = signals[offset + i].value;
    }

    result = queue->driver->signalExternalSemaphoresAsync(native,
                                                          params,
                                                          batchCount,
                                                          queue->stream);

    if (result != CUDA_SUCCESS) {
      return result;
    }

    offset += batchCount;
  }

  return CUDA_SUCCESS;
}

static GPUQueue*
cuda_getQueue(GPUDevice *__restrict device,
              GPUQueueFlagBits      bits,
              uint32_t              index) {
  GPUDeviceCuda *native;

  native = cuda_device(device);

  if (!native || bits != GPU_QUEUE_COMPUTE_BIT || index >= native->queueCount) {
    return NULL;
  }

  return &native->queues[index].queue;
}

static GPUCommandBuffer*
cuda_newCommandBuffer(GPUQueue         *__restrict queue,
                      const char       *__restrict label,
                      void             *__restrict sender,
                      GPUCommandBufferCompletionFn onComplete) {
  GPUCommandCuda *command;
  GPUQueueCuda   *native;

  GPU__UNUSED(label);
  native = cuda_queue(queue);

  if (!native) {
    return NULL;
  }

  cuda_queueLock(native);
  command = native->freeCommands;

  if (command) {
    native->freeCommands = command->next;
  }

  cuda_queueUnlock(native);

  if (!command) {
    if (cuda_push(native->driver, native->context) != GPU_OK) {
      return NULL;
    }

    command = cuda_createCommand(native);
    cuda_pop(native->driver);

    if (!command) {
      return NULL;
    }

    deviceRecordHotPathAlloc(queue->_device,
                             sizeof(*command) + CUDA_INITIAL_DISPATCH_CAPACITY * sizeof(*command->dispatches));
  }

  memset(&command->command, 0, sizeof(command->command));
  memset(&command->compute, 0, sizeof(command->compute));
  command->command._priv             = command;
  command->command._queue            = queue;
  command->command._onCompleteSender = sender;
  command->command._onComplete       = onComplete;
  command->pipeline                  = NULL;
  command->dispatchCount             = 0u;
  command->paramDataCount            = 0u;
  command->recordResult              = GPU_OK;
  command->pending                   = false;
  command->next                      = NULL;
  memset(command->boundParamMask, 0, sizeof(command->boundParamMask));

  return &command->command;
}

static void
cuda_commandBufferOnComplete(GPUCommandBuffer *__restrict cmdb,
                             void             *__restrict sender,
                             GPUCommandBufferCompletionFn onComplete) {
  if (!cmdb) {
    return;
  }

  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = onComplete;
}

static GPUResult
cuda_discardCommandBuffer(GPUCommandBuffer *__restrict cmdb) {
  if (!cuda_command(cmdb)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  discardCommandBufferState(cmdb, cuda_recycleCommand);

  return GPU_OK;
}

static GPUResult
cuda_commitCommandBuffer(GPUCommandBuffer *__restrict cmdb) {
  GPUCommandCuda *command;
  GPUQueueCuda   *queue;
  CUresult        result;
  GPUResult       recordResult;

  command = cuda_command(cmdb);
  queue   = command ? command->owner : NULL;

  if (!command || !queue) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (command->recordResult != GPU_OK) {
    recordResult = command->recordResult;
    finishCommandBuffer(cmdb, cuda_recycleCommand);
    return recordResult;
  }

  cuda_queueLock(queue);

  if (cuda_push(queue->driver, queue->context) != GPU_OK) {
    cuda_queueUnlock(queue);
    finishCommandBuffer(cmdb, cuda_recycleCommand);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = cuda__launchCommand(queue, command);

  if (result == CUDA_SUCCESS) {
    result = queue->driver->eventRecord(command->completion, queue->stream);
  }

  if (result != CUDA_SUCCESS) {
    (void)queue->driver->streamSynchronize(queue->stream);
    cuda_pop(queue->driver);
    cuda_queueUnlock(queue);
    cuda_report(queue->queue._device, result, "command submission");
    finishCommandBuffer(cmdb, cuda_recycleCommand);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cuda_pop(queue->driver);

  cuda__queuePending(queue, command);
  cuda_queueSignal(queue);
  cuda_queueUnlock(queue);

  return GPU_OK;
}

static GPUResult
cuda_createSemaphore(GPUDevice                    *device,
                     const GPUSemaphoreCreateInfo *info,
                     GPUSemaphore                 *semaphore) {
  GPU__UNUSED(device);
  GPU__UNUSED(info);
  GPU__UNUSED(semaphore);
  return GPU_ERROR_UNSUPPORTED;
}

static void
cuda_destroySemaphore(GPUSemaphore *semaphore) {
  GPUSemaphoreCuda *native;
  GPUDeviceCuda    *device;

  native = semaphore ? semaphore->_priv : NULL;
  device = semaphore ? cuda_device(semaphore->_device) : NULL;

  if (native && native->semaphore && native->driver && device
      && cuda_push(native->driver, device->context) == GPU_OK) {
    if (native->driver->destroyExternalSemaphore) {
      (void)native->driver->destroyExternalSemaphore(native->semaphore);
    }

    cuda_pop(native->driver);
  }

  free(native);

  if (semaphore) {
    semaphore->_priv = NULL;
  }
}

static GPUResult
cuda_submitEx(GPUQueue                   *queueHandle,
              const GPUQueueSubmitExInfo *info) {
  GPUCommandCuda *commands[CUDA_SUBMIT_STACK_COUNT];
  GPUQueueCuda   *queue;
  CUresult        result;
  GPUResult       recordResult;
  uint32_t        i;

  queue = cuda_queue(queueHandle);

  if (!queue || !info) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (info->commandBufferCount == 0u
      || info->commandBufferCount > CUDA_SUBMIT_STACK_COUNT
      || info->waitCount > CUDA_SUBMIT_STACK_COUNT
      || info->signalCount > CUDA_SUBMIT_STACK_COUNT) {
    cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
    return GPU_ERROR_UNSUPPORTED;
  }

  if ((info->waitCount > 0u
       && !queue->driver->waitExternalSemaphoresAsync)
      || (info->signalCount > 0u
          && !queue->driver->signalExternalSemaphoresAsync)) {
    cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
    return GPU_ERROR_UNSUPPORTED;
  }

  for (i = 0u; i < info->commandBufferCount; i++) {
    commands[i] = cuda_command(info->ppCommandBuffers[i]);

    if (!commands[i] || commands[i]->owner != queue) {
      cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    if (commands[i]->recordResult != GPU_OK) {
      recordResult = commands[i]->recordResult;
      cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
      return recordResult;
    }
  }

  if (!cuda__validSemaphores(queue,
                             info->pWaits,
                             info->waitCount,
                             info->pSignals,
                             info->signalCount)) {
    cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cuda_queueLock(queue);

  if (cuda_push(queue->driver, queue->context) != GPU_OK) {
    cuda_queueUnlock(queue);
    cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = info->waitCount > 0u ? cuda__waitSemaphores(queue, info->pWaits, info->waitCount) : CUDA_SUCCESS;

  for (i = 0u; i < info->commandBufferCount && result == CUDA_SUCCESS; i++) {
    result = cuda__launchCommand(queue, commands[i]);
  }

  if (result == CUDA_SUCCESS && info->signalCount > 0u) {
    result = cuda__signalSemaphores(queue,
                                    info->pSignals,
                                    info->signalCount);
  }

  for (i = 0u; i < info->commandBufferCount && result == CUDA_SUCCESS; i++) {
    result = queue->driver->eventRecord(commands[i]->completion, queue->stream);
  }

  if (result != CUDA_SUCCESS) {
    (void)queue->driver->streamSynchronize(queue->stream);
    cuda_pop(queue->driver);
    cuda_queueUnlock(queue);
    cuda_report(queueHandle->_device, result, "advanced queue submission");
    cuda__finishCommands(info->commandBufferCount, info->ppCommandBuffers);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  cuda_pop(queue->driver);

  for (i = 0u; i < info->commandBufferCount; i++) {
    cuda__queuePending(queue, commands[i]);
  }

  cuda_queueSignal(queue);
  cuda_queueUnlock(queue);

  return GPU_OK;
}

void
cuda_initQueue(GPUCommandQueueApi *api) {
  api->getCommandQueue         = cuda_getQueue;
  api->newCommandBuffer        = cuda_newCommandBuffer;
  api->commandBufferOnComplete = cuda_commandBufferOnComplete;
  api->discard                 = cuda_discardCommandBuffer;
  api->commit                  = cuda_commitCommandBuffer;
  api->createSemaphore         = cuda_createSemaphore;
  api->destroySemaphore        = cuda_destroySemaphore;
  api->submitEx                = cuda_submitEx;
}
