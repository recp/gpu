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
#include "../../../api/texture_internal.h"

enum {
  GPU_VK_BARRIER_CHUNK_SIZE = 16u
};

static VkAttachmentLoadOp
vk__loadOp(GPULoadOp op) {
  static const VkAttachmentLoadOp operations[] = {
    [GPU_LOAD_OP_LOAD]      = VK_ATTACHMENT_LOAD_OP_LOAD,
    [GPU_LOAD_OP_CLEAR]     = VK_ATTACHMENT_LOAD_OP_CLEAR,
    [GPU_LOAD_OP_DONT_CARE] = VK_ATTACHMENT_LOAD_OP_DONT_CARE
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp
vk__storeOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE
           ? VK_ATTACHMENT_STORE_OP_STORE
           : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static void
vk__layoutAccess(VkImageLayout        layout,
                 VkPipelineStageFlags *outStage,
                 VkAccessFlags        *outAccess) {
  switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      *outStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      *outAccess = 0u;
      break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      *outStage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      *outAccess = 0u;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      *outAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      *outAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    default:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      break;
  }
}

GPU_HIDE
void
vk_transitionView(VkCommandBuffer   command,
                  GPUTextureViewVk *view,
                  VkImageLayout     nextLayout) {
  VkImageMemoryBarrier  barrier = {0};
  VkPipelineStageFlags  srcStage;
  VkPipelineStageFlags  dstStage;
  VkAccessFlags         srcAccess;
  VkAccessFlags         dstAccess;

  if (!command || !view || !view->image || !view->layout ||
      *view->layout == nextLayout) {
    return;
  }

  vk__layoutAccess(*view->layout, &srcStage, &srcAccess);
  vk__layoutAccess(nextLayout, &dstStage, &dstAccess);
  barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask                   = srcAccess;
  barrier.dstAccessMask                   = dstAccess;
  barrier.oldLayout                       = *view->layout;
  barrier.newLayout                       = nextLayout;
  barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.image                           = view->image;
  barrier.subresourceRange.aspectMask     = view->aspect;
  barrier.subresourceRange.baseMipLevel   = view->baseMip;
  barrier.subresourceRange.levelCount     = view->mipCount;
  barrier.subresourceRange.baseArrayLayer = view->baseLayer;
  barrier.subresourceRange.layerCount     = view->layerCount;
  vkCmdPipelineBarrier(command,
                       srcStage,
                       dstStage,
                       0u,
                       0u,
                       NULL,
                       0u,
                       NULL,
                       1u,
                       &barrier);
  *view->layout = nextLayout;
}

static void
vk__transitionTexture(VkCommandBuffer command,
                      GPUTexture     *texture,
                      VkImageLayout   nextLayout) {
  GPUTextureVk         *native;
  VkImageMemoryBarrier  barrier = {0};
  VkPipelineStageFlags  srcStage;
  VkPipelineStageFlags  dstStage;
  VkAccessFlags         srcAccess;
  VkAccessFlags         dstAccess;

  native = texture ? texture->_priv : NULL;
  if (!command || !native || !native->image || native->layout == nextLayout) {
    return;
  }

  vk__layoutAccess(native->layout, &srcStage, &srcAccess);
  vk__layoutAccess(nextLayout, &dstStage, &dstAccess);
  barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask                   = srcAccess;
  barrier.dstAccessMask                   = dstAccess;
  barrier.oldLayout                       = native->layout;
  barrier.newLayout                       = nextLayout;
  barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.image                           = native->image;
  barrier.subresourceRange.aspectMask     = native->aspect;
  barrier.subresourceRange.baseMipLevel   = 0u;
  barrier.subresourceRange.levelCount     = texture->mipLevelCount;
  barrier.subresourceRange.baseArrayLayer = 0u;
  barrier.subresourceRange.layerCount     =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? 1u
      : texture->depthOrLayers;
  vkCmdPipelineBarrier(command,
                       srcStage,
                       dstStage,
                       0u,
                       0u,
                       NULL,
                       0u,
                       NULL,
                       1u,
                       &barrier);
  native->layout = nextLayout;
}

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

static VkImageLayout
vk__textureBarrierLayout(const GPUTexture *texture,
                         GPUAccessMask     access,
                         bool              source) {
  uint32_t categoryCount;

  if (access == GPU_ACCESS_NONE) {
    return source ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
  }

  categoryCount  = (access & (GPU_ACCESS_SHADER_READ |
                              GPU_ACCESS_SHADER_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_COLOR_READ |
                              GPU_ACCESS_COLOR_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_DEPTH_READ |
                              GPU_ACCESS_DEPTH_WRITE)) != 0u;
  categoryCount += (access & (GPU_ACCESS_TRANSFER_READ |
                              GPU_ACCESS_TRANSFER_WRITE)) != 0u;
  if (categoryCount != 1u) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u ||
      ((access & GPU_ACCESS_SHADER_READ) != 0u &&
       (texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u)) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if ((access & (GPU_ACCESS_DEPTH_READ | GPU_ACCESS_DEPTH_WRITE)) != 0u) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u &&
      (access & GPU_ACCESS_TRANSFER_WRITE) == 0u) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u &&
      (access & GPU_ACCESS_TRANSFER_READ) == 0u) {
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  }

  return VK_IMAGE_LAYOUT_GENERAL;
}

