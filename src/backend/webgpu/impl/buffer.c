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
#include "../impl.h"

#if !defined(__EMSCRIPTEN__)
typedef struct WebGPUBufferMapRequest {
  atomic_int status;
} WebGPUBufferMapRequest;

static void
webgpu_bufferMapped(WGPUMapAsyncStatus status,
                    WGPUStringView     message,
                    void              *userData,
                    void              *unused) {
  WebGPUBufferMapRequest *request;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  request = userData;
  atomic_store_explicit(&request->status, status, memory_order_release);
}
#endif

static WGPUBufferUsage
webgpu_bufferUsage(GPUBufferUsageFlags usage) {
  WGPUBufferUsage result;

  result = WGPUBufferUsage_None;

  if (usage & GPU_BUFFER_USAGE_VERTEX)
    result |= WGPUBufferUsage_Vertex;

  if (usage & GPU_BUFFER_USAGE_INDEX)
    result |= WGPUBufferUsage_Index;

  if (usage & GPU_BUFFER_USAGE_UNIFORM)
    result |= WGPUBufferUsage_Uniform;

  /* texel views are read-only storage arrays here */
  if (usage & (GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_TEXEL))
    result |= WGPUBufferUsage_Storage;

  if (usage & GPU_BUFFER_USAGE_COPY_SRC)
    result |= WGPUBufferUsage_CopySrc;

  /* map-read combines with copy-destination alone */
  if (usage & GPU_BUFFER_USAGE_HOST_READ)
    return WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;

  if (usage & GPU_BUFFER_USAGE_COPY_DST) {
    result |= WGPUBufferUsage_CopyDst | WGPUBufferUsage_QueryResolve;
  }

  if (usage & GPU_BUFFER_USAGE_INDIRECT)
    result |= WGPUBufferUsage_Indirect;

  return result;
}

/* a WGPUBuffer each: WebGPU checks a pass's usages per whole buffer, so
 * storage buffers can't share one, and pooling the rest measured too small
 * to pay */

