/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPUBufferUsage
webgpu_bufferUsage(GPUBufferUsageFlags usage) {
  WGPUBufferUsage result;

  result = WGPUBufferUsage_None;
  if (usage & GPU_BUFFER_USAGE_VERTEX)   result |= WGPUBufferUsage_Vertex;
  if (usage & GPU_BUFFER_USAGE_INDEX)    result |= WGPUBufferUsage_Index;
  if (usage & GPU_BUFFER_USAGE_UNIFORM)  result |= WGPUBufferUsage_Uniform;
  if (usage & GPU_BUFFER_USAGE_STORAGE)  result |= WGPUBufferUsage_Storage;
  if (usage & GPU_BUFFER_USAGE_COPY_SRC) result |= WGPUBufferUsage_CopySrc;
  if (usage & GPU_BUFFER_USAGE_COPY_DST) {
    result |= WGPUBufferUsage_CopyDst | WGPUBufferUsage_QueryResolve;
  }
  if (usage & GPU_BUFFER_USAGE_INDIRECT) result |= WGPUBufferUsage_Indirect;
  return result;
}

static GPUResult
webgpu_createBuffer(GPUDevice                 * __restrict device,
                    const GPUBufferCreateInfo * __restrict info,
                    GPUBuffer                ** __restrict outBuffer) {
  WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU     *native;
  GPUBuffer           *buffer;
  WGPUBufferUsage      usage;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->usage &
      (GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT |
       GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT |
       GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  usage = webgpu_bufferUsage(info->usage);
  if (usage == WGPUBufferUsage_None) {
    return GPU_ERROR_UNSUPPORTED;
  }

  buffer = calloc(1, sizeof(*buffer));
  if (!buffer) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label = gpu_webgpuString(info->label);
  descriptor.usage = usage;
  descriptor.size  = info->sizeBytes;
  buffer->_priv    = wgpuDeviceCreateBuffer(native->device, &descriptor);
  if (!buffer->_priv) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outBuffer = buffer;
  return GPU_OK;
}

static void
webgpu_destroyBuffer(GPUBuffer * __restrict buffer) {
  if (!buffer) {
    return;
  }
  if (buffer->_priv) {
    wgpuBufferDestroy(buffer->_priv);
    wgpuBufferRelease(buffer->_priv);
  }
  free(buffer);
}

static GPUResult
webgpu_writeBuffer(GPUQueue  * __restrict queue,
                   GPUBuffer * __restrict buffer,
                   uint64_t               dstOffset,
                   const void * __restrict data,
                   uint64_t               sizeBytes) {
  GPUDeviceWebGPU *native;

  native = gpu_webgpuDevice(gpuCommandQueueDevice(queue));
  if (!native || !native->queue || !buffer || !buffer->_priv || !data ||
      sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  wgpuQueueWriteBuffer(native->queue,
                       buffer->_priv,
                       dstOffset,
                       data,
                       (size_t)sizeBytes);
  return GPU_OK;
}

static GPUResult
webgpu_readBuffer(GPUQueue  * __restrict queue,
                  GPUBuffer * __restrict buffer,
                  uint64_t               srcOffset,
                  void      * __restrict outData,
                  uint64_t               sizeBytes) {
  GPU__UNUSED(queue);
  GPU__UNUSED(buffer);
  GPU__UNUSED(srcOffset);
  GPU__UNUSED(outData);
  GPU__UNUSED(sizeBytes);
  return GPU_ERROR_UNSUPPORTED;
}

static void *
webgpu_bufferContents(GPUBuffer * __restrict buffer) {
  GPU__UNUSED(buffer);
  return NULL;
}

void
webgpu_initBuffer(GPUApiBuffer *api) {
  api->create   = webgpu_createBuffer;
  api->destroy  = webgpu_destroyBuffer;
  api->write    = webgpu_writeBuffer;
  api->read     = webgpu_readBuffer;
  api->contents = webgpu_bufferContents;
}
