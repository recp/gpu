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

#include <d3d12sdklayers.h>

enum {
  DX12_COMPLETION_STACK_SIZE     = 64u * 1024u,
  DX12_SUBMIT_STACK_COUNT        = 64u,
  DX12_TRANSFER_OFFSET_ALIGNMENT = 512u
};

static GPUResult
dx12__flushTransfers(GPUQueue *queue, bool wait);

static void
dx12__queueLock(GPUQueueDX12 *queue) {
  EnterCriticalSection(&queue->poolLock);
}

static void
dx12__queueUnlock(GPUQueueDX12 *queue) {
  LeaveCriticalSection(&queue->poolLock);
}

static void
dx12__queueSignal(GPUQueueDX12 *queue) {
  WakeConditionVariable(&queue->pendingCondition);
}

static void
dx12__queueBroadcast(GPUQueueDX12 *queue) {
  WakeAllConditionVariable(&queue->pendingCondition);
}

static void
dx12__queueWait(GPUQueueDX12 *queue) {
  SleepConditionVariableCS(&queue->pendingCondition,
                           &queue->poolLock,
                           INFINITE);
}

static void
dx12__waitForCompletions(GPUQueueDX12 *queue) {
  dx12__queueLock(queue);
  while (queue->inFlightCount > 0u) {
    dx12__queueWait(queue);
  }
  dx12__queueUnlock(queue);
}

static GPUDevice *
dx12__queueDevice(GPUQueueDX12 *queue) {
  return queue && queue->queue ? queue->queue->_device : NULL;
}

GPU_HIDE
GPUResult
dx12_getTimestampPeriod(GPUQueue *queue,
                        double   *outNanosecondsPerTick) {
  GPUQueueDX12 *native;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->commandQueue || !outNanosecondsPerTick ||
      native->timestampFrequency == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outNanosecondsPerTick = 1000000000.0 /
                           (double)native->timestampFrequency;
  return GPU_OK;
}

