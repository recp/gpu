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
#include "format_internal.h"
#include "texture_internal.h"

enum {
  UPLOAD_MAX_SLOTS = 8u,
  UPLOAD_ALIGNMENT = 256u
};

typedef struct UploadSlot {
  GPUBuffer *buffer;
  GPUFence  *fence;
  uint8_t   *host;
  uint64_t   used;
} UploadSlot;

struct GPUUploader {
  GPUQueue               *queue;
  GPUCommandBuffer       *command;
  GPUTransferPassEncoder *pass;
  uint64_t                capacity;
  uint32_t                slotCount;
  uint32_t                current;
  UploadSlot              slots[UPLOAD_MAX_SLOTS];
};

static GPUResult
beginUpload(GPUUploader *uploader, uint64_t sizeBytes, uint64_t *offset) {
  UploadSlot *slot;
  GPUResult   result;

  if (!sizeBytes || sizeBytes > uploader->capacity)
    return GPU_ERROR_INSUFFICIENT_CAPACITY;

  slot = &uploader->slots[uploader->current];
  *offset = (slot->used + UPLOAD_ALIGNMENT - 1u) & ~(uint64_t)(UPLOAD_ALIGNMENT - 1u);
  if (*offset > uploader->capacity || sizeBytes > uploader->capacity - *offset) {
    result = GPUFlushUploads(uploader, NULL);
    if (result != GPU_OK)
      return result;

    slot = &uploader->slots[uploader->current];
    *offset = 0u;
  }

  if (slot->fence) {
    result = GPUWaitFence(slot->fence, UINT64_MAX);
    if (result != GPU_OK)
      return result;

    GPUDestroyFence(slot->fence);
    slot->fence = NULL;
    slot->used  = 0u;
    *offset     = 0u;
  }

  if (!uploader->command) {
    if (GPUAcquireCommandBuffer(uploader->queue, "uploader batch", &uploader->command) != GPU_OK)
      return GPU_ERROR_BACKEND_FAILURE;

    uploader->command->_orderedUploads = true;
  }

  if (!uploader->pass
      && !(uploader->pass = GPUBeginTransferPass(uploader->command, "uploader copies")))
    return GPU_ERROR_BACKEND_FAILURE;

  slot->used = *offset + sizeBytes;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateUploader(GPUQueue *queue, const GPUUploaderCreateInfo *info, GPUUploader **outUploader) {
  GPUBufferCreateInfo bufferInfo = {0};
  GPUUploader        *uploader;
  GPUApi             *api;
  uint64_t            capacity;
  uint32_t            count, i;
  GPUResult           result;

  if (outUploader)
    *outUploader = NULL;

  if (!queue || !info || !outUploader
      || info->chain.sType != GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO
      || info->chain.structSize < sizeof(*info) || info->chain.pNext
      || info->stagingBufferCount > UPLOAD_MAX_SLOTS)
    return GPU_ERROR_INVALID_ARGUMENT;

  api = commandQueueApi(queue);
  if (!GPUIsFeatureEnabled(queue->_device, GPU_FEATURE_THREADED_UPLOADS)
      || !api || !api->cmdque.lock || !api->cmdque.unlock
      || !api->cmdque.commandBufferOnComplete || !api->cmdque.commit
      || !api->renderPass.copyBufferToBuffer || !api->renderPass.copyBufferToTexture)
    return GPU_ERROR_UNSUPPORTED;

  capacity = info->stagingSizeBytes ? info->stagingSizeBytes : 4u * 1024u * 1024u;
  count    = info->stagingBufferCount ? info->stagingBufferCount : 3u;
  if (capacity > SIZE_MAX - UPLOAD_ALIGNMENT)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!(uploader = calloc(1u, sizeof(*uploader))))
    return GPU_ERROR_OUT_OF_MEMORY;

  uploader->queue     = queue;
  uploader->capacity  = capacity;
  uploader->slotCount = count;
  bufferInfo.label     = info->label;
  bufferInfo.sizeBytes = capacity;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC;
  for (i = 0u; i < count; i++) {
    result = GPUCreateBuffer(queue->_device, &bufferInfo, &uploader->slots[i].buffer);
    if (result != GPU_OK)
      goto fail;

    if (!(uploader->slots[i].host = GPUGetBufferHostAddress(uploader->slots[i].buffer))) {
      result = GPU_ERROR_UNSUPPORTED;
      goto fail;
    }
  }

  *outUploader = uploader;
  return GPU_OK;

fail:
  GPUDestroyUploader(uploader);
  return result;
}

