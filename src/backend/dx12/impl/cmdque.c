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

static void
dx12__waitForCompletions(GPUCommandQueueDX12 *queue) {
  dx12__queueLock(queue);
  while (queue->inFlightCount > 0u) {
    dx12__queueWait(queue);
  }
  dx12__queueUnlock(queue);
}

static GPUDevice *
dx12__queueDevice(GPUCommandQueueDX12 *queue) {
  return queue && queue->queue ? queue->queue->_device : NULL;
}

GPU_HIDE
GPUResult
dx12_getTimestampPeriod(GPUCommandQueue *queue,
                        double          *outNanosecondsPerTick) {
  GPUCommandQueueDX12 *native;
  UINT64               frequency;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->commandQueue || !outNanosecondsPerTick ||
      FAILED(native->commandQueue->lpVtbl->GetTimestampFrequency(
        native->commandQueue,
        &frequency
      )) || frequency == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outNanosecondsPerTick = 1000000000.0 / (double)frequency;
  return GPU_OK;
}

static bool
dx12__lostReason(GPUCommandQueueDX12 *queue,
                 HRESULT              result,
                 GPUDeviceLostReason *outReason) {
  GPUDeviceDX12 *native;
  GPUDevice     *device;
  HRESULT        removedReason;

  device = dx12__queueDevice(queue);
  native = device ? device->_priv : NULL;
  removedReason = result;
  if (result == DXGI_ERROR_DEVICE_REMOVED && native && native->d3dDevice) {
    removedReason = native->d3dDevice->lpVtbl->GetDeviceRemovedReason(
      native->d3dDevice
    );
    if (SUCCEEDED(removedReason)) {
      removedReason = result;
    }
  }

  switch (removedReason) {
    case DXGI_ERROR_DEVICE_REMOVED:
      *outReason = GPU_DEVICE_LOST_REASON_REMOVED;
      return true;
    case DXGI_ERROR_DEVICE_RESET:
      *outReason = GPU_DEVICE_LOST_REASON_RESET;
      return true;
    case DXGI_ERROR_DEVICE_HUNG:
      *outReason = GPU_DEVICE_LOST_REASON_HUNG;
      return true;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
      *outReason = GPU_DEVICE_LOST_REASON_DRIVER_ERROR;
      return true;
    default:
      return false;
  }
}

static void
dx12__reportQueueError(GPUCommandQueueDX12 *queue,
                       const char          *operation,
                       HRESULT             result) {
  GPUDeviceErrorType  type;
  GPUDeviceLostReason lostReason;
  GPUDevice          *device;
  GPUResult           gpuResult;
  char                message[128];

  device = dx12__queueDevice(queue);
  if (!device) {
    return;
  }

  type       = GPU_DEVICE_ERROR_BACKEND;
  lostReason = GPU_DEVICE_LOST_REASON_UNKNOWN;
  gpuResult  = GPU_ERROR_BACKEND_FAILURE;
  if (dx12__lostReason(queue, result, &lostReason)) {
    type = GPU_DEVICE_ERROR_LOST;
  } else if (result == E_OUTOFMEMORY) {
    type      = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
    gpuResult = GPU_ERROR_OUT_OF_MEMORY;
  }

  snprintf(message,
           sizeof(message),
           "Direct3D 12 %s failed: 0x%08lx",
           operation,
           (unsigned long)result);
  gpuDeviceReportError(device, type, lostReason, gpuResult, message);
}

#if GPU_BUILD_WITH_VALIDATION
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
#  define dx12__logDebugMessages(queue) ((void)0)
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
dx12__waitForFence(GPUCommandQueueDX12 *queue,
                   UINT64                value,
                   HANDLE                event) {
  UINT64  completedValue;
  HRESULT result;
  DWORD   waitResult;

  if (!queue || !queue->completionFence || !event || value == 0u) {
    return value == 0u;
  }

  completedValue = queue->completionFence->lpVtbl->GetCompletedValue(
    queue->completionFence
  );
  if (completedValue == UINT64_MAX) {
    dx12__reportQueueError(queue,
                           "fence completion",
                           DXGI_ERROR_DEVICE_REMOVED);
    return false;
  }
  if (completedValue >= value) {
    return true;
  }

  result = queue->completionFence->lpVtbl->SetEventOnCompletion(
    queue->completionFence,
    value,
    event
  );
  if (FAILED(result)) {
    dx12__reportQueueError(queue, "fence wait", result);
    return false;
  }

  waitResult = WaitForSingleObject(event, INFINITE);
  if (waitResult != WAIT_OBJECT_0) {
    dx12__reportQueueError(queue,
                           "completion event wait",
                           HRESULT_FROM_WIN32(GetLastError()));
    return false;
  }
  return true;
}

