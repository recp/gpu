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

enum {
  VK_SYNC_BARRIER_CHUNK_SIZE = 16u
};

GPU_HIDE
void
vk_pipelineBarrier(GPUDeviceVk                *device,
                   VkCommandBuffer             command,
                   VkPipelineStageFlags        srcStages,
                   VkPipelineStageFlags        dstStages,
                   uint32_t                    bufferBarrierCount,
                   const VkBufferMemoryBarrier *bufferBarriers,
                   uint32_t                    imageBarrierCount,
                   const VkImageMemoryBarrier  *imageBarriers) {
  uint32_t bufferOffset;
  uint32_t imageOffset;

  if (!command || (bufferBarrierCount == 0u && imageBarrierCount == 0u)) {
    return;
  }
  if (!device || !device->synchronization2) {
    vkCmdPipelineBarrier(command,
                         srcStages,
                         dstStages,
                         0u,
                         0u,
                         NULL,
                         bufferBarrierCount,
                         bufferBarriers,
                         imageBarrierCount,
                         imageBarriers);
    return;
  }

  bufferOffset = 0u;
  imageOffset  = 0u;
  while (bufferOffset < bufferBarrierCount ||
         imageOffset < imageBarrierCount) {
    VkBufferMemoryBarrier2KHR buffers[VK_SYNC_BARRIER_CHUNK_SIZE];
    VkImageMemoryBarrier2KHR  images[VK_SYNC_BARRIER_CHUNK_SIZE];
    VkDependencyInfoKHR       dependency = {0};
    uint32_t                  bufferCount;
    uint32_t                  imageCount;

    bufferCount = bufferBarrierCount - bufferOffset;
    if (bufferCount > VK_SYNC_BARRIER_CHUNK_SIZE) {
      bufferCount = VK_SYNC_BARRIER_CHUNK_SIZE;
    }
    imageCount = imageBarrierCount - imageOffset;
    if (imageCount > VK_SYNC_BARRIER_CHUNK_SIZE) {
      imageCount = VK_SYNC_BARRIER_CHUNK_SIZE;
    }

    for (uint32_t i = 0u; i < bufferCount; i++) {
      const VkBufferMemoryBarrier *src;
      VkBufferMemoryBarrier2KHR   *dst;

      src                      = &bufferBarriers[bufferOffset + i];
      dst                      = &buffers[i];
      memset(dst, 0, sizeof(*dst));
      dst->sType               =
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
      dst->srcStageMask        = srcStages;
      dst->srcAccessMask       = src->srcAccessMask;
      dst->dstStageMask        = dstStages;
      dst->dstAccessMask       = src->dstAccessMask;
      dst->srcQueueFamilyIndex = src->srcQueueFamilyIndex;
      dst->dstQueueFamilyIndex = src->dstQueueFamilyIndex;
      dst->buffer              = src->buffer;
      dst->offset              = src->offset;
      dst->size                = src->size;
    }
    for (uint32_t i = 0u; i < imageCount; i++) {
      const VkImageMemoryBarrier *src;
      VkImageMemoryBarrier2KHR   *dst;

      src                        = &imageBarriers[imageOffset + i];
      dst                        = &images[i];
      memset(dst, 0, sizeof(*dst));
      dst->sType                 =
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
      dst->srcStageMask          = srcStages;
      dst->srcAccessMask         = src->srcAccessMask;
      dst->dstStageMask          = dstStages;
      dst->dstAccessMask         = src->dstAccessMask;
      dst->oldLayout             = src->oldLayout;
      dst->newLayout             = src->newLayout;
      dst->srcQueueFamilyIndex   = src->srcQueueFamilyIndex;
      dst->dstQueueFamilyIndex   = src->dstQueueFamilyIndex;
      dst->image                 = src->image;
      dst->subresourceRange      = src->subresourceRange;
    }

    dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
    dependency.bufferMemoryBarrierCount = bufferCount;
    dependency.pBufferMemoryBarriers    = bufferCount > 0u ? buffers : NULL;
    dependency.imageMemoryBarrierCount  = imageCount;
    dependency.pImageMemoryBarriers     = imageCount > 0u ? images : NULL;
    device->pipelineBarrier2(command, &dependency);

    bufferOffset += bufferCount;
    imageOffset  += imageCount;
  }
}
