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

  *outBuffer = (GPUBuffer *)buffer;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyBuffer(GPUBuffer * __restrict buff) {
  if (!buff) {
    return;
  }

  [(id<MTLBuffer>)buff release];
}

GPU_HIDE
GPUResult
mt_writeBuffer(GPUCommandQueue * __restrict queue,
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

  buffer = (id<MTLBuffer>)buff;
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
mt_readBuffer(GPUCommandQueue * __restrict queue,
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

  buffer = (id<MTLBuffer>)buff;
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
void
mt_initBuff(GPUApiBuffer *api) {
  api->create  = mt_createBuffer;
  api->destroy = mt_destroyBuffer;
  api->write   = mt_writeBuffer;
  api->read    = mt_readBuffer;
}
