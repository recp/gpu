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
#include "../../../api/vrs_internal.h"

enum {
  GPU_VK_BARRIER_CHUNK_SIZE  = 16u,
  GPU_VK_TRANSFER_CHUNK_SIZE = 64u * 1024u
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

static VkClearColorValue
vk__clearColor(const GPUClearColorValue *color, GPUFormat format) {
  VkClearColorValue result;

  switch (gpuFormatNumericType(format)) {
    case GPU_FORMAT_NUMERIC_UINT:
      memcpy(result.uint32, color->uint32, sizeof(result.uint32));
      break;
    case GPU_FORMAT_NUMERIC_SINT:
      memcpy(result.int32, color->sint32, sizeof(result.int32));
      break;
    default:
      memcpy(result.float32, color->float32, sizeof(result.float32));
      break;
  }
  return result;
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
#ifdef VK_KHR_fragment_shading_rate
    case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
      *outStage  = VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
      *outAccess = VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
      break;
#endif
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
  GPUDeviceVk          *device;
  VkPipelineStageFlags  srcStage;
  VkPipelineStageFlags  dstStage;
  VkAccessFlags         srcAccess;
  VkAccessFlags         dstAccess;

  if (!command || !view || !view->image) {
    return;
  }
  if (view->texture) {
    (void)vk_transitionTexture(command,
                               view->texture,
                               view->baseMip,
                               view->mipCount,
                               view->baseLayer,
                               view->layerCount,
                               nextLayout);
    return;
  }
  if (!view->layout || *view->layout == nextLayout) {
    return;
  }

  device = view->swapchain && view->swapchain->gpuDevice
             ? view->swapchain->gpuDevice->_priv
             : NULL;
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
  vk_pipelineBarrier(device,
                     command,
                     srcStage,
                     dstStage,
                     0u,
                     NULL,
                     1u,
                     &barrier);
  *view->layout = nextLayout;
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
  if (barriers->aliasingBarrierCount > 0u) {
    VkMemoryBarrier native = {0};

    native.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    native.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    native.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT |
                           VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command->command,
                         srcStages,
                         dstStages,
                         0u,
                         1u,
                         &native,
                         0u,
                         NULL,
                         0u,
                         NULL);

    for (uint32_t i = 0u; i < barriers->aliasingBarrierCount; i++) {
      GPUTexture *after;

      after = barriers->pAliasingBarriers[i].afterTexture;
      if (after && after->_priv) {
        GPUTextureVk *texture = after->_priv;

        vk_setTextureLayout(texture,
                            0u,
                            texture->mipLevelCount,
                            0u,
                            texture->arrayLayerCount,
                            VK_IMAGE_LAYOUT_UNDEFINED);
      }
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
      VkImageLayout            newLayout;

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

      newLayout = vk__textureBarrierLayout(barrier->texture,
                                           barrier->dstAccess,
                                           false);
      if (!texture->layoutUniform) {
        if (nativeBarrierCount > 0u || nativeImageCount > 0u) {
          vk_pipelineBarrier(device,
                             command->command,
                             srcStages,
                             dstStages,
                             nativeBarrierCount,
                             nativeBarriers,
                             nativeImageCount,
                             nativeImages);
          nativeBarrierCount = 0u;
          nativeImageCount   = 0u;
        }
        if (!vk_transitionTextureBarrier(
              command->command,
              texture,
              barrier->baseMip,
              barrier->mipCount,
              barrier->baseLayer,
              barrier->layerCount,
              newLayout,
              srcStages,
              dstStages,
              vk__barrierAccess(barrier->srcAccess),
              vk__barrierAccess(barrier->dstAccess)
            )) {
          gpuDeviceRecordValidationError(
            gpuDevice,
            "Vulkan texture barrier range transition failed"
          );
        }
        continue;
      }

      native                                = &nativeImages[nativeImageCount++];
      memset(native, 0, sizeof(*native));
      native->sType                         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      native->srcAccessMask                 = vk__barrierAccess(barrier->srcAccess);
      native->dstAccessMask                 = vk__barrierAccess(barrier->dstAccess);
      native->oldLayout                     = texture->layout;
      native->newLayout                     = newLayout;
      native->srcQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
      native->dstQueueFamilyIndex           = VK_QUEUE_FAMILY_IGNORED;
      native->image                         = texture->image;
      native->subresourceRange.aspectMask     = texture->aspect;
      native->subresourceRange.baseMipLevel   = barrier->baseMip;
      native->subresourceRange.levelCount     = barrier->mipCount;
      native->subresourceRange.baseArrayLayer = barrier->baseLayer;
      native->subresourceRange.layerCount     = barrier->layerCount;
      vk_setTextureLayout(texture,
                          barrier->baseMip,
                          barrier->mipCount,
                          barrier->baseLayer,
                          barrier->layerCount,
                          native->newLayout);
    }

    if (nativeBarrierCount > 0u || nativeImageCount > 0u) {
      vk_pipelineBarrier(device,
                         command->command,
                         srcStages,
                         dstStages,
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
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;
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
  if (!gpuRenderPassVRSExtensions(info, &shadingRate, &rateMap) || rateMap) {
    return NULL;
  }

  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *attachment;
    GPUTextureViewVk                   *view;
    GPUTextureViewVk                   *resolveView;
    GPUSwapchainVk                     *swapchain;
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
    nativeAttachment->clearValue.color =
      vk__clearColor(&attachment->clearColor, attachment->view->format);
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
#ifdef VK_KHR_fragment_shading_rate
  if (shadingRate) {
    GPUTextureViewVk *view;
    uint32_t           minWidth;
    uint32_t           minHeight;
    uint32_t           maxAxis;
    uint32_t           minAxis;

    view = shadingRate->view ? shadingRate->view->_priv : NULL;
    if (shadingRate->texelSize.width == 0u ||
        shadingRate->texelSize.height == 0u ||
        (shadingRate->texelSize.width &
         (shadingRate->texelSize.width - 1u)) != 0u ||
        (shadingRate->texelSize.height &
         (shadingRate->texelSize.height - 1u)) != 0u) {
      return NULL;
    }
    maxAxis = shadingRate->texelSize.width > shadingRate->texelSize.height
                ? shadingRate->texelSize.width
                : shadingRate->texelSize.height;
    minAxis = shadingRate->texelSize.width < shadingRate->texelSize.height
                ? shadingRate->texelSize.width
                : shadingRate->texelSize.height;
    minWidth  = (native->extent.width - 1u) /
                shadingRate->texelSize.width + 1u;
    minHeight = (native->extent.height - 1u) /
                shadingRate->texelSize.height + 1u;
    if (!device->vrsAttachment || !view || !view->view || !view->image ||
        !view->layout ||
        shadingRate->texelSize.width < device->minVRSTexelSize.width ||
        shadingRate->texelSize.height < device->minVRSTexelSize.height ||
        shadingRate->texelSize.width > device->maxVRSTexelSize.width ||
        shadingRate->texelSize.height > device->maxVRSTexelSize.height ||
        minAxis == 0u ||
        (device->maxVRSTexelAspectRatio > 0u &&
         maxAxis > device->maxVRSTexelAspectRatio * minAxis) ||
        view->extent.width < minWidth || view->extent.height < minHeight) {
      return NULL;
    }

    vk_transitionView(
      command->command,
      view,
      VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR
    );
    native->shadingRateView = view;
    native->shadingRateAttachment.sType =
      VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
    native->shadingRateAttachment.imageView = view->view;
    native->shadingRateAttachment.imageLayout =
      VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    native->shadingRateAttachment.shadingRateAttachmentTexelSize.width =
      shadingRate->texelSize.width;
    native->shadingRateAttachment.shadingRateAttachmentTexelSize.height =
      shadingRate->texelSize.height;
  }
#else
  if (shadingRate) {
    return NULL;
  }
#endif
  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }

  native->colorCount                         = info->colorAttachmentCount;
  native->dynamic                            = true;
  native->renderingInfo.sType                =
    VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
  native->renderingInfo.pNext                = shadingRate
                                                  ? &native->shadingRateAttachment
                                                  : NULL;
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
  GPUSwapchainVk                      *swapchain;
  GPURenderPassDesc                   *pass;
  GPURenderPassVk                     *native;
  GPUDeviceVk                         *device;
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;

  device = cmdb && cmdb->_queue && cmdb->_queue->_device
             ? cmdb->_queue->_device->_priv
             : NULL;
  if (device && device->dynamicRendering) {
    return vk_beginDynamicRenderPass(cmdb, info, cmdb->_priv, device);
  }

  if (!gpuRenderPassVRSExtensions(info, &shadingRate, &rateMap) ||
      shadingRate || rateMap) {
    return NULL;
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
    vk_setTextureLayout(view->texture,
                        view->baseMip,
                        view->mipCount,
                        view->baseLayer,
                        view->layerCount,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  }
  if (!native->renderPass || !native->framebuffer ||
      native->extent.width == 0u || native->extent.height == 0u) {
    return NULL;
  }
  if (info->occlusionQuerySet) {
    vk_resetQuerySet(cmdb, info->occlusionQuerySet);
  }
  native->clearValue.color =
    vk__clearColor(&color->clearColor, color->view->format);

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
vk__copyAspect(GPUFormat           format,
               GPUTextureAspect    aspect,
               VkImageAspectFlags *outAspect) {
  GPUTextureAspect resolved;

  if (!outAspect ||
      !gpuFormatResolveCopyAspect(format, aspect, &resolved)) {
    return false;
  }

  switch (resolved) {
    case GPU_TEXTURE_ASPECT_DEPTH_ONLY:
      *outAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
      return true;
    case GPU_TEXTURE_ASPECT_STENCIL_ONLY:
      *outAspect = VK_IMAGE_ASPECT_STENCIL_BIT;
      return true;
    case GPU_TEXTURE_ASPECT_ALL:
      *outAspect = VK_IMAGE_ASPECT_COLOR_BIT;
      return true;
    default:
      return false;
  }
}

static bool
vk__bufferImageCopy(GPUTexture                       *texture,
                    const GPUBufferTextureCopyRegion *region,
                    VkBufferImageCopy                *outCopy) {
  GPUFormatLayout formatLayout;
  VkImageAspectFlags aspect;
  uint32_t           rowBlocks;
  uint32_t           rowLength;

  if (!texture || !texture->_priv || !region || !outCopy ||
      !vk__copyAspect(texture->format,
                      region->texture.texture.aspect,
                      &aspect) ||
      !gpuFormatAspectLayout(texture->format,
                             region->texture.texture.aspect,
                             &formatLayout) ||
      region->bytesPerRow % formatLayout.bytesPerBlock != 0u) {
    return false;
  }

  rowBlocks = region->bytesPerRow / formatLayout.bytesPerBlock;
  if (rowBlocks > UINT32_MAX / formatLayout.blockWidth) {
    return false;
  }
  rowLength = rowBlocks * formatLayout.blockWidth;

  memset(outCopy, 0, sizeof(*outCopy));
  outCopy->bufferOffset                    = region->bufferOffset;
  outCopy->bufferRowLength                 = rowLength;
  outCopy->bufferImageHeight               = region->rowsPerImage;
  outCopy->imageSubresource.aspectMask     = aspect;
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

  (void)vk_transitionTexture(command->command,
                             texture,
                             region->texture.texture.mipLevel,
                             1u,
                             dst->dimension == GPU_TEXTURE_DIMENSION_3D
                               ? 0u
                               : region->texture.texture.baseArrayLayer,
                             dst->dimension == GPU_TEXTURE_DIMENSION_3D
                               ? 1u
                               : region->texture.layerCount,
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

  (void)vk_transitionTexture(command->command,
                             texture,
                             region->texture.texture.mipLevel,
                             1u,
                             src->dimension == GPU_TEXTURE_DIMENSION_3D
                               ? 0u
                               : region->texture.texture.baseArrayLayer,
                             src->dimension == GPU_TEXTURE_DIMENSION_3D
                               ? 1u
                               : region->texture.layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  vkCmdCopyImageToBuffer(command->command,
                         texture->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         buffer->buffer,
                         1u,
                         &copy);
}

#ifdef __APPLE__
static bool
vk__reserveScratch(GPUCopyPassEncoder *pass,
                   uint64_t            sizeBytes,
                   uint64_t            alignment,
                   VkBuffer           *outBuffer,
                   VkDeviceSize       *outOffset) {
  GPUCommandBufferVk *command;
  GPUTransferChunkVk *chunk;
  GPUTransferChunkVk *candidate;
  GPUBufferCreateInfo info = {0};
  GPUBufferVk        *bufferVk;
  uint64_t            alignedOffset;
  uint64_t            capacity;

  if (!pass || !pass->_cmdb || !pass->_cmdb->_queue ||
      !pass->_cmdb->_queue->_device || sizeBytes == 0u ||
      alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
      !outBuffer || !outOffset) {
    return false;
  }
  *outBuffer = VK_NULL_HANDLE;
  *outOffset = 0u;

  command       = pass->_cmdb->_priv;
  chunk         = NULL;
  alignedOffset = 0u;
  if (!command) {
    return false;
  }

  for (candidate = command->transferChunks; candidate;
       candidate = candidate->next) {
    uint64_t offset;

    if (candidate->offset > UINT64_MAX - (alignment - 1u)) {
      continue;
    }
    offset = (candidate->offset + alignment - 1u) & ~(alignment - 1u);
    if (offset <= candidate->capacity &&
        sizeBytes <= candidate->capacity - offset) {
      chunk         = candidate;
      alignedOffset = offset;
      break;
    }
  }

  if (!chunk) {
    if (sizeBytes > UINT64_MAX - (alignment - 1u)) {
      return false;
    }
    capacity = sizeBytes > GPU_VK_TRANSFER_CHUNK_SIZE
                 ? (sizeBytes + alignment - 1u) & ~(alignment - 1u)
                 : GPU_VK_TRANSFER_CHUNK_SIZE;
    chunk = calloc(1, sizeof(*chunk));
    if (!chunk) {
      return false;
    }

    info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.label            = "vulkan-transfer-scratch";
    info.sizeBytes        = capacity;
    info.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                            GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(pass->_cmdb->_queue->_device,
                        &info,
                        &chunk->buffer) != GPU_OK) {
      free(chunk);
      return false;
    }

    chunk->capacity         = capacity;
    chunk->next             = command->transferChunks;
    command->transferChunks = chunk;
    gpuDeviceRecordHotPathAlloc(pass->_cmdb->_queue->_device,
                                sizeof(*chunk) + capacity);
  }

  bufferVk = chunk->buffer->_priv;
  if (!bufferVk || !bufferVk->buffer) {
    return false;
  }
  chunk->offset = alignedOffset + sizeBytes;
  *outBuffer    = bufferVk->buffer;
  *outOffset    = alignedOffset;
  return true;
}

static bool
vk__copyDepthStencilPlane(GPUCopyPassEncoder                  *pass,
                          GPUTexture                          *src,
                          GPUTexture                          *dst,
                          const GPUTextureToTextureCopyRegion *region,
                          VkImageAspectFlags                   aspect) {
  GPUCommandBufferVk   *command;
  GPUTextureVk         *srcVk;
  GPUTextureVk         *dstVk;
  GPUFormatLayout       layout;
  VkBuffer              scratch;
  VkDeviceSize          scratchOffset;
  VkBufferImageCopy     copy = {0};
  VkBufferMemoryBarrier barrier = {0};
  uint64_t              rowBytes;
  uint64_t              rowPitch;
  uint64_t              imageBytes;
  uint64_t              scratchBytes;
  uint64_t              rowLength;
  bool                  texture3D;

  command   = vk__copyCommand(pass);
  srcVk     = src ? src->_priv : NULL;
  dstVk     = dst ? dst->_priv : NULL;
  texture3D = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcVk || !dstVk || !region ||
      !gpuFormatAspectLayout(src->format, region->src.aspect, &layout) ||
      layout.blockWidth != 1u || layout.blockHeight != 1u ||
      region->width > UINT64_MAX / layout.bytesPerBlock) {
    return false;
  }

  rowBytes = (uint64_t)region->width * layout.bytesPerBlock;
  if (rowBytes > UINT64_MAX - 255u) {
    return false;
  }
  rowPitch = (rowBytes + 255u) & ~UINT64_C(255);
  rowLength = rowPitch / layout.bytesPerBlock;
  if (rowLength > UINT32_MAX) {
    return false;
  }
  if (region->height > UINT64_MAX / rowPitch) {
    return false;
  }
  imageBytes = rowPitch * region->height;
  if (region->depth > UINT64_MAX / imageBytes) {
    return false;
  }
  scratchBytes = imageBytes * region->depth;
  if (region->layerCount > UINT64_MAX / scratchBytes) {
    return false;
  }
  scratchBytes *= region->layerCount;
  if (!vk__reserveScratch(pass,
                          scratchBytes,
                          256u,
                          &scratch,
                          &scratchOffset)) {
    return false;
  }

  (void)vk_transitionTexture(command->command,
                             srcVk,
                             region->src.mipLevel,
                             1u,
                             texture3D ? 0u : region->src.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  (void)vk_transitionTexture(command->command,
                             dstVk,
                             region->dst.mipLevel,
                             1u,
                             texture3D ? 0u : region->dst.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  copy.bufferOffset                    = scratchOffset;
  copy.bufferRowLength                 = (uint32_t)rowLength;
  copy.bufferImageHeight               = region->height;
  copy.imageSubresource.aspectMask     = aspect;
  copy.imageSubresource.mipLevel       = region->src.mipLevel;
  copy.imageSubresource.baseArrayLayer = texture3D
                                           ? 0u
                                           : region->src.baseArrayLayer;
  copy.imageSubresource.layerCount     = texture3D
                                           ? 1u
                                           : region->layerCount;
  copy.imageExtent.width               = region->width;
  copy.imageExtent.height              = region->height;
  copy.imageExtent.depth               = texture3D ? region->depth : 1u;
  vkCmdCopyImageToBuffer(command->command,
                         srcVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         scratch,
                         1u,
                         &copy);

  barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer              = scratch;
  barrier.offset              = scratchOffset;
  barrier.size                = scratchBytes;
  vk_pipelineBarrier(srcVk->gpuDevice,
                     command->command,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     1u,
                     &barrier,
                     0u,
                     NULL);

  copy.imageSubresource.mipLevel       = region->dst.mipLevel;
  copy.imageSubresource.baseArrayLayer = texture3D
                                           ? 0u
                                           : region->dst.baseArrayLayer;
  vkCmdCopyBufferToImage(command->command,
                         scratch,
                         dstVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);
  return true;
}
#endif

static void
vk_copyTextureToTexture(GPUCopyPassEncoder                  *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  GPUCommandBufferVk *command;
  GPUTextureVk       *srcVk;
  GPUTextureVk       *dstVk;
  VkImageCopy         copy = {0};
  VkImageAspectFlags  srcAspect;
  VkImageAspectFlags  dstAspect;
  bool                texture3D;

  command   = vk__copyCommand(pass);
  srcVk     = src ? src->_priv : NULL;
  dstVk     = dst ? dst->_priv : NULL;
  texture3D = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcVk || !dstVk || !region ||
      !vk__copyAspect(src->format, region->src.aspect, &srcAspect) ||
      !vk__copyAspect(dst->format, region->dst.aspect, &dstAspect) ||
      srcAspect != dstAspect) {
    return;
  }

#ifdef __APPLE__
  if (src->format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
      src->format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    (void)vk__copyDepthStencilPlane(pass,
                                    src,
                                    dst,
                                    region,
                                    srcAspect);
    return;
  }
#endif

  (void)vk_transitionTexture(command->command,
                             srcVk,
                             region->src.mipLevel,
                             1u,
                             texture3D ? 0u : region->src.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  (void)vk_transitionTexture(command->command,
                             dstVk,
                             region->dst.mipLevel,
                             1u,
                             texture3D ? 0u : region->dst.baseArrayLayer,
                             texture3D ? 1u : region->layerCount,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copy.srcSubresource.aspectMask     = srcAspect;
  copy.srcSubresource.mipLevel       = region->src.mipLevel;
  copy.srcSubresource.baseArrayLayer = texture3D
                                         ? 0u
                                         : region->src.baseArrayLayer;
  copy.srcSubresource.layerCount     = texture3D ? 1u : region->layerCount;
  copy.srcOffset.x                   = (int32_t)region->src.x;
  copy.srcOffset.y                   = (int32_t)region->src.y;
  copy.srcOffset.z                   = texture3D ? (int32_t)region->src.z : 0;
  copy.dstSubresource.aspectMask     = dstAspect;
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
