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
#include "../../../api/frame_feedback_internal.h"
#if defined(__APPLE__)
#  include <pthread/qos.h>
#elif defined(__ANDROID__)
#  include <sys/resource.h>
#  include <unistd.h>
#endif

static const VkCompositeAlphaFlagBitsKHR vk_compositeAlphaModes[] = {
  VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
};

static const VkAttachmentLoadOp vk_loadOps[] = {
  VK_ATTACHMENT_LOAD_OP_LOAD,
  VK_ATTACHMENT_LOAD_OP_CLEAR,
  VK_ATTACHMENT_LOAD_OP_DONT_CARE
};

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
  return vk_loadOps[index];
}

static VkAttachmentStoreOp
vk__storeOp(uint32_t index) {
  return index == 0u ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkResult
vk__createRenderPass(GPUSwapchainVk *swapchain,
                     uint32_t        loadIndex,
                     uint32_t        storeIndex,
                     VkRenderPass   *outRenderPass) {
  VkAttachmentDescription attachment = {0};
  VkAttachmentReference   colorRef   = {0};
  VkSubpassDescription    subpass    = {0};
  VkSubpassDependency     dependency = {0};
  VkRenderPassCreateInfo  info       = {0};

  attachment.format         = swapchain->format;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = vk__loadOp(loadIndex);
  attachment.storeOp        = vk__storeOp(storeIndex);
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = loadIndex == GPU_LOAD_OP_LOAD
                               ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                               : VK_IMAGE_LAYOUT_UNDEFINED;
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
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
  GPUDeviceVk *device;
  uint32_t     i;
  uint32_t     load;
  uint32_t     store;

  if (!swapchain || !swapchain->device) {
    return;
  }

  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;

  for (i = 0u; i < swapchain->imageCount; i++) {
    if (device && swapchain->imageViews && swapchain->imageViews[i]) {
      vk_invalidateClassicFramebuffers(device, swapchain->imageViews[i]);
    }

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

      if (swapchain->frameSync[i].fence) {
        vkDestroyFence(swapchain->device, swapchain->frameSync[i].fence, NULL);
      }
    }

    if (swapchain->renderFinished && swapchain->renderFinished[i]) {
      vkDestroySemaphore(swapchain->device,
                         swapchain->renderFinished[i],
                         NULL);
    }
  }

  for (load = 0u; load < 3u; load++) {
    for (store = 0u; store < 2u; store++) {
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
  free(swapchain->renderFinished);
  free(swapchain->nativeViews);
  free(swapchain->textureViews);
  free(swapchain->textures);
  free(swapchain->framebuffers);
  free(swapchain->imageViews);
  free(swapchain->images);

  swapchain->frameSync      = NULL;
  swapchain->renderFinished = NULL;
  swapchain->nativeViews    = NULL;
  swapchain->textureViews   = NULL;
  swapchain->textures       = NULL;
  swapchain->framebuffers   = NULL;
  swapchain->imageViews     = NULL;
  swapchain->images         = NULL;
  swapchain->swapchain      = VK_NULL_HANDLE;
  swapchain->imageCount     = 0u;
  memset(swapchain->renderPasses, 0, sizeof(swapchain->renderPasses));
}

static bool
vk__chooseSurfaceFormat(const VkSurfaceFormatKHR *formats,
                        uint32_t                  count,
                        VkFormat                  requested,
                        VkSurfaceFormatKHR       *outFormat) {
  VkSurfaceFormatKHR fallback;
  uint32_t           i;

  if (!formats || count == 0u || !outFormat) {
    return false;
  }

  fallback.format     = requested;
  fallback.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

  if (count == 1u && formats[0].format == VK_FORMAT_UNDEFINED) {
    *outFormat = fallback;
    return true;
  }

  for (i = 0u; i < count; i++) {
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
  uint32_t         i;

  wanted = vk__presentMode(requested);

  for (i = 0u; i < count; i++) {
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

  extent.width  = (uint32_t)((double)width * scale);
  extent.height = (uint32_t)((double)height * scale);

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
  uint32_t i;

  for (i = 0u; i < GPU_ARRAY_LEN(vk_compositeAlphaModes); i++) {
    if ((supported & vk_compositeAlphaModes[i]) != 0u) {
      return vk_compositeAlphaModes[i];
    }
  }

  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static VkSurfaceTransformFlagBitsKHR
vk__preTransform(const VkSurfaceCapabilitiesKHR *caps) {
  if ((caps->supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0u) {
    return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }

  return caps->currentTransform;
}

static bool
vk__allocateArrays(GPUSwapchainVk *swapchain, uint32_t count) {
  swapchain->images         = calloc(count, sizeof(*swapchain->images));
  swapchain->imageViews     = calloc(count, sizeof(*swapchain->imageViews));
  swapchain->framebuffers   = calloc(count, sizeof(*swapchain->framebuffers));
  swapchain->textures       = calloc(count, sizeof(*swapchain->textures));
  swapchain->textureViews   = calloc(count, sizeof(*swapchain->textureViews));
  swapchain->nativeViews    = calloc(count, sizeof(*swapchain->nativeViews));
  swapchain->frameSync      = calloc(count, sizeof(*swapchain->frameSync));
  swapchain->renderFinished = calloc(count,
                                     sizeof(*swapchain->renderFinished));

  return swapchain->images && swapchain->imageViews && swapchain->framebuffers && swapchain->textures
         && swapchain->textureViews && swapchain->nativeViews && swapchain->frameSync && swapchain->renderFinished;
}

static bool
vk__createImageState(GPUSwapchainVk *swapchain) {
  VkSemaphoreCreateInfo   semaphoreInfo   = {0};
  VkFenceCreateInfo       fenceInfo       = {0};
  VkImageViewCreateInfo   viewInfo        = {0};
  VkFramebufferCreateInfo framebufferInfo = {0};
  GPUDeviceVk            *device;
  GPUTexture             *texture;
  GPUTextureView         *view;
  GPUTextureViewVk       *nativeView;
  uint32_t                count;
  uint32_t                load;
  uint32_t                store;
  uint32_t                i;

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
                              swapchain->images) != VK_SUCCESS
      || count != swapchain->imageCount) {
    return false;
  }

  if (!device->dynamicRendering) {
    for (load = 0u; load < 3u; load++) {
      for (store = 0u; store < 2u; store++) {
        if (vk__createRenderPass(swapchain,
                                 load,
                                 store,
                                 &swapchain->renderPasses[load][store]) != VK_SUCCESS) {
          return false;
        }
      }
    }
  }

  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  fenceInfo.sType     = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags     = VK_FENCE_CREATE_SIGNALED_BIT;

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
  framebufferInfo.renderPass      = swapchain->renderPasses[GPU_LOAD_OP_CLEAR][GPU_STORE_OP_STORE];
  framebufferInfo.attachmentCount = 1u;
  framebufferInfo.width           = swapchain->extent.width;
  framebufferInfo.height          = swapchain->extent.height;
  framebufferInfo.layers          = 1u;

  for (i = 0u; i < swapchain->imageCount; i++) {
    swapchain->frameSync[i].swapchain = swapchain;
    viewInfo.image                    = swapchain->images[i];

    if (vkCreateImageView(swapchain->device,
                          &viewInfo,
                          NULL,
                          &swapchain->imageViews[i]) != VK_SUCCESS) {
      return false;
    }

    framebufferInfo.pAttachments = &swapchain->imageViews[i];

    if ((!device->dynamicRendering
         && vkCreateFramebuffer(swapchain->device,
                                &framebufferInfo,
                                NULL,
                                &swapchain->framebuffers[i]) != VK_SUCCESS)
        || vkCreateSemaphore(swapchain->device,
                             &semaphoreInfo,
                             NULL,
                             &swapchain->frameSync[i].imageAvailable) != VK_SUCCESS
        || vkCreateSemaphore(swapchain->device,
                             &semaphoreInfo,
                             NULL,
                             &swapchain->renderFinished[i]) != VK_SUCCESS
        || vkCreateFence(swapchain->device,
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

    view                  = &swapchain->textureViews[i];
    view->_priv           = nativeView;
    view->_texture        = texture;
    view->format          = swapchain->gpuFormat;
    view->viewType        = GPU_TEXTURE_VIEW_2D;
    view->mipLevelCount   = 1u;
    view->arrayLayerCount = 1u;
    view->_ownsNative     = false;
  }

  return true;
}

static bool
vk__waitPresent(GPUSwapchainVk *swapchain) {
#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  GPUDeviceVk *device;
  VkResult     result;
#endif

#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  device = swapchain && swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;

  if (device && device->presentWait && device->waitForPresent && swapchain->lastPresentId > 0u) {
    result = device->waitForPresent(swapchain->device,
                                    swapchain->swapchain,
                                    swapchain->lastPresentId,
                                    UINT64_MAX);
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR;
  }

  if (device && device->presentWait) {
    return true;
  }
#endif

  return false;
}

/* presents on a thread of the swapchain's own. vkQueuePresentKHR can block for
 * long: Android's swapchain connects the window as an EGL producer, so
 * queueBuffer waits for the previous frame's GPU work and for SurfaceFlinger's
 * binder reply (milliseconds a frame on phones, on the thread that finished
 * the frame). The submit queues the present here and returns. The
 * present goes to the graphics family's next queue where the device has one
 * (a present blocked in queueBuffer then holds no queue a submit needs), else
 * to the frame's queue under its submit lock. Presents keep their order. Calls
 * on one swapchain must not overlap: an acquire, a present-wait, a resize or
 * a destroy claims it (vk_swapchainClaim) once the queued presents are issued,
 * and the thread presents or reads timings only while it isn't claimed. On by
 * default on Android; GPU_VULKAN_PRESENT_THREAD=0/1 chooses anywhere */
enum { GPU_VK_PRESENT_RING = 8u };

typedef struct GPUPresenterVk {
  GPUSwapchainVk    *swapchain;
#if defined(_WIN32) || defined(WIN32)
  HANDLE             thread;
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE changed;
#else
  pthread_t          thread;
  pthread_mutex_t    lock;
  pthread_cond_t     changed;
#endif
  VkSemaphore        waits[GPU_VK_PRESENT_RING];
  GPUFrameFeedback  *feedbacks[GPU_VK_PRESENT_RING];
  uint32_t           images[GPU_VK_PRESENT_RING];
  uint32_t           head;
  uint32_t           count;
  bool               busy;     /* the swapchain in use: a present, a timing read, or claimed */
  bool               stopping;
} GPUPresenterVk;

static void
vk__presenterLock(GPUPresenterVk *presenter) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&presenter->lock);
#else
  pthread_mutex_lock(&presenter->lock);
#endif
}

static void
vk__presenterUnlock(GPUPresenterVk *presenter) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&presenter->lock);
#else
  pthread_mutex_unlock(&presenter->lock);
#endif
}

/* false when `ms` passed first (zero: no limit) */
static bool
vk__presenterWait(GPUPresenterVk *presenter, uint32_t ms) {
#if defined(_WIN32) || defined(WIN32)
  return SleepConditionVariableCS(&presenter->changed, &presenter->lock, ms ? ms : INFINITE) != 0;
#else
  struct timespec until;

  if (!ms) {
    return pthread_cond_wait(&presenter->changed, &presenter->lock) == 0;
  }

  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_nsec += (long)ms * 1000000L;
  if (until.tv_nsec >= 1000000000L) {
    until.tv_sec  += until.tv_nsec / 1000000000L;
    until.tv_nsec %= 1000000000L;
  }

  return pthread_cond_timedwait(&presenter->changed, &presenter->lock, &until) == 0;
#endif
}

static void
vk__presenterSignal(GPUPresenterVk *presenter) {
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&presenter->changed);
#else
  pthread_cond_broadcast(&presenter->changed);
#endif
}

/* frame feedback on Vulkan: a frame watched (GPUCreateFrameFeedback) presents
 * with a VK_GOOGLE_display_timing id, and the display's time for it comes
 * from vkGetPastPresentationTimingGOOGLE, read after each present and, on the
 * present thread, every few milliseconds while some are still pending. The
 * times are the presentation engine's: CLOCK_MONOTONIC on Android and Linux,
 * the host clock of the frame timing on Apple. A present older than one
 * reported and never reported itself is presentation unavailable */

static void
vk__timingPop(GPUSwapchainVk *swapchain, double time) {
  GPUFrameFeedback *feedback;

  feedback = swapchain->timingFeedbacks[swapchain->timingHead];
  swapchain->timingFeedbacks[swapchain->timingHead] = NULL;
  swapchain->timingHead = (swapchain->timingHead + 1u) % GPU_VK_TIMING_RING;
  swapchain->timingCount--;
  frameFeedbackPresent(feedback, time);
  frameFeedbackRelease(feedback);
}

static uint32_t
vk__timingPush(GPUSwapchainVk *swapchain, GPUFrameFeedback *feedback) {
  uint32_t slot;

  if (swapchain->timingCount == GPU_VK_TIMING_RING) {
    vk__timingPop(swapchain, 0.0);
  }

  if (++swapchain->timingNextId == 0u) {
    swapchain->timingNextId = 1u;
  }

  slot = (swapchain->timingHead + swapchain->timingCount) % GPU_VK_TIMING_RING;
  frameFeedbackRetain(feedback);
  swapchain->timingFeedbacks[slot] = feedback;
  swapchain->timingIds[slot]       = swapchain->timingNextId;
  swapchain->timingCount++;
  return swapchain->timingNextId;
}

/* the newest one, its present refused */
static void
vk__timingDrop(GPUSwapchainVk *swapchain) {
  uint32_t slot;

  slot = (swapchain->timingHead + swapchain->timingCount - 1u) % GPU_VK_TIMING_RING;
  swapchain->timingCount--;
  frameFeedbackPresent(swapchain->timingFeedbacks[slot], 0.0);
  frameFeedbackRelease(swapchain->timingFeedbacks[slot]);
  swapchain->timingFeedbacks[slot] = NULL;
}

static void
vk__timingsRead(GPUSwapchainVk *swapchain) {
  VkPastPresentationTimingGOOGLE past[GPU_VK_TIMING_RING];
  GPUDeviceVk                   *device;
  VkResult                       result;
  uint32_t                       count, i;

  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (!swapchain->timingCount || !device || !device->getPastTiming || !swapchain->swapchain) {
    return;
  }

  do {
    count  = GPU_VK_TIMING_RING;
    result = device->getPastTiming(swapchain->device, swapchain->swapchain, &count, past);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
      return;
    }

    for (i = 0u; i < count && swapchain->timingCount; i++) {
      if (!past[i].presentID) {
        continue; /* a present no one watched */
      }

      while (swapchain->timingCount && swapchain->timingIds[swapchain->timingHead] != past[i].presentID
             && (uint32_t)(past[i].presentID - swapchain->timingIds[swapchain->timingHead]) < 0x80000000u) {
        vk__timingPop(swapchain, 0.0); /* older, never reported */
      }

      if (swapchain->timingCount && swapchain->timingIds[swapchain->timingHead] == past[i].presentID) {
        vk__timingPop(swapchain, (double)past[i].actualPresentTime * 1e-9);
      }
    }
  } while (result == VK_INCOMPLETE && count > 0u);
}