GPU_HIDE
void
vk_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUCommandBufferVk  *command;
  GPUDevice           *gpuDevice;
  GPUDeviceVk         *device;
  VkPipelineStageFlags srcStages;
  VkPipelineStageFlags dstStages;
  uint32_t             bufferOffset;
  uint32_t             textureOffset;

  command   = cmdb ? cmdb->_priv : NULL;
  gpuDevice = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  device    = gpuDevice ? gpuDevice->_priv : NULL;
  if (!command || !command->command || !device || !barriers) {
    return;
  }

  srcStages     = vk__barrierStages(barriers->srcStages);
  dstStages     = vk__barrierStages(barriers->dstStages);
  for (uint32_t i = 0u; i < barriers->bufferBarrierCount; i++) {
    const GPUBufferBarrier *barrier = &barriers->pBufferBarriers[i];

    if ((barrier->srcAccess & GPU_ACCESS_INDIRECT_READ) != 0u) {
      srcStages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }
    if ((barrier->dstAccess & GPU_ACCESS_INDIRECT_READ) != 0u) {
      dstStages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    }
  }
  bufferOffset  = 0u;
  textureOffset = 0u;
  while (bufferOffset < barriers->bufferBarrierCount ||
         textureOffset < barriers->textureBarrierCount) {
    VkBufferMemoryBarrier nativeBarriers[GPU_VK_BARRIER_CHUNK_SIZE];
    VkImageMemoryBarrier  nativeImages[GPU_VK_BARRIER_CHUNK_SIZE];
    uint32_t              nativeBarrierCount;
    uint32_t              nativeImageCount;
    uint32_t              bufferChunkCount;
    uint32_t              textureChunkCount;

    bufferChunkCount = barriers->bufferBarrierCount - bufferOffset;
    if (bufferChunkCount > GPU_VK_BARRIER_CHUNK_SIZE) {
      bufferChunkCount = GPU_VK_BARRIER_CHUNK_SIZE;
    }
    textureChunkCount = barriers->textureBarrierCount - textureOffset;
    if (textureChunkCount > GPU_VK_BARRIER_CHUNK_SIZE) {
      textureChunkCount = GPU_VK_BARRIER_CHUNK_SIZE;
    }

    nativeBarrierCount = 0u;
    for (uint32_t i = 0u; i < bufferChunkCount; i++) {
      const GPUBufferBarrier *barrier;
      GPUBufferVk            *buffer;
      VkBufferMemoryBarrier  *native;

      barrier = &barriers->pBufferBarriers[bufferOffset + i];
      buffer  = barrier->buffer ? barrier->buffer->_priv : NULL;
      if (!buffer || !buffer->buffer || buffer->device != device->device) {
        continue;
      }

      native                      = &nativeBarriers[nativeBarrierCount++];
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

    nativeImageCount = 0u;
    for (uint32_t i = 0u; i < textureChunkCount; i++) {
      const GPUTextureBarrier *barrier;
      GPUTextureVk            *texture;
      VkImageMemoryBarrier    *native;

      barrier = &barriers->pTextureBarriers[textureOffset + i];
      if (!barrier->texture->_ownsNative) {
        gpuDeviceRecordValidationError(
          gpuDevice,
          "Vulkan barriers do not support swapchain textures"
        );
        continue;
      }

      texture = barrier->texture->_priv;
      if (!texture || !texture->image || texture->device != device->device) {
        gpuDeviceRecordValidationError(
          gpuDevice,
          "Vulkan texture barrier has no compatible native image"
        );
        continue;
      }

      native                                = &nativeImages[nativeImageCount++];
      memset(native, 0, sizeof(*native));
      native->sType                         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      native->srcAccessMask                 = vk__barrierAccess(barrier->srcAccess);
      native->dstAccessMask                 = vk__barrierAccess(barrier->dstAccess);
      native->oldLayout                     = texture->layout;
      native->newLayout                     =
        vk__textureBarrierLayout(barrier->texture,
                                 barrier->dstAccess,
                                 false);
      native->srcQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
      native->dstQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
      native->image                         = texture->image;
      native->subresourceRange.aspectMask     = texture->aspect;
      native->subresourceRange.baseMipLevel   = barrier->baseMip;
      native->subresourceRange.levelCount     = barrier->mipCount;
      native->subresourceRange.baseArrayLayer = barrier->baseLayer;
      native->subresourceRange.layerCount     = barrier->layerCount;
      texture->layout                        = native->newLayout;
    }

    if (nativeBarrierCount > 0u || nativeImageCount > 0u) {
      vkCmdPipelineBarrier(command->command,
                           srcStages,
                           dstStages,
                           0u,
                           0u,
                           NULL,
                           nativeBarrierCount,
                           nativeBarriers,
                           nativeImageCount,
                           nativeImages);
    }
    bufferOffset  += bufferChunkCount;
    textureOffset += textureChunkCount;
  }
}

