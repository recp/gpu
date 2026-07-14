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

enum {
  VK_TEXTURE_BARRIER_CHUNK_SIZE = 16u
};

static VkSampleCountFlagBits
vk__sampleCount(uint32_t count) {
  static const VkSampleCountFlagBits counts[] = {
    [1] = VK_SAMPLE_COUNT_1_BIT,
    [2] = VK_SAMPLE_COUNT_2_BIT,
    [4] = VK_SAMPLE_COUNT_4_BIT,
    [8] = VK_SAMPLE_COUNT_8_BIT
  };

  return count < GPU_ARRAY_LEN(counts) ? counts[count] : 0;
}

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
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      *outType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
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
    case GPU_FORMAT_DEPTH16_UNORM:
    case GPU_FORMAT_DEPTH32_FLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;
    case GPU_FORMAT_STENCIL8:
      return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
  }
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

static bool
vk__textureRangeValid(const GPUTextureVk *texture,
                      uint32_t            baseMip,
                      uint32_t            mipCount,
                      uint32_t            baseLayer,
                      uint32_t            layerCount) {
  return texture && texture->image && texture->layouts &&
         mipCount > 0u && layerCount > 0u &&
         baseMip < texture->mipLevelCount &&
         mipCount <= texture->mipLevelCount - baseMip &&
         baseLayer < texture->arrayLayerCount &&
         layerCount <= texture->arrayLayerCount - baseLayer;
}

static uint32_t
vk__textureSubresource(const GPUTextureVk *texture,
                       uint32_t            mip,
                       uint32_t            layer) {
  return mip + layer * texture->mipLevelCount;
}

static bool
vk__textureRangeFull(const GPUTextureVk *texture,
                     uint32_t            baseMip,
                     uint32_t            mipCount,
                     uint32_t            baseLayer,
                     uint32_t            layerCount) {
  return baseMip == 0u && mipCount == texture->mipLevelCount &&
         baseLayer == 0u && layerCount == texture->arrayLayerCount;
}

static void
vk__materializeTextureLayouts(GPUTextureVk *texture) {
  if (!texture || !texture->layouts || !texture->layoutUniform) {
    return;
  }

  for (uint32_t i = 0u; i < texture->subresourceCount; i++) {
    texture->layouts[i] = texture->layout;
  }
}

GPU_HIDE
void
vk_setTextureLayout(GPUTextureVk *texture,
                    uint32_t      baseMip,
                    uint32_t      mipCount,
                    uint32_t      baseLayer,
                    uint32_t      layerCount,
                    VkImageLayout layout) {
  if (!vk__textureRangeValid(texture,
                             baseMip,
                             mipCount,
                             baseLayer,
                             layerCount)) {
    return;
  }
  if (vk__textureRangeFull(texture,
                           baseMip,
                           mipCount,
                           baseLayer,
                           layerCount)) {
    texture->layout        = layout;
    texture->layoutUniform = true;
    return;
  }
  if (texture->layoutUniform && texture->layout == layout) {
    return;
  }

  vk__materializeTextureLayouts(texture);
  for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++) {
    for (uint32_t mip = baseMip; mip < baseMip + mipCount; mip++) {
      uint32_t subresource;

      subresource = vk__textureSubresource(texture, mip, layer);
      texture->layouts[subresource] = layout;
    }
  }
  texture->layoutUniform = false;
}

static void
vk__flushTextureBarriers(VkCommandBuffer       command,
                         VkImageMemoryBarrier  *barriers,
                         uint32_t               barrierCount,
                         VkPipelineStageFlags   srcStages,
                         VkPipelineStageFlags   dstStages) {
  if (barrierCount == 0u) {
    return;
  }

  vkCmdPipelineBarrier(command,
                       srcStages,
                       dstStages,
                       0u,
                       0u,
                       NULL,
                       0u,
                       NULL,
                       barrierCount,
                       barriers);
}