/* what is still pending gets its time where it came, else none */
static void
vk__timingsFinish(GPUSwapchainVk *swapchain) {
  vk__timingsRead(swapchain);
  while (swapchain->timingCount) {
    vk__timingPop(swapchain, 0.0);
  }
}

/* now, on the present queue: the device's own, or the frame's under its lock */
static VkResult
vk__presentNow(GPUSwapchainVk   *swapchain,
               GPUQueueVk       *queue,
               VkSemaphore       wait,
               uint32_t          imageIndex,
               GPUFrameFeedback *feedback) {
  GPUDeviceVk *device;
  VkResult     result;

  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (device && device->presentQueue && swapchain->presenter) {
#if defined(_WIN32) || defined(WIN32)
    EnterCriticalSection(&device->presentLock);
    result = vk_presentSwapchain(swapchain, device->presentQueue, wait, imageIndex, feedback);
    LeaveCriticalSection(&device->presentLock);
#else
    pthread_mutex_lock(&device->presentLock);
    result = vk_presentSwapchain(swapchain, device->presentQueue, wait, imageIndex, feedback);
    pthread_mutex_unlock(&device->presentLock);
#endif
  } else {
    vk_queueSubmitLock(queue);
    result = vk_presentSwapchain(swapchain, queue->queRaw, wait, imageIndex, feedback);
    vk_queueSubmitUnlock(queue);
  }

  vk__timingsRead(swapchain);
  return result;
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
#else
static void*
#endif
vk__presenterRun(void *user) {
  GPUPresenterVk   *presenter;
  GPUFrameFeedback *feedback;
  VkSemaphore       wait;
  uint32_t          imageIndex;
  bool              present, read;

  presenter = user;
  wait       = VK_NULL_HANDLE;
  feedback   = NULL;
  imageIndex = 0u;
  /* a present is part of a frame: it runs at the display's urgency */
#if defined(__APPLE__)
  pthread_setname_np("gpu-present");
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(__ANDROID__)
  pthread_setname_np(pthread_self(), "gpu-present");
  (void)setpriority(PRIO_PROCESS, (id_t)gettid(), -4); /* Android's display priority; refused stays as is */
#endif
  vk__presenterLock(presenter);
  for (;;) {
    /* idle with timings pending: a read every few milliseconds */
    read = false;
    while ((!presenter->count || presenter->busy) && !presenter->stopping) {
      if (!vk__presenterWait(presenter, presenter->swapchain->timingCount ? 8u : 0u)
          && !presenter->busy && !presenter->count && presenter->swapchain->timingCount) {
        read = true;
        break;
      }
    }

    if (presenter->stopping && !presenter->count) {
      break; /* every present issued */
    }

    present = presenter->count && !presenter->busy;
    if (present) {
      wait       = presenter->waits[presenter->head];
      feedback   = presenter->feedbacks[presenter->head];
      imageIndex = presenter->images[presenter->head];
      presenter->feedbacks[presenter->head] = NULL;
      presenter->head = (presenter->head + 1u) % GPU_VK_PRESENT_RING;
      presenter->count--;
    } else if (!read) {
      continue;
    }

    presenter->busy = true;
    vk__presenterUnlock(presenter);

    if (present) {
      (void)vk__presentNow(presenter->swapchain, presenter->swapchain->queue, wait, imageIndex, feedback);
      if (feedback) {
        frameFeedbackRelease(feedback);
      }
    } else {
      vk__timingsRead(presenter->swapchain);
    }

    vk__presenterLock(presenter);
    presenter->busy = false;
    vk__presenterSignal(presenter);
  }

  vk__presenterUnlock(presenter);
#if defined(_WIN32) || defined(WIN32)
  return 0;
#else
  return NULL;
#endif
}

static void
vk__createPresenter(GPUSwapchainVk *swapchain) {
  GPUPresenterVk *presenter;
  GPUDeviceVk    *device;

  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (!device || !device->presentThread || !(presenter = calloc(1u, sizeof(*presenter)))) {
    return;
  }

  presenter->swapchain = swapchain;
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&presenter->lock);
  InitializeConditionVariable(&presenter->changed);
  if (!(presenter->thread = CreateThread(NULL, 0u, vk__presenterRun, presenter, 0u, NULL))) {
    DeleteCriticalSection(&presenter->lock);
    free(presenter);
    return;
  }
#else
  if (pthread_mutex_init(&presenter->lock, NULL) != 0) {
    free(presenter);
    return;
  }

  if (pthread_cond_init(&presenter->changed, NULL) != 0) {
    pthread_mutex_destroy(&presenter->lock);
    free(presenter);
    return;
  }

  if (pthread_create(&presenter->thread, NULL, vk__presenterRun, presenter) != 0) {
    pthread_cond_destroy(&presenter->changed);
    pthread_mutex_destroy(&presenter->lock);
    free(presenter);
    return;
  }
#endif

  swapchain->presenter = presenter;
}

