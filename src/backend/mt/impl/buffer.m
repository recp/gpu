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
#include <string.h>

GPU_HIDE
GPUResult
mt_createBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer) {
  GPUDeviceMT *deviceMT;
  id<MTLBuffer> buffer;
  GPUBuffer *gpuBuffer;
  
  if (!device || !info || !outBuffer || info->sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceMT = device->_priv;
  if (info->sizeBytes > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = [deviceMT->device newBufferWithLength:(NSUInteger)info->sizeBytes
                                         options:MTLResourceStorageModeShared];
  if (!buffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    buffer.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  gpuBuffer = calloc(1, sizeof(*gpuBuffer));
  if (!gpuBuffer) {
    [buffer release];
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  gpuBuffer->_priv     = buffer;
  gpuBuffer->device    = device;
  gpuBuffer->sizeBytes = info->sizeBytes;
  gpuBuffer->usage     = info->usage;
  if (@available(macOS 13.0, iOS 16.0, *)) {
    gpuBuffer->_gpuAddress = buffer.gpuAddress;
  }
  *outBuffer = gpuBuffer;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyBuffer(GPUBuffer * __restrict buff) {
  if (!buff) {
    return;
  }

  if (buff->_priv) {
    [(id<MTLBuffer>)buff->_priv release];
  }
  free(buff);
}

GPU_HIDE
GPUResult
mt_writeBuffer(GPUQueue * __restrict queue,
               GPUBuffer       * __restrict buff,
               uint64_t                     dstOffset,
               const void      * __restrict data,
               uint64_t                     sizeBytes) {
  id<MTLBuffer> buffer;
  uint8_t *contents;

  (void)queue;

  if (!buff || !data || sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = (id<MTLBuffer>)buff->_priv;
  if (dstOffset > [buffer length] || sizeBytes > [buffer length] - dstOffset) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  contents = (uint8_t *)[buffer contents];
  if (!contents) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(contents + dstOffset, data, (size_t)sizeBytes);
  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_readBuffer(GPUQueue * __restrict queue,
              GPUBuffer       * __restrict buff,
              uint64_t                     srcOffset,
              void           * __restrict outData,
              uint64_t                     sizeBytes) {
  id<MTLBuffer> buffer;
  const uint8_t *contents;

  (void)queue;

  if (!buff || !outData || sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = (id<MTLBuffer>)buff->_priv;
  if (srcOffset > [buffer length] || sizeBytes > [buffer length] - srcOffset) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  contents = (const uint8_t *)[buffer contents];
  if (!contents) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(outData, contents + srcOffset, (size_t)sizeBytes);
  return GPU_OK;
}

GPU_HIDE
void*
mt_bufferContents(GPUBuffer * __restrict buff) {
  if (!buff) {
    return NULL;
  }

  return [(id<MTLBuffer>)buff->_priv contents];
}

GPU_HIDE
void
mt_initBuff(GPUApiBuffer *api) {
  api->create  = mt_createBuffer;
  api->destroy = mt_destroyBuffer;
  api->write   = mt_writeBuffer;
  api->read    = mt_readBuffer;
  api->contents = mt_bufferContents;
}
