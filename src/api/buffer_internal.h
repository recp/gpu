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

#ifndef gpu_buffer_internal_h
#define gpu_buffer_internal_h

#include "../common.h"
#include "device_internal.h"

struct GPUBuffer {
  void                *_priv;
  GPUDevice           *device;
  uint64_t             sizeBytes;
  GPUBufferUsageFlags  usage;
};

static inline GPUApi *
gpuBufferApi(const GPUBuffer *buffer) {
  return buffer ? gpuDeviceApi(buffer->device) : NULL;
}

static inline bool
gpuBufferHasUsage(const GPUBuffer *buffer, GPUBufferUsageFlags usage) {
  return buffer && (buffer->usage & usage) == usage;
}

static inline bool
gpuBufferRangeValid(const GPUBuffer *buffer,
                    uint64_t         offset,
                    uint64_t         sizeBytes) {
  return buffer &&
         sizeBytes > 0u &&
         offset <= buffer->sizeBytes &&
         sizeBytes <= buffer->sizeBytes - offset;
}

static inline bool
gpuBufferOffsetValid(const GPUBuffer *buffer, uint64_t offset) {
  return buffer && offset <= buffer->sizeBytes;
}

#endif /* gpu_buffer_internal_h */
