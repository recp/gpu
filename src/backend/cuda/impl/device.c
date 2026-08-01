/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

void
cuda_queueLock(GPUQueueCuda *queue) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&queue->lock);
#else
  pthread_mutex_lock(&queue->lock);
#endif
}

void
cuda_queueUnlock(GPUQueueCuda *queue) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&queue->lock);
#else
  pthread_mutex_unlock(&queue->lock);
#endif
}

void
cuda_queueSignal(GPUQueueCuda *queue) {
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&queue->condition);
#else
  pthread_cond_broadcast(&queue->condition);
#endif
}

static void
cuda__queueWait(GPUQueueCuda *queue) {
#if defined(_WIN32) || defined(WIN32)
  SleepConditionVariableCS(&queue->condition, &queue->lock, INFINITE);
#else
  pthread_cond_wait(&queue->condition, &queue->lock);
#endif
}

static void
cuda__releaseDispatches(GPUCommandCuda *command) {
  for (uint32_t i = 0u; i < command->dispatchCount; i++) {
    GPUDestroyComputePipeline(command->dispatches[i].pipeline);
  }
  command->dispatchCount = 0u;
}

GPUCommandCuda *
cuda_createCommand(GPUQueueCuda *queue) {
  GPUCommandCuda *command;

  if (!queue || !queue->driver) {
    return NULL;
  }
  command = calloc(1, sizeof(*command));
  if (!command) {
    return NULL;
  }
  command->owner            = queue;
  command->command._priv    = command;
  command->command._queue   = &queue->queue;
  command->dispatchCapacity = CUDA_INITIAL_DISPATCH_CAPACITY;
  command->dispatches       = calloc(command->dispatchCapacity,
                                     sizeof(*command->dispatches));
  if (!command->dispatches ||
      queue->driver->eventCreate(&command->completion,
                                 CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS) {
    free(command->dispatches);
    free(command);
    return NULL;
  }

  cuda_queueLock(queue);
  command->allNext = queue->commands;
  queue->commands  = command;
  cuda_queueUnlock(queue);
  return command;
}

void
cuda_recycleCommand(GPUCommandBuffer *cmdb) {
  GPUCommandCuda *command;
  GPUQueueCuda   *queue;

  command = cuda_command(cmdb);
  queue   = command ? command->owner : NULL;
  if (!command || !queue) {
    return;
  }

  cuda__releaseDispatches(command);
  cuda_queueLock(queue);
  command->next       = queue->freeCommands;
  queue->freeCommands = command;
  if (command->pending && queue->pendingCount > 0u) {
    queue->pendingCount--;
  }
  command->pending = false;
  cuda_queueSignal(queue);
  cuda_queueUnlock(queue);
}

static void
cuda__completionLoop(GPUQueueCuda *queue) {
  GPUCommandCuda *command;
  CUresult        result;

  for (;;) {
    cuda_queueLock(queue);
    while (!queue->pendingHead && !queue->stopping) {
      cuda__queueWait(queue);
    }
    command = queue->pendingHead;
    if (command) {
      queue->pendingHead = command->next;
      if (!queue->pendingHead) {
        queue->pendingTail = NULL;
      }
      command->next = NULL;
    } else if (queue->stopping) {
      cuda_queueUnlock(queue);
      break;
    }
    cuda_queueUnlock(queue);

    if (cuda_push(queue->driver, queue->context) == GPU_OK) {
      result = queue->driver->eventSynchronize(command->completion);
      cuda_pop(queue->driver);
    } else {
      result = -1;
    }
    if (result != CUDA_SUCCESS) {
      cuda_report(queue->queue._device, result, "event synchronization");
    }
    gpuFinishCommandBuffer(&command->command, cuda_recycleCommand);
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
cuda__completionThread(void *userData) {
  cuda__completionLoop(userData);
  return 0;
}
#else
static void *
cuda__completionThread(void *userData) {
  cuda__completionLoop(userData);
  return NULL;
}
#endif

static void
cuda__destroyQueue(GPUQueueCuda *queue) {
  if (!queue || !queue->driver) {
    return;
  }

  if (queue->workerStarted) {
    cuda_queueLock(queue);
    queue->stopping = true;
    cuda_queueSignal(queue);
    cuda_queueUnlock(queue);
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(queue->worker, INFINITE);
    CloseHandle(queue->worker);
#else
    pthread_join(queue->worker, NULL);
#endif
  }

  if (cuda_push(queue->driver, queue->context) == GPU_OK) {
    GPUCommandCuda *command;
    GPUCommandCuda *next;

    for (command = queue->commands; command; command = next) {
      next = command->allNext;
      cuda__releaseDispatches(command);
      if (command->completion) {
        (void)queue->driver->eventDestroy(command->completion);
      }
      free(command->dispatches);
      free(command);
    }
    if (queue->stream) {
      (void)queue->driver->streamDestroy(queue->stream);
    }
    cuda_pop(queue->driver);
  }

#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(&queue->lock);
#else
  pthread_cond_destroy(&queue->condition);
  pthread_mutex_destroy(&queue->lock);
#endif
  memset(queue, 0, sizeof(*queue));
}

static bool
cuda__initQueue(GPUDevice      *device,
                GPUDeviceCuda *native,
                GPUQueueCuda  *queue) {
  CUresult result;

  queue->driver        = native->driver;
  queue->context       = native->context;
  queue->queue._priv   = queue;
  queue->queue._device = device;
  queue->queue.bits    = GPU_QUEUE_COMPUTE_BIT;
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&queue->lock);
  InitializeConditionVariable(&queue->condition);
#else
  if (pthread_mutex_init(&queue->lock, NULL) != 0) {
    return false;
  }
  if (pthread_cond_init(&queue->condition, NULL) != 0) {
    pthread_mutex_destroy(&queue->lock);
    return false;
  }
#endif

  result = native->driver->streamCreate(&queue->stream,
                                        CU_STREAM_NON_BLOCKING);
  if (result != CUDA_SUCCESS) {
    cuda__destroyQueue(queue);
    return false;
  }

  for (uint32_t i = 0u; i < CUDA_COMMAND_SLOT_COUNT; i++) {
    GPUCommandCuda *command;

    command = cuda_createCommand(queue);
    if (!command) {
      cuda__destroyQueue(queue);
      return false;
    }
    command->next       = queue->freeCommands;
    queue->freeCommands = command;
  }

#if defined(_WIN32) || defined(WIN32)
  queue->worker = CreateThread(NULL,
                               0u,
                               cuda__completionThread,
                               queue,
                               0u,
                               NULL);
  if (!queue->worker) {
    cuda__destroyQueue(queue);
    return false;
  }
  queue->workerStarted = true;
#else
  if (pthread_create(&queue->worker,
                     NULL,
                     cuda__completionThread,
                     queue) != 0) {
    memset(&queue->worker, 0, sizeof(queue->worker));
    cuda__destroyQueue(queue);
    return false;
  }
  queue->workerStarted = true;
#endif
  return true;
}

static GPUAdapter *
cuda_getAvailableAdapters(GPUInstance *inst, uint32_t maxNumberOfItems) {
  GPUAdapter *head;
  GPUAdapter *tail;
  GPUCUDA    *driver;
  uint32_t    emitted;
  int         count;

  driver = cuda_driver();
  if (!driver || maxNumberOfItems == 0u ||
      driver->deviceGetCount(&count) != CUDA_SUCCESS || count <= 0) {
    return NULL;
  }

  head    = NULL;
  tail    = NULL;
  emitted = 0u;
  for (int ordinal = 0;
       ordinal < count && emitted < maxNumberOfItems;
       ordinal++) {
    GPUAdapterCuda  candidate = {0};
    GPUAdapterCuda *native;
    GPUAdapter     *adapter;

    candidate.driver  = driver;
    candidate.ordinal = ordinal;
    if (driver->deviceGet(&candidate.device, ordinal) != CUDA_SUCCESS ||
        driver->deviceGetName(candidate.name,
                              (int)sizeof(candidate.name),
                              candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetUuid(&candidate.uuid,
                              candidate.device) != CUDA_SUCCESS) {
      continue;
    }
    candidate.name[sizeof(candidate.name) - 1u] = '\0';
    if (driver->deviceGetAttribute(
          &candidate.computeMajor,
          CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
          candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(
          &candidate.computeMinor,
          CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
          candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(
          &candidate.maxThreadsPerBlock,
          CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK,
          candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxBlockDim[0],
                                   CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X,
                                   candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxBlockDim[1],
                                   CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y,
                                   candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxBlockDim[2],
                                   CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z,
                                   candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxGridDim[0],
                                   CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X,
                                   candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxGridDim[1],
                                   CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y,
                                   candidate.device) != CUDA_SUCCESS ||
        driver->deviceGetAttribute(&candidate.maxGridDim[2],
                                   CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z,
                                   candidate.device) != CUDA_SUCCESS ||
        candidate.computeMajor <= 0 || candidate.computeMinor < 0 ||
        candidate.maxThreadsPerBlock <= 0 ||
        candidate.maxBlockDim[0] <= 0 || candidate.maxBlockDim[1] <= 0 ||
        candidate.maxBlockDim[2] <= 0 || candidate.maxGridDim[0] <= 0 ||
        candidate.maxGridDim[1] <= 0 || candidate.maxGridDim[2] <= 0) {
      continue;
    }
    (void)driver->deviceGetAttribute(&candidate.unifiedAddressing,
                                     CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING,
                                     candidate.device);

    adapter = calloc(1, sizeof(*adapter));
    native  = malloc(sizeof(*native));
    if (!adapter || !native) {
      free(native);
      free(adapter);
      break;
    }
    *native = candidate;

    adapter->inst  = inst;
    adapter->_priv = native;
    if (tail) {
      tail->next = adapter;
    } else {
      head = adapter;
    }
    tail = adapter;
    emitted++;
  }
  return head;
}

static GPUAdapter *
cuda_selectAdapter(GPUInstance       *inst,
                   GPUAdapter        *adapters,
                   GPUPowerPreference powerPreference) {
  GPU__UNUSED(inst);
  GPU__UNUSED(powerPreference);
  return adapters;
}

static void
cuda_destroyAdapter(GPUAdapter *adapter) {
  if (adapter) {
    free(adapter->_priv);
    free(adapter);
  }
}

static GPUResult
cuda_getAdapterProperties(const GPUAdapter     *adapter,
                          GPUAdapterProperties *outProperties) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native || !outProperties) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outProperties, 0, sizeof(*outProperties));
  outProperties->name           = native->name;
  outProperties->backend        = GPU_BACKEND_CUDA;
  outProperties->type           = GPU_ADAPTER_TYPE_DISCRETE;
  outProperties->executionFlags = GPU_EXECUTION_COMPUTE_BIT;
  return GPU_OK;
}

static GPUResult
cuda_getAdapterIdentity(const GPUAdapter   *adapter,
                        GPUAdapterIdentity *outIdentity) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native || !outIdentity) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outIdentity, 0, sizeof(*outIdentity));
  memcpy(outIdentity->deviceUUID,
         native->uuid.bytes,
         sizeof(outIdentity->deviceUUID));
  outIdentity->validFlags = GPU_ADAPTER_IDENTITY_UUID_BIT;
