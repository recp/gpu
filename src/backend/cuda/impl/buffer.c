/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUResult
cuda_createBuffer(GPUDevice                 *device,
                  const GPUBufferCreateInfo *info,
                  GPUBuffer                **outBuffer) {
  GPUDeviceCuda *deviceNative;
  GPUBufferCuda *native;
  GPUBuffer     *buffer;
  GPUBufferUsageFlags allowedUsage;
  CUresult       result;

  if (!device || !info || !outBuffer || info->sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  allowedUsage = (GPUBufferUsageFlags)(
    (uint32_t)GPU_BUFFER_USAGE_STORAGE |
    (uint32_t)GPU_BUFFER_USAGE_COPY_SRC |
    (uint32_t)GPU_BUFFER_USAGE_COPY_DST |
    (uint32_t)GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT
  );
  if ((info->usage & ~allowedUsage) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  *outBuffer   = NULL;
  deviceNative = cuda_device(device);
  if (!deviceNative) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = calloc(1, sizeof(*buffer));
  native = calloc(1, sizeof(*native));
  if (!buffer || !native) {
    free(native);
    free(buffer);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->driver = deviceNative->driver;
  if (cuda_push(native->driver, deviceNative->context) != GPU_OK) {
    free(native);
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = native->driver->memAlloc(&native->address, (size_t)info->sizeBytes);
  cuda_pop(native->driver);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "buffer allocation");
    free(native);
    free(buffer);
    return result == CUDA_ERROR_OUT_OF_MEMORY
             ? GPU_ERROR_OUT_OF_MEMORY
             : GPU_ERROR_BACKEND_FAILURE;
  }

  buffer->_priv      = native;
  buffer->_gpuAddress = native->address;
  *outBuffer         = buffer;
  return GPU_OK;
}

static void
cuda_destroyBuffer(GPUBuffer *buffer) {
  GPUBufferCuda *native;
  GPUDeviceCuda *device;

  native = buffer ? buffer->_priv : NULL;
  device = buffer ? cuda_device(buffer->device) : NULL;
  if (native && device &&
      cuda_push(native->driver, device->context) == GPU_OK) {
    (void)native->driver->memFree(native->address);
    if (native->externalMemory && native->driver->destroyExternalMemory) {
      (void)native->driver->destroyExternalMemory(native->externalMemory);
    }
    cuda_pop(native->driver);
  }
  free(native);
  free(buffer);
}

static GPUResult
cuda_writeBuffer(GPUQueue   *queue,
                 GPUBuffer  *buffer,
                 uint64_t    dstOffset,
                 const void *data,
                 uint64_t    sizeBytes) {
  GPUBufferCuda *native;
  GPUQueueCuda  *queueNative;
  CUresult       result;

  native      = buffer ? buffer->_priv : NULL;
  queueNative = cuda_queue(queue);
  if (!native || !queueNative || sizeBytes > SIZE_MAX ||
      native->address > UINT64_MAX - dstOffset) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  cuda_queueLock(queueNative);
  if (cuda_push(queueNative->driver, queueNative->context) != GPU_OK) {
    cuda_queueUnlock(queueNative);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = queueNative->driver->streamSynchronize(queueNative->stream);
  if (result == CUDA_SUCCESS) {
    result = queueNative->driver->memcpyHtoD(native->address + dstOffset,
                                             data,
                                             (size_t)sizeBytes);
  }
  cuda_pop(queueNative->driver);
  cuda_queueUnlock(queueNative);
  if (result != CUDA_SUCCESS) {
    cuda_report(queue->_device, result, "buffer upload");
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static GPUResult
cuda_readBuffer(GPUQueue  *queue,
                GPUBuffer *buffer,
                uint64_t   srcOffset,
                void      *outData,
                uint64_t   sizeBytes) {
  GPUBufferCuda *native;
  GPUQueueCuda  *queueNative;
  CUresult       result;

  native      = buffer ? buffer->_priv : NULL;
  queueNative = cuda_queue(queue);
  if (!native || !queueNative || sizeBytes > SIZE_MAX ||
      native->address > UINT64_MAX - srcOffset) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  cuda_queueLock(queueNative);
  if (cuda_push(queueNative->driver, queueNative->context) != GPU_OK) {
    cuda_queueUnlock(queueNative);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = queueNative->driver->streamSynchronize(queueNative->stream);
  if (result == CUDA_SUCCESS) {
    result = queueNative->driver->memcpyDtoH(outData,
                                             native->address + srcOffset,
                                             (size_t)sizeBytes);
  }
  cuda_pop(queueNative->driver);
  cuda_queueUnlock(queueNative);
  if (result != CUDA_SUCCESS) {
    cuda_report(queue->_device, result, "buffer readback");
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

void
cuda_initBuffer(GPUApiBuffer *api) {
  api->create  = cuda_createBuffer;
  api->destroy = cuda_destroyBuffer;
  api->write   = cuda_writeBuffer;
  api->read    = cuda_readBuffer;
}
