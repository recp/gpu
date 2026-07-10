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
#include "../../../api/buffer_internal.h"

enum {
  GPU_VK_BARRIER_CHUNK_SIZE = 16u
};

static VkPipelineStageFlags
vk__barrierStages(GPUPipelineStageMask stages) {
  VkPipelineStageFlags result;

  result = 0u;
  if ((stages & GPU_STAGE_TOP) != 0u) {
    result |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  }
  if ((stages & GPU_STAGE_VERTEX) != 0u) {
    result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  }
  if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
    result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  }
  if ((stages & GPU_STAGE_COMPUTE) != 0u) {
    result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  }
  if ((stages & GPU_STAGE_TRANSFER) != 0u) {
    result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  if ((stages & GPU_STAGE_BOTTOM) != 0u) {
    result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  }

  return result;
}

static VkAccessFlags
vk__barrierAccess(GPUAccessMask access) {
  VkAccessFlags result;

  result = 0u;
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    result |= VK_ACCESS_SHADER_READ_BIT;
  }
  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    result |= VK_ACCESS_SHADER_WRITE_BIT;
  }
  if ((access & GPU_ACCESS_COLOR_READ) != 0u) {
    result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
  }
  if ((access & GPU_ACCESS_COLOR_WRITE) != 0u) {
    result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if ((access & GPU_ACCESS_DEPTH_READ) != 0u) {
    result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }
  if ((access & GPU_ACCESS_DEPTH_WRITE) != 0u) {
    result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    result |= VK_ACCESS_TRANSFER_READ_BIT;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    result |= VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  if ((access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  }

  return result;
}

static VkAccessFlags
vk__bufferBarrierAccess(const GPUBuffer       *buffer,
                        GPUAccessMask          access,
                        GPUPipelineStageMask   stages) {
  VkAccessFlags result;

  result = vk__barrierAccess(access);
  if ((access & GPU_ACCESS_SHADER_READ) != 0u &&
      (stages & GPU_STAGE_VERTEX) != 0u) {
    if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_VERTEX)) {
      result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_INDEX)) {
      result |= VK_ACCESS_INDEX_READ_BIT;
    }
  }

  return result;
}

GPU_HIDE
void
vk_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUCommandBufferVk  *command;
  GPUDeviceVk         *device;
  VkPipelineStageFlags srcStages;
  VkPipelineStageFlags dstStages;
  uint32_t             offset;

  command = cmdb ? cmdb->_priv : NULL;
  device  = cmdb && cmdb->_queue && cmdb->_queue->_device
              ? cmdb->_queue->_device->_priv
              : NULL;
  if (!command || !command->command || !device || !barriers) {
    return;
  }

  srcStages = vk__barrierStages(barriers->srcStages);
  dstStages = vk__barrierStages(barriers->dstStages);
  offset    = 0u;
  while (offset < barriers->bufferBarrierCount) {
    VkBufferMemoryBarrier nativeBarriers[GPU_VK_BARRIER_CHUNK_SIZE];
    uint32_t              nativeCount;
    uint32_t              chunkCount;

    chunkCount = barriers->bufferBarrierCount - offset;
    if (chunkCount > GPU_VK_BARRIER_CHUNK_SIZE) {
      chunkCount = GPU_VK_BARRIER_CHUNK_SIZE;
    }

    nativeCount = 0u;
    for (uint32_t i = 0u; i < chunkCount; i++) {
      const GPUBufferBarrier *barrier;
      GPUBufferVk            *buffer;
      VkBufferMemoryBarrier  *native;

      barrier = &barriers->pBufferBarriers[offset + i];
      buffer  = barrier->buffer ? barrier->buffer->_priv : NULL;
      if (!buffer || !buffer->buffer || buffer->device != device->device) {
        continue;
      }

      native                      = &nativeBarriers[nativeCount++];
      memset(native, 0, sizeof(*native));
      native->sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      native->srcAccessMask       = vk__bufferBarrierAccess(barrier->buffer,
                                                            barrier->srcAccess,
                                                            barriers->srcStages);
      native->dstAccessMask       = vk__bufferBarrierAccess(barrier->buffer,
                                                            barrier->dstAccess,
                                                            barriers->dstStages);
      native->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      native->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      native->buffer              = buffer->buffer;
      native->offset              = barrier->offset;
      native->size                = barrier->sizeBytes;
    }

    if (nativeCount > 0u) {
      vkCmdPipelineBarrier(command->command,
                           srcStages,
                           dstStages,
                           0u,
                           0u,
                           NULL,
                           nativeCount,
                           nativeBarriers,
                           0u,
                           NULL);
    }
    offset += chunkCount;
  }
}

GPU_HIDE
GPURenderPassDesc*
vk_beginRenderPass(GPUCommandBuffer              *cmdb,
                   const GPURenderPassCreateInfo *info) {
  const GPURenderPassColorAttachment *color;
  GPUCommandBufferVk                  *command;
  GPUTextureViewVk                    *view;
  GPUSwapChainVk                      *swapchain;
  GPURenderPassDesc                   *pass;
  GPURenderPassVk                     *native;

  if (!cmdb || !info || info->colorAttachmentCount != 1u ||
      !info->pColorAttachments || info->pDepthStencilAttachment) {
    return NULL;
  }

  color     = &info->pColorAttachments[0];
  command   = cmdb->_priv;
  view      = color->view ? color->view->_priv : NULL;
  swapchain = view ? view->swapchain : NULL;
  if (!command || !view || !swapchain || !swapchain->frameActive ||
      color->resolveView ||
      view->imageIndex != swapchain->acquiredImageIndex) {
    return NULL;
  }

  pass   = &command->renderPass;
  native = &command->renderPassState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  native->swapchain   = swapchain;
  native->renderPass  = swapchain->renderPasses[color->loadOp][color->storeOp];
  native->framebuffer = swapchain->framebuffers[view->imageIndex];
  native->extent      = swapchain->extent;
  native->clearValue.color.float32[0] = color->clearColor.float32[0];
  native->clearValue.color.float32[1] = color->clearColor.float32[1];
  native->clearValue.color.float32[2] = color->clearColor.float32[2];
  native->clearValue.color.float32[3] = color->clearColor.float32[3];

  pass->_priv = native;
  pass->label = info->label;
  return pass;
}

GPU_HIDE
void
vk_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

GPU_HIDE
void
vk_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = vk_beginRenderPass;
  api->destroyRenderPass = vk_destroyRenderPass;
  api->encodeBarriers    = vk_encodeBarriers;
}
