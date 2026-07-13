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

#include <d3d12sdklayers.h>

enum {
  DX12_COMPLETION_STACK_SIZE = 64u * 1024u
};

static void
dx12__queueLock(GPUCommandQueueDX12 *queue) {
  EnterCriticalSection(&queue->poolLock);
}

static void
dx12__queueUnlock(GPUCommandQueueDX12 *queue) {
  LeaveCriticalSection(&queue->poolLock);
}

static void
dx12__queueSignal(GPUCommandQueueDX12 *queue) {
  WakeConditionVariable(&queue->pendingCondition);
}

static void
dx12__queueBroadcast(GPUCommandQueueDX12 *queue) {
  WakeAllConditionVariable(&queue->pendingCondition);
}

static void
dx12__queueWait(GPUCommandQueueDX12 *queue) {
  SleepConditionVariableCS(&queue->pendingCondition,
                           &queue->poolLock,
                           INFINITE);
}

#if GPU_BUILD_WITH_VALIDATION
static void
dx12__logQueueError(GPUCommandQueueDX12 *queue,
                    const char          *operation,
                    HRESULT             result) {
  GPUDevice *device;

  device = queue && queue->queue ? queue->queue->_device : NULL;
  if (device && device->runtimeConfig.enableVerboseLogs) {
    fprintf(stderr,
            "GPU Direct3D 12 %s failed: 0x%08lx\n",
            operation,
            (unsigned long)result);
  }
}

static void
dx12__logDebugMessages(GPUCommandQueueDX12 *queue) {
  GPUDeviceDX12   *device;
  ID3D12InfoQueue *infoQueue;
  D3D12_MESSAGE   *message;
  UINT64           messageCount;
  UINT64           firstMessage;

  device    = queue && queue->queue && queue->queue->_device
                ? queue->queue->_device->_priv
                : NULL;
  infoQueue = NULL;
  if (!device || !device->d3dDevice ||
      !queue->queue->_device->runtimeConfig.enableVerboseLogs ||
      FAILED(device->d3dDevice->lpVtbl->QueryInterface(
        device->d3dDevice,
        &IID_ID3D12InfoQueue,
        (void **)&infoQueue
      ))) {
    return;
  }

  messageCount = infoQueue->lpVtbl->GetNumStoredMessages(infoQueue);
  firstMessage = messageCount > 16u ? messageCount - 16u : 0u;
  for (UINT64 i = firstMessage; i < messageCount; i++) {
    SIZE_T messageBytes;

    messageBytes = 0u;
    if (FAILED(infoQueue->lpVtbl->GetMessage(infoQueue,
                                             i,
                                             NULL,
                                             &messageBytes)) ||
        messageBytes == 0u || !(message = malloc(messageBytes))) {
      continue;
    }
    if (SUCCEEDED(infoQueue->lpVtbl->GetMessage(infoQueue,
                                                i,
                                                message,
                                                &messageBytes))) {
      fprintf(stderr, "GPU Direct3D 12: %s\n", message->pDescription);
    }
    free(message);
  }
  infoQueue->lpVtbl->ClearStoredMessages(infoQueue);
  infoQueue->lpVtbl->Release(infoQueue);
}
#else
#  define dx12__logQueueError(queue, operation, result) ((void)0)
#  define dx12__logDebugMessages(queue)                 ((void)0)
#endif

static void
dx12__recycleCommandBuffer(GPUCommandBuffer *cmdb) {
  GPUCommandBufferDX12 *native;
  GPUCommandQueueDX12  *queue;

  native = cmdb ? cmdb->_priv : NULL;
  queue  = native ? native->owner : NULL;
  if (!native || !queue) {
    return;
  }

  dx12__queueLock(queue);
  native->poolNext    = queue->freeCommands;
  queue->freeCommands = native;
  dx12__queueUnlock(queue);
}