#if defined(_WIN32) || defined(WIN32)
  if (native->driver->deviceGetLuid) {
    char         luid[8];
    unsigned int nodeMask;

    nodeMask = 0u;
    if (native->driver->deviceGetLuid(luid,
                                      &nodeMask,
                                      native->device) == CUDA_SUCCESS) {
      memcpy(&outIdentity->luid, luid, sizeof(luid));
      outIdentity->luidNodeMask = nodeMask;
      outIdentity->validFlags  |= GPU_ADAPTER_IDENTITY_LUID_BIT;
    }
  }
#endif
  return GPU_OK;
}

static bool
cuda_supportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native) {
    return false;
  }
  return feature == GPU_FEATURE_COMPUTE ||
         (feature == GPU_FEATURE_BUFFER_DEVICE_ADDRESS &&
          native->unifiedAddressing != 0);
}

static bool
cuda_supportsSubgroupOperations(const GPUAdapter                 *adapter,
                                GPUShaderStageFlags               stage,
                                GPUBackendSubgroupOperationFlags  operations) {
  GPU__UNUSED(adapter);
  GPU__UNUSED(stage);
  GPU__UNUSED(operations);
  return false;
}

static void
cuda_getLimits(const GPUAdapter *adapter, GPULimits *outLimits) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native || !outLimits) {
    return;
  }
  memset(outLimits, 0, sizeof(*outLimits));
  outLimits->maxBindGroups       = 1u;
  outLimits->maxBindingsPerGroup = 1u;
  outLimits->minUniformBufferOffsetAlignment = 1u;
  outLimits->minStorageBufferOffsetAlignment = 1u;
  outLimits->maxComputeWorkgroupSizeX = (uint32_t)native->maxBlockDim[0];
  outLimits->maxComputeWorkgroupSizeY = (uint32_t)native->maxBlockDim[1];
  outLimits->maxComputeWorkgroupSizeZ = (uint32_t)native->maxBlockDim[2];
}

