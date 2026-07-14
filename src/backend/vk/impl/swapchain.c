/*
 * Copyright (C) 2020 Recep Aslantas
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

static VkPresentModeKHR
vk__presentMode(GPUPresentMode mode) {
  switch (mode) {
    case GPU_PRESENT_MODE_MAILBOX:
      return VK_PRESENT_MODE_MAILBOX_KHR;
    case GPU_PRESENT_MODE_IMMEDIATE:
      return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case GPU_PRESENT_MODE_FIFO:
    default:
      return VK_PRESENT_MODE_FIFO_KHR;
  }
}

static VkAttachmentLoadOp
vk__loadOp(uint32_t index) {
  static const VkAttachmentLoadOp ops[] = {
    VK_ATTACHMENT_LOAD_OP_LOAD,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE
  };

  return ops[index];
}

static VkAttachmentStoreOp
vk__storeOp(uint32_t index) {
  return index == 0u ?
    VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkResult
vk__createRenderPass(GPUSwapchainVk *swapchain,
                     uint32_t        loadIndex,
                     uint32_t        storeIndex,
                     VkRenderPass   *outRenderPass) {
  VkAttachmentDescription attachment = {0};
  VkAttachmentReference   colorRef = {0};
  VkSubpassDescription    subpass = {0};
  VkSubpassDependency     dependency = {0};
  VkRenderPassCreateInfo  info = {0};

  attachment.format         = swapchain->format;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = vk__loadOp(loadIndex);
  attachment.storeOp        = vk__storeOp(storeIndex);
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = loadIndex == GPU_LOAD_OP_LOAD ?
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  colorRef.attachment = 0u;
  colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1u;
  subpass.pColorAttachments    = &colorRef;

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
  return vkCreateRenderPass(swapchain->device, &info, NULL, outRenderPass);
}

static void
vk__destroyResources(GPUSwapchainVk *swapchain) {
  uint32_t i;

  if (!swapchain || !swapchain->device) {
    return;
  }

  for (i = 0u; i < swapchain->imageCount; i++) {
    if (swapchain->framebuffers && swapchain->framebuffers[i]) {
      vkDestroyFramebuffer(swapchain->device,
                           swapchain->framebuffers[i],
                           NULL);
    }
    if (swapchain->imageViews && swapchain->imageViews[i]) {
      vkDestroyImageView(swapchain->device, swapchain->imageViews[i], NULL);
    }
    if (swapchain->frameSync) {
      if (swapchain->frameSync[i].imageAvailable) {
        vkDestroySemaphore(swapchain->device,
                           swapchain->frameSync[i].imageAvailable,
                           NULL);
      }
      if (swapchain->frameSync[i].renderFinished) {
        vkDestroySemaphore(swapchain->device,
                           swapchain->frameSync[i].renderFinished,
                           NULL);
      }
      if (swapchain->frameSync[i].fence) {
        vkDestroyFence(swapchain->device, swapchain->frameSync[i].fence, NULL);
      }
    }
  }

  for (uint32_t load = 0u; load < 3u; load++) {
    for (uint32_t store = 0u; store < 2u; store++) {
      if (swapchain->renderPasses[load][store]) {
        vkDestroyRenderPass(swapchain->device,
                            swapchain->renderPasses[load][store],
                            NULL);
      }
    }
  }

  if (swapchain->swapchain) {
    vkDestroySwapchainKHR(swapchain->device, swapchain->swapchain, NULL);
  }

  free(swapchain->frameSync);
  free(swapchain->nativeViews);
  free(swapchain->textureViews);
  free(swapchain->textures);
  free(swapchain->framebuffers);
  free(swapchain->imageViews);
  free(swapchain->images);

  swapchain->frameSync    = NULL;
  swapchain->nativeViews  = NULL;
  swapchain->textureViews = NULL;
  swapchain->textures     = NULL;
  swapchain->framebuffers = NULL;
  swapchain->imageViews   = NULL;
  swapchain->images       = NULL;
  swapchain->swapchain    = VK_NULL_HANDLE;
  swapchain->imageCount   = 0u;
  memset(swapchain->renderPasses, 0, sizeof(swapchain->renderPasses));
}

static bool
vk__chooseSurfaceFormat(const VkSurfaceFormatKHR *formats,
                        uint32_t                  count,
                        VkFormat                  requested,
                        VkSurfaceFormatKHR       *outFormat) {
  VkSurfaceFormatKHR fallback;

  if (!formats || count == 0u || !outFormat) {
    return false;
  }

  fallback.format     = requested;
  fallback.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  if (count == 1u && formats[0].format == VK_FORMAT_UNDEFINED) {
    *outFormat = fallback;
    return true;
  }

  for (uint32_t i = 0u; i < count; i++) {
    if (formats[i].format == requested) {
      *outFormat = formats[i];
      return true;
    }
  }

  return false;
}

static VkPresentModeKHR
vk__choosePresentMode(const VkPresentModeKHR *modes,
                      uint32_t                count,
                      GPUPresentMode          requested) {
  VkPresentModeKHR wanted;

  wanted = vk__presentMode(requested);
  for (uint32_t i = 0u; i < count; i++) {
    if (modes[i] == wanted) {
      return wanted;
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D
vk__chooseExtent(const VkSurfaceCapabilitiesKHR *caps,
                 uint32_t                        width,
                 uint32_t                        height,
                 float                           scale) {
  VkExtent2D extent;

  if (caps->currentExtent.width != UINT32_MAX) {
    return caps->currentExtent;
  }

  extent.width  = (uint32_t)((float)width * scale);
  extent.height = (uint32_t)((float)height * scale);
  if (extent.width < caps->minImageExtent.width) {
    extent.width = caps->minImageExtent.width;
  }
  if (extent.width > caps->maxImageExtent.width) {
    extent.width = caps->maxImageExtent.width;
  }
  if (extent.height < caps->minImageExtent.height) {
    extent.height = caps->minImageExtent.height;
  }
  if (extent.height > caps->maxImageExtent.height) {
    extent.height = caps->maxImageExtent.height;
  }
  return extent;
}

static VkCompositeAlphaFlagBitsKHR
vk__compositeAlpha(VkCompositeAlphaFlagsKHR supported) {
  static const VkCompositeAlphaFlagBitsKHR modes[] = {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
  };

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(modes); i++) {
    if ((supported & modes[i]) != 0u) {
      return modes[i];
    }
  }
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static bool
vk__allocateArrays(GPUSwapchainVk *swapchain, uint32_t count) {
  swapchain->images       = calloc(count, sizeof(*swapchain->images));
  swapchain->imageViews   = calloc(count, sizeof(*swapchain->imageViews));
  swapchain->framebuffers = calloc(count, sizeof(*swapchain->framebuffers));
  swapchain->textures     = calloc(count, sizeof(*swapchain->textures));
  swapchain->textureViews = calloc(count, sizeof(*swapchain->textureViews));
  swapchain->nativeViews  = calloc(count, sizeof(*swapchain->nativeViews));
  swapchain->frameSync    = calloc(count, sizeof(*swapchain->frameSync));

  return swapchain->images && swapchain->imageViews &&
         swapchain->framebuffers && swapchain->textures &&
         swapchain->textureViews && swapchain->nativeViews &&
         swapchain->frameSync;
}

static bool
vk__createImageState(GPUSwapchainVk *swapchain) {
  GPUDeviceVk             *device;
  VkSemaphoreCreateInfo semaphoreInfo = {0};
  VkFenceCreateInfo     fenceInfo = {0};
  VkImageViewCreateInfo viewInfo = {0};
  VkFramebufferCreateInfo framebufferInfo = {0};
  uint32_t count;

  count = swapchain->imageCount;
  if (!vk__allocateArrays(swapchain, count)) {
    return false;
  }
  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (!device) {
    return false;
  }
  if (vkGetSwapchainImagesKHR(swapchain->device,
                              swapchain->swapchain,
                              &count,
                              swapchain->images) != VK_SUCCESS ||
      count != swapchain->imageCount) {
    return false;
  }

  if (!device->dynamicRendering) {
    for (uint32_t load = 0u; load < 3u; load++) {
      for (uint32_t store = 0u; store < 2u; store++) {
        if (vk__createRenderPass(swapchain,
                                 load,
                                 store,
                                 &swapchain->renderPasses[load][store]) !=
            VK_SUCCESS) {
          return false;
        }
      }
    }
  }

  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  fenceInfo.sType      = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags      = VK_FENCE_CREATE_SIGNALED_BIT;

  viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format                          = swapchain->format;
  viewInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel   = 0u;
  viewInfo.subresourceRange.levelCount     = 1u;
  viewInfo.subresourceRange.baseArrayLayer = 0u;
  viewInfo.subresourceRange.layerCount     = 1u;

  framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass      = swapchain->renderPasses[GPU_LOAD_OP_CLEAR]
                                                           [GPU_STORE_OP_STORE];
  framebufferInfo.attachmentCount = 1u;
  framebufferInfo.width           = swapchain->extent.width;
  framebufferInfo.height          = swapchain->extent.height;
  framebufferInfo.layers          = 1u;

  for (uint32_t i = 0u; i < swapchain->imageCount; i++) {
    GPUTexture       *texture;
    GPUTextureView   *view;
    GPUTextureViewVk *nativeView;

    viewInfo.image = swapchain->images[i];
    if (vkCreateImageView(swapchain->device,
                          &viewInfo,
                          NULL,
                          &swapchain->imageViews[i]) != VK_SUCCESS) {
      return false;
    }

    framebufferInfo.pAttachments = &swapchain->imageViews[i];
    if ((!device->dynamicRendering &&
         vkCreateFramebuffer(swapchain->device,
                             &framebufferInfo,
                             NULL,
                             &swapchain->framebuffers[i]) != VK_SUCCESS) ||
        vkCreateSemaphore(swapchain->device,
                          &semaphoreInfo,
                          NULL,
                          &swapchain->frameSync[i].imageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(swapchain->device,
                          &semaphoreInfo,
                          NULL,
                          &swapchain->frameSync[i].renderFinished) != VK_SUCCESS ||
        vkCreateFence(swapchain->device,
                      &fenceInfo,
                      NULL,
                      &swapchain->frameSync[i].fence) != VK_SUCCESS) {
      return false;
    }

    nativeView             = &swapchain->nativeViews[i];
    nativeView->swapchain  = swapchain;
    nativeView->layout     = &nativeView->localLayout;
    nativeView->device     = swapchain->device;
    nativeView->image      = swapchain->images[i];
    nativeView->view       = swapchain->imageViews[i];
    nativeView->extent     = swapchain->extent;
    nativeView->aspect     = VK_IMAGE_ASPECT_COLOR_BIT;
    nativeView->imageIndex = i;
    nativeView->mipCount   = 1u;
    nativeView->layerCount = 1u;

    texture                = &swapchain->textures[i];
    texture->_priv         = nativeView;
    texture->format        = swapchain->gpuFormat;
    texture->dimension     = GPU_TEXTURE_DIMENSION_2D;
    texture->width         = swapchain->extent.width;
    texture->height        = swapchain->extent.height;
    texture->depthOrLayers = 1u;
    texture->mipLevelCount = 1u;
    texture->sampleCount   = 1u;
    texture->usage         = GPU_TEXTURE_USAGE_COLOR_TARGET;
    texture->_ownsNative   = false;

    view                   = &swapchain->textureViews[i];
    view->_priv            = nativeView;
    view->_texture         = texture;
    view->format           = swapchain->gpuFormat;
    view->viewType         = GPU_TEXTURE_VIEW_2D;
    view->mipLevelCount    = 1u;
    view->arrayLayerCount  = 1u;
    view->_ownsNative      = false;
  }

  return true;
}

static bool
vk__createResources(GPUSwapchain  *swapchainObj,
                    uint32_t       width,
                    uint32_t       height,
                    VkSwapchainKHR oldSwapchain) {
  GPUSwapchainVk          *swapchain;
  VkSurfaceCapabilitiesKHR caps;
  VkSurfaceFormatKHR      *formats;
  VkPresentModeKHR        *modes;
  VkSurfaceFormatKHR       surfaceFormat;
  VkPresentModeKHR         presentMode;
  VkSwapchainCreateInfoKHR info = {0};
  uint32_t                 formatCount;
  uint32_t                 modeCount;
  uint32_t                 imageCount;
  VkResult                 result;

  swapchain   = swapchainObj->_priv;
  formats     = NULL;
  modes       = NULL;
  formatCount = 0u;
  modeCount   = 0u;

#if defined(__APPLE__)
  vk_resizeMetalLayer(swapchain->surface->metalLayer,
                      width,
                      height,
                      swapchainObj->backingScaleFactor);
#endif

  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(swapchain->physicalDevice,
                                                       swapchain->surface->surface,
                                                       &caps);
  if (result != VK_SUCCESS ||
      vkGetPhysicalDeviceSurfaceFormatsKHR(swapchain->physicalDevice,
                                           swapchain->surface->surface,
                                           &formatCount,
                                           NULL) != VK_SUCCESS ||
      formatCount == 0u ||
      vkGetPhysicalDeviceSurfacePresentModesKHR(swapchain->physicalDevice,
                                                swapchain->surface->surface,
                                                &modeCount,
                                                NULL) != VK_SUCCESS ||
      modeCount == 0u) {
    return false;
  }

  formats = malloc((size_t)formatCount * sizeof(*formats));
  modes   = malloc((size_t)modeCount * sizeof(*modes));
  if (!formats || !modes ||
      vkGetPhysicalDeviceSurfaceFormatsKHR(swapchain->physicalDevice,
                                           swapchain->surface->surface,
                                           &formatCount,
                                           formats) != VK_SUCCESS ||
      vkGetPhysicalDeviceSurfacePresentModesKHR(swapchain->physicalDevice,
                                                swapchain->surface->surface,
                                                &modeCount,
                                                modes) != VK_SUCCESS) {
    free(modes);
    free(formats);
    return false;
  }

  if (!vk__chooseSurfaceFormat(formats,
                               formatCount,
                               swapchain->format,
                               &surfaceFormat)) {
    free(modes);
    free(formats);
    return false;
  }
  presentMode = vk__choosePresentMode(modes,
                                      modeCount,
                                      swapchain->presentMode);
  free(modes);
  free(formats);

  swapchain->format    = surfaceFormat.format;
  swapchain->gpuFormat = vk_formatToGPU(surfaceFormat.format);
  swapchain->extent    = vk__chooseExtent(&caps,
                                          width,
                                          height,
                                          swapchainObj->backingScaleFactor);
  if (swapchain->gpuFormat == GPU_FORMAT_UNDEFINED ||
      swapchain->extent.width == 0u || swapchain->extent.height == 0u) {
    return false;
  }

  imageCount = swapchain->requestedImageCount;
  if (imageCount < caps.minImageCount) {
    imageCount = caps.minImageCount;
  }
  if (caps.maxImageCount > 0u && imageCount > caps.maxImageCount) {
    imageCount = caps.maxImageCount;
  }

  info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface          = swapchain->surface->surface;
  info.minImageCount    = imageCount;
  info.imageFormat      = surfaceFormat.format;
  info.imageColorSpace  = surfaceFormat.colorSpace;
  info.imageExtent      = swapchain->extent;
  info.imageArrayLayers = 1u;
  info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform     = caps.currentTransform;
  info.compositeAlpha   = vk__compositeAlpha(caps.supportedCompositeAlpha);
  info.presentMode      = presentMode;
  info.clipped          = VK_TRUE;
  info.oldSwapchain     = oldSwapchain;
  if (vkCreateSwapchainKHR(swapchain->device,
                           &info,
                           NULL,
                           &swapchain->swapchain) != VK_SUCCESS ||
      vkGetSwapchainImagesKHR(swapchain->device,
                              swapchain->swapchain,
                              &swapchain->imageCount,
                              NULL) != VK_SUCCESS ||
      swapchain->imageCount == 0u ||
      !vk__createImageState(swapchain)) {
    vk__destroyResources(swapchain);
    return false;
  }

  swapchain->frameIndex           = 0u;
  swapchain->acquiredImageIndex   = 0u;
  swapchain->inFlightCommandCount = 0u;
  swapchain->frameActive          = false;
  swapchain->frameScheduled       = false;
  swapchain->frameSubmitted       = false;
  return true;
}

GPU_HIDE
GPUSwapchain*
vk_createSwapchain(GPUApi          * __restrict api,
                   GPUDevice       * __restrict device,
                   GPUQueue        * __restrict cmdQue,
                   const GPUSwapchainCreateInfo * __restrict info) {
  GPUDeviceVk         *deviceVk;
  GPUAdapterVk        *adapterVk;
  GPUSwapchain        *swapchainObj;
  GPUSwapchainVk      *swapchain;
  GPUSurfaceVk        *surface;
  VkBool32             presentSupported;

  GPU__UNUSED(api);

  if (!device || !device->_priv || !device->adapter ||
      !cmdQue || !cmdQue->_priv || !info || !info->surface ||
      !info->surface->_priv || info->width == 0u || info->height == 0u) {
    return NULL;
  }

  deviceVk         = device->_priv;
  adapterVk        = device->adapter->_priv;
  surface          = info->surface->_priv;
  presentSupported = VK_FALSE;
  if (vkGetPhysicalDeviceSurfaceSupportKHR(adapterVk->physicalDevice,
                                           ((GPUQueueVk *)cmdQue->_priv)->familyIndex,
                                           surface->surface,
                                           &presentSupported) != VK_SUCCESS ||
      !presentSupported) {
    return NULL;
  }

  swapchainObj = calloc(1, sizeof(*swapchainObj));
  swapchain    = calloc(1, sizeof(*swapchain));
  if (!swapchainObj || !swapchain) {
    free(swapchain);
    free(swapchainObj);
    return NULL;
  }

  swapchainObj->_priv             = swapchain;
  swapchainObj->backingScaleFactor = info->surface->scale;
  swapchain->gpuDevice            = device;
  swapchain->gpuSwapchain         = swapchainObj;
  swapchain->queue                = cmdQue->_priv;
  swapchain->surface              = surface;
  swapchain->device               = deviceVk->device;
  swapchain->physicalDevice       = adapterVk->physicalDevice;
  swapchain->requestedImageCount = info->imageCount ? info->imageCount : 3u;
  swapchain->gpuFormat            = info->format;
  swapchain->presentMode          = info->presentMode;
  if (!vk_formatFromGPU(info->format, &swapchain->format) ||
      !vk__createResources(swapchainObj,
                           info->width,
                           info->height,
                           VK_NULL_HANDLE)) {
    free(swapchain);
    free(swapchainObj);
    return NULL;
  }

  return swapchainObj;
}

GPU_HIDE
GPUResult
vk_resizeSwapchain(GPUSwapchain *swapchainObj, GPUExtent2D size) {
  GPUSwapchainVk *swapchain;
  GPUSwapchainVk  replacement;
  GPUSwapchain    replacementObj;

  if (!swapchainObj || !swapchainObj->_priv ||
      size.width == 0u || size.height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  swapchain = swapchainObj->_priv;
  if (swapchain->frameActive) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (vkDeviceWaitIdle(swapchain->device) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  vk_waitSwapchainIdle(swapchain);

  memset(&replacement, 0, sizeof(replacement));
  replacement.gpuDevice            = swapchain->gpuDevice;
  replacement.gpuSwapchain         = swapchain->gpuSwapchain;
  replacement.queue                = swapchain->queue;
  replacement.surface              = swapchain->surface;
  replacement.device               = swapchain->device;
  replacement.physicalDevice       = swapchain->physicalDevice;
  replacement.format               = swapchain->format;
  replacement.requestedImageCount  = swapchain->requestedImageCount;
  replacement.gpuFormat            = swapchain->gpuFormat;
  replacement.presentMode          = swapchain->presentMode;
  replacementObj                   = *swapchainObj;
  replacementObj._priv             = &replacement;
  if (!vk__createResources(&replacementObj,
                           size.width,
                           size.height,
                           swapchain->swapchain)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk__destroyResources(swapchain);
  *swapchain = replacement;
  for (uint32_t i = 0u; i < swapchain->imageCount; i++) {
    swapchain->nativeViews[i].swapchain = swapchain;
  }
  return GPU_OK;
}

GPU_HIDE
void
vk_destroySwapchain(GPUSwapchain *swapchainObj) {
  GPUSwapchainVk *swapchain;

  if (!swapchainObj) {
    return;
  }

  swapchain = swapchainObj->_priv;
  if (swapchain) {
    if (swapchain->device) {
      (void)vkDeviceWaitIdle(swapchain->device);
    }
    vk_waitSwapchainIdle(swapchain);
    vk__destroyResources(swapchain);
    free(swapchain);
  }
  free(swapchainObj);
}

GPU_HIDE
void
vk_initSwapchain(GPUApiSwapchain *apiSwapchain) {
  apiSwapchain->createSwapchain  = vk_createSwapchain;
  apiSwapchain->resizeSwapchain  = vk_resizeSwapchain;
  apiSwapchain->destroySwapchain = vk_destroySwapchain;
}
