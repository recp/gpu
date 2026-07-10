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
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "device_internal.h"
#include "texture_internal.h"

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
gpu_validTextureAccess(const GPUTexture *texture, GPUAccessMask access) {
  GPUTextureUsageFlags usage;

  if (!texture || !gpu_validAccessMask(access) ||
      (access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    return false;
  }

  usage = texture->usage;
  if ((access & GPU_ACCESS_SHADER_READ) != 0u &&
      (usage & (GPU_TEXTURE_USAGE_SAMPLED |
                GPU_TEXTURE_USAGE_STORAGE)) == 0u) {
    return false;
  }
  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u &&
      (usage & GPU_TEXTURE_USAGE_STORAGE) == 0u) {
    return false;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u &&
      (usage & GPU_TEXTURE_USAGE_COLOR_TARGET) == 0u) {
    return false;
  }
  if ((access & (GPU_ACCESS_DEPTH_READ | GPU_ACCESS_DEPTH_WRITE)) != 0u &&
      (usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) == 0u) {
    return false;
  }
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u &&
      (usage & GPU_TEXTURE_USAGE_COPY_SRC) == 0u) {
    return false;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u &&
      (usage & GPU_TEXTURE_USAGE_COPY_DST) == 0u) {
    return false;
  }

  return true;
}

static bool
gpu_validBarrierBatch(GPUDevice *device, const GPUBarrierBatch *barriers) {
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

    if (!barrier->buffer || barrier->buffer->device != device ||
        !gpuBufferRangeValid(barrier->buffer,
                             barrier->offset,
                             barrier->sizeBytes) ||
        !gpu_validAccessMask(barrier->srcAccess) ||
        !gpu_validAccessMask(barrier->dstAccess)) {
      return false;
    }
  }

  for (uint32_t i = 0; i < barriers->textureBarrierCount; i++) {
    const GPUTextureBarrier *barrier = &barriers->pTextureBarriers[i];

    if (!barrier->texture || barrier->texture->device != device ||
        !gpuTextureSubresourceRangeValid(barrier->texture,
                                         barrier->baseMip,
                                         barrier->mipCount,
                                         barrier->baseLayer,
                                         barrier->layerCount) ||
        !gpu_validTextureAccess(barrier->texture, barrier->srcAccess) ||
        !gpu_validTextureAccess(barrier->texture, barrier->dstAccess)) {
      return false;
    }
  }

  return barriers->bufferBarrierCount > 0u || barriers->textureBarrierCount > 0u;
}

GPU_EXPORT
void
GPUEncodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUDevice *device;
  GPUApi    *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return;
  }

  device = cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!gpu_validBarrierBatch(device, barriers) ||
      !(api = gpuDeviceApi(device)) ||
      !api->renderPass.encodeBarriers) {
    return;
  }

  api->renderPass.encodeBarriers(cmdb, barriers);
}