static void
cuda_getFormatCapabilities(const GPUAdapter      *adapter,
                           GPUFormat              format,
                           GPUFormatCapabilities *outCapabilities) {
  GPU__UNUSED(adapter);
  GPU__UNUSED(format);
  if (outCapabilities) {
    memset(outCapabilities, 0, sizeof(*outCapabilities));
  }
}

static GPUResult
cuda_getSubgroupMatrixProperties(
  const GPUAdapter               *adapter,
  uint32_t                       *inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT *outProperties
) {
  GPU__UNUSED(adapter);
  GPU__UNUSED(outProperties);
  if (!inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *inoutPropertyCount = 0u;
  return GPU_ERROR_UNSUPPORTED;
}

static GPUDevice *
cuda_createDevice(GPUAdapter               *adapter,
                  const GPUQueueCreateInfo  queueInfos[],
                  uint32_t                  queueInfoCount,
                  uint64_t                  enabledFeatureMask) {
  uint64_t        supportedMask;
  GPUAdapterCuda *adapterNative;
  GPUDeviceCuda  *native;
  GPUDevice      *device;
  uint32_t        queueCount;
  CUresult        result;

  adapterNative = cuda_adapter(adapter);
  supportedMask = 1ull << GPU_FEATURE_COMPUTE;
  if (adapterNative && adapterNative->unifiedAddressing != 0) {
    supportedMask |= 1ull << GPU_FEATURE_BUFFER_DEVICE_ADDRESS;
  }
  if (!adapterNative || (enabledFeatureMask & ~supportedMask) != 0u) {
    return NULL;
  }

  queueCount = queueInfoCount == 0u ? 1u : 0u;
  for (uint32_t i = 0u; i < queueInfoCount; i++) {
    if (queueInfos[i].flags != GPU_QUEUE_COMPUTE_BIT ||
        ((uint32_t)queueInfos[i].optionalFlags &
          ~(uint32_t)GPU_QUEUE_COMPUTE_BIT) != 0u ||
        queueInfos[i].count > 64u - queueCount) {
      return NULL;
    }
    queueCount += queueInfos[i].count;
  }
  if (queueCount == 0u || queueCount > 64u) {
    return NULL;
  }

  device = calloc(1, sizeof(*device));
  native = calloc(1, sizeof(*native));
  if (!device || !native) {
    free(native);
    free(device);
    return NULL;
  }
  native->queues = calloc(queueCount, sizeof(*native->queues));
  if (!native->queues) {
    free(native);
    free(device);
    return NULL;
  }

  native->driver     = adapterNative->driver;
  native->cudaDevice = adapterNative->device;
  native->queueCount = queueCount;
  native->maxThreadsPerBlock = (uint32_t)adapterNative->maxThreadsPerBlock;
  for (uint32_t i = 0u; i < 3u; i++) {
    native->maxBlockDim[i] = (uint32_t)adapterNative->maxBlockDim[i];
    native->maxGridDim[i]  = (uint32_t)adapterNative->maxGridDim[i];
  }
  device->_priv      = native;
  device->inst       = adapter->inst;
  device->adapter    = adapter;
  device->queueFamilies = GPU_QUEUE_COMPUTE_BIT;
  device->uslTargetArchitecture =
    (uint32_t)(adapterNative->computeMajor * 10 +
               adapterNative->computeMinor);
  result = native->driver->primaryCtxRetain(&native->context,
                                             native->cudaDevice);
  if (result != CUDA_SUCCESS ||
      cuda_push(native->driver, native->context) != GPU_OK) {
    if (result == CUDA_SUCCESS) {
      (void)native->driver->primaryCtxRelease(native->cudaDevice);
    }
    free(native->queues);
    free(native);
    free(device);
    return NULL;
  }

  for (uint32_t i = 0u; i < queueCount; i++) {
    if (!cuda__initQueue(device, native, &native->queues[i])) {
      cuda_pop(native->driver);
      for (uint32_t j = 0u; j < i; j++) {
        cuda__destroyQueue(&native->queues[j]);
      }
      (void)native->driver->primaryCtxRelease(native->cudaDevice);
      free(native->queues);
      free(native);
      free(device);
      return NULL;
    }
  }
  cuda_pop(native->driver);
  return device;
}

static GPUResult
cuda_waitIdle(GPUDevice *device) {
  GPUDeviceCuda *native;
  GPUResult       result;

  native = cuda_device(device);
  if (!native) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = GPU_OK;
  for (uint32_t i = 0u; i < native->queueCount; i++) {
    GPUQueueCuda *queue;
    CUresult cudaResult;

    queue = &native->queues[i];
    cuda_queueLock(queue);
    if (cuda_push(native->driver, native->context) != GPU_OK) {
      cuda_queueUnlock(queue);
      result = GPU_ERROR_BACKEND_FAILURE;
      continue;
    }
    cudaResult = native->driver->streamSynchronize(queue->stream);
    cuda_pop(native->driver);
    cuda_queueUnlock(queue);
    if (cudaResult != CUDA_SUCCESS) {
      cuda_report(device, cudaResult, "stream synchronization");
      result = GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (uint32_t i = 0u; i < native->queueCount; i++) {
    GPUQueueCuda *queue;

    queue = &native->queues[i];
    cuda_queueLock(queue);
    while (queue->pendingCount > 0u) {
      cuda__queueWait(queue);
    }
    cuda_queueUnlock(queue);
  }
  return result;
}

static void
cuda_destroyDevice(GPUDevice *device) {
  GPUDeviceCuda *native;

  native = cuda_device(device);
  if (!native) {
    free(device);
    return;
  }
  for (uint32_t i = 0u; i < native->queueCount; i++) {
    cuda__destroyQueue(&native->queues[i]);
  }
  (void)native->driver->primaryCtxRelease(native->cudaDevice);
  free(native->queues);
  free(native);
  free(device);
}

void
cuda_initDevice(GPUApiDevice *api) {
  api->getAvailableAdapters        = cuda_getAvailableAdapters;
  api->selectAdapter               = cuda_selectAdapter;
  api->destroyAdapter              = cuda_destroyAdapter;
  api->getAdapterProperties        = cuda_getAdapterProperties;
  api->getAdapterIdentity          = cuda_getAdapterIdentity;
  api->supportsFeature             = cuda_supportsFeature;
  api->supportsSubgroupOperations  = cuda_supportsSubgroupOperations;
  api->getLimits                   = cuda_getLimits;
  api->getFormatCapabilities       = cuda_getFormatCapabilities;
  api->getSubgroupMatrixProperties = cuda_getSubgroupMatrixProperties;
  api->createDevice                = cuda_createDevice;
  api->waitIdle                    = cuda_waitIdle;
  api->destroyDevice               = cuda_destroyDevice;
}