static GPUCommandBufferDX12*
dx12__takePendingCommand(GPUCommandQueueDX12 *queue) {
  GPUCommandBufferDX12 *native;

  dx12__queueLock(queue);
  while (!queue->stopping && !queue->pendingHead) {
    dx12__queueWait(queue);
  }

  native = queue->pendingHead;
  if (native) {
    queue->pendingHead = native->pendingNext;
    if (!queue->pendingHead) {
      queue->pendingTail = NULL;
    }
    native->pendingNext = NULL;
  }
  dx12__queueUnlock(queue);
  return native;
}

static bool
dx12__waitForFence(GPUCommandQueueDX12 *queue, UINT64 value) {
  UINT64  completedValue;
  HRESULT result;
  DWORD   waitResult;

  completedValue = queue->completionFence->lpVtbl->GetCompletedValue(
    queue->completionFence
  );
  if (completedValue == UINT64_MAX) {
    dx12__logQueueError(queue, "fence completion", DXGI_ERROR_DEVICE_REMOVED);
    return false;
  }
  if (completedValue >= value) {
    return true;
  }

  result = queue->completionFence->lpVtbl->SetEventOnCompletion(
    queue->completionFence,
    value,
    queue->completionEvent
  );
  if (FAILED(result)) {
    dx12__logQueueError(queue, "fence wait", result);
    return false;
  }

  waitResult = WaitForSingleObject(queue->completionEvent, INFINITE);
  if (waitResult != WAIT_OBJECT_0) {
    dx12__logQueueError(queue,
                        "completion event wait",
                        HRESULT_FROM_WIN32(GetLastError()));
    return false;
  }
  return true;
}

static DWORD WINAPI
dx12__completionMain(LPVOID context) {
  GPUCommandQueueDX12  *queue;
  GPUCommandBufferDX12 *native;
  GPUCommandBuffer     *cmdb;
  bool                  completed;

  queue = context;
  while ((native = dx12__takePendingCommand(queue))) {
    cmdb      = &native->commandBuffer;
    completed = dx12__waitForFence(queue, native->fenceValue);
    gpuFinishCommandBuffer(cmdb,
                           completed ? dx12__recycleCommandBuffer : NULL);
  }
  return 0;
}

static bool
dx12__startWorker(GPUCommandQueueDX12 *queue) {
  InitializeCriticalSection(&queue->poolLock);
  InitializeConditionVariable(&queue->pendingCondition);

  queue->completionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!queue->completionEvent) {
    DeleteCriticalSection(&queue->poolLock);
    return false;
  }

  queue->worker = CreateThread(NULL,
                               DX12_COMPLETION_STACK_SIZE,
                               dx12__completionMain,
                               queue,
                               0,
                               NULL);
  queue->workerStarted = queue->worker != NULL;
  if (!queue->workerStarted) {
    CloseHandle(queue->completionEvent);
    queue->completionEvent = NULL;
    DeleteCriticalSection(&queue->poolLock);
  }
  return queue->workerStarted;
}

static void
dx12__stopWorker(GPUCommandQueueDX12 *queue) {
  if (!queue || !queue->workerStarted) {
    return;
  }

  dx12__queueLock(queue);
  queue->stopping = true;
  dx12__queueBroadcast(queue);
  dx12__queueUnlock(queue);

  WaitForSingleObject(queue->worker, INFINITE);
  CloseHandle(queue->worker);
  CloseHandle(queue->completionEvent);
  DeleteCriticalSection(&queue->poolLock);
  queue->worker          = NULL;
  queue->completionEvent = NULL;
  queue->workerStarted   = false;
}

static bool
dx12__queueType(GPUQueueFlagBits bits, D3D12_COMMAND_LIST_TYPE *outType) {
  GPUQueueFlagBits knownBits;

  knownBits = GPU_QUEUE_GRAPHICS_BIT |
              GPU_QUEUE_COMPUTE_BIT |
              GPU_QUEUE_TRANSFER_BIT;
  if (!outType || bits == 0u || (bits & ~knownBits) != 0u) {
    return false;
  }

  if (bits & GPU_QUEUE_GRAPHICS_BIT) {
    *outType = D3D12_COMMAND_LIST_TYPE_DIRECT;
  } else if (bits & GPU_QUEUE_COMPUTE_BIT) {
    *outType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  } else {
    *outType = D3D12_COMMAND_LIST_TYPE_COPY;
  }
  return true;
}