static void
vk__destroyPresenter(GPUSwapchainVk *swapchain) {
  GPUPresenterVk *presenter;

  if (!(presenter = swapchain->presenter)) {
    return;
  }

  vk__presenterLock(presenter);
  presenter->stopping = true;
  vk__presenterSignal(presenter);
  vk__presenterUnlock(presenter);
#if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(presenter->thread, INFINITE);
  CloseHandle(presenter->thread);
  DeleteCriticalSection(&presenter->lock);
#else
  pthread_join(presenter->thread, NULL);
  pthread_cond_destroy(&presenter->changed);
  pthread_mutex_destroy(&presenter->lock);
#endif
  free(presenter);
  swapchain->presenter = NULL;
}

GPU_HIDE
VkResult
vk_presentFrame(GPUSwapchainVk   *swapchain,
                GPUQueueVk       *queue,
                VkSemaphore       wait,
                uint32_t          imageIndex,
                GPUFrameFeedback *feedback) {
  GPUPresenterVk *presenter;
  uint32_t        slot;

  if (!(presenter = swapchain->presenter)) {
    return vk__presentNow(swapchain, queue, wait, imageIndex, feedback);
  }

  if (feedback) {
    frameFeedbackRetain(feedback); /* the command buffer's goes at its completion */
  }

  vk__presenterLock(presenter);
  while (presenter->count == GPU_VK_PRESENT_RING) {
    vk__presenterWait(presenter, 0u);
  }

  slot = (presenter->head + presenter->count) % GPU_VK_PRESENT_RING;
  presenter->waits[slot]     = wait;
  presenter->feedbacks[slot] = feedback;
  presenter->images[slot]    = imageIndex;
  presenter->count++;
  vk__presenterSignal(presenter);
  vk__presenterUnlock(presenter);

  /* its result reaches the swapchain's status */
  return VK_SUCCESS;
}

