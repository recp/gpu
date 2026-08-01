/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUQueue *
cuda_getQueue(GPUDevice *device, GPUQueueFlagBits bits, uint32_t index) {
  GPUDeviceCuda *native;

  native = cuda_device(device);
  if (!native || bits != GPU_QUEUE_COMPUTE_BIT || index >= native->queueCount) {
    return NULL;
  }
  return &native->queues[index].queue;
}

static GPUCommandBuffer *
cuda_newCommandBuffer(GPUQueue                    *queue,
                      const char                  *label,
                      void                        *sender,
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
    gpuDeviceRecordHotPathAlloc(queue->_device,
                                sizeof(*command) +
                                  CUDA_INITIAL_DISPATCH_CAPACITY *
                                    sizeof(*command->dispatches));
  }

  memset(&command->command, 0, sizeof(command->command));
  memset(&command->compute, 0, sizeof(command->compute));
  command->command._priv             = command;
  command->command._queue            = queue;
  command->command._onCompleteSender = sender;
  command->command._onComplete       = onComplete;
  command->pipeline                  = NULL;
  command->buffer                    = NULL;
  command->bufferOffset              = 0u;
  command->dispatchCount             = 0u;
  command->recordResult              = GPU_OK;
  command->pending                   = false;
  command->next                      = NULL;
  return &command->command;
}

static void
cuda_commandBufferOnComplete(GPUCommandBuffer             *cmdb,
                             void                         *sender,
                             GPUCommandBufferCompletionFn  onComplete) {
  if (!cmdb) {
    return;
  }
  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = onComplete;
}

static GPUResult
cuda_discardCommandBuffer(GPUCommandBuffer *cmdb) {
  if (!cuda_command(cmdb)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  gpuDiscardCommandBufferState(cmdb, cuda_recycleCommand);
  return GPU_OK;
}

static GPUResult
cuda_commitCommandBuffer(GPUCommandBuffer *cmdb) {
  GPUCommandCuda *command;
  GPUQueueCuda   *queue;
  CUresult        result;

  command = cuda_command(cmdb);
  queue   = command ? command->owner : NULL;
  if (!command || !queue) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (command->recordResult != GPU_OK) {
    GPUResult recordResult;

    recordResult = command->recordResult;
    gpuFinishCommandBuffer(cmdb, cuda_recycleCommand);
    return recordResult;
  }

  cuda_queueLock(queue);
  if (cuda_push(queue->driver, queue->context) != GPU_OK) {
    cuda_queueUnlock(queue);
    gpuFinishCommandBuffer(cmdb, cuda_recycleCommand);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = CUDA_SUCCESS;
  for (uint32_t i = 0u; i < command->dispatchCount; i++) {
    GPUComputePipelineCuda *pipeline;
    GPUDispatchCuda        *dispatch;
    CUdeviceptr             buffer;
    void                   *parameters[1];

    dispatch      = &command->dispatches[i];
    pipeline      = dispatch->pipeline->_state;
    buffer        = dispatch->buffer;
    parameters[0] = &buffer;
    result = queue->driver->launchKernel(pipeline->function,
                                         dispatch->grid[0],
                                         dispatch->grid[1],
                                         dispatch->grid[2],
                                         dispatch->block[0],
                                         dispatch->block[1],
                                         dispatch->block[2],
                                         0u,
                                         queue->stream,
                                         parameters,
                                         NULL);
    if (result != CUDA_SUCCESS) {
      break;
    }
  }
  if (result == CUDA_SUCCESS) {
    result = queue->driver->eventRecord(command->completion, queue->stream);
  }
  if (result != CUDA_SUCCESS) {
    (void)queue->driver->streamSynchronize(queue->stream);
    cuda_pop(queue->driver);
    cuda_queueUnlock(queue);
    cuda_report(queue->queue._device, result, "command submission");
    gpuFinishCommandBuffer(cmdb, cuda_recycleCommand);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cuda_pop(queue->driver);

  command->next    = NULL;
  command->pending = true;
  if (queue->pendingTail) {
    queue->pendingTail->next = command;
  } else {
    queue->pendingHead = command;
  }
  queue->pendingTail = command;
  queue->pendingCount++;
  cuda_queueSignal(queue);
  cuda_queueUnlock(queue);
  return GPU_OK;
}

void
cuda_initQueue(GPUApiCommandQueue *api) {
  api->getCommandQueue        = cuda_getQueue;
  api->newCommandBuffer       = cuda_newCommandBuffer;
  api->commandBufferOnComplete = cuda_commandBufferOnComplete;
  api->discard                = cuda_discardCommandBuffer;
  api->commit                 = cuda_commitCommandBuffer;
}