GPU_HIDE
GPUCommandQueue*
dx12_createCommandQueue(GPUDevice *device, GPUQueueFlagBits bits) {
  GPUCommandQueueDX12      *native;
  GPUCommandQueue          *queue;
  GPUDeviceDX12            *deviceDX12;
  D3D12_COMMAND_QUEUE_DESC  queueDesc = {0};
  D3D12_COMMAND_LIST_TYPE   type;
  HRESULT                   result;

  if (!device || !device->_priv || !dx12__queueType(bits, &type)) {
    return NULL;
  }

  queue  = calloc(1, sizeof(*queue));
  native = calloc(1, sizeof(*native));
  if (!queue || !native) {
    free(native);
    free(queue);
    return NULL;
  }

  deviceDX12             = device->_priv;
  queue->_priv           = native;
  queue->_device         = device;
  queue->bits            = bits;
  native->queue          = queue;
  native->type           = type;
  native->nextFenceValue = 1u;

  queueDesc.Type  = type;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommandQueue(
    deviceDX12->d3dDevice,
    &queueDesc,
    &IID_ID3D12CommandQueue,
    (void **)&native->commandQueue
  );
  if (FAILED(result)) {
    goto fail;
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateFence(
    deviceDX12->d3dDevice,
    0u,
    D3D12_FENCE_FLAG_NONE,
    &IID_ID3D12Fence,
    (void **)&native->completionFence
  );
  if (FAILED(result) || !dx12__startWorker(native)) {
    goto fail;
  }

  return queue;

fail:
  if (native->completionFence) {
    native->completionFence->lpVtbl->Release(native->completionFence);
  }
  if (native->commandQueue) {
    native->commandQueue->lpVtbl->Release(native->commandQueue);
  }
  free(native);
  free(queue);
  return NULL;
}

GPU_HIDE
void
dx12_destroyCommandQueue(GPUCommandQueue *queue) {
  GPUCommandQueueDX12  *native;
  GPUCommandBufferDX12 *command;
  GPUCommandBufferDX12 *next;

  if (!queue) {
    return;
  }

  native = queue->_priv;
  if (native) {
    dx12__stopWorker(native);
    command = native->commands;
    while (command) {
      next = command->next;
      if (command->commandList7) {
        command->commandList7->lpVtbl->Release(command->commandList7);
      }
      if (command->commandList) {
        command->commandList->lpVtbl->Release(command->commandList);
      }
      if (command->allocator) {
        command->allocator->lpVtbl->Release(command->allocator);
      }
      free(command);
      command = next;
    }
    if (native->completionFence) {
      native->completionFence->lpVtbl->Release(native->completionFence);
    }
    if (native->commandQueue) {
      native->commandQueue->lpVtbl->Release(native->commandQueue);
    }
    free(native);
  }
  free(queue);
}

GPU_HIDE
bool
dx12_waitCommandQueueIdle(GPUCommandQueueDX12 *queue) {
  HANDLE  event;
  UINT64  fenceValue;
  HRESULT result;
  DWORD   waitResult;

  if (!queue || !queue->commandQueue || !queue->completionFence) {
    return false;
  }

  event = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!event) {
    return false;
  }

  fenceValue = queue->nextFenceValue++;
  result = queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                queue->completionFence,
                                                fenceValue);
  if (SUCCEEDED(result)) {
    result = queue->completionFence->lpVtbl->SetEventOnCompletion(
      queue->completionFence,
      fenceValue,
      event
    );
  }
  waitResult = SUCCEEDED(result)
                 ? WaitForSingleObject(event, INFINITE)
                 : WAIT_FAILED;
  CloseHandle(event);
  return SUCCEEDED(result) && waitResult == WAIT_OBJECT_0;
}

