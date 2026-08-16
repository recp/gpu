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
      free(command->paramData);
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
cuda_getAvailableAdapters(GPUInstance * __restrict inst,
                          uint32_t                  maxNumberOfItems) {
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
        driver->deviceGetAttribute(&candidate.warpSize,
                                   CU_DEVICE_ATTRIBUTE_WARP_SIZE,
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
        candidate.maxThreadsPerBlock <= 0 || candidate.warpSize <= 0 ||
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
cuda_selectAdapter(GPUInstance       * __restrict inst,
                   GPUAdapter        * __restrict adapters,
                   GPUPowerPreference             powerPreference) {
  GPU__UNUSED(inst);
  GPU__UNUSED(powerPreference);
  return adapters;
}

static void
cuda_destroyAdapter(GPUAdapter * __restrict adapter) {
  if (adapter) {
    free(adapter->_priv);
    free(adapter);
  }
}

static GPUResult
cuda_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                          GPUAdapterProperties * __restrict outProperties) {
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
cuda_getAdapterIdentity(const GPUAdapter   * __restrict adapter,
                        GPUAdapterIdentity * __restrict outIdentity) {
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
cuda_hasSubgroups(const GPUAdapterCuda *adapter) {
  return adapter && adapter->computeMajor >= 3 && adapter->warpSize == 32;
}

static bool
cuda_hasShaderF16(const GPUAdapterCuda *adapter) {
  return adapter &&
         (adapter->computeMajor > 5 ||
          (adapter->computeMajor == 5 && adapter->computeMinor >= 3));
}

static bool
cuda_hasSubgroupMatrix(const GPUAdapterCuda *adapter) {
  return cuda_hasSubgroups(adapter) && adapter->computeMajor >= 7;
}

static bool
cuda_hasAtomic64(const GPUAdapterCuda *adapter) {
  return adapter && adapter->computeMajor >= 5;
}

static bool
cuda_supportsFeature(const GPUAdapter * __restrict adapter,
                     GPUFeature                    feature) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native) {
    return false;
  }
  switch (feature) {
    case GPU_FEATURE_COMPUTE:
    case GPU_FEATURE_DESCRIPTOR_INDEXING:
      return true;
    case GPU_FEATURE_SUBGROUPS:
      return cuda_hasSubgroups(native);
    case GPU_FEATURE_SHADER_F16:
      return cuda_hasShaderF16(native);
    case GPU_FEATURE_SUBGROUP_MATRIX:
      return cuda_hasSubgroupMatrix(native);
    case GPU_FEATURE_ATOMIC64:
      return cuda_hasAtomic64(native);
    case GPU_FEATURE_BUFFER_DEVICE_ADDRESS:
      return native->unifiedAddressing != 0;
    default:
      return false;
  }
}

static bool
cuda_supportsSubgroupOperations(
  const GPUAdapter                * __restrict adapter,
  GPUShaderStageFlags                          stage,
  GPUBackendSubgroupOperationFlags             operations
) {
  const GPUBackendSubgroupOperationFlags supported =
    GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  return cuda_hasSubgroups(native) && stage == GPU_SHADER_STAGE_COMPUTE_BIT &&
         operations != 0u && (operations & ~supported) == 0u;
}

static void
cuda_getLimits(const GPUAdapter * __restrict adapter,
               GPULimits       * __restrict outLimits) {
  GPUAdapterCuda *native;

  native = cuda_adapter(adapter);
  if (!native || !outLimits) {
    return;
  }
  memset(outLimits, 0, sizeof(*outLimits));
  outLimits->maxBindGroups       = GPU_ENCODER_MAX_BIND_GROUPS;
  outLimits->maxBindingsPerGroup = 64u;
  outLimits->minUniformBufferOffsetAlignment = 1u;
  outLimits->minStorageBufferOffsetAlignment = 1u;
  outLimits->maxComputeWorkgroupSizeX = (uint32_t)native->maxBlockDim[0];
  outLimits->maxComputeWorkgroupSizeY = (uint32_t)native->maxBlockDim[1];
  outLimits->maxComputeWorkgroupSizeZ = (uint32_t)native->maxBlockDim[2];
  if (cuda_hasSubgroups(native)) {
    outLimits->minSubgroupSize = (uint32_t)native->warpSize;
    outLimits->maxSubgroupSize = (uint32_t)native->warpSize;
  }
}

static void
cuda_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat                          format,
  GPUFormatCapabilities * __restrict outCapabilities
) {
  if (outCapabilities) {
    memset(outCapabilities, 0, sizeof(*outCapabilities));
    if (cuda_adapter(adapter) && format == GPU_FORMAT_RGBA32_FLOAT) {
      outCapabilities->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT;
      outCapabilities->sampled               = true;
      outCapabilities->filterable            = true;
      outCapabilities->storage               = true;
    }
  }
}

static GPUResult
cuda_getSubgroupMatrixProperties(
  const GPUAdapter               * __restrict adapter,
  uint32_t                       * __restrict inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT * __restrict outProperties
) {
  GPUAdapterCuda *native;
  uint32_t        capacity;

  native = cuda_adapter(adapter);
  if (!native || !inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!cuda_hasSubgroupMatrix(native)) {
    *inoutPropertyCount = 0u;
    return GPU_ERROR_UNSUPPORTED;
  }

  capacity = *inoutPropertyCount;
  *inoutPropertyCount = 1u;
  if (outProperties && capacity > 0u) {
    memset(outProperties, 0, sizeof(*outProperties));
    outProperties->m          = 16u;
    outProperties->n          = 16u;
    outProperties->k          = 16u;
    outProperties->aType      = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
    outProperties->bType      = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
    outProperties->cType      = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
    outProperties->resultType = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
    outProperties->stages     = GPU_SHADER_STAGE_COMPUTE_BIT;
    outProperties->scope      = GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
  }
  return outProperties && capacity == 0u
           ? GPU_ERROR_INSUFFICIENT_CAPACITY
           : GPU_OK;
}

static GPUDevice *
cuda_createDevice(GPUAdapter               * __restrict adapter,
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
  supportedMask = (1ull << GPU_FEATURE_COMPUTE) |
                  (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING);
  if (cuda_hasSubgroups(adapterNative)) {
    supportedMask |= 1ull << GPU_FEATURE_SUBGROUPS;
  }
  if (cuda_hasShaderF16(adapterNative)) {
    supportedMask |= 1ull << GPU_FEATURE_SHADER_F16;
  }
  if (cuda_hasSubgroupMatrix(adapterNative)) {
    supportedMask |= 1ull << GPU_FEATURE_SUBGROUP_MATRIX;
  }
  if (cuda_hasAtomic64(adapterNative)) {
    supportedMask |= 1ull << GPU_FEATURE_ATOMIC64;
  }
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
  device->uslBoundedDescriptorIndexing =
    (enabledFeatureMask &
     (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING)) != 0u;
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
cuda_waitIdle(GPUDevice * __restrict device) {
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
cuda_destroyDevice(GPUDevice * __restrict device) {
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