static void
vk__fillTextureBarrier(VkImageMemoryBarrier *barrier,
                       GPUTextureVk         *texture,
                       VkImageLayout         oldLayout,
                       VkImageLayout         newLayout,
                       uint32_t              baseMip,
                       uint32_t              mipCount,
                       uint32_t              baseLayer,
                       uint32_t              layerCount,
                       VkAccessFlags         srcAccess,
                       VkAccessFlags         dstAccess) {
  memset(barrier, 0, sizeof(*barrier));
  barrier->sType                           =
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier->srcAccessMask                   = srcAccess;
  barrier->dstAccessMask                   = dstAccess;
  barrier->oldLayout                       = oldLayout;
  barrier->newLayout                       = newLayout;
  barrier->srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier->dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier->image                           = texture->image;
  barrier->subresourceRange.aspectMask     = texture->aspect;
  barrier->subresourceRange.baseMipLevel   = baseMip;
  barrier->subresourceRange.levelCount     = mipCount;
  barrier->subresourceRange.baseArrayLayer = baseLayer;
  barrier->subresourceRange.layerCount     = layerCount;
}

static bool
vk__transitionTexture(VkCommandBuffer      command,
                      GPUTextureVk        *texture,
                      uint32_t             baseMip,
                      uint32_t             mipCount,
                      uint32_t             baseLayer,
                      uint32_t             layerCount,
                      VkImageLayout        nextLayout,
                      VkPipelineStageFlags explicitSrcStages,
                      VkPipelineStageFlags explicitDstStages,
                      VkAccessFlags        explicitSrcAccess,
                      VkAccessFlags        explicitDstAccess,
                      bool                 explicitSync) {
  VkImageMemoryBarrier barriers[VK_TEXTURE_BARRIER_CHUNK_SIZE];
  VkPipelineStageFlags chunkSrcStages;
  VkPipelineStageFlags dstStages;
  VkAccessFlags        dstAccess;
  uint32_t             barrierCount;
  bool                 fullRange;

  if (!command ||
      !vk__textureRangeValid(texture,
                             baseMip,
                             mipCount,
                             baseLayer,
                             layerCount)) {
    return false;
  }

  fullRange = vk__textureRangeFull(texture,
                                   baseMip,
                                   mipCount,
                                   baseLayer,
                                   layerCount);
  if (texture->layoutUniform) {
    VkPipelineStageFlags srcStages;
    VkAccessFlags        srcAccess;

    if (!explicitSync && texture->layout == nextLayout) {
      return true;
    }

    if (explicitSync) {
      srcStages = explicitSrcStages;
      dstStages = explicitDstStages;
      srcAccess = explicitSrcAccess;
      dstAccess = explicitDstAccess;
    } else {
      vk__layoutSource(texture->layout, &srcStages, &srcAccess);
      vk__layoutSource(nextLayout, &dstStages, &dstAccess);
    }
    vk__fillTextureBarrier(&barriers[0],
                           texture,
                           texture->layout,
                           nextLayout,
                           baseMip,
                           mipCount,
                           baseLayer,
                           layerCount,
                           srcAccess,
                           dstAccess);
    vk__flushTextureBarriers(command,
                             barriers,
                             1u,
                             srcStages,
                             dstStages);
    if (fullRange) {
      texture->layout        = nextLayout;
      texture->layoutUniform = true;
    } else {
      vk_setTextureLayout(texture,
                          baseMip,
                          mipCount,
                          baseLayer,
                          layerCount,
                          nextLayout);
    }
    return true;
  }

  if (explicitSync) {
    dstStages = explicitDstStages;
    dstAccess = explicitDstAccess;
  } else {
    vk__layoutSource(nextLayout, &dstStages, &dstAccess);
  }
  barrierCount   = 0u;
  chunkSrcStages = explicitSync ? explicitSrcStages : 0u;
  for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++) {
    for (uint32_t mip = baseMip; mip < baseMip + mipCount; mip++) {
      VkPipelineStageFlags srcStages;
      VkAccessFlags        srcAccess;
      VkImageLayout        oldLayout;
      uint32_t             subresource;

      subresource = vk__textureSubresource(texture, mip, layer);
      oldLayout   = texture->layouts[subresource];
      if (!explicitSync && oldLayout == nextLayout) {
        continue;
      }

      if (explicitSync) {
        srcStages = explicitSrcStages;
        srcAccess = explicitSrcAccess;
      } else {
        vk__layoutSource(oldLayout, &srcStages, &srcAccess);
        chunkSrcStages |= srcStages;
      }
      vk__fillTextureBarrier(&barriers[barrierCount++],
                             texture,
                             oldLayout,
                             nextLayout,
                             mip,
                             1u,
                             layer,
                             1u,
                             srcAccess,
                             dstAccess);
      texture->layouts[subresource] = nextLayout;
      if (barrierCount == VK_TEXTURE_BARRIER_CHUNK_SIZE) {
        vk__flushTextureBarriers(command,
                                 barriers,
                                 barrierCount,
                                 chunkSrcStages,
                                 dstStages);
        barrierCount   = 0u;
        chunkSrcStages = explicitSync ? explicitSrcStages : 0u;
      }
    }
  }
  if (barrierCount > 0u) {
    vk__flushTextureBarriers(command,
                             barriers,
                             barrierCount,
                             chunkSrcStages,
                             dstStages);
  }

  if (fullRange) {
    texture->layout        = nextLayout;
    texture->layoutUniform = true;
  }
  return true;
}