GPU_HIDE
GPUCommandQueue*
dx12_getCommandQueue(GPUDevice * __restrict device,
                     GPUQueueFlagBits         bits,
                     uint32_t                 index) {
  GPUCommandQueue *queue;
  GPUDeviceDX12   *deviceDX12;
  uint32_t         matchIndex;
  uint32_t         i;

  if (!device || !device->_priv || bits == 0u) {
    return NULL;
  }

  deviceDX12 = device->_priv;
  matchIndex = 0u;
  for (i = 0u; i < deviceDX12->nCreatedQueues; i++) {
    queue = deviceDX12->createdQueues[i];
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
dx12_newCommandQueue(GPUDevice * __restrict device) {
  return dx12_getCommandQueue(device,
                              GPU_QUEUE_GRAPHICS_BIT |
                              GPU_QUEUE_COMPUTE_BIT,
                              0u);
}

static GPUCommandBufferDX12*
dx12__createCommandBufferState(GPUCommandQueue *queue) {
  GPUCommandQueueDX12  *queueDX12;
  GPUCommandBufferDX12 *native;
  GPUCommandBuffer     *cmdb;
  GPUDeviceDX12        *deviceDX12;
  HRESULT               result;

  queueDX12  = queue->_priv;
  deviceDX12 = queue->_device->_priv;
  native     = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  gpuDeviceRecordHotPathAlloc(queue->_device, sizeof(*native));

  result = deviceDX12->d3dDevice->lpVtbl->CreateCommandAllocator(
    deviceDX12->d3dDevice,
    queueDX12->type,
    &IID_ID3D12CommandAllocator,
    (void **)&native->allocator
  );
  if (FAILED(result)) {
    goto fail;
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateCommandList(
    deviceDX12->d3dDevice,
    0u,
    queueDX12->type,
    native->allocator,
    NULL,
    &IID_ID3D12GraphicsCommandList,
    (void **)&native->commandList
  );
  if (FAILED(result)) {
    goto fail;
  }

  if (deviceDX12->enhancedBarriers) {
    (void)native->commandList->lpVtbl->QueryInterface(
      native->commandList,
      &IID_ID3D12GraphicsCommandList7,
      (void **)&native->commandList7
    );
  }

  result = native->commandList->lpVtbl->Close(native->commandList);
  if (FAILED(result)) {
    goto fail;
  }

  cmdb          = &native->commandBuffer;
  native->owner = queueDX12;
  cmdb->_priv   = native;
  cmdb->_queue  = queue;

  dx12__queueLock(queueDX12);
  native->next          = queueDX12->commands;
  queueDX12->commands   = native;
  dx12__queueUnlock(queueDX12);
  return native;

fail:
  if (native->commandList7) {
    native->commandList7->lpVtbl->Release(native->commandList7);
  }
  if (native->commandList) {
    native->commandList->lpVtbl->Release(native->commandList);
  }
  if (native->allocator) {
    native->allocator->lpVtbl->Release(native->allocator);
  }
  gpuDeviceRecordHotPathFree(queue->_device, sizeof(*native));
  free(native);
  return NULL;
}

static GPUCommandBufferDX12*
dx12__takeCommandBufferState(GPUCommandQueue *queue) {
  GPUCommandQueueDX12  *queueDX12;
  GPUCommandBufferDX12 *native;

  queueDX12 = queue->_priv;
  dx12__queueLock(queueDX12);
  native = queueDX12->freeCommands;
  if (native) {
    queueDX12->freeCommands = native->poolNext;
    native->poolNext        = NULL;
  }
  dx12__queueUnlock(queueDX12);
  return native ? native : dx12__createCommandBufferState(queue);
}

GPU_HIDE
GPUCommandBuffer*
dx12_newCommandBuffer(GPUCommandQueue  * __restrict queue,
                      const char       * __restrict label,
                      void             * __restrict sender,
                      GPUCommandBufferCompletionFn  oncomplete) {
  GPUCommandBufferDX12 *native;
  GPUCommandBuffer     *cmdb;
  HRESULT               result;

  GPU__UNUSED(label);
  if (!queue || !queue->_priv || !queue->_device) {
    return NULL;
  }

  native = dx12__takeCommandBufferState(queue);
  if (!native) {
    return NULL;
  }

  result = native->allocator->lpVtbl->Reset(native->allocator);
  if (SUCCEEDED(result)) {
    result = native->commandList->lpVtbl->Reset(native->commandList,
                                                 native->allocator,
                                                 NULL);
  }
  if (FAILED(result)) {
    dx12__logQueueError(native->owner, "command buffer reset", result);
    dx12__recycleCommandBuffer(&native->commandBuffer);
    return NULL;
  }

  cmdb = &native->commandBuffer;
  memset(cmdb, 0, sizeof(*cmdb));
  memset(&native->renderPassDesc, 0, sizeof(native->renderPassDesc));
  memset(&native->renderPass, 0, sizeof(native->renderPass));
  memset(&native->renderEncoder, 0, sizeof(native->renderEncoder));
  memset(&native->renderState, 0, sizeof(native->renderState));
  memset(&native->computeEncoder, 0, sizeof(native->computeEncoder));
  memset(&native->computeState, 0, sizeof(native->computeState));
  memset(&native->copyEncoder, 0, sizeof(native->copyEncoder));
  native->presentSwapchain  = NULL;
  native->pendingNext       = NULL;
  native->fenceValue        = 0u;
  cmdb->_priv               = native;
  cmdb->_queue              = queue;
  cmdb->_onCompleteSender   = sender;
  cmdb->_onComplete         = oncomplete;
  return cmdb;
}

GPU_HIDE
void
dx12_commandBufferOnComplete(GPUCommandBuffer * __restrict cmdb,
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
dx12_commitCommandBuffer(GPUCommandBuffer * __restrict cmdb) {
  GPUCommandBufferDX12 *native;
  GPUCommandQueueDX12  *queue;
  GPUSwapChainDX12     *swapchain;
  ID3D12CommandList    *commandLists[1];
  HRESULT               result;
  HRESULT               presentResult;
  GPUResult              commitResult;
  UINT64                fenceValue;

  native = cmdb ? cmdb->_priv : NULL;
  queue  = native ? native->owner : NULL;
  if (!native || !queue || !native->commandList) {
    gpuFinishCommandBuffer(cmdb, dx12__recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = native->commandList->lpVtbl->Close(native->commandList);
  if (FAILED(result)) {
    dx12__logQueueError(queue, "command list close", result);
    dx12__logDebugMessages(queue);
    gpuFinishCommandBuffer(cmdb, dx12__recycleCommandBuffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  commandLists[0] = (ID3D12CommandList *)native->commandList;
  queue->commandQueue->lpVtbl->ExecuteCommandLists(queue->commandQueue,
                                                    1u,
                                                    commandLists);

  commitResult = GPU_OK;
  swapchain    = native->presentSwapchain;
  if (swapchain && swapchain->swapChain) {
    presentResult = swapchain->swapChain->lpVtbl->Present(
      swapchain->swapChain,
      swapchain->syncInterval,
      swapchain->presentFlags
    );
    if (FAILED(presentResult)) {
      dx12__logQueueError(queue, "present", presentResult);
      commitResult = GPU_ERROR_BACKEND_FAILURE;
    }
  }
  native->presentSwapchain = NULL;

  fenceValue = queue->nextFenceValue++;
  result = queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                queue->completionFence,
                                                fenceValue);
  if (FAILED(result)) {
    dx12__logQueueError(queue, "queue signal", result);
    gpuFinishCommandBuffer(cmdb, NULL);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->fenceValue = fenceValue;
  dx12__queueLock(queue);
  if (queue->pendingTail) {
    queue->pendingTail->pendingNext = native;
  } else {
    queue->pendingHead = native;
  }
  queue->pendingTail = native;
  dx12__queueSignal(queue);
  dx12__queueUnlock(queue);
  return commitResult;
}

GPU_HIDE
void
dx12_initCmdQue(GPUApiCommandQueue *api) {
  api->newCommandQueue         = dx12_newCommandQueue;
  api->getCommandQueue         = dx12_getCommandQueue;
  api->newCommandBuffer        = dx12_newCommandBuffer;
  api->commandBufferOnComplete = dx12_commandBufferOnComplete;
  api->commit                  = dx12_commitCommandBuffer;
}
