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

#ifndef gpu_barrier_h
#define gpu_barrier_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct GPUBuffer        GPUBuffer;
typedef struct GPUCommandBuffer GPUCommandBuffer;
typedef struct GPUTexture       GPUTexture;

typedef enum GPUAccessMask {
  GPU_ACCESS_NONE           = 0,
  GPU_ACCESS_SHADER_READ    = 1u << 0,
  GPU_ACCESS_SHADER_WRITE   = 1u << 1,
  GPU_ACCESS_COLOR_READ     = 1u << 2,
  GPU_ACCESS_COLOR_WRITE    = 1u << 3,
  GPU_ACCESS_DEPTH_READ     = 1u << 4,
  GPU_ACCESS_DEPTH_WRITE    = 1u << 5,
  GPU_ACCESS_TRANSFER_READ  = 1u << 6,
  GPU_ACCESS_TRANSFER_WRITE = 1u << 7,
  GPU_ACCESS_INDIRECT_READ  = 1u << 8
} GPUAccessMask;

typedef struct GPUBufferBarrier {
  GPUBuffer    *buffer;
  GPUAccessMask srcAccess;
  GPUAccessMask dstAccess;
  uint64_t      offset;
  uint64_t      sizeBytes;
} GPUBufferBarrier;

typedef struct GPUTextureBarrier {
  GPUTexture   *texture;
  GPUAccessMask srcAccess;
  GPUAccessMask dstAccess;
  uint32_t      baseMip;
  uint32_t      mipCount;
  uint32_t      baseLayer;
  uint32_t      layerCount;
} GPUTextureBarrier;

typedef struct GPUBarrierBatch {
  const GPUBufferBarrier  *pBufferBarriers;
  const GPUTextureBarrier *pTextureBarriers;
  GPUPipelineStageMask     srcStages;
  GPUPipelineStageMask     dstStages;
  uint32_t                 bufferBarrierCount;
  uint32_t                 textureBarrierCount;
} GPUBarrierBatch;

GPU_EXPORT
void
GPUEncodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers);

#ifdef __cplusplus
}
#endif
#endif /* gpu_barrier_h */
