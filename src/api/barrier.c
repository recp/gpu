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
#include "cmdqueue_internal.h"

static bool
gpu_validStageMask(GPUPipelineStageMask stages) {
  const uint32_t knownMask = GPU_STAGE_TOP |
                             GPU_STAGE_VERTEX |
                             GPU_STAGE_FRAGMENT |
                             GPU_STAGE_COMPUTE |
                             GPU_STAGE_TRANSFER |
                             GPU_STAGE_BOTTOM;

  return stages != 0u && (((uint32_t)stages & ~knownMask) == 0u);
}

static bool
gpu_validAccessMask(GPUAccessMask access) {
  const uint32_t knownMask = GPU_ACCESS_SHADER_READ |
                             GPU_ACCESS_SHADER_WRITE |
                             GPU_ACCESS_COLOR_READ |
                             GPU_ACCESS_COLOR_WRITE |
                             GPU_ACCESS_DEPTH_READ |
                             GPU_ACCESS_DEPTH_WRITE |
                             GPU_ACCESS_TRANSFER_READ |
                             GPU_ACCESS_TRANSFER_WRITE |
                             GPU_ACCESS_INDIRECT_READ;

  return (((uint32_t)access & ~knownMask) == 0u);
}

static bool
gpu_validBarrierBatch(const GPUBarrierBatch *barriers) {
  if (!barriers) {
    return false;
  }
  if (!gpu_validStageMask(barriers->srcStages) ||
      !gpu_validStageMask(barriers->dstStages)) {
    return false;
  }
  if ((barriers->bufferBarrierCount > 0u && !barriers->pBufferBarriers) ||
      (barriers->textureBarrierCount > 0u && !barriers->pTextureBarriers)) {
    return false;
  }

  for (uint32_t i = 0; i < barriers->bufferBarrierCount; i++) {
    const GPUBufferBarrier *barrier = &barriers->pBufferBarriers[i];

    if (!barrier->buffer || barrier->sizeBytes == 0u ||
        !gpu_validAccessMask(barrier->srcAccess) ||
        !gpu_validAccessMask(barrier->dstAccess)) {
      return false;
    }
  }

  for (uint32_t i = 0; i < barriers->textureBarrierCount; i++) {
    const GPUTextureBarrier *barrier = &barriers->pTextureBarriers[i];

    if (!barrier->texture || barrier->mipCount == 0u || barrier->layerCount == 0u ||
        !gpu_validAccessMask(barrier->srcAccess) ||
        !gpu_validAccessMask(barrier->dstAccess)) {
      return false;
    }
  }

  return barriers->bufferBarrierCount > 0u || barriers->textureBarrierCount > 0u;
}

GPU_EXPORT
void
GPUEncodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !gpu_validBarrierBatch(barriers)) {
    return;
  }

}