GPU_HIDE
bool
dx12_waitQueueFence(GPUCommandQueueDX12 *queue,
                    UINT64                value,
                    HANDLE                event) {
  bool finished;

  if (value == 0u) {
    return true;
  }
  if (!dx12__waitForFence(queue, value, event)) {
    return false;
  }

  dx12__queueLock(queue);
  while (!queue->stopping && queue->finishedFenceValue < value) {
    dx12__queueWait(queue);
  }
  finished = queue->finishedFenceValue >= value;
  dx12__queueUnlock(queue);
  return finished;
}

static DWORD WINAPI
dx12__completionMain(LPVOID context) {
  GPUCommandQueueDX12  *queue;
  GPUCommandBufferDX12 *native;
  GPUCommandBuffer     *cmdb;
  UINT64                fenceValue;
  bool                  completed;

  queue = context;
  while ((native = dx12__takePendingCommand(queue))) {
    cmdb       = &native->commandBuffer;
    fenceValue = native->fenceValue;
    completed  = dx12__waitForFence(queue,
                                    fenceValue,
                                    queue->completionEvent);
    gpuFinishCommandBuffer(cmdb,
                           completed ? dx12__recycleCommandBuffer : NULL);

    dx12__queueLock(queue);
    if (completed && fenceValue > queue->finishedFenceValue) {
      queue->finishedFenceValue = fenceValue;
    }
    if (queue->inFlightCount > 0u) {
      queue->inFlightCount--;
    }
    dx12__queueBroadcast(queue);
    dx12__queueUnlock(queue);
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

static ID3D12Resource *
dx12__createTransferStaging(GPUDeviceDX12 *device,
                            uint64_t        sizeBytes,
                            D3D12_HEAP_TYPE heapType) {
  ID3D12Resource        *resource;
  D3D12_HEAP_PROPERTIES  heap = {0};
  D3D12_RESOURCE_DESC    desc = {0};
  D3D12_RESOURCE_STATES  initialState;
  HRESULT                result;

  if (!device || !device->d3dDevice || sizeBytes == 0u ||
      (heapType != D3D12_HEAP_TYPE_UPLOAD &&
       heapType != D3D12_HEAP_TYPE_READBACK)) {
    return NULL;
  }

  resource                 = NULL;
  heap.Type                = heapType;
  heap.CreationNodeMask    = 1u;
  heap.VisibleNodeMask     = 1u;
  desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width               = sizeBytes;
  desc.Height              = 1u;
  desc.DepthOrArraySize    = 1u;
  desc.MipLevels           = 1u;
  desc.SampleDesc.Count    = 1u;
  desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  initialState = heapType == D3D12_HEAP_TYPE_UPLOAD
                   ? D3D12_RESOURCE_STATE_GENERIC_READ
                   : D3D12_RESOURCE_STATE_COPY_DEST;
  result = device->d3dDevice->lpVtbl->CreateCommittedResource(
    device->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    initialState,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  return SUCCEEDED(result) ? resource : NULL;
}

static uint64_t
dx12__transferCapacity(uint64_t sizeBytes) {
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
dx12__ensureTransferContext(GPUCommandQueueDX12 *queue,
                            GPUDeviceDX12       *device,
                            GPUTransferSlotDX12 *slot) {
  ID3D12CommandAllocator    *allocator;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Fence               *fence;
  HANDLE                     event;
  HRESULT                    result;

  if (!queue || !device || !device->d3dDevice || !slot) {
    return false;
  }
  if ((!queue->transferFence) != (!queue->transferEvent) ||
      (!slot->allocator) != (!slot->commandList)) {
    return false;
  }
  if (!queue->transferFence) {
    fence = NULL;
    event = NULL;
    result = device->d3dDevice->lpVtbl->CreateFence(device->d3dDevice,
                                                     0u,
                                                     D3D12_FENCE_FLAG_NONE,
                                                     &IID_ID3D12Fence,
                                                     (void **)&fence);
    if (FAILED(result) || !(event = CreateEventW(NULL, FALSE, FALSE, NULL))) {
      if (fence) {
        fence->lpVtbl->Release(fence);
      }
      return false;
    }
    queue->transferFence = fence;
    queue->transferEvent = event;
  }
  if (slot->allocator) {
    return true;
  }

  allocator   = NULL;
  commandList = NULL;
  result = device->d3dDevice->lpVtbl->CreateCommandAllocator(
    device->d3dDevice,
    queue->type,
    &IID_ID3D12CommandAllocator,
    (void **)&allocator
  );
  if (FAILED(result)) {
    goto fail;
  }
  result = device->d3dDevice->lpVtbl->CreateCommandList(
    device->d3dDevice,
    0u,
    queue->type,
    allocator,
    NULL,
    &IID_ID3D12GraphicsCommandList,
    (void **)&commandList
  );
  if (FAILED(result) || FAILED(commandList->lpVtbl->Close(commandList))) {
    goto fail;
  }

  slot->allocator   = allocator;
  slot->commandList = commandList;
  return true;

fail:
  if (commandList) {
    commandList->lpVtbl->Release(commandList);
  }
  if (allocator) {
    allocator->lpVtbl->Release(allocator);
  }
  return false;
}

static bool
dx12__ensureTransferStaging(GPUCommandQueueDX12 *queue,
                            GPUDeviceDX12       *device,
                            GPUTransferSlotDX12 *slot,
                            uint64_t              sizeBytes,
                            D3D12_HEAP_TYPE       heapType) {
  ID3D12Resource *resource;
  void           *mapped;
  D3D12_RANGE     readRange = {0};
  uint64_t        capacity;
  bool            upload;

  if (!queue || !device || !slot || sizeBytes == 0u ||
      (heapType != D3D12_HEAP_TYPE_UPLOAD &&
       heapType != D3D12_HEAP_TYPE_READBACK)) {
    return false;
  }

  upload = heapType == D3D12_HEAP_TYPE_UPLOAD;
  if (upload && slot->uploadStaging &&
      slot->uploadCapacity >= sizeBytes) {
    return slot->uploadMapped != NULL;
  }
  if (!upload && queue->readbackStaging &&
      queue->readbackCapacity >= sizeBytes) {
    return true;
  }

  capacity = dx12__transferCapacity(sizeBytes);
  resource = dx12__createTransferStaging(device, capacity, heapType);
  mapped   = NULL;
  if (!resource ||
      (upload &&
       (FAILED(resource->lpVtbl->Map(resource, 0u, &readRange, &mapped)) ||
        !mapped))) {
    if (resource) {
      resource->lpVtbl->Release(resource);
    }
    return false;
  }

  if (upload) {
    if (slot->uploadStaging) {
      slot->uploadStaging->lpVtbl->Unmap(slot->uploadStaging, 0u, NULL);
      slot->uploadStaging->lpVtbl->Release(slot->uploadStaging);
    }
    slot->uploadStaging  = resource;
    slot->uploadMapped   = mapped;
    slot->uploadCapacity = capacity;
  } else {
    if (queue->readbackStaging) {
      queue->readbackStaging->lpVtbl->Release(queue->readbackStaging);
    }
    queue->readbackStaging  = resource;
    queue->readbackCapacity = capacity;
  }
  return true;
}

static GPUResult
dx12__waitTransfer(GPUCommandQueue     *queue,
                   GPUTransferSlotDX12 *slot,
                   bool                 countStall) {
  GPUCommandQueueDX12 *native;
  GPUDeviceDX12       *device;
  UINT64               completedValue;
  HRESULT              result;
  DWORD                waitResult;

  native = queue ? queue->_priv : NULL;
  device = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!native || !device || !device->d3dDevice || !slot) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!slot->pending) {
    return GPU_OK;
  }
  if (!native->transferFence || !native->transferEvent) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  completedValue = native->transferFence->lpVtbl->GetCompletedValue(
    native->transferFence
  );
  if (completedValue == UINT64_MAX) {
    result = device->d3dDevice->lpVtbl->GetDeviceRemovedReason(
      device->d3dDevice
    );
    dx12__reportQueueError(native, "wait for transfer", result);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (completedValue < slot->fenceValue) {
    result = native->transferFence->lpVtbl->SetEventOnCompletion(
      native->transferFence,
      slot->fenceValue,
      native->transferEvent
    );
    if (FAILED(result)) {
      dx12__reportQueueError(native, "wait for transfer", result);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (countStall) {
      queue->_device->allocatorStats.uploadStallCount++;
    }
    waitResult = WaitForSingleObject(native->transferEvent, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
      result = waitResult == WAIT_FAILED
                 ? HRESULT_FROM_WIN32(GetLastError())
                 : E_FAIL;
      dx12__reportQueueError(native, "wait for transfer", result);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  slot->pending = false;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_beginTransfer(GPUCommandQueue             *queue,
                   D3D12_HEAP_TYPE              heapType,
                   uint64_t                     stagingBytes,
                   ID3D12GraphicsCommandList  **outCommandList,
                   ID3D12Resource             **outStaging,
                   void                       **outMapped) {
  GPUCommandQueueDX12 *native;
  GPUDeviceDX12       *device;
  GPUTransferSlotDX12 *slot;
  GPUResult            waitResult;
  HRESULT              result;
  bool                 upload;

  if (!queue || !queue->_device || !outCommandList || !outStaging ||
      !outMapped || stagingBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCommandList = NULL;
  *outStaging     = NULL;
  *outMapped      = NULL;
  native          = queue->_priv;
  device          = queue->_device->_priv;
  upload          = heapType == D3D12_HEAP_TYPE_UPLOAD;
  if (!native || !native->commandQueue || !device || !device->d3dDevice ||
      native->type == D3D12_COMMAND_LIST_TYPE_COPY ||
      (!upload && heapType != D3D12_HEAP_TYPE_READBACK)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (native->transferOpen) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  slot       = &native->transferSlots[native->nextTransferSlot];
  waitResult = dx12__waitTransfer(queue, slot, true);
  if (waitResult != GPU_OK) {
    return waitResult;
  }
  if (!dx12__ensureTransferContext(native, device, slot) ||
      !dx12__ensureTransferStaging(native,
                                   device,
                                   slot,
                                   stagingBytes,
                                   heapType)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = slot->allocator->lpVtbl->Reset(
    slot->allocator
  );
  if (FAILED(result)) {
    dx12__reportQueueError(native, "reset transfer allocator", result);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = slot->commandList->lpVtbl->Reset(
    slot->commandList,
    slot->allocator,
    NULL
  );
  if (FAILED(result)) {
    dx12__reportQueueError(native, "reset transfer command list", result);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->activeTransferSlot = native->nextTransferSlot;
  native->transferOpen       = true;
  *outCommandList            = slot->commandList;
  *outStaging                = upload ? slot->uploadStaging
                                      : native->readbackStaging;
  *outMapped                 = upload ? slot->uploadMapped : NULL;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_submitTransfer(GPUCommandQueue *queue, bool wait) {
  GPUCommandQueueDX12 *native;
  GPUTransferSlotDX12 *slot;
  ID3D12CommandList   *commandLists[1];
  UINT64               fenceValue;
  HRESULT              result;

  native = queue ? queue->_priv : NULL;
  slot = native && native->activeTransferSlot < GPU_DX12_TRANSFER_SLOT_COUNT
           ? &native->transferSlots[native->activeTransferSlot]
           : NULL;
  if (!native || !native->commandQueue || !native->transferOpen ||
      !slot || !slot->commandList || !native->transferFence ||
      !native->transferEvent) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = slot->commandList->lpVtbl->Close(
    slot->commandList
  );
  native->transferOpen = false;
  if (FAILED(result)) {
    dx12__reportQueueError(native, "close transfer command list", result);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  commandLists[0] = (ID3D12CommandList *)slot->commandList;
  native->commandQueue->lpVtbl->ExecuteCommandLists(native->commandQueue,
                                                     1u,
                                                     commandLists);
  fenceValue = ++native->transferFenceValue;
  result = native->commandQueue->lpVtbl->Signal(native->commandQueue,
                                                 native->transferFence,
                                                 fenceValue);
  if (FAILED(result)) {
    dx12__reportQueueError(native, "signal transfer fence", result);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  slot->fenceValue         = fenceValue;
  slot->pending            = true;
  native->nextTransferSlot =
    (native->activeTransferSlot + 1u) % GPU_DX12_TRANSFER_SLOT_COUNT;
  return wait ? dx12__waitTransfer(queue, slot, false) : GPU_OK;
}

GPU_HIDE
void
dx12_abortTransfer(GPUCommandQueue *queue) {
  GPUCommandQueueDX12 *native;
  GPUTransferSlotDX12 *slot;

  native = queue ? queue->_priv : NULL;
  slot = native && native->activeTransferSlot < GPU_DX12_TRANSFER_SLOT_COUNT
           ? &native->transferSlots[native->activeTransferSlot]
           : NULL;
  if (!native || !slot || !native->transferOpen || !slot->commandList) {
    return;
  }

  (void)slot->commandList->lpVtbl->Close(slot->commandList);
  native->transferOpen = false;
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
    dx12_abortTransfer(queue);
    for (uint32_t slot = 0u; slot < GPU_DX12_TRANSFER_SLOT_COUNT; slot++) {
      (void)dx12__waitTransfer(queue, &native->transferSlots[slot], false);
    }
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
    for (uint32_t slot = 0u; slot < GPU_DX12_TRANSFER_SLOT_COUNT; slot++) {
      GPUTransferSlotDX12 *transfer;

      transfer = &native->transferSlots[slot];
      if (transfer->uploadStaging) {
        if (transfer->uploadMapped) {
          transfer->uploadStaging->lpVtbl->Unmap(transfer->uploadStaging,
                                                 0u,
                                                 NULL);
        }
        transfer->uploadStaging->lpVtbl->Release(transfer->uploadStaging);
      }
      if (transfer->commandList) {
        transfer->commandList->lpVtbl->Release(transfer->commandList);
      }
      if (transfer->allocator) {
        transfer->allocator->lpVtbl->Release(transfer->allocator);
      }
    }
    if (native->readbackStaging) {
      native->readbackStaging->lpVtbl->Release(native->readbackStaging);
    }
    if (native->transferFence) {
      native->transferFence->lpVtbl->Release(native->transferFence);
    }
    if (native->transferEvent) {
      CloseHandle(native->transferEvent);
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
  if (SUCCEEDED(result) && waitResult == WAIT_OBJECT_0) {
    for (uint32_t slot = 0u; slot < GPU_DX12_TRANSFER_SLOT_COUNT; slot++) {
      queue->transferSlots[slot].pending = false;
    }
    return true;
  }
  return false;
}

GPU_HIDE
GPUResult
dx12_waitDeviceIdle(GPUDevice * __restrict device) {
  GPUDeviceDX12 *deviceDX12;
  bool           idle;

  deviceDX12 = device ? device->_priv : NULL;
  if (!deviceDX12) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  idle = true;
  for (uint32_t i = 0u; i < deviceDX12->nCreatedQueues; i++) {
    GPUCommandQueueDX12 *queue;

    queue = deviceDX12->createdQueues[i] ?
      deviceDX12->createdQueues[i]->_priv : NULL;
    if (!queue) {
      continue;
    }

    idle = dx12_waitCommandQueueIdle(queue) && idle;
    dx12__waitForCompletions(queue);
  }
  return idle ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
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
    dx12__reportQueueError(native->owner, "command buffer reset", result);
    dx12__recycleCommandBuffer(&native->commandBuffer);
    return NULL;
  }

  dx12_setCommandListName(queue->_device, native->commandList, label);

  cmdb = &native->commandBuffer;
  memset(cmdb, 0, sizeof(*cmdb));
  memset(&native->renderPassDesc, 0, sizeof(native->renderPassDesc));
  memset(&native->renderPass, 0, sizeof(native->renderPass));
  memset(&native->renderEncoder, 0, sizeof(native->renderEncoder));
  memset(&native->renderState, 0, sizeof(native->renderState));
  memset(&native->computeEncoder, 0, sizeof(native->computeEncoder));
  memset(&native->computeState, 0, sizeof(native->computeState));
  memset(&native->copyEncoder, 0, sizeof(native->copyEncoder));
  native->copyDebugEventActive = false;
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
    dx12__reportQueueError(queue, "command list close", result);
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
      dx12__reportQueueError(queue, "present", presentResult);
      commitResult = GPU_ERROR_BACKEND_FAILURE;
    }
  }
  native->presentSwapchain = NULL;

  fenceValue = queue->nextFenceValue++;
  result = queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                queue->completionFence,
                                                fenceValue);
  if (FAILED(result)) {
    dx12__reportQueueError(queue, "queue signal", result);
    gpuFinishCommandBuffer(cmdb, NULL);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->fenceValue = fenceValue;
  if (swapchain && swapchain->frameIndex < swapchain->imageCount) {
    swapchain->frames[swapchain->frameIndex].fenceValue = fenceValue;
  }
  dx12__queueLock(queue);
  queue->inFlightCount++;
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
  api->getTimestampPeriod      = dx12_getTimestampPeriod;
  api->newCommandBuffer        = dx12_newCommandBuffer;
  api->commandBufferOnComplete = dx12_commandBufferOnComplete;
  api->commit                  = dx12_commitCommandBuffer;
}
