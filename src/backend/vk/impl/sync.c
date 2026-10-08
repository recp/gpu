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
vk_pipelineBarrier(GPUDeviceVk                 *device,
                   VkCommandBuffer              command,
                   VkPipelineStageFlags         srcStages,
                   VkPipelineStageFlags         dstStages,
                   uint32_t                     bufferBarrierCount,
                   const VkBufferMemoryBarrier *bufferBarriers,
                   uint32_t                     imageBarrierCount,
                   const VkImageMemoryBarrier  *imageBarriers) {
  VkBufferMemoryBarrier2KHR    buffers[VK_SYNC_BARRIER_CHUNK_SIZE];
  VkImageMemoryBarrier2KHR     images[VK_SYNC_BARRIER_CHUNK_SIZE];
  VkDependencyInfoKHR          dependency;
  const VkBufferMemoryBarrier *bufferSrc;
  VkBufferMemoryBarrier2KHR   *bufferDst;
  const VkImageMemoryBarrier  *imageSrc;
  VkImageMemoryBarrier2KHR    *imageDst;
  uint32_t                     bufferOffset;
  uint32_t                     imageOffset;
  uint32_t                     bufferCount;
  uint32_t                     imageCount;
  uint32_t                     bufferIndex;
  uint32_t                     imageIndex;

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

  while (bufferOffset < bufferBarrierCount || imageOffset < imageBarrierCount) {
    dependency = (VkDependencyInfoKHR){0};

    bufferCount = bufferBarrierCount - bufferOffset;

    if (bufferCount > VK_SYNC_BARRIER_CHUNK_SIZE) {
      bufferCount = VK_SYNC_BARRIER_CHUNK_SIZE;
    }

    imageCount = imageBarrierCount - imageOffset;

    if (imageCount > VK_SYNC_BARRIER_CHUNK_SIZE) {
      imageCount = VK_SYNC_BARRIER_CHUNK_SIZE;
    }

    for (bufferIndex = 0u; bufferIndex < bufferCount; bufferIndex++) {
      bufferSrc = &bufferBarriers[bufferOffset + bufferIndex];
      bufferDst = &buffers[bufferIndex];
      memset(bufferDst, 0, sizeof(*bufferDst));
      bufferDst->sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR;
      bufferDst->srcStageMask        = srcStages;
      bufferDst->srcAccessMask       = bufferSrc->srcAccessMask;
      bufferDst->dstStageMask        = dstStages;
      bufferDst->dstAccessMask       = bufferSrc->dstAccessMask;
      bufferDst->srcQueueFamilyIndex = bufferSrc->srcQueueFamilyIndex;
      bufferDst->dstQueueFamilyIndex = bufferSrc->dstQueueFamilyIndex;
      bufferDst->buffer              = bufferSrc->buffer;
      bufferDst->offset              = bufferSrc->offset;
      bufferDst->size                = bufferSrc->size;
    }

    for (imageIndex = 0u; imageIndex < imageCount; imageIndex++) {
      imageSrc = &imageBarriers[imageOffset + imageIndex];
      imageDst = &images[imageIndex];
      memset(imageDst, 0, sizeof(*imageDst));
      imageDst->sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
      imageDst->srcStageMask        = srcStages;
      imageDst->srcAccessMask       = imageSrc->srcAccessMask;
      imageDst->dstStageMask        = dstStages;
      imageDst->dstAccessMask       = imageSrc->dstAccessMask;
      imageDst->oldLayout           = imageSrc->oldLayout;
      imageDst->newLayout           = imageSrc->newLayout;
      imageDst->srcQueueFamilyIndex = imageSrc->srcQueueFamilyIndex;
      imageDst->dstQueueFamilyIndex = imageSrc->dstQueueFamilyIndex;
      imageDst->image               = imageSrc->image;
      imageDst->subresourceRange    = imageSrc->subresourceRange;
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