GPU_HIDE
void
vk_swapchainClaim(GPUSwapchainVk *swapchain) {
  GPUPresenterVk *presenter;

  if (!swapchain || !(presenter = swapchain->presenter)) {
    return;
  }

  vk__presenterLock(presenter);
  while (presenter->count || presenter->busy) {
    vk__presenterWait(presenter, 0u);
  }

  presenter->busy = true;
  vk__presenterUnlock(presenter);
}

GPU_HIDE
void
vk_swapchainUnclaim(GPUSwapchainVk *swapchain) {
  GPUPresenterVk *presenter;

  if (!swapchain || !(presenter = swapchain->presenter)) {
    return;
  }

  vk__presenterLock(presenter);
  presenter->busy = false;
  vk__presenterSignal(presenter);
  vk__presenterUnlock(presenter);
}

GPU_HIDE
bool
vk_presenterIdle(GPUSwapchainVk *swapchain) {
  GPUPresenterVk *presenter;
  bool            idle;

  if (!swapchain || !(presenter = swapchain->presenter)) {
    return true;
  }

  vk__presenterLock(presenter);
  idle = !presenter->count && !presenter->busy;
  vk__presenterUnlock(presenter);
  return idle;
}

static bool
vk__waitResourcesIdle(GPUSwapchainVk *swapchain) {
  if (!swapchain || !swapchain->device) {
    return false;
  }

  vk_waitSwapchainIdle(swapchain);

  /* from the background no device wait (vk_gpuRefused): MoltenVK's commits a
   * command buffer the system refuses. The swapchain's own work is waited */
  return vk__waitPresent(swapchain) || vk_gpuRefused() || vkDeviceWaitIdle(swapchain->device) == VK_SUCCESS;
}

