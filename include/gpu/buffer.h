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

#ifndef gpu_buffer_h
#define gpu_buffer_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"

typedef struct GPUBuffer GPUBuffer;
typedef struct GPUDevice GPUDevice;

typedef uint32_t GPUBufferUsageFlags;
enum {
  GPU_BUFFER_USAGE_VERTEX    = 1u << 0,
  GPU_BUFFER_USAGE_INDEX     = 1u << 1,
  GPU_BUFFER_USAGE_UNIFORM   = 1u << 2,
  GPU_BUFFER_USAGE_STORAGE   = 1u << 3,
  GPU_BUFFER_USAGE_COPY_SRC  = 1u << 4,
  GPU_BUFFER_USAGE_COPY_DST  = 1u << 5,
  GPU_BUFFER_USAGE_INDIRECT  = 1u << 6
};

typedef struct GPUBufferCreateInfo {
  GPUChainedStruct chain;
  const char       *label;
  uint64_t          sizeBytes;
  GPUBufferUsageFlags usage;
} GPUBufferCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer);

GPU_EXPORT
void
GPUDestroyBuffer(GPUBuffer * __restrict buff);

GPU_EXPORT
GPUResult
GPUQueueWriteBuffer(GPUCommandQueue * __restrict queue,
                    GPUBuffer       * __restrict buff,
                    uint64_t                     dstOffset,
                    const void      * __restrict data,
                    uint64_t                     sizeBytes);

GPU_EXPORT
GPUResult
GPUQueueReadBuffer(GPUCommandQueue * __restrict queue,
                   GPUBuffer       * __restrict buff,
                   uint64_t                     srcOffset,
                   void           * __restrict outData,
                   uint64_t                     sizeBytes);

#ifdef __cplusplus
}
#endif
#endif /* gpu_buffer_h */