static GPURenderPassDesc*
vk_beginDynamicRenderPass(GPUCommandBuffer              *cmdb,
                          const GPURenderPassCreateInfo *info,
                          GPUCommandBufferVk            *command,
                          GPUDeviceVk                   *device) {
  GPURenderPassDesc *pass;
  GPURenderPassVk   *native;
  uint32_t           layerCount;

  if (!cmdb || !info || !command || !device || !device->dynamicRendering ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS) {
    return NULL;
  }

  pass       = &command->renderPass;
  native     = &command->renderPassState;
  layerCount = 0u;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *attachment;
    GPUTextureViewVk                   *view;
    GPUTextureViewVk                   *resolveView;
    GPUSwapChainVk                     *swapchain;
    VkRenderingAttachmentInfoKHR       *nativeAttachment;

    attachment = &info->pColorAttachments[i];
    view       = attachment->view ? attachment->view->_priv : NULL;
    resolveView = attachment->resolveView
                    ? attachment->resolveView->_priv
                    : NULL;
    swapchain  = view ? view->swapchain : NULL;
    if (!view || !view->view || !view->image || !view->layout ||
        view->extent.width == 0u ||
        view->extent.height == 0u || view->layerCount == 0u ||
        (swapchain &&
         (!swapchain->frameActive ||
          view->imageIndex != swapchain->acquiredImageIndex)) ||
        (native->extent.width > 0u &&
         (native->extent.width != view->extent.width ||
          native->extent.height != view->extent.height ||
          layerCount != view->layerCount))) {
      return NULL;
    }
    if (resolveView &&
        (!resolveView->view || !resolveView->image || !resolveView->layout ||
         resolveView->extent.width != view->extent.width ||
         resolveView->extent.height != view->extent.height ||
         resolveView->layerCount != view->layerCount)) {
      return NULL;
    }

    vk_transitionView(command->command,
                      view,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    nativeAttachment = &native->colorAttachments[i];
    nativeAttachment->sType =
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    nativeAttachment->imageView   = view->view;
    nativeAttachment->imageLayout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    nativeAttachment->loadOp  = vk__loadOp(attachment->loadOp);
    nativeAttachment->storeOp = vk__storeOp(attachment->storeOp);
    if (resolveView) {
      vk_transitionView(command->command,
                        resolveView,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      nativeAttachment->resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
      nativeAttachment->resolveImageView   = resolveView->view;
      nativeAttachment->resolveImageLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    nativeAttachment->clearValue.color.float32[0] =
      attachment->clearColor.float32[0];
    nativeAttachment->clearValue.color.float32[1] =
      attachment->clearColor.float32[1];
    nativeAttachment->clearValue.color.float32[2] =
      attachment->clearColor.float32[2];
    nativeAttachment->clearValue.color.float32[3] =
      attachment->clearColor.float32[3];
    native->colorViews[i] = view;
    native->resolveViews[i] = resolveView;
    native->extent        = view->extent;
    native->swapchain     = swapchain;
    layerCount            = view->layerCount;
  }

  if (info->pDepthStencilAttachment) {
    const GPURenderPassDepthStencilAttachment *attachment;
    GPUTextureViewVk                          *view;
    bool                                       hasStencil;

    attachment = info->pDepthStencilAttachment;
    view       = attachment->view ? attachment->view->_priv : NULL;
    if (!view || !view->view || !view->image || !view->layout ||
        view->extent.width == 0u || view->extent.height == 0u ||
        view->layerCount == 0u ||
        (native->extent.width > 0u &&
         (native->extent.width != view->extent.width ||
          native->extent.height != view->extent.height ||
          layerCount != view->layerCount))) {
      return NULL;
    }

    vk_transitionView(command->command,
                      view,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    if (attachment->view->format != GPU_FORMAT_STENCIL8) {
      native->depthAttachment.sType =
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      native->depthAttachment.imageView   = view->view;
      native->depthAttachment.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      native->depthAttachment.loadOp =
        vk__loadOp(attachment->depthLoadOp);
      native->depthAttachment.storeOp =
        vk__storeOp(attachment->depthStoreOp);
      native->depthAttachment.clearValue.depthStencil.depth =
        attachment->clearDepth;
      native->depthAttachment.clearValue.depthStencil.stencil =
        attachment->clearStencil;
    }
    native->depthStencilView = view;
    native->extent           = view->extent;
    layerCount               = view->layerCount;

    hasStencil = attachment->view->format == GPU_FORMAT_STENCIL8 ||
                 attachment->view->format ==
                   GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
                 attachment->view->format ==
                   GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
    if (hasStencil) {
      native->stencilAttachment.sType =
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      native->stencilAttachment.imageView   = view->view;
      native->stencilAttachment.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      native->stencilAttachment.loadOp =
        vk__loadOp(attachment->stencilLoadOp);
      native->stencilAttachment.storeOp =
        vk__storeOp(attachment->stencilStoreOp);
      native->stencilAttachment.clearValue.depthStencil.depth =
        attachment->clearDepth;
      native->stencilAttachment.clearValue.depthStencil.stencil =
        attachment->clearStencil;
    }
  }

  if (native->extent.width == 0u || native->extent.height == 0u ||
      layerCount == 0u) {
    return NULL;
  }
  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }

  native->colorCount                         = info->colorAttachmentCount;
  native->dynamic                            = true;
  native->renderingInfo.sType                =
    VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
  native->renderingInfo.renderArea.extent    = native->extent;
  native->renderingInfo.layerCount           = layerCount;
  native->renderingInfo.colorAttachmentCount = native->colorCount;
  native->renderingInfo.pColorAttachments    = native->colorCount > 0u
                                                  ? native->colorAttachments
                                                  : NULL;
  native->renderingInfo.pDepthAttachment =
    native->depthAttachment.imageView ? &native->depthAttachment : NULL;
  native->renderingInfo.pStencilAttachment =
    native->stencilAttachment.imageView
      ? &native->stencilAttachment
      : NULL;
  pass->_priv = native;
  pass->label = info->label;
  return pass;
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
  GPUDeviceVk                         *device;

  device = cmdb && cmdb->_queue && cmdb->_queue->_device
             ? cmdb->_queue->_device->_priv
             : NULL;
  if (device && device->dynamicRendering) {
    return vk_beginDynamicRenderPass(cmdb, info, cmdb->_priv, device);
  }

  if (!cmdb || !info || info->colorAttachmentCount != 1u ||
      !info->pColorAttachments || info->pDepthStencilAttachment) {
    return NULL;
  }

  color     = &info->pColorAttachments[0];
  command   = cmdb->_priv;
  view      = color->view ? color->view->_priv : NULL;
  swapchain = view ? view->swapchain : NULL;
  if (!command || !view || color->resolveView) {
    return NULL;
  }
  pass   = &command->renderPass;
  native = &command->renderPassState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  native->swapchain = swapchain;
  if (swapchain) {
    if (!swapchain->frameActive ||
        view->imageIndex != swapchain->acquiredImageIndex) {
      return NULL;
    }
    native->renderPass  = swapchain->renderPasses[color->loadOp]
                                                  [color->storeOp];
    native->framebuffer = swapchain->framebuffers[view->imageIndex];
    native->extent      = swapchain->extent;
  } else {
    if (!view->texture || !view->framebuffer) {
      return NULL;
    }
    native->renderPass  = view->texture->renderPasses[color->loadOp]
                                                   [color->storeOp];
    native->framebuffer = view->framebuffer;
    native->extent      = view->extent;
    view->texture->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if (!native->renderPass || !native->framebuffer ||
      native->extent.width == 0u || native->extent.height == 0u) {
    return NULL;
  }
  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }
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

static GPUCommandBufferVk*
vk__copyCommand(GPUCopyPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static GPUCopyPassEncoder*
vk_beginCopyPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferVk *command;
  GPUCopyPassEncoder *pass;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->command) {
    return NULL;
  }

  pass = &command->copyEncoder;
  memset(pass, 0, sizeof(*pass));
  pass->_priv = command;
  command->copyDebugLabelActive = vk_beginDebugLabel(
    gpuCommandBufferDevice(cmdb),
    command->command,
    label
  );
  return pass;
}

static void
vk_copyBufferToBuffer(GPUCopyPassEncoder        *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region) {
  GPUCommandBufferVk *command;
  GPUBufferVk        *srcVk;
  GPUBufferVk        *dstVk;
  VkBufferCopy        copy = {0};

  command = vk__copyCommand(pass);
  srcVk   = src ? src->_priv : NULL;
  dstVk   = dst ? dst->_priv : NULL;
  if (!command || !srcVk || !dstVk || !region) {
    return;
  }

  copy.srcOffset = region->srcOffset;
  copy.dstOffset = region->dstOffset;
  copy.size      = region->sizeBytes;
  vkCmdCopyBuffer(command->command, srcVk->buffer, dstVk->buffer, 1u, &copy);
}

static bool
vk__bufferImageCopy(GPUTexture                       *texture,
                    const GPUBufferTextureCopyRegion *region,
                    VkBufferImageCopy                *outCopy) {
  GPUTextureVk *textureVk;
  uint32_t      formatBytes;

  textureVk   = texture ? texture->_priv : NULL;
  formatBytes = texture ? vk_formatBytes(texture->format) : 0u;
  if (!textureVk || !region || !outCopy || formatBytes == 0u ||
      region->bytesPerRow % formatBytes != 0u) {
    return false;
  }

  memset(outCopy, 0, sizeof(*outCopy));
  outCopy->bufferOffset                    = region->bufferOffset;
  outCopy->bufferRowLength                 = region->bytesPerRow / formatBytes;
  outCopy->bufferImageHeight               = region->rowsPerImage;
  outCopy->imageSubresource.aspectMask     = textureVk->aspect;
  outCopy->imageSubresource.mipLevel       = region->texture.texture.mipLevel;
  outCopy->imageSubresource.baseArrayLayer =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? 0u
      : region->texture.texture.baseArrayLayer;
  outCopy->imageSubresource.layerCount =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? 1u
      : region->texture.layerCount;
  outCopy->imageOffset.x      = (int32_t)region->texture.texture.x;
  outCopy->imageOffset.y      = (int32_t)region->texture.texture.y;
  outCopy->imageOffset.z      =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? (int32_t)region->texture.texture.z
      : 0;
  outCopy->imageExtent.width  = region->texture.width;
  outCopy->imageExtent.height = region->texture.height;
  outCopy->imageExtent.depth  =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? region->texture.depth
      : 1u;
  return true;
}

static void
vk_copyBufferToTexture(GPUCopyPassEncoder               *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUCommandBufferVk *command;
  GPUBufferVk        *buffer;
  GPUTextureVk       *texture;
  VkBufferImageCopy   copy;

  command = vk__copyCommand(pass);
  buffer  = src ? src->_priv : NULL;
  texture = dst ? dst->_priv : NULL;
  if (!command || !buffer || !texture ||
      !vk__bufferImageCopy(dst, region, &copy)) {
    return;
  }

  vk__transitionTexture(command->command,
                        dst,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vkCmdCopyBufferToImage(command->command,
                         buffer->buffer,
                         texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);
}

static void
vk_copyTextureToBuffer(GPUCopyPassEncoder               *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUCommandBufferVk *command;
  GPUTextureVk       *texture;
  GPUBufferVk        *buffer;
  VkBufferImageCopy   copy;

  command = vk__copyCommand(pass);
  texture = src ? src->_priv : NULL;
  buffer  = dst ? dst->_priv : NULL;
  if (!command || !texture || !buffer ||
      !vk__bufferImageCopy(src, region, &copy)) {
    return;
  }

  vk__transitionTexture(command->command,
                        src,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  vkCmdCopyImageToBuffer(command->command,
                         texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         buffer->buffer,
                         1u,
                         &copy);
}

static void
vk_copyTextureToTexture(GPUCopyPassEncoder                  *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  GPUCommandBufferVk *command;
  GPUTextureVk       *srcVk;
  GPUTextureVk       *dstVk;
  VkImageCopy         copy = {0};
  bool                texture3D;

  command   = vk__copyCommand(pass);
  srcVk     = src ? src->_priv : NULL;
  dstVk     = dst ? dst->_priv : NULL;
  texture3D = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcVk || !dstVk || !region) {
    return;
  }

  vk__transitionTexture(command->command,
                        src,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  vk__transitionTexture(command->command,
                        dst,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copy.srcSubresource.aspectMask     = srcVk->aspect;
  copy.srcSubresource.mipLevel       = region->src.mipLevel;
  copy.srcSubresource.baseArrayLayer = texture3D
                                         ? 0u
                                         : region->src.baseArrayLayer;
  copy.srcSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.srcOffset.x                   = (int32_t)region->src.x;
  copy.srcOffset.y                   = (int32_t)region->src.y;
  copy.srcOffset.z                   = texture3D ? (int32_t)region->src.z : 0;
  copy.dstSubresource.aspectMask     = dstVk->aspect;
  copy.dstSubresource.mipLevel       = region->dst.mipLevel;
  copy.dstSubresource.baseArrayLayer = texture3D
                                         ? 0u
                                         : region->dst.baseArrayLayer;
  copy.dstSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.dstOffset.x                   = (int32_t)region->dst.x;
  copy.dstOffset.y                   = (int32_t)region->dst.y;
  copy.dstOffset.z                   = texture3D ? (int32_t)region->dst.z : 0;
  copy.extent.width                  = region->width;
  copy.extent.height                 = region->height;
  copy.extent.depth                  = texture3D ? region->depth : 1u;
  vkCmdCopyImage(command->command,
                 srcVk->image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 dstVk->image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 1u,
                 &copy);
}

static void
vk_endCopyPass(GPUCopyPassEncoder *pass) {
  GPUCommandBufferVk *command;

  command = vk__copyCommand(pass);
  if (command && command->copyDebugLabelActive) {
    vk_endDebugLabel(gpuCommandBufferDevice(pass->_cmdb), command->command);
    command->copyDebugLabelActive = false;
  }
}

GPU_HIDE
void
vk_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass      = vk_beginRenderPass;
  api->destroyRenderPass    = vk_destroyRenderPass;
  api->beginCopyPass        = vk_beginCopyPass;
  api->copyBufferToBuffer   = vk_copyBufferToBuffer;
  api->copyBufferToTexture  = vk_copyBufferToTexture;
  api->copyTextureToBuffer  = vk_copyTextureToBuffer;
  api->copyTextureToTexture = vk_copyTextureToTexture;
  api->endCopyPass          = vk_endCopyPass;
  api->encodeBarriers       = vk_encodeBarriers;
}
