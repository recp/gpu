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
#include "buffer_internal.h"

GPU_EXPORT
GPUResult
GPUCreateBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer) {
  GPUApi *api;

  if (!outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;

  if (!device || !info || info->sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->usage == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi())) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->buf.create) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  
  return api->buf.create(device, info, outBuffer);
}

GPU_EXPORT
void
GPUDestroyBuffer(GPUBuffer * __restrict buff) {
  GPUApi *api;

  if (!buff) {
    return;
  }

  if (!(api = gpuActiveGPUApi())) {
    return;
  }

  if (api->buf.destroy) {
    api->buf.destroy(buff);
  }
}

GPU_EXPORT
GPUResult
GPUQueueWriteBuffer(GPUCommandQueue * __restrict queue,
                    GPUBuffer       * __restrict buff,
                    uint64_t                     dstOffset,
                    const void      * __restrict data,
                    uint64_t                     sizeBytes) {
  GPUApi *api;

  if (!queue || !buff || !data || sizeBytes == 0 ||
      !gpuBufferHasUsage(buff, GPU_BUFFER_USAGE_COPY_DST) ||
      !gpuBufferRangeValid(buff, dstOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi())) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->buf.write) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->buf.write(queue, buff, dstOffset, data, sizeBytes);
}

GPU_EXPORT
GPUResult
GPUQueueReadBuffer(GPUCommandQueue * __restrict queue,
                   GPUBuffer       * __restrict buff,
                   uint64_t                     srcOffset,
                   void           * __restrict outData,
                   uint64_t                     sizeBytes) {
  GPUApi *api;

  if (!queue || !buff || !outData || sizeBytes == 0 ||
      !gpuBufferHasUsage(buff, GPU_BUFFER_USAGE_COPY_SRC) ||
      !gpuBufferRangeValid(buff, srcOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi())) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->buf.read) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->buf.read(queue, buff, srcOffset, outData, sizeBytes);
}