static GPUResult
webgpu_createBuffer(GPUDevice                 *__restrict device,
                    const GPUBufferCreateInfo *__restrict info,
                    GPUBuffer                **__restrict outBuffer) {
  WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU     *native;
  GPUBuffer           *buffer;
  WGPUBufferUsage      usage;
  uint64_t             nativeSize;

  native = webgpuDevice(device);

  if (!native || !native->device || !info || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->usage &
      (GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT |
       GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT |
       GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (info->sizeBytes > native->limits.maxBufferSize) {
    return GPU_ERROR_UNSUPPORTED;
  }

  usage = webgpu_bufferUsage(info->usage);

  if (usage == WGPUBufferUsage_None) {
    return GPU_ERROR_UNSUPPORTED;
  }

  nativeSize = (info->sizeBytes + 3u) & ~UINT64_C(3);

  if (nativeSize < info->sizeBytes || nativeSize > native->limits.maxBufferSize) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(buffer = calloc(1, sizeof(*buffer)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label = webgpuString(info->label);
  descriptor.usage = usage;
  descriptor.size  = nativeSize;

  if (!(buffer->_priv = wgpuDeviceCreateBuffer(native->device, &descriptor))) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outBuffer = buffer;

  return GPU_OK;
}

static void
webgpu_destroyBuffer(GPUBuffer *__restrict buffer) {
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
webgpu_writeBuffer(GPUQueue   *__restrict queue,
                   GPUBuffer  *__restrict buffer,
                   uint64_t               dstOffset,
                   const void *__restrict data,
                   uint64_t               sizeBytes) {
  GPUDeviceWebGPU *native;
  uint64_t         alignedSize;
  uint32_t         tail;
  uint32_t         tailSize;

  native = webgpuDevice(commandQueueDevice(queue));

  if (!native || !native->queue || !buffer || !buffer->_priv || !data
      || sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((dstOffset & 3u) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  alignedSize = sizeBytes & ~UINT64_C(3);
  tailSize    = (uint32_t)(sizeBytes - alignedSize);

  if (tailSize != 0u && dstOffset + sizeBytes != buffer->sizeBytes) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!webgpuQueueWrite(native, sizeBytes)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (alignedSize != 0u) {
    wgpuQueueWriteBuffer(native->queue,
                         buffer->_priv,
                         dstOffset,
                         data,
                         (size_t)alignedSize);
  }

  if (tailSize != 0u) {
    tail = 0u;
    memcpy(&tail, (const uint8_t *)data + alignedSize, tailSize);
    wgpuQueueWriteBuffer(native->queue,
                         buffer->_priv,
                         dstOffset + alignedSize,
                         &tail,
                         sizeof(tail));
  }

  return GPU_OK;
}

#if !defined(__EMSCRIPTEN__)
/* maps [offset, offset + size) of a map-read buffer, waits for it and copies
 * sizeBytes from prefix bytes in; mapping waits for the work given before
 * that writes the buffer */
static GPUResult
webgpu_mapRead(GPUDevice  *device,
               WGPUBuffer  buffer,
               uint64_t    offset,
               uint64_t    size,
               uint64_t    prefix,
               void       *outData,
               uint64_t    sizeBytes) {
  WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
#if !GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  WGPUFutureWaitInfo        waitInfo = WGPU_FUTURE_WAIT_INFO_INIT;
  GPUInstanceWebGPU        *instance;
  WGPUWaitStatus            waitStatus;
#else
  GPUDeviceWebGPU          *native;
#endif
  WebGPUBufferMapRequest    mapRequest = {0};
  const uint8_t            *mapped;
  int                       mapStatus;

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  if (!(native = webgpuDevice(device)) || !native->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  callbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
#else
  instance = webgpuInstance(device->inst);

  if (!instance || !instance->instance) {
    return GPU_ERROR_UNSUPPORTED;
  }

  callbackInfo.mode      = WGPUCallbackMode_WaitAnyOnly;
#endif
  callbackInfo.callback  = webgpu_bufferMapped;
  callbackInfo.userdata1 = &mapRequest;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  wgpuBufferMapAsync(buffer, WGPUMapMode_Read, (size_t)offset, (size_t)size, callbackInfo);

  do {
    wgpuDevicePoll(native->device, true, NULL);
    mapStatus = atomic_load_explicit(&mapRequest.status, memory_order_acquire);
  } while (mapStatus == 0);
#else
  waitInfo.future = wgpuBufferMapAsync(buffer, WGPUMapMode_Read, (size_t)offset, (size_t)size, callbackInfo);
  waitStatus      = wgpuInstanceWaitAny(instance->instance, 1u, &waitInfo, UINT64_MAX);
  mapStatus       = atomic_load_explicit(&mapRequest.status, memory_order_acquire);

  if (waitStatus != WGPUWaitStatus_Success || !waitInfo.completed) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  if (mapStatus != WGPUMapAsyncStatus_Success) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!(mapped = wgpuBufferGetConstMappedRange(buffer, (size_t)offset, (size_t)size))) {
    wgpuBufferUnmap(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(outData, mapped + prefix, (size_t)sizeBytes);
  wgpuBufferUnmap(buffer);

  return GPU_OK;
}
#endif

static GPUResult
webgpu_readBuffer(GPUQueue  *__restrict queue,
                  GPUBuffer *__restrict buffer,
                  uint64_t              srcOffset,
                  void      *__restrict outData,
                  uint64_t              sizeBytes) {
#if !defined(__EMSCRIPTEN__)
  WGPUBufferDescriptor         bufferInfo  = WGPU_BUFFER_DESCRIPTOR_INIT;
  WGPUCommandEncoderDescriptor encoderInfo = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  WGPUCommandBufferDescriptor  commandInfo = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  GPUDevice                   *device;
  GPUDeviceWebGPU             *native;
  WGPUCommandEncoder           encoder;
  WGPUCommandBuffer            command;
  WGPUBuffer                   staging;
  uint64_t                     copyOffset;
  uint64_t                     copySize;
  uint64_t                     prefix;
  GPUResult                    result;

  device = commandQueueDevice(queue);
  native = webgpuDevice(device);

  if (!native || !native->device || !native->queue
      || !buffer || !buffer->_priv || !outData || sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
#if GPU_WEBGPU_PROVIDER_DAWN
  if (!webgpuInstance(device->inst) || !webgpuInstance(device->inst)->timedWaitAny) {
    return GPU_ERROR_UNSUPPORTED;
  }
#endif

  /* from the background Dawn would commit what a map read waits behind, and
   * a copy to read through couldn't go (webgpuGPURefused) */
  if (webgpuMapReadRefused(native)
      || (webgpuGPURefused() && (buffer->usage & GPU_BUFFER_USAGE_HOST_READ) == 0u)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  /* a map-read buffer (HOST_READ) maps itself: offsets in 8s, sizes in 4s */
  if ((buffer->usage & GPU_BUFFER_USAGE_HOST_READ) != 0u) {
    copyOffset = srcOffset & ~UINT64_C(7);
    prefix     = srcOffset - copyOffset;
    copySize   = (prefix + sizeBytes + 3u) & ~UINT64_C(3);

    return webgpu_mapRead(device, buffer->_priv, copyOffset, copySize, prefix, outData, sizeBytes);
  }

  copyOffset = srcOffset & ~UINT64_C(3);
  prefix     = srcOffset - copyOffset;
  copySize   = (prefix + sizeBytes + 3u) & ~UINT64_C(3);

  if (copySize < sizeBytes || copySize > SIZE_MAX) {
    return GPU_ERROR_UNSUPPORTED;
  }

  bufferInfo.label = webgpuString("gpu-readback");
  bufferInfo.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
  bufferInfo.size  = copySize;

  if (!(staging = wgpuDeviceCreateBuffer(native->device, &bufferInfo))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  result = GPU_ERROR_BACKEND_FAILURE;

  if (!(encoder = wgpuDeviceCreateCommandEncoder(native->device, &encoderInfo))) {
    goto cleanup_staging;
  }

  wgpuCommandEncoderCopyBufferToBuffer(encoder,
                                       buffer->_priv,
                                       copyOffset,
                                       staging,
                                       0u,
                                       copySize);
  command = wgpuCommandEncoderFinish(encoder, &commandInfo);
  wgpuCommandEncoderRelease(encoder);

  if (!command) {
    goto cleanup_staging;
  }

  webgpuQueueWritesSubmitted(native);
  wgpuQueueSubmit(native->queue, 1u, &command);
  wgpuCommandBufferRelease(command);
  result = webgpu_mapRead(device, staging, 0u, copySize, prefix, outData, sizeBytes);

cleanup_staging:
  wgpuBufferDestroy(staging);
  wgpuBufferRelease(staging);
  return result;
#else
  GPU__UNUSED(queue);
  GPU__UNUSED(buffer);
  GPU__UNUSED(srcOffset);
  GPU__UNUSED(outData);
  GPU__UNUSED(sizeBytes);
  return GPU_ERROR_UNSUPPORTED;
#endif
}

static void*
webgpu_bufferContents(GPUBuffer *__restrict buffer) {
  GPU__UNUSED(buffer);
  return NULL;
}

void
webgpu_initBuffer(GPUBufferApi *api) {
  api->create   = webgpu_createBuffer;
  api->destroy  = webgpu_destroyBuffer;
  api->write    = webgpu_writeBuffer;
  api->read     = webgpu_readBuffer;
  api->contents = webgpu_bufferContents;
}