GPU_HIDE
bool
vk_transitionTexture(VkCommandBuffer command,
                     GPUTextureVk   *texture,
                     uint32_t        baseMip,
                     uint32_t        mipCount,
                     uint32_t        baseLayer,
                     uint32_t        layerCount,
                     VkImageLayout   nextLayout) {
  return vk__transitionTexture(command,
                               texture,
                               baseMip,
                               mipCount,
                               baseLayer,
                               layerCount,
                               nextLayout,
                               0u,
                               0u,
                               0u,
                               0u,
                               false);
}

GPU_HIDE
bool
vk_transitionTextureBarrier(VkCommandBuffer      command,
                            GPUTextureVk        *texture,
                            uint32_t             baseMip,
                            uint32_t             mipCount,
                            uint32_t             baseLayer,
                            uint32_t             layerCount,
                            VkImageLayout        nextLayout,
                            VkPipelineStageFlags srcStages,
                            VkPipelineStageFlags dstStages,
                            VkAccessFlags        srcAccess,
                            VkAccessFlags        dstAccess) {
  return vk__transitionTexture(command,
                               texture,
                               baseMip,
                               mipCount,
                               baseLayer,
                               layerCount,
                               nextLayout,
                               srcStages,
                               dstStages,
                               srcAccess,
                               dstAccess,
                               true);
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
  VkSampleCountFlagBits  sampleCount;
  uint32_t               arrayLayerCount;
  uint32_t               subresourceCount;
  uint32_t               memoryTypeIndex;

  if (!device || !device->_priv || !info || !outTexture ||
      !vk__imageType(info->dimension, &imageInfo.imageType) ||
      !vk_formatFromGPU(info->format, &imageInfo.format) ||
      !vk__textureUsage(info->usage, &imageInfo.usage) ||
      (info->dimension == GPU_TEXTURE_DIMENSION_1D && info->height != 1u)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outTexture             = NULL;
  deviceVk                = device->_priv;
  sampleCount             = vk__sampleCount(info->sampleCount
                                              ? info->sampleCount
                                              : 1u);
  if (!sampleCount ||
      (sampleCount > VK_SAMPLE_COUNT_1_BIT && !deviceVk->dynamicRendering) ||
      (((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) &&
       (deviceVk->colorSampleCounts & sampleCount) == 0u) ||
      (((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) &&
       (deviceVk->depthSampleCounts & sampleCount) == 0u)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  state.gpuDevice         = deviceVk;
  state.device            = deviceVk->device;
  state.layout            = VK_IMAGE_LAYOUT_UNDEFINED;
  state.aspect            = vk__imageAspect(info->format);
  if (((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
       state.aspect != VK_IMAGE_ASPECT_COLOR_BIT) ||
      ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u &&
       state.aspect == VK_IMAGE_ASPECT_COLOR_BIT) ||
      (info->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                      GPU_TEXTURE_USAGE_DEPTH_STENCIL)) ==
        (GPU_TEXTURE_USAGE_COLOR_TARGET |
         GPU_TEXTURE_USAGE_DEPTH_STENCIL) ||
      ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u &&
       info->dimension != GPU_TEXTURE_DIMENSION_2D)) {
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
  imageInfo.samples       = sampleCount;
  imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (info->dimension == GPU_TEXTURE_DIMENSION_2D &&
      info->depthOrLayers >= 6u && info->depthOrLayers % 6u == 0u) {
    imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }
  arrayLayerCount = imageInfo.arrayLayers;
  if (imageInfo.mipLevels > UINT32_MAX / arrayLayerCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  subresourceCount = imageInfo.mipLevels * arrayLayerCount;
  state.mipLevelCount    = imageInfo.mipLevels;
  state.arrayLayerCount  = arrayLayerCount;
  state.subresourceCount = subresourceCount;
  state.layoutUniform    = true;

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

  if ((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
      !deviceVk->dynamicRendering) {
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

  texture = calloc(1,
                   sizeof(*texture) + sizeof(*native) +
                     (size_t)subresourceCount * sizeof(*native->layouts));
  if (!texture) {
    vk__destroyTextureState(&state);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native                 = (GPUTextureVk *)(texture + 1);
  *native                = state;
  native->layouts        = (VkImageLayout *)(native + 1);
  texture->_priv         = native;
  texture->format        = info->format;
  texture->dimension     = info->dimension;
  texture->width         = info->width;
  texture->height        = info->height;
  texture->depthOrLayers = info->depthOrLayers;
  texture->mipLevelCount = imageInfo.mipLevels;
  texture->sampleCount   = info->sampleCount ? info->sampleCount : 1u;
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
  bool attachmentView;

  textureVk = texture ? texture->_priv : NULL;
  if (!texture || !textureVk || !textureVk->image || !info || !outView ||
      info->format != texture->format ||
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

  native->texture    = textureVk;
  native->layout     = &textureVk->layout;
  native->image      = textureVk->image;
  native->aspect     = viewInfo.subresourceRange.aspectMask;
  native->baseMip    = info->baseMipLevel;
  native->mipCount   = info->mipLevelCount;
  native->baseLayer  = info->baseArrayLayer;
  native->layerCount = info->arrayLayerCount;

  attachmentView =
    (texture->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_DEPTH_STENCIL)) != 0u &&
    info->mipLevelCount == 1u &&
    (info->viewType == GPU_TEXTURE_VIEW_2D ||
     info->viewType == GPU_TEXTURE_VIEW_2D_ARRAY);
  if ((texture->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                         GPU_TEXTURE_USAGE_DEPTH_STENCIL)) != 0u &&
      (texture->usage & GPU_TEXTURE_USAGE_SAMPLED) == 0u &&
      !attachmentView) {
    vkDestroyImageView(native->device, native->view, NULL);
    free(view);
    return GPU_ERROR_UNSUPPORTED;
  }
  if (attachmentView) {
    native->extent.width  = texture->width >> info->baseMipLevel;
    native->extent.height = texture->height >> info->baseMipLevel;
    if (native->extent.width == 0u) {
      native->extent.width = 1u;
    }
    if (native->extent.height == 0u) {
      native->extent.height = 1u;
    }

    if ((texture->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
        !textureVk->gpuDevice->dynamicRendering) {
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
vk__recordTextureWrite(VkCommandBuffer             command,
                       VkBuffer                    staging,
                       uint64_t                    stagingOffset,
                       GPUTexture                 *texture,
                       const GPUTextureWriteRegion *region) {
  GPUFormatLayout   formatLayout;
  GPUTextureVk     *textureVk;
  VkBufferImageCopy copy = {0};
  VkImageAspectFlags aspect;
  VkImageLayout      finalLayout;
  uint32_t           rowBlocks;
  uint32_t           rowLength;

  textureVk = texture ? texture->_priv : NULL;
  if (!command || !staging || !textureVk || !region) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!vk__copyAspect(texture->format, region->aspect, &aspect) ||
      !gpuFormatAspectLayout(texture->format,
                             region->aspect,
                             &formatLayout)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (region->bytesPerRow % formatLayout.bytesPerBlock != 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  rowBlocks = region->bytesPerRow / formatLayout.bytesPerBlock;
  if (rowBlocks > UINT32_MAX / formatLayout.blockWidth) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  rowLength = rowBlocks * formatLayout.blockWidth;
  if (rowLength < region->width) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!vk_transitionTexture(command,
                            textureVk,
                            region->mipLevel,
                            1u,
                            texture->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? 0u
                              : region->baseArrayLayer,
                            texture->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? 1u
                              : region->layerCount,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  copy.bufferOffset                    = stagingOffset;
  copy.bufferRowLength                 = rowLength;
  copy.bufferImageHeight               = region->rowsPerImage;
  copy.imageSubresource.aspectMask     = aspect;
  copy.imageSubresource.mipLevel       = region->mipLevel;
  copy.imageSubresource.baseArrayLayer = region->baseArrayLayer;
  copy.imageSubresource.layerCount     = region->layerCount;
  copy.imageExtent.width               = region->width;
  copy.imageExtent.height              = region->height;
  copy.imageExtent.depth               = region->depth;
  vkCmdCopyBufferToImage(command,
                         staging,
                         textureVk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1u,
                         &copy);

  finalLayout = vk__finalImageLayout(texture->usage);
  if (finalLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
      !vk_transitionTexture(command,
                            textureVk,
                            region->mipLevel,
                            1u,
                            texture->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? 0u
                              : region->baseArrayLayer,
                            texture->dimension == GPU_TEXTURE_DIMENSION_3D
                              ? 1u
                              : region->layerCount,
                            finalLayout)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_writeTexture(GPUQueue             * __restrict queue,
                GPUTexture                  * __restrict texture,
                const GPUTextureWriteRegion * __restrict region,
                const void                  * __restrict data,
                uint64_t                                 sizeBytes) {
  VkCommandBuffer command;
  GPUBuffer      *staging;
  GPUBufferVk    *stagingVk;
  uint64_t        stagingOffset;
  GPUResult       result;

  if (!queue || !texture || !texture->_ownsNative || !region || !data ||
      sizeBytes == 0u || sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vk_beginTransfer(queue,
                            true,
                            sizeBytes,
                            GPU_VK_TEXTURE_TRANSFER_CAPACITY,
                            &command,
                            &staging,
                            &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }

  result = vk_writeBuffer(queue,
                          staging,
                          stagingOffset,
                          data,
                          sizeBytes);
  stagingVk = staging ? staging->_priv : NULL;
  if (result == GPU_OK && (!stagingVk || !stagingVk->buffer)) {
    result = GPU_ERROR_BACKEND_FAILURE;
  }
  if (result == GPU_OK) {
    result = vk__recordTextureWrite(command,
                                    stagingVk->buffer,
                                    stagingOffset,
                                    texture,
                                    region);
  }
  if (result != GPU_OK) {
    vk_abortTransfer(queue);
    return result;
  }
  return vk_submitTransfer(queue, false);
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
