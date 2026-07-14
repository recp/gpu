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

GPU_HIDE
bool
vk_restoreFrameFence(GPUSwapchainVk *swapchain, GPUFrameSyncVk *sync) {
  VkFenceCreateInfo info = {0};
  VkFence           replacement;

  if (!swapchain || !sync) {
    return false;
  }

  replacement = VK_NULL_HANDLE;
  info.sType   = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  info.flags   = VK_FENCE_CREATE_SIGNALED_BIT;
  if (vkCreateFence(swapchain->device,
                    &info,
                    NULL,
                    &replacement) != VK_SUCCESS) {
    if (sync->fence) {
      vkDestroyFence(swapchain->device, sync->fence, NULL);
      sync->fence = VK_NULL_HANDLE;
    }
    return false;
  }

  if (sync->fence) {
    vkDestroyFence(swapchain->device, sync->fence, NULL);
  }
  sync->fence = replacement;
  return true;
}

static bool
vk__presentAcquiredFrame(GPUSwapchainVk *swapchain) {
  GPUFrameSyncVk      *sync;
  VkPipelineStageFlags waitStage;
  VkSubmitInfo         submitInfo = {0};
  VkPresentInfoKHR     presentInfo = {0};
  VkResult             result;

  sync      = &swapchain->frameSync[swapchain->frameIndex];
  waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (!sync->fence ||
      vkResetFences(swapchain->device, 1u, &sync->fence) != VK_SUCCESS) {
    return false;
  }

  submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount   = 1u;
  submitInfo.pWaitSemaphores      = &sync->imageAvailable;
  submitInfo.pWaitDstStageMask    = &waitStage;
  submitInfo.signalSemaphoreCount = 1u;
  submitInfo.pSignalSemaphores    = &sync->renderFinished;
  if (vkQueueSubmit(swapchain->queue->queRaw,
                    1u,
                    &submitInfo,
                    sync->fence) != VK_SUCCESS) {
    (void)vk_restoreFrameFence(swapchain, sync);
    return false;
  }

  presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1u;
  presentInfo.pWaitSemaphores    = &sync->renderFinished;
  presentInfo.swapchainCount     = 1u;
  presentInfo.pSwapchains        = &swapchain->swapchain;
  presentInfo.pImageIndices      = &swapchain->acquiredImageIndex;
  result = vkQueuePresentKHR(swapchain->queue->queRaw, &presentInfo);
  return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

GPU_HIDE
GPUFrame*
vk_beginFrame(GPUApi *api, GPUSwapchain *swapchainObj) {
  GPUSwapchainVk *swapchain;
  GPUFrameSyncVk *sync;
  GPUFrame       *frame;
  VkResult        result;
  uint32_t        imageIndex;

  GPU__UNUSED(api);

  swapchain = swapchainObj ? swapchainObj->_priv : NULL;
  if (!swapchain || swapchain->frameActive || swapchain->imageCount == 0u) {
    return NULL;
  }

  sync = &swapchain->frameSync[swapchain->frameIndex];
  if (!sync->fence ||
      vkWaitForFences(swapchain->device,
                      1u,
                      &sync->fence,
                      VK_TRUE,
                      UINT64_MAX) != VK_SUCCESS) {
    return NULL;
  }

  imageIndex = 0u;
  result = vkAcquireNextImageKHR(swapchain->device,
                                 swapchain->swapchain,
                                 UINT64_MAX,
                                 sync->imageAvailable,
                                 VK_NULL_HANDLE,
                                 &imageIndex);
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return NULL;
  }

  frame = &swapchain->frame;
  memset(frame, 0, sizeof(*frame));
  frame->_priv      = swapchain;
  frame->target     = &swapchain->textures[imageIndex];
  frame->targetView = &swapchain->textureViews[imageIndex];
  frame->drawable   = swapchain;

  swapchain->acquiredImageIndex = imageIndex;
  swapchain->frameActive        = true;
  swapchain->frameScheduled     = false;
  swapchain->frameSubmitted     = false;
  return frame;
}

GPU_HIDE
void
vk_endFrame(GPUApi *api, GPUFrame *frame) {
  GPUSwapchainVk *swapchain;

  GPU__UNUSED(api);

  swapchain = frame ? frame->_priv : NULL;
  if (!swapchain || !swapchain->frameActive) {
    return;
  }

  if (!swapchain->frameSubmitted) {
    (void)vk__presentAcquiredFrame(swapchain);
  }

  swapchain->frameIndex = (swapchain->frameIndex + 1u) % swapchain->imageCount;
  swapchain->frameActive    = false;
  swapchain->frameScheduled = false;
  swapchain->frameSubmitted = false;
  memset(frame, 0, sizeof(*frame));
}

GPU_HIDE
bool
vk_schedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUCommandBufferVk *command;
  GPUSwapchainVk     *swapchain;

  command   = cmdb ? cmdb->_priv : NULL;
  swapchain = frame ? frame->_priv : NULL;
  if (!command || !swapchain || !swapchain->frameActive ||
      swapchain->frameScheduled || command->presentSwapchain ||
      command->owner != swapchain->queue) {
    return false;
  }

  command->presentSwapchain = swapchain;
  command->presentImageIndex = swapchain->acquiredImageIndex;
  command->presentFrameIndex = swapchain->frameIndex;
  swapchain->frameScheduled  = true;
  return true;
}

GPU_HIDE
void
vk_initFrame(GPUApiFrame *api) {
  api->beginFrame = vk_beginFrame;
  api->endFrame   = vk_endFrame;
}

GPU_HIDE
void
vk_initCmdbuf(GPUApiCommandBuffer *api) {
  api->presentDrawable = vk_schedulePresent;
}
