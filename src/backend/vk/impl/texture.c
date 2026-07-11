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
#include "../impl.h"
#include "../../../api/buffer_internal.h"
#include "../../../api/texture_internal.h"

static bool
vk__textureUsage(GPUTextureUsageFlags usage, VkImageUsageFlags *outUsage) {
  const GPUTextureUsageFlags known = GPU_TEXTURE_USAGE_SAMPLED |
                                     GPU_TEXTURE_USAGE_STORAGE |
                                     GPU_TEXTURE_USAGE_COLOR_TARGET |
                                     GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                                     GPU_TEXTURE_USAGE_COPY_SRC |
                                     GPU_TEXTURE_USAGE_COPY_DST;
  VkImageUsageFlags result;

  if (!outUsage || usage == 0u || (usage & ~known) != 0u) {
    return false;
  }

  result = 0u;
  if ((usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u) {
    result |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if ((usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    result |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  if ((usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) {
    result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if ((usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) {
    result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if ((usage & GPU_TEXTURE_USAGE_COPY_SRC) != 0u) {
    result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  if ((usage & GPU_TEXTURE_USAGE_COPY_DST) != 0u) {
    result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  *outUsage = result;
  return true;
}

static bool
vk__imageType(GPUTextureDimension dimension, VkImageType *outType) {
  if (!outType) {
    return false;
  }

  switch (dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      *outType = VK_IMAGE_TYPE_1D;
      return true;
    case GPU_TEXTURE_DIMENSION_2D:
      *outType = VK_IMAGE_TYPE_2D;
      return true;
    case GPU_TEXTURE_DIMENSION_3D:
      *outType = VK_IMAGE_TYPE_3D;
      return true;
    default:
      return false;
  }
}

static bool
vk__imageViewType(GPUTextureViewType viewType, VkImageViewType *outType) {
  if (!outType) {
    return false;
  }

  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      *outType = VK_IMAGE_VIEW_TYPE_1D;
      return true;
    case GPU_TEXTURE_VIEW_2D:
      *outType = VK_IMAGE_VIEW_TYPE_2D;
      return true;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      *outType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      return true;
    case GPU_TEXTURE_VIEW_CUBE:
      *outType = VK_IMAGE_VIEW_TYPE_CUBE;
      return true;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      *outType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      return true;
    case GPU_TEXTURE_VIEW_3D:
      *outType = VK_IMAGE_VIEW_TYPE_3D;
      return true;
    default:
      return false;
  }
}

static VkImageAspectFlags
vk__imageAspect(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case GPU_FORMAT_DEPTH32_FLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

static uint32_t
vk__formatBytes(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UNORM_SRGB:
    case GPU_FORMAT_BGRA8_UNORM:
    case GPU_FORMAT_BGRA8_UNORM_SRGB:
    case GPU_FORMAT_RG11B10_UFLOAT:
    case GPU_FORMAT_DEPTH32_FLOAT:
      return 4u;
    case GPU_FORMAT_RGBA16_FLOAT:
      return 8u;
    case GPU_FORMAT_RGBA32_FLOAT:
      return 16u;
    default:
      return 0u;
  }
}

static void
vk__layoutSource(VkImageLayout layout,
                 VkPipelineStageFlags *outStage,
                 VkAccessFlags *outAccess) {
  switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      *outStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      *outAccess = 0u;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_GENERAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
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
    default:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      break;
  }
}

static VkImageLayout
vk__finalImageLayout(GPUTextureUsageFlags usage) {
  if ((usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    return VK_IMAGE_LAYOUT_GENERAL;
  }
  if ((usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u) {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  if ((usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  if ((usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) {
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  if ((usage & GPU_TEXTURE_USAGE_COPY_SRC) != 0u) {
    return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

static void
vk__finalLayoutTarget(VkImageLayout layout,
                      VkPipelineStageFlags *outStage,
                      VkAccessFlags *outAccess) {
  switch (layout) {
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_GENERAL:
      *outStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      *outAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
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
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    default:
      *outStage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
      *outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
  }
}

static void
vk__imageBarrier(VkCommandBuffer command,
                 GPUTextureVk *texture,
                 const GPUTexture *gpuTexture,
                 VkImageLayout oldLayout,
                 VkImageLayout newLayout,
                 VkPipelineStageFlags srcStage,
                 VkPipelineStageFlags dstStage,
                 VkAccessFlags srcAccess,
                 VkAccessFlags dstAccess) {
  VkImageMemoryBarrier barrier = {0};

  barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask                   = srcAccess;
  barrier.dstAccessMask                   = dstAccess;
  barrier.oldLayout                       = oldLayout;
  barrier.newLayout                       = newLayout;
  barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.image                           = texture->image;
  barrier.subresourceRange.aspectMask     = texture->aspect;
  barrier.subresourceRange.baseMipLevel   = 0u;
  barrier.subresourceRange.levelCount     = gpuTexture->mipLevelCount;
  barrier.subresourceRange.baseArrayLayer = 0u;
  barrier.subresourceRange.layerCount     =
    gpuTexture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? 1u
      : gpuTexture->depthOrLayers;

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
}

static void
vk__destroyTextureState(GPUTextureVk *native) {
  if (!native || !native->device) {
    return;
  }

  for (uint32_t load = 0u; load < 3u; load++) {
    for (uint32_t store = 0u; store < 2u; store++) {
      if (native->renderPasses[load][store]) {
        vkDestroyRenderPass(native->device,
                            native->renderPasses[load][store],
                            NULL);
      }
    }
  }
  if (native->image) {
    vkDestroyImage(native->device, native->image, NULL);
  }
  if (native->memory) {
    vkFreeMemory(native->device, native->memory, NULL);
  }
}

static VkAttachmentLoadOp
vk__colorLoadOp(uint32_t load) {
  static const VkAttachmentLoadOp ops[] = {
    VK_ATTACHMENT_LOAD_OP_LOAD,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE
  };

  return ops[load];
}

static VkAttachmentStoreOp
vk__colorStoreOp(uint32_t store) {
  return store == GPU_STORE_OP_STORE
           ? VK_ATTACHMENT_STORE_OP_STORE
           : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkResult
vk__createColorRenderPass(VkDevice      device,
                          VkFormat      format,
                          uint32_t      load,
                          uint32_t      store,
                          VkRenderPass *outRenderPass) {
  VkAttachmentDescription attachment = {0};
  VkAttachmentReference   color      = {0};
  VkSubpassDescription    subpass    = {0};
  VkSubpassDependency     dependency = {0};
  VkRenderPassCreateInfo  info       = {0};

  attachment.format         = format;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = vk__colorLoadOp(load);
  attachment.storeOp        = vk__colorStoreOp(store);
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = load == GPU_LOAD_OP_LOAD
                                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                : VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  color.attachment = 0u;
  color.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1u;
  subpass.pColorAttachments    = &color;

  dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass    = 0u;
  dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1u;
  info.pAttachments    = &attachment;
  info.subpassCount    = 1u;
  info.pSubpasses      = &subpass;
  info.dependencyCount = 1u;
  info.pDependencies   = &dependency;
  return vkCreateRenderPass(device, &info, NULL, outRenderPass);
}

GPU_HIDE
GPUResult
vk_createTexture(GPUDevice                  * __restrict device,
                 const GPUTextureCreateInfo * __restrict info,
                 GPUTexture                ** __restrict outTexture) {
  GPUDeviceVk           *deviceVk;
  GPUTexture            *texture;
  GPUTextureVk          *native;
  GPUTextureVk           state = {0};
  VkImageCreateInfo      imageInfo = {0};
  VkMemoryRequirements   requirements;
  VkMemoryAllocateInfo   allocationInfo = {0};
  VkMemoryPropertyFlags  memoryFlags;
  uint32_t               memoryTypeIndex;

  if (!device || !device->_priv || !info || !outTexture ||
      !vk__imageType(info->dimension, &imageInfo.imageType) ||
      !vk_formatFromGPU(info->format, &imageInfo.format) ||
      !vk__textureUsage(info->usage, &imageInfo.usage) ||
      (info->sampleCount != 0u && info->sampleCount != 1u) ||
      (info->dimension == GPU_TEXTURE_DIMENSION_1D && info->height != 1u)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outTexture             = NULL;
  deviceVk                = device->_priv;
  state.device            = deviceVk->device;
  state.layout            = VK_IMAGE_LAYOUT_UNDEFINED;
  state.aspect            = vk__imageAspect(info->format);
  if ((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
      state.aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
    return GPU_ERROR_UNSUPPORTED;
  }
  imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.extent.width  = info->width;
  imageInfo.extent.height = info->dimension == GPU_TEXTURE_DIMENSION_1D
                              ? 1u
                              : info->height;
  imageInfo.extent.depth  = info->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? info->depthOrLayers
                              : 1u;
  imageInfo.mipLevels     = info->mipLevelCount ? info->mipLevelCount : 1u;
  imageInfo.arrayLayers   = info->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? 1u
                              : info->depthOrLayers;
  imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (info->dimension == GPU_TEXTURE_DIMENSION_2D &&
      info->depthOrLayers >= 6u && info->depthOrLayers % 6u == 0u) {
    imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }

  if (vkCreateImage(state.device,
                    &imageInfo,
                    NULL,
                    &state.image) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetImageMemoryRequirements(state.device, state.image, &requirements);
  if (!vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    vk__destroyTextureState(&state);
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryFlags);

  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(state.device,
                       &allocationInfo,
                       NULL,
                       &state.memory) != VK_SUCCESS ||
      vkBindImageMemory(state.device,
                        state.image,
                        state.memory,
                        0u) != VK_SUCCESS) {
    vk__destroyTextureState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) {
    for (uint32_t load = 0u; load < 3u; load++) {
      for (uint32_t store = 0u; store < 2u; store++) {
        if (vk__createColorRenderPass(
              state.device,
              imageInfo.format,
              load,
              store,
              &state.renderPasses[load][store]
            ) != VK_SUCCESS) {
          vk__destroyTextureState(&state);
          return GPU_ERROR_BACKEND_FAILURE;
        }
      }
    }
  }

  texture = calloc(1, sizeof(*texture) + sizeof(*native));
  if (!texture) {
    vk__destroyTextureState(&state);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native                 = (GPUTextureVk *)(texture + 1);
  *native                = state;
  texture->_priv         = native;
  texture->format        = info->format;
  texture->dimension     = info->dimension;
  texture->width         = info->width;
  texture->height        = info->height;
  texture->depthOrLayers = info->depthOrLayers;
  texture->mipLevelCount = imageInfo.mipLevels;
  texture->sampleCount   = 1u;
  texture->usage         = info->usage;
  texture->_ownsNative   = true;
  *outTexture            = texture;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyTexture(GPUTexture * __restrict texture) {
  if (!texture || !texture->_ownsNative) {
    return;
  }

  vk__destroyTextureState(texture->_priv);
  free(texture);
}

GPU_HIDE
GPUResult
vk_createTextureView(GPUTexture                     * __restrict texture,
                     const GPUTextureViewCreateInfo * __restrict info,
                     GPUTextureView                ** __restrict outView) {
  GPUTextureVk         *textureVk;
  GPUTextureView       *view;
  GPUTextureViewVk     *native;
  VkImageViewCreateInfo viewInfo = {0};
  VkFramebufferCreateInfo framebufferInfo = {0};
  bool targetView;

  textureVk = texture ? texture->_priv : NULL;
  if (!texture || !textureVk || !textureVk->image || !info || !outView ||
      !vk__imageViewType(info->viewType, &viewInfo.viewType) ||
      !vk_formatFromGPU(info->format, &viewInfo.format)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outView                               = NULL;
  viewInfo.sType                         = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image                         = textureVk->image;
  viewInfo.subresourceRange.aspectMask   = vk__imageAspect(info->format);
  viewInfo.subresourceRange.baseMipLevel = info->baseMipLevel;
  viewInfo.subresourceRange.levelCount   = info->mipLevelCount;
  viewInfo.subresourceRange.baseArrayLayer = info->baseArrayLayer;
  viewInfo.subresourceRange.layerCount     = info->arrayLayerCount;

  view = calloc(1, sizeof(*view) + sizeof(*native));
  if (!view) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUTextureViewVk *)(view + 1);
  native->device = textureVk->device;
  if (vkCreateImageView(native->device,
                        &viewInfo,
                        NULL,
                        &native->view) != VK_SUCCESS) {
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  targetView = (texture->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
               info->mipLevelCount == 1u &&
               (info->viewType == GPU_TEXTURE_VIEW_2D ||
                info->viewType == GPU_TEXTURE_VIEW_2D_ARRAY);
  if ((texture->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
      (texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u &&
      !targetView) {
    vkDestroyImageView(native->device, native->view, NULL);
    free(view);
    return GPU_ERROR_UNSUPPORTED;
  }
  if (targetView) {
    native->texture       = textureVk;
    native->extent.width  = texture->width >> info->baseMipLevel;
    native->extent.height = texture->height >> info->baseMipLevel;
    if (native->extent.width == 0u) {
      native->extent.width = 1u;
    }
    if (native->extent.height == 0u) {
      native->extent.height = 1u;
    }

    framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass      =
      textureVk->renderPasses[GPU_LOAD_OP_CLEAR][GPU_STORE_OP_STORE];
    framebufferInfo.attachmentCount = 1u;
    framebufferInfo.pAttachments    = &native->view;
    framebufferInfo.width           = native->extent.width;
    framebufferInfo.height          = native->extent.height;
    framebufferInfo.layers          = info->arrayLayerCount;
    if (!framebufferInfo.renderPass ||
        vkCreateFramebuffer(native->device,
                            &framebufferInfo,
                            NULL,
                            &native->framebuffer) != VK_SUCCESS) {
      vkDestroyImageView(native->device, native->view, NULL);
      free(view);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  view->_priv       = native;
  view->_texture    = texture;
  view->_ownsNative = true;
  *outView          = view;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyTextureView(GPUTextureView * __restrict view) {
  GPUTextureViewVk *native;

  if (!view || !view->_ownsNative) {
    return;
  }

  native = view->_priv;
  if (native && native->device) {
    if (native->framebuffer) {
      vkDestroyFramebuffer(native->device, native->framebuffer, NULL);
    }
    if (native->view) {
      vkDestroyImageView(native->device, native->view, NULL);
    }
  }
  free(view);
}

static GPUResult
vk__submitTextureWrite(GPUCommandQueue             *queue,
                       GPUTexture                  *texture,
                       const GPUTextureWriteRegion *region,
                       GPUBuffer                   *staging) {
  GPUCommandQueueVk          *queueVk;
  GPUDeviceVk                *deviceVk;
  GPUTextureVk               *textureVk;
  GPUBufferVk                *bufferVk;
  VkCommandBuffer             command;
  VkFence                     fence;
  VkCommandBufferAllocateInfo allocationInfo = {0};
  VkCommandBufferBeginInfo    beginInfo = {0};
  VkFenceCreateInfo           fenceInfo = {0};
  VkBufferImageCopy           copy = {0};
  VkSubmitInfo                submitInfo = {0};
  VkPipelineStageFlags        srcStage;
  VkPipelineStageFlags        dstStage;
  VkAccessFlags               srcAccess;
  VkAccessFlags               dstAccess;
  VkImageLayout               finalLayout;
  VkResult                    result;
  uint32_t                    bytesPerPixel;
  bool                        submitted;

  queueVk       = queue ? queue->_priv : NULL;
  deviceVk      = queue && queue->_device ? queue->_device->_priv : NULL;
  textureVk     = texture ? texture->_priv : NULL;
  bufferVk      = staging ? staging->_priv : NULL;
  bytesPerPixel = texture ? vk__formatBytes(texture->format) : 0u;
  if (!queueVk || !deviceVk || !textureVk || !bufferVk || !region ||
      textureVk->device != deviceVk->device ||
      bufferVk->device != deviceVk->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (bytesPerPixel == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (region->bytesPerRow % bytesPerPixel != 0u ||
      region->bytesPerRow / bytesPerPixel < region->width) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  command   = VK_NULL_HANDLE;
  fence     = VK_NULL_HANDLE;
  submitted = false;
  allocationInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocationInfo.commandPool        = queueVk->commandPool;
  allocationInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocationInfo.commandBufferCount = 1u;
  if (vkAllocateCommandBuffers(deviceVk->device,
                               &allocationInfo,
                               &command) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(command, &beginInfo);
  if (result != VK_SUCCESS) {
    goto cleanup;
  }

  vk__layoutSource(textureVk->layout, &srcStage, &srcAccess);
  vk__imageBarrier(command,
                   textureVk,
                   texture,
                   textureVk->layout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   srcStage,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   srcAccess,
                   VK_ACCESS_TRANSFER_WRITE_BIT);

  copy.bufferOffset                    = 0u;
  copy.bufferRowLength                 = region->bytesPerRow / bytesPerPixel;
  copy.bufferImageHeight               = region->rowsPerImage
                                           ? region->rowsPerImage
                                           : region->height;
  copy.imageSubresource.aspectMask     = textureVk->aspect;
  copy.imageSubresource.mipLevel       = region->mipLevel;
  copy.imageSubresource.baseArrayLayer = region->baseArrayLayer;
  copy.imageSubresource.layerCount     = region->layerCount;
  copy.imageExtent.width               = region->width;
  copy.imageExtent.height              = region->height;
  copy.imageExtent.depth               = region->depth;
  vkCmdCopyBufferToImage(command,
                         bufferVk->buffer,
                         textureVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);

  finalLayout = vk__finalImageLayout(texture->usage);
  if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    vk__finalLayoutTarget(finalLayout, &dstStage, &dstAccess);
    vk__imageBarrier(command,
                     textureVk,
                     texture,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     finalLayout,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     dstStage,
                     VK_ACCESS_TRANSFER_WRITE_BIT,
                     dstAccess);
  }

  result = vkEndCommandBuffer(command);
  if (result != VK_SUCCESS) {
    goto cleanup;
  }

  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  result = vkCreateFence(deviceVk->device, &fenceInfo, NULL, &fence);
  if (result != VK_SUCCESS) {
    goto cleanup;
  }

  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1u;
  submitInfo.pCommandBuffers    = &command;
  result = vkQueueSubmit(queueVk->queRaw, 1u, &submitInfo, fence);
  if (result == VK_SUCCESS) {
    submitted = true;
    result = vkWaitForFences(deviceVk->device,
                             1u,
                             &fence,
                             VK_TRUE,
                             UINT64_MAX);
  }
  if (result == VK_SUCCESS) {
    textureVk->layout = finalLayout;
  }

cleanup:
  if (submitted && result != VK_SUCCESS) {
    (void)vkDeviceWaitIdle(deviceVk->device);
  }
  if (fence) {
    vkDestroyFence(deviceVk->device, fence, NULL);
  }
  if (command) {
    vkFreeCommandBuffers(deviceVk->device,
                         queueVk->commandPool,
                         1u,
                         &command);
  }
  return result == VK_SUCCESS ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
GPUResult
vk_writeTexture(GPUCommandQueue             * __restrict queue,
                GPUTexture                  * __restrict texture,
                const GPUTextureWriteRegion * __restrict region,
                const void                  * __restrict data,
                uint64_t                                 sizeBytes) {
  GPUBufferCreateInfo stagingInfo = {0};
  GPUBuffer          *staging;
  GPUResult           result;

  if (!queue || !texture || !texture->_ownsNative || !region || !data ||
      sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  stagingInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingInfo.chain.structSize = sizeof(stagingInfo);
  stagingInfo.label            = "vulkan-texture-upload";
  stagingInfo.sizeBytes        = sizeBytes;
  stagingInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                 GPU_BUFFER_USAGE_COPY_DST;
  staging = NULL;
  result = vk_createBuffer(queue->_device, &stagingInfo, &staging);
  if (result != GPU_OK) {
    return result;
  }

  result = vk_writeBuffer(queue, staging, 0u, data, sizeBytes);
  if (result == GPU_OK) {
    result = vk__submitTextureWrite(queue, texture, region, staging);
  }
  vk_destroyBuffer(staging);
  return result;
}

GPU_HIDE
void
vk_initTexture(GPUApiTexture *api) {
  api->create      = vk_createTexture;
  api->destroy     = vk_destroyTexture;
  api->createView  = vk_createTextureView;
  api->destroyView = vk_destroyTextureView;
  api->write       = vk_writeTexture;
}