static bool
vk__createResources(GPUSwapchain  *swapchainObj,
                    uint32_t       width,
                    uint32_t       height,
                    VkSwapchainKHR oldSwapchain) {
  VkSurfaceCapabilitiesKHR caps;
  VkSurfaceFormatKHR       surfaceFormat;
  VkSwapchainCreateInfoKHR info = {0};
  GPUSwapchainVk          *swapchain;
  VkSurfaceFormatKHR       localFormats[64];
  VkPresentModeKHR         localModes[8];
  VkSurfaceFormatKHR      *formats;
  VkPresentModeKHR        *modes;
  VkPresentModeKHR         presentMode;
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
  resizeMetalLayer(swapchain->surface->metalLayer,
                   width,
                   height,
                   swapchainObj->backingScaleFactor);
#endif

  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(swapchain->physicalDevice,
                                                     swapchain->surface->surface,
                                                     &caps);

  if (result != VK_SUCCESS
      || !vk_getSurfaceLists(swapchain->physicalDevice,
                             swapchain->surface->surface,
                             localFormats,
                             (uint32_t)(sizeof(localFormats) / sizeof(localFormats[0])),
                             localModes,
                             (uint32_t)(sizeof(localModes) / sizeof(localModes[0])),
                             &formats,
                             &formatCount,
                             &modes,
                             &modeCount)) {
    return false;
  }

  if (!vk__chooseSurfaceFormat(formats,
                               formatCount,
                               swapchain->format,
                               &surfaceFormat)) {
    vk_freeSurfaceLists(formats, localFormats, modes, localModes);
    return false;
  }

  presentMode = vk__choosePresentMode(modes,
                                      modeCount,
                                      swapchain->presentMode);
  vk_freeSurfaceLists(formats, localFormats, modes, localModes);

  swapchain->format    = surfaceFormat.format;
  swapchain->gpuFormat = vk_formatToGPU(surfaceFormat.format);
  swapchain->extent    = vk__chooseExtent(&caps,
                                          width,
                                          height,
                                          swapchainObj->backingScaleFactor);

  if (swapchain->gpuFormat == GPU_FORMAT_UNDEFINED || swapchain->extent.width == 0u || swapchain->extent.height == 0u) {
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
  info.preTransform     = vk__preTransform(&caps);
  info.compositeAlpha   = vk__compositeAlpha(caps.supportedCompositeAlpha);
  info.presentMode      = presentMode;
  info.clipped          = VK_TRUE;
  info.oldSwapchain     = oldSwapchain;

  if (vkCreateSwapchainKHR(swapchain->device,
                           &info,
                           NULL,
                           &swapchain->swapchain) != VK_SUCCESS
      || vkGetSwapchainImagesKHR(swapchain->device,
                                 swapchain->swapchain,
                                 &swapchain->imageCount,
                                 NULL) != VK_SUCCESS
      || swapchain->imageCount == 0u || !vk_reserveCommandBuffers(swapchain->queue->queue,
                                                                  swapchain->imageCount)
      || !vk__createImageState(swapchain)) {
    vk__destroyResources(swapchain);
    return false;
  }

  swapchain->frameIndex           = 0u;
  swapchain->acquiredImageIndex   = 0u;
  swapchain->inFlightCommandCount = 0u;
  swapchain->nextPresentId        = 1u;
  swapchain->lastPresentId        = 0u;
  swapchain->frameActive          = false;
  swapchain->frameScheduled       = false;
  swapchain->frameSubmitted       = false;

  return true;
}

GPU_HIDE
VkResult
vk_presentSwapchain(GPUSwapchainVk   *swapchain,
                    VkQueue           queue,
                    VkSemaphore       waitSemaphore,
                    uint32_t          imageIndex,
                    GPUFrameFeedback *feedback) {
  VkPresentInfoKHR         info      = {0};
  VkPresentTimesInfoGOOGLE timesInfo = {0};
  VkPresentTimeGOOGLE      time      = {0};
  GPUDeviceVk             *timing;
#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  VkPresentIdKHR           idInfo    = {0};
  GPUDeviceVk             *device;
  uint64_t                 presentId;
#endif
  VkResult                 result;

  if (!swapchain || !queue || !waitSemaphore || imageIndex >= swapchain->imageCount) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1u;
  info.pWaitSemaphores    = &waitSemaphore;
  info.swapchainCount     = 1u;
  info.pSwapchains        = &swapchain->swapchain;
  info.pImageIndices      = &imageIndex;
#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  device    = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  presentId = swapchain->nextPresentId;

  if (device && device->presentWait) {
    idInfo.sType          = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
    idInfo.swapchainCount = 1u;
    idInfo.pPresentIds    = &presentId;
    info.pNext            = &idInfo;
  }
#endif

  /* a watched frame's present carries an id its display time comes back by */
  timing = feedback && swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (timing && timing->getPastTiming) {
    time.presentID            = vk__timingPush(swapchain, feedback);
    timesInfo.sType           = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
    timesInfo.pNext           = info.pNext;
    timesInfo.swapchainCount  = 1u;
    timesInfo.pTimes          = &time;
    info.pNext                = &timesInfo;
  } else if (feedback) {
    frameFeedbackPresent(feedback, 0.0);
  }

  result = vkQueuePresentKHR(queue, &info);
  vk_setSwapchainStatus(swapchain, result);
  if (time.presentID && result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    vk__timingDrop(swapchain);
  }
#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  if (device && device->presentWait) {
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
      swapchain->lastPresentId = presentId;
    }

    swapchain->nextPresentId++;
  }