GPU_EXPORT
GPUResult
GPUUploadBuffer(GPUUploader *uploader, GPUBuffer *buffer, uint64_t offset, const void *data, uint64_t sizeBytes) {
  GPUBufferCopyRegion copy;
  UploadSlot         *slot;
  uint64_t            stagingOffset;
  GPUResult           result;

  if (!uploader || !data || !buffer || buffer->device != uploader->queue->_device
      || !bufferHasUsage(buffer, GPU_BUFFER_USAGE_COPY_DST) || !bufferRangeValid(buffer, offset, sizeBytes))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = beginUpload(uploader, sizeBytes, &stagingOffset);
  if (result != GPU_OK)
    return result;

  slot = &uploader->slots[uploader->current];
  memcpy(slot->host + stagingOffset, data, (size_t)sizeBytes);
  copy.srcOffset = stagingOffset;
  copy.dstOffset = offset;
  copy.sizeBytes = sizeBytes;
  GPUCopyBufferToBuffer(uploader->pass, slot->buffer, buffer, &copy);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUUploadTexture(GPUUploader *uploader, GPUTexture *texture, const GPUTextureWriteRegion *region,
                  const void *data, uint64_t sizeBytes) {
  GPUBufferTextureCopyRegion copy = {0};
  GPUFormatDataLayout        source, target;
  GPUFormatLayout            format;
  UploadSlot                *slot;
  const uint8_t             *bytes;
  uint8_t                   *destination;
  uint64_t                   offset, image, images;
  uint32_t                   pitch, row;
  GPUResult                  result;

  if (!uploader || !data || !textureWriteRegionValid(texture, region, sizeBytes, &source)
      || texture->device != uploader->queue->_device
      || !formatAspectLayout(texture->format, region->aspect, &format))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (source.bytesInLastRow > UINT32_MAX - (UPLOAD_ALIGNMENT - 1u))
    return GPU_ERROR_INSUFFICIENT_CAPACITY;

  pitch = (source.bytesInLastRow + UPLOAD_ALIGNMENT - 1u) & ~(uint32_t)(UPLOAD_ALIGNMENT - 1u);
  if (!formatAspectDataLayout(texture->format, region->aspect, region->width, region->height,
                              region->depth, region->layerCount, pitch, source.blockRows * format.blockHeight, &target))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = beginUpload(uploader, target.requiredBytes, &offset);
  if (result != GPU_OK)
    return result;

  slot        = &uploader->slots[uploader->current];
  bytes       = data;
  destination = slot->host + offset;
  images      = (uint64_t)region->depth * region->layerCount;
  for (image = 0u; image < images; image++) {
    for (row = 0u; row < source.blockRows; row++) {
      memcpy(destination + image * target.bytesPerImage + (uint64_t)row * pitch,
             bytes + image * source.bytesPerImage + (uint64_t)row * region->bytesPerRow,
             source.bytesInLastRow);
    }
  }

  copy.texture.texture.aspect         = region->aspect;
  copy.texture.texture.x              = region->x;
  copy.texture.texture.y              = region->y;
  copy.texture.texture.z              = region->z;
  copy.texture.texture.mipLevel       = region->mipLevel;
  copy.texture.texture.baseArrayLayer = region->baseArrayLayer;
  copy.texture.width                  = region->width;
  copy.texture.height                 = region->height;
  copy.texture.depth                  = region->depth;
  copy.texture.layerCount             = region->layerCount;
  copy.bufferOffset = offset;
  copy.bytesPerRow   = pitch;
  copy.rowsPerImage  = source.blockRows * format.blockHeight;
  GPUCopyBufferToTexture(uploader->pass, slot->buffer, texture, &copy);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUFlushUploads(GPUUploader *uploader, GPUFence **outFence) {
  GPUFenceCreateInfo emptyInfo = {0};
  GPUCommandBuffer *command;
  UploadSlot       *slot;
  GPUFence         *fence;
  GPUResult        result;

  if (outFence)
    *outFence = NULL;

  if (!uploader)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!uploader->command) {
    slot = &uploader->slots[(uploader->current + uploader->slotCount - 1u) % uploader->slotCount];
    if (slot->fence) {
      if (outFence) {
        retainFence(slot->fence);
        *outFence = slot->fence;
      }
      return GPU_OK;
    }

    if (!outFence)
      return GPU_OK;

    emptyInfo.signaled = true;
    return GPUCreateFence(uploader->queue->_device, &emptyInfo, outFence);
  }

  slot = &uploader->slots[uploader->current];
  if (slot->fence) {
    result = GPUWaitFence(slot->fence, UINT64_MAX);
    if (result != GPU_OK)
      return result;

    GPUDestroyFence(slot->fence);
    slot->fence = NULL;
  }

  if (uploader->pass) {
    GPUEndTransferPass(uploader->pass);
    uploader->pass = NULL;
  }

  result = GPUCreateFence(uploader->queue->_device, NULL, &fence);
  if (result != GPU_OK)
    return result;

  command           = uploader->command;
  uploader->command = NULL;
  slot->fence       = fence;
  result = submitUploadCommandBuffer(command, fence);
  if (result == GPU_OK && outFence) {
    retainFence(fence);
    *outFence = fence;
  }

  uploader->current = (uploader->current + 1u) % uploader->slotCount;
  uploader->slots[uploader->current].used = 0u;
  return result;
}

GPU_EXPORT
void
GPUDestroyUploader(GPUUploader *uploader) {
  uint32_t i;

  if (!uploader)
    return;

  if (uploader->command && GPUFlushUploads(uploader, NULL) != GPU_OK && uploader->command) {
    if (uploader->pass)
      GPUEndTransferPass(uploader->pass);

    GPUDiscardCommandBuffer(uploader->command);
  }

  for (i = 0u; i < uploader->slotCount; i++) {
    if (uploader->slots[i].fence) {
      GPUWaitFence(uploader->slots[i].fence, UINT64_MAX);
      GPUDestroyFence(uploader->slots[i].fence);
    }

    GPUDestroyBuffer(uploader->slots[i].buffer);
  }

  free(uploader);
}
