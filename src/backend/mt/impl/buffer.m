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
mt_wrapBuffer(GPUDevice                 *device,
              const GPUBufferCreateInfo *info,
              id<MTLBuffer>              nativeBuffer,
              GPUBuffer                **outBuffer) {
  GPUBuffer *buffer;

  if (!device || !info || !nativeBuffer || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    nativeBuffer.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  buffer = calloc(1, sizeof(*buffer));
  if (!buffer) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  buffer->_priv     = nativeBuffer;
  buffer->device    = device;
  buffer->sizeBytes = info->sizeBytes;
  buffer->usage     = info->usage;
  if (@available(macOS 13.0, iOS 16.0, *)) {
    buffer->_gpuAddress = nativeBuffer.gpuAddress;
  }
  *outBuffer = buffer;
  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_createBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer) {
  GPUDeviceMT *deviceMT;
  id<MTLBuffer> buffer;
  GPUResult result;
  
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

  result = mt_wrapBuffer(device, info, buffer, outBuffer);
  if (result != GPU_OK) {
    [buffer release];
    return result;
  }
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
  id<MTLBuffer> staging;
  id<MTLBlitCommandEncoder> blit;
  uint8_t    *contents;
  uint64_t    stagingOffset;
  GPUResult   result;

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
    result = mt_beginTransfer(queue,
                              sizeBytes,
                              &blit,
                              &staging,
                              &stagingOffset);
    if (result != GPU_OK) {
      return result;
    }
    contents = (uint8_t *)[staging contents];
    if (!contents) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    memcpy(contents + stagingOffset, data, (size_t)sizeBytes);
    [blit copyFromBuffer:staging
            sourceOffset:(NSUInteger)stagingOffset
                toBuffer:buffer
       destinationOffset:(NSUInteger)dstOffset
                    size:(NSUInteger)sizeBytes];
    return GPU_OK;
  }

  memcpy(contents + dstOffset, data, (size_t)sizeBytes);
#if TARGET_OS_OSX
  if (buffer.storageMode == MTLStorageModeManaged) {
    [buffer didModifyRange:NSMakeRange((NSUInteger)dstOffset,
                                      (NSUInteger)sizeBytes)];
  }
#endif
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
  id<MTLBuffer> staging;
  id<MTLBlitCommandEncoder> blit;
  const uint8_t *contents;
  uint64_t       stagingOffset;
  GPUResult      result;

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
    result = mt_beginTransfer(queue,
                              sizeBytes,
                              &blit,
                              &staging,
                              &stagingOffset);
    if (result != GPU_OK) {
      return result;
    }
    [blit copyFromBuffer:buffer
            sourceOffset:(NSUInteger)srcOffset
                toBuffer:staging
       destinationOffset:(NSUInteger)stagingOffset
                    size:(NSUInteger)sizeBytes];
    result = mt_flushTransfers(queue, true);
    if (result != GPU_OK) {
      return result;
    }
    contents = (const uint8_t *)[staging contents];
    if (!contents) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    memcpy(outData, contents + stagingOffset, (size_t)sizeBytes);
    return GPU_OK;
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