#endif

  return result;
}

GPU_HIDE
GPUSwapchain*
vk_createSwapchain(GPUApi                       *__restrict api,
                   GPUDevice                    *__restrict device,
                   GPUQueue                     *__restrict cmdQue,
                   const GPUSwapchainCreateInfo *__restrict info) {
  GPUDeviceVk    *deviceVk;
  GPUAdapterVk   *adapterVk;
  GPUSwapchain   *swapchainObj;
  GPUSwapchainVk *swapchain;
  GPUSurfaceVk   *surface;
  VkBool32        presentSupported;

  GPU__UNUSED(api);

  if (!device || !device->_priv || !device->adapter || !cmdQue || !cmdQue->_priv || !info || !info->surface
      || !info->surface->_priv || info->width == 0u || info->height == 0u) {
    return NULL;
  }

  deviceVk         = device->_priv;
  adapterVk        = device->adapter->_priv;
  surface          = info->surface->_priv;
  presentSupported = VK_FALSE;

  if (vkGetPhysicalDeviceSurfaceSupportKHR(adapterVk->physicalDevice,
                                           ((GPUQueueVk *)cmdQue->_priv)->familyIndex,
                                           surface->surface,
                                           &presentSupported) != VK_SUCCESS
      || !presentSupported) {
    return NULL;
  }

  swapchainObj = calloc(1, sizeof(*swapchainObj));
  swapchain    = calloc(1, sizeof(*swapchain));

  if (!swapchainObj || !swapchain) {
    free(swapchain);
    free(swapchainObj);
    return NULL;
  }

  swapchainObj->_priv              = swapchain;
  swapchainObj->backingScaleFactor = info->surface->scale;
  swapchain->gpuDevice             = device;
  swapchain->gpuSwapchain          = swapchainObj;
  swapchain->queue                 = cmdQue->_priv;
  swapchain->surface               = surface;
  swapchain->device                = deviceVk->device;
  swapchain->physicalDevice        = adapterVk->physicalDevice;
  swapchain->requestedImageCount   = info->imageCount ? info->imageCount : 3u;
  swapchain->gpuFormat             = info->format;
  swapchain->presentMode           = info->presentMode;

  if (!vk_formatFromGPU(info->format, &swapchain->format)
      || !vk__createResources(swapchainObj,
                              info->width,
                              info->height,
                              VK_NULL_HANDLE)) {
    free(swapchain);
    free(swapchainObj);
    return NULL;
  }

  vk__createPresenter(swapchain);
  return swapchainObj;
}