static bool
dx12__lostReason(GPUQueueDX12 *queue,
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
dx12__reportQueueError(GPUQueueDX12 *queue,
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

static void
dx12__recordFrameTime(GPUCommandBufferDX12 *native) {
  GPUQueueDX12 *queue;
  GPUDevice    *device;
  UINT64        elapsed;
  double        milliseconds;

  queue  = native ? native->owner : NULL;
  device = dx12__queueDevice(queue);
  if (!native || !native->frameTimeActive ||
      !native->commandBuffer._recordsGPUFrameTime ||
      !native->frameTimeMapped || !device || queue->timestampFrequency == 0u) {
    return;
  }

  elapsed      = native->frameTimeMapped[1] - native->frameTimeMapped[0];
  milliseconds = (double)elapsed * 1000.0 /
                 (double)queue->timestampFrequency;
  gpuDeviceRecordGPUFrameTime(device, milliseconds);
}

#if GPU_BUILD_WITH_VALIDATION
static void
dx12__logDebugMessages(GPUQueueDX12 *queue) {
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
  GPUQueueDX12         *queue;

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
dx12__takePendingCommand(GPUQueueDX12 *queue) {
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
dx12__waitForFence(GPUQueueDX12 *queue,
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
dx12_waitQueueFence(GPUQueueDX12 *queue,
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
  GPUQueueDX12         *queue;
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
    if (completed) {
      dx12__recordFrameTime(native);
    }
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
dx12__startWorker(GPUQueueDX12 *queue) {
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
dx12__stopWorker(GPUQueueDX12 *queue) {
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

static bool
dx12__beginFrameTime(GPUCommandBufferDX12 *native) {
  GPUQueueDX12          *queue;
  GPUDevice             *device;
  GPUDeviceDX12         *deviceDX12;
  ID3D12QueryHeap       *queries;
  ID3D12Resource        *readback;
  UINT64                *mapped;
  D3D12_QUERY_HEAP_DESC  queryDesc = {0};
  D3D12_RANGE            readRange = {0};
  HRESULT                result;

  queue      = native ? native->owner : NULL;
  device     = dx12__queueDevice(queue);
  deviceDX12 = device ? device->_priv : NULL;
  if (!native || !queue || !device || !deviceDX12 ||
      !device->runtimeConfig.enableStats ||
      !deviceDX12->queryResultsReliable ||
      !(queue->queue->bits & GPU_QUEUE_GRAPHICS_BIT) ||
      queue->timestampFrequency == 0u) {
    return false;
  }
  if ((!native->frameTimeQueries) != (!native->frameTimeReadback) ||
      (!native->frameTimeQueries) != (!native->frameTimeMapped)) {
    return false;
  }

  if (!native->frameTimeQueries) {
    queries            = NULL;
    readback           = NULL;
    mapped             = NULL;
    queryDesc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count    = 2u;
    queryDesc.NodeMask = 0u;
    result = deviceDX12->d3dDevice->lpVtbl->CreateQueryHeap(
      deviceDX12->d3dDevice,
      &queryDesc,
      &IID_ID3D12QueryHeap,
      (void **)&queries
    );
    if (FAILED(result) || !queries) {
      return false;
    }

    readback = dx12__createTransferStaging(deviceDX12,
                                           2u * sizeof(UINT64),
                                           D3D12_HEAP_TYPE_READBACK);
    readRange.End = 2u * sizeof(UINT64);
    if (!readback ||
        FAILED(readback->lpVtbl->Map(readback,
                                    0u,
                                    &readRange,
                                    (void **)&mapped)) ||
        !mapped) {
      if (readback) {
        readback->lpVtbl->Release(readback);
      }
      queries->lpVtbl->Release(queries);
      return false;
    }

    native->frameTimeQueries  = queries;
    native->frameTimeReadback = readback;
    native->frameTimeMapped   = mapped;
  }

  native->commandList->lpVtbl->EndQuery(native->commandList,
                                         native->frameTimeQueries,
                                         D3D12_QUERY_TYPE_TIMESTAMP,
                                         0u);
  return true;
}

static void
dx12__endFrameTime(GPUCommandBufferDX12 *native) {
  if (!native || !native->frameTimeActive ||
      !native->commandBuffer._recordsGPUFrameTime) {
    return;
  }

  native->commandList->lpVtbl->EndQuery(native->commandList,
                                         native->frameTimeQueries,
                                         D3D12_QUERY_TYPE_TIMESTAMP,
                                         1u);
  native->commandList->lpVtbl->ResolveQueryData(
    native->commandList,
    native->frameTimeQueries,
    D3D12_QUERY_TYPE_TIMESTAMP,
    0u,
    2u,
    native->frameTimeReadback,
    0u
  );
}

static uint64_t
dx12__transferCapacity(uint64_t sizeBytes, uint64_t minimumCapacity) {
  uint64_t capacity;

  capacity = minimumCapacity;
  while (capacity < sizeBytes) {
    if (capacity > UINT64_MAX / 2u) {
      return sizeBytes;
    }
    capacity *= 2u;
  }
  return capacity;
}

static bool
dx12__ensureTransferContext(GPUQueueDX12 *queue,
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
dx12__ensureTransferStaging(GPUQueueDX12 *queue,
                            GPUDeviceDX12       *device,
                            GPUTransferSlotDX12 *slot,
                            uint64_t              sizeBytes,
                            uint64_t              minimumCapacity,
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
      slot->uploadCapacity >= sizeBytes &&
      slot->uploadCapacity >= minimumCapacity) {
    return slot->uploadMapped != NULL;
  }
  if (!upload && queue->readbackStaging &&
      queue->readbackCapacity >= sizeBytes &&
      queue->readbackCapacity >= minimumCapacity) {
    return true;
  }

  capacity = dx12__transferCapacity(sizeBytes, minimumCapacity);
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
dx12__waitTransfer(GPUQueue     *queue,
                   GPUTransferSlotDX12 *slot,
                   bool                 countStall) {
  GPUQueueDX12        *native;
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
dx12_beginTransfer(GPUQueue             *queue,
                   D3D12_HEAP_TYPE              heapType,
                   uint64_t                     stagingBytes,
                   uint64_t                     minimumCapacity,
                   ID3D12GraphicsCommandList  **outCommandList,
                   ID3D12Resource             **outStaging,
                   void                       **outMapped,
                   uint64_t                    *outOffset) {
  GPUQueueDX12        *native;
  GPUDeviceDX12       *device;
  GPUTransferSlotDX12 *slot;
  GPUResult            waitResult;
  HRESULT              result;
  bool                 upload;

  if (!queue || !queue->_device || !outCommandList || !outStaging ||
      !outMapped || !outOffset || stagingBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outCommandList = NULL;
  *outStaging     = NULL;
  *outMapped      = NULL;
  *outOffset      = 0u;
  native          = queue->_priv;
  device          = queue->_device->_priv;
  upload          = heapType == D3D12_HEAP_TYPE_UPLOAD;
  if (!native || !native->commandQueue || !device || !device->d3dDevice ||
      native->type == D3D12_COMMAND_LIST_TYPE_COPY ||
      (!upload && heapType != D3D12_HEAP_TYPE_READBACK)) {
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
          UINT64_MAX - (DX12_TRANSFER_OFFSET_ALIGNMENT - 1u)) {
        offset = (slot->uploadUsed + DX12_TRANSFER_OFFSET_ALIGNMENT - 1u) &
                 ~(uint64_t)(DX12_TRANSFER_OFFSET_ALIGNMENT - 1u);
        if (offset <= slot->uploadCapacity &&
            stagingBytes <= slot->uploadCapacity - offset) {
          slot->uploadUsed = offset + stagingBytes;
          *outCommandList  = slot->commandList;
          *outStaging      = slot->uploadStaging;
          *outMapped       = slot->uploadMapped;
          *outOffset       = offset;
          return GPU_OK;
        }
      }
    }
    waitResult = dx12__flushTransfers(queue, false);
    if (waitResult != GPU_OK) {
      return waitResult;
    }
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
                                   minimumCapacity,
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
  native->transferUpload     = upload;
  slot->uploadUsed           = upload ? stagingBytes : 0u;
  *outCommandList            = slot->commandList;
  *outStaging                = upload ? slot->uploadStaging
                                      : native->readbackStaging;
  *outMapped                 = upload ? slot->uploadMapped : NULL;
  *outOffset                 = 0u;
  return GPU_OK;
}

static GPUResult
dx12__flushTransfers(GPUQueue *queue, bool wait) {
  GPUQueueDX12        *native;
  GPUTransferSlotDX12 *slot;
  ID3D12CommandList   *commandLists[1];
  UINT64               fenceValue;
  HRESULT              result;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->commandQueue) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (native->transferOpen) {
    slot = native->activeTransferSlot < GPU_DX12_TRANSFER_SLOT_COUNT
             ? &native->transferSlots[native->activeTransferSlot]
             : NULL;
    if (!slot || !slot->commandList || !native->transferFence ||
        !native->transferEvent) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    result = slot->commandList->lpVtbl->Close(slot->commandList);
    native->transferOpen   = false;
    native->transferUpload = false;
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
  }

  if (wait) {
    for (uint32_t i = 0u; i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
      GPUResult waitResult;

      waitResult = dx12__waitTransfer(queue, &native->transferSlots[i], false);
      if (waitResult != GPU_OK) {
        return waitResult;
      }
    }
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_submitTransfer(GPUQueue *queue, bool wait) {
  GPUQueueDX12        *native;

  native = queue ? queue->_priv : NULL;
  if (!native || !native->transferOpen) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!wait && native->transferUpload) {
    return GPU_OK;
  }
  return dx12__flushTransfers(queue, wait);
}

GPU_HIDE
void
dx12_abortTransfer(GPUQueue *queue) {
  GPUQueueDX12        *native;
  GPUTransferSlotDX12 *slot;

  native = queue ? queue->_priv : NULL;
  slot = native && native->activeTransferSlot < GPU_DX12_TRANSFER_SLOT_COUNT
           ? &native->transferSlots[native->activeTransferSlot]
           : NULL;
  if (!native || !slot || !native->transferOpen || !slot->commandList) {
    return;
  }

  (void)slot->commandList->lpVtbl->Close(slot->commandList);
  slot->uploadUsed         = 0u;
  native->transferOpen     = false;
  native->transferUpload   = false;
}

GPU_HIDE
GPUQueue*
dx12_createCommandQueue(GPUDevice *device, GPUQueueFlagBits bits) {
  GPUQueueDX12             *native;
  GPUQueue                 *queue;
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
  if (deviceDX12->queryResultsReliable &&
      FAILED(native->commandQueue->lpVtbl->GetTimestampFrequency(
        native->commandQueue,
        &native->timestampFrequency
      ))) {
    native->timestampFrequency = 0u;
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
dx12_destroyCommandQueue(GPUQueue *queue) {
  GPUQueueDX12         *native;
  GPUCommandBufferDX12 *command;
  GPUCommandBufferDX12 *next;
  D3D12_RANGE           noWrites = {0};

  if (!queue) {
    return;
  }

  native = queue->_priv;
  if (native) {
    dx12__stopWorker(native);
    if (dx12__flushTransfers(queue, true) != GPU_OK) {
      dx12_abortTransfer(queue);
    }
    for (uint32_t slot = 0u; slot < GPU_DX12_TRANSFER_SLOT_COUNT; slot++) {
      (void)dx12__waitTransfer(queue, &native->transferSlots[slot], false);
    }
    command = native->commands;
    while (command) {
      next = command->next;
      if (command->frameTimeReadback) {
        if (command->frameTimeMapped) {
          command->frameTimeReadback->lpVtbl->Unmap(
            command->frameTimeReadback,
            0u,
            &noWrites
          );
        }
        command->frameTimeReadback->lpVtbl->Release(
          command->frameTimeReadback
        );
      }
      if (command->frameTimeQueries) {
        command->frameTimeQueries->lpVtbl->Release(
          command->frameTimeQueries
        );
      }
      dx12_destroyCopyScratch(command);
      if (command->commandList7) {
        command->commandList7->lpVtbl->Release(command->commandList7);
      }
      if (command->commandList6) {
        command->commandList6->lpVtbl->Release(command->commandList6);
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
dx12_waitCommandQueueIdle(GPUQueueDX12 *queue) {
  HANDLE  event;
  UINT64  fenceValue;
  HRESULT result;
  DWORD   waitResult;

  if (!queue || !queue->commandQueue || !queue->completionFence) {
    return false;
  }
  if (dx12__flushTransfers(queue->queue, false) != GPU_OK) {
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
    GPUQueueDX12        *queue;

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
GPUQueue*
dx12_getCommandQueue(GPUDevice * __restrict device,
                     GPUQueueFlagBits         bits,
                     uint32_t                 index) {
  GPUQueue        *queue;
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
GPUQueue*
dx12_newCommandQueue(GPUDevice * __restrict device) {
  return dx12_getCommandQueue(device,
                              GPU_QUEUE_GRAPHICS_BIT |
                              GPU_QUEUE_COMPUTE_BIT,
                              0u);
}

static GPUCommandBufferDX12*
dx12__createCommandBufferState(GPUQueue *queue) {
  GPUQueueDX12         *queueDX12;
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
  if (deviceDX12->meshShader) {
    (void)native->commandList->lpVtbl->QueryInterface(
      native->commandList,
      &IID_ID3D12GraphicsCommandList6,
      (void **)&native->commandList6
    );
    if (!native->commandList6) {
      goto fail;
    }
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
  if (native->commandList6) {
    native->commandList6->lpVtbl->Release(native->commandList6);
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
dx12__takeCommandBufferState(GPUQueue *queue) {
  GPUQueueDX12         *queueDX12;
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
dx12_newCommandBuffer(GPUQueue  * __restrict queue,
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
  dx12_resetCopyScratch(native);
  native->copyDebugEventActive = false;
  native->presentSwapchain     = NULL;
  native->pendingNext          = NULL;
  native->fenceValue           = 0u;
  native->frameTimeActive      = dx12__beginFrameTime(native);

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
  GPUQueueDX12         *queue;
  GPUSwapchainDX12     *swapchain;
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

  commitResult = dx12__flushTransfers(queue->queue, false);
  if (commitResult != GPU_OK) {
    gpuFinishCommandBuffer(cmdb, dx12__recycleCommandBuffer);
    return commitResult;
  }

  dx12__endFrameTime(native);
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
  if (swapchain && swapchain->swapchain) {
    presentResult = swapchain->swapchain->lpVtbl->Present(
      swapchain->swapchain,
      swapchain->syncInterval,
      swapchain->presentFlags
    );
    dx12_setSwapchainStatus(swapchain, presentResult);
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

static GPUResult
dx12__commitCommandBuffers(uint32_t                  count,
                           GPUCommandBuffer * const *buffers) {
  GPUResult result;

  result = GPU_OK;
  for (uint32_t i = 0u; i < count; i++) {
    GPUResult commitResult;

    commitResult = dx12_commitCommandBuffer(buffers[i]);
    if (result == GPU_OK && commitResult != GPU_OK) {
      result = commitResult;
    }
  }
  return result;
}

static void
dx12__finishCommandBuffers(uint32_t                  count,
                           GPUCommandBuffer * const *buffers,
                           bool                      recycle) {
  for (uint32_t i = 0u; i < count; i++) {
    gpuFinishCommandBuffer(buffers[i],
                           recycle ? dx12__recycleCommandBuffer : NULL);
  }
}

GPU_HIDE
GPUResult
dx12_submitCommandBuffers(GPUQueue                  * __restrict queueHandle,
                          uint32_t                                count,
                          GPUCommandBuffer * const * __restrict buffers) {
  GPUCommandBufferDX12 *natives[DX12_SUBMIT_STACK_COUNT];
  ID3D12CommandList    *commandLists[DX12_SUBMIT_STACK_COUNT];
  GPUQueueDX12         *queue;
  GPUResult             flushResult;
  HRESULT               result;
  UINT64                fenceValue;

  queue = queueHandle ? queueHandle->_priv : NULL;
  if (!queue || !buffers || count < 2u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (count > DX12_SUBMIT_STACK_COUNT) {
    return dx12__commitCommandBuffers(count, buffers);
  }

  for (uint32_t i = 0u; i < count; i++) {
    natives[i] = buffers[i] ? buffers[i]->_priv : NULL;
    if (!natives[i] || natives[i]->owner != queue ||
        !natives[i]->commandList || natives[i]->presentSwapchain) {
      return dx12__commitCommandBuffers(count, buffers);
    }
    commandLists[i] = (ID3D12CommandList *)natives[i]->commandList;
  }

  flushResult = dx12__flushTransfers(queueHandle, false);
  if (flushResult != GPU_OK) {
    dx12__finishCommandBuffers(count, buffers, true);
    return flushResult;
  }

  for (uint32_t i = 0u; i < count; i++) {
    dx12__endFrameTime(natives[i]);
    result = natives[i]->commandList->lpVtbl->Close(natives[i]->commandList);
    if (FAILED(result)) {
      dx12__reportQueueError(queue, "command list close", result);
      dx12__logDebugMessages(queue);
      dx12__finishCommandBuffers(count, buffers, true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  queue->commandQueue->lpVtbl->ExecuteCommandLists(queue->commandQueue,
                                                    count,
                                                    commandLists);
  fenceValue = queue->nextFenceValue++;
  result = queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                queue->completionFence,
                                                fenceValue);
  if (FAILED(result)) {
    dx12__reportQueueError(queue, "queue batch signal", result);
    dx12__finishCommandBuffers(count, buffers, false);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  dx12__queueLock(queue);
  for (uint32_t i = 0u; i < count; i++) {
    natives[i]->fenceValue  = fenceValue;
    natives[i]->pendingNext = NULL;
    if (queue->pendingTail) {
      queue->pendingTail->pendingNext = natives[i];
    } else {
      queue->pendingHead = natives[i];
    }
    queue->pendingTail = natives[i];
  }
  queue->inFlightCount += count;
  dx12__queueSignal(queue);
  dx12__queueUnlock(queue);
  return GPU_OK;
}

static GPUResult
dx12_createSemaphore(GPUDevice                    *device,
                     const GPUSemaphoreCreateInfo *info,
                     GPUSemaphore                 *semaphore) {
  GPUDeviceDX12 *native;
  ID3D12Fence   *fence;
  HRESULT        result;

  native = device ? device->_priv : NULL;
  if (!native || !native->d3dDevice || !semaphore) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  fence = NULL;
  result = native->d3dDevice->lpVtbl->CreateFence(
    native->d3dDevice,
    info ? info->initialValue : 0u,
    D3D12_FENCE_FLAG_NONE,
    &IID_ID3D12Fence,
    (void **)&fence
  );
  if (FAILED(result)) {
    return result == E_OUTOFMEMORY
             ? GPU_ERROR_OUT_OF_MEMORY
             : GPU_ERROR_BACKEND_FAILURE;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (info && gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    wchar_t name[256];

    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            info->label,
                            -1,
                            name,
                            (int)GPU_ARRAY_LEN(name)) > 0) {
      (void)fence->lpVtbl->SetName(fence, name);
    }
  }
#endif

  semaphore->_priv = fence;
  return GPU_OK;
}

static void
dx12_destroySemaphore(GPUSemaphore *semaphore) {
  ID3D12Fence *fence;

  fence = semaphore ? semaphore->_priv : NULL;
  if (fence) {
    fence->lpVtbl->Release(fence);
  }
  if (semaphore) {
    semaphore->_priv = NULL;
  }
}

static GPUResult
dx12_submitEx(GPUQueue                   *queueHandle,
              const GPUQueueSubmitExInfo *info) {
  GPUCommandBufferDX12 *natives[DX12_SUBMIT_STACK_COUNT];
  ID3D12CommandList    *commandLists[DX12_SUBMIT_STACK_COUNT];
  GPUQueueDX12         *queue;
  GPUSwapchainDX12     *swapchain;
  GPUCommandBufferDX12 *presentNative;
  GPUResult             submitResult;
  GPUResult             flushResult;
  HRESULT               result;
  HRESULT               presentResult;
  UINT64                fenceValue;

  queue = queueHandle ? queueHandle->_priv : NULL;
  if (!queue || !info || info->commandBufferCount == 0u ||
      info->commandBufferCount > DX12_SUBMIT_STACK_COUNT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  swapchain     = NULL;
  presentNative = NULL;
  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    natives[i] = info->ppCommandBuffers[i]
                   ? info->ppCommandBuffers[i]->_priv
                   : NULL;
    if (!natives[i] || natives[i]->owner != queue ||
        !natives[i]->commandList) {
      dx12__finishCommandBuffers(info->commandBufferCount,
                                 info->ppCommandBuffers,
                                 true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (natives[i]->presentSwapchain) {
      if (swapchain) {
        dx12__finishCommandBuffers(info->commandBufferCount,
                                   info->ppCommandBuffers,
                                   true);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      swapchain     = natives[i]->presentSwapchain;
      presentNative = natives[i];
    }
    commandLists[i] = (ID3D12CommandList *)natives[i]->commandList;
  }

  flushResult = dx12__flushTransfers(queueHandle, false);
  if (flushResult != GPU_OK) {
    dx12__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               true);
    return flushResult;
  }

  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    dx12__endFrameTime(natives[i]);
    result = natives[i]->commandList->lpVtbl->Close(natives[i]->commandList);
    if (FAILED(result)) {
      dx12__reportQueueError(queue, "advanced command list close", result);
      dx12__logDebugMessages(queue);
      dx12__finishCommandBuffers(info->commandBufferCount,
                                 info->ppCommandBuffers,
                                 true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (uint32_t i = 0u; i < info->waitCount; i++) {
    ID3D12Fence *fence;

    fence = info->pWaits[i].semaphore->_priv;
    if (!fence) {
      dx12__finishCommandBuffers(info->commandBufferCount,
                                 info->ppCommandBuffers,
                                 true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    result = queue->commandQueue->lpVtbl->Wait(queue->commandQueue,
                                               fence,
                                               info->pWaits[i].value);
    if (FAILED(result)) {
      dx12__reportQueueError(queue, "advanced queue wait", result);
      dx12__finishCommandBuffers(info->commandBufferCount,
                                 info->ppCommandBuffers,
                                 true);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  queue->commandQueue->lpVtbl->ExecuteCommandLists(
    queue->commandQueue,
    info->commandBufferCount,
    commandLists
  );

  submitResult = GPU_OK;
  if (swapchain && swapchain->swapchain) {
    presentResult = swapchain->swapchain->lpVtbl->Present(
      swapchain->swapchain,
      swapchain->syncInterval,
      swapchain->presentFlags
    );
    dx12_setSwapchainStatus(swapchain, presentResult);
    if (FAILED(presentResult)) {
      dx12__reportQueueError(queue, "advanced present", presentResult);
      submitResult = GPU_ERROR_BACKEND_FAILURE;
    }
  }
  if (swapchain) {
    presentNative->presentSwapchain = NULL;
  }

  for (uint32_t i = 0u; i < info->signalCount; i++) {
    ID3D12Fence *fence;

    fence = info->pSignals[i].semaphore->_priv;
    result = fence
               ? queue->commandQueue->lpVtbl->Signal(
                   queue->commandQueue,
                   fence,
                   info->pSignals[i].value
                 )
               : E_INVALIDARG;
    if (FAILED(result)) {
      dx12__reportQueueError(queue, "advanced queue signal", result);
      submitResult = GPU_ERROR_BACKEND_FAILURE;
    }
  }

  fenceValue = queue->nextFenceValue++;
  result = queue->commandQueue->lpVtbl->Signal(queue->commandQueue,
                                                queue->completionFence,
                                                fenceValue);
  if (FAILED(result)) {
    dx12__reportQueueError(queue, "advanced completion signal", result);
    dx12__finishCommandBuffers(info->commandBufferCount,
                               info->ppCommandBuffers,
                               false);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (swapchain && swapchain->frameIndex < swapchain->imageCount) {
    swapchain->frames[swapchain->frameIndex].fenceValue = fenceValue;
  }
  dx12__queueLock(queue);
  for (uint32_t i = 0u; i < info->commandBufferCount; i++) {
    natives[i]->fenceValue  = fenceValue;
    natives[i]->pendingNext = NULL;
    if (queue->pendingTail) {
      queue->pendingTail->pendingNext = natives[i];
    } else {
      queue->pendingHead = natives[i];
    }
    queue->pendingTail = natives[i];
  }
  queue->inFlightCount += info->commandBufferCount;
  dx12__queueSignal(queue);
  dx12__queueUnlock(queue);
  return submitResult;
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
  api->submit                  = dx12_submitCommandBuffers;
  api->createSemaphore         = dx12_createSemaphore;
  api->destroySemaphore        = dx12_destroySemaphore;
  api->submitEx                = dx12_submitEx;
}