GPU_HIDE
GPUResult
vk_resizeSwapchain(GPUSwapchain *swapchainObj, GPUExtent2D size) {
  GPUSwapchainVk  replacement;
  GPUSwapchain    replacementObj;
  GPUSwapchainVk *swapchain;
  uint32_t        i;

  if (!swapchainObj || !swapchainObj->_priv || size.width == 0u || size.height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  swapchain = swapchainObj->_priv;

  if (swapchain->frameActive) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  vk_swapchainClaim(swapchain);
  if (!vk__waitResourcesIdle(swapchain)) {
    vk_swapchainUnclaim(swapchain);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk__timingsFinish(swapchain); /* the old VkSwapchainKHR answers no more */

  memset(&replacement, 0, sizeof(replacement));
  replacement.gpuDevice           = swapchain->gpuDevice;
  replacement.gpuSwapchain        = swapchain->gpuSwapchain;
  replacement.queue               = swapchain->queue;
  replacement.surface             = swapchain->surface;
  replacement.device              = swapchain->device;
  replacement.physicalDevice      = swapchain->physicalDevice;
  replacement.format              = swapchain->format;
  replacement.requestedImageCount = swapchain->requestedImageCount;
  replacement.gpuFormat           = swapchain->gpuFormat;
  replacement.presentMode         = swapchain->presentMode;
  replacement.presenter           = swapchain->presenter; /* claimed above */
  replacementObj                  = *swapchainObj;
  replacementObj._priv            = &replacement;

  if (!vk__createResources(&replacementObj,
                           size.width,
                           size.height,
                           swapchain->swapchain)) {
    vk_swapchainUnclaim(swapchain);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk__destroyResources(swapchain);

  *swapchain = replacement;
  for (i = 0u; i < swapchain->imageCount; i++) {
    swapchain->frameSync[i].swapchain   = swapchain;
    swapchain->nativeViews[i].swapchain = swapchain;
  }

  vk_swapchainUnclaim(swapchain);
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
    vk_settleAcquiredFrame(swapchain);
    vk_swapchainClaim(swapchain);
    (void)vk__waitResourcesIdle(swapchain);
    vk__destroyPresenter(swapchain);
    vk__timingsFinish(swapchain);
    vk__destroyResources(swapchain);
    free(swapchain);
  }

  free(swapchainObj);
}

GPU_HIDE
void
vk_initSwapchain(GPUSwapchainApi *apiSwapchain) {
  apiSwapchain->createSwapchain  = vk_createSwapchain;
  apiSwapchain->resizeSwapchain  = vk_resizeSwapchain;
  apiSwapchain->destroySwapchain = vk_destroySwapchain;
}
