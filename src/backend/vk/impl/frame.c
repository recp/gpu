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
#include "../../../api/pacer_internal.h"

static bool
vk__presentAcquiredFrame(GPUSwapchainVk *swapchain) {
  VkSubmitInfo         submitInfo = {0};
  GPUFrameSyncVk      *sync;
  VkSemaphore          renderFinished;
  VkPipelineStageFlags waitStage;
  VkResult             result;

  sync           = &swapchain->frameSync[swapchain->frameIndex];
  renderFinished = swapchain->renderFinished[swapchain->acquiredImageIndex];
  waitStage      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  /* nothing to MoltenVK from the background (vk_gpuRefused) */
  if (vk_gpuRefused()) {
    return false;
  }

  if (!sync->fence || !renderFinished
      || vkResetFences(swapchain->device, 1u, &sync->fence) != VK_SUCCESS) {
    vk_setSwapchainStatus(swapchain, VK_ERROR_UNKNOWN);
    return false;
  }

  submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount   = 1u;
  submitInfo.pWaitSemaphores      = &sync->imageAvailable;
  submitInfo.pWaitDstStageMask    = &waitStage;
  submitInfo.signalSemaphoreCount = 1u;
  submitInfo.pSignalSemaphores    = &renderFinished;
  vk_queueSubmitLock(swapchain->queue);
  result = vkQueueSubmit(swapchain->queue->queRaw, 1u, &submitInfo, sync->fence);
  vk_queueSubmitUnlock(swapchain->queue);

  if (result != VK_SUCCESS) {
    vk_setSwapchainStatus(swapchain, result);
    (void)vk_restoreFrameFence(swapchain, sync);
    return false;
  }

  result = vk_presentFrame(swapchain, swapchain->queue, renderFinished, swapchain->acquiredImageIndex, NULL);
  return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

/* an acquired image's semaphore is waited by an empty batch, its fence waited
 * for: a swapchain goes with no acquire still signalling (a semaphore destroyed
 * with its signal pending loses the device on MoltenVK). Not presented. A held
 * image too; from the background no batch goes (vk_gpuRefused): its acquire
 * was made before, and its signal has landed */
GPU_HIDE
void
vk_settleAcquiredFrame(GPUSwapchainVk *swapchain) {
  VkSubmitInfo         submitInfo = {0};
  GPUFrameSyncVk      *sync;
  VkPipelineStageFlags waitStage;
  VkResult             result;

  if (!swapchain || (!swapchain->imageHeld
                     && (!swapchain->frameActive || !swapchain->imageAcquired || swapchain->frameSubmitted))) {
    return;
  }

  if (vk_gpuRefused()) {
    swapchain->imageAcquired = false;
    swapchain->imageHeld     = false;
    return;
  }

  sync      = &swapchain->frameSync[swapchain->frameIndex];
  waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (!sync->fence || vkResetFences(swapchain->device, 1u, &sync->fence) != VK_SUCCESS) {
    return;
  }

  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1u;
  submitInfo.pWaitSemaphores    = &sync->imageAvailable;
  submitInfo.pWaitDstStageMask  = &waitStage;
  vk_queueSubmitLock(swapchain->queue);
  result = vkQueueSubmit(swapchain->queue->queRaw, 1u, &submitInfo, sync->fence);
  vk_queueSubmitUnlock(swapchain->queue);

  if (result != VK_SUCCESS) {
    (void)vk_restoreFrameFence(swapchain, sync);
    return;
  }

  (void)vkWaitForFences(swapchain->device, 1u, &sync->fence, VK_TRUE, UINT64_MAX);
  swapchain->imageAcquired = false;
  swapchain->imageHeld     = false;
}

/* the frame's image: a held one (vk_endFrame), its semaphore still to be
 * waited, else the next one acquired into the slot's semaphore */
static VkResult
vk__takeImage(GPUSwapchainVk *swapchain, GPUFrameSyncVk *sync, uint64_t timeout, uint32_t *imageIndex) {
  VkResult result;

  if (swapchain->imageHeld) {
    *imageIndex          = swapchain->acquiredImageIndex;
    swapchain->imageHeld = false;
    return VK_SUCCESS;
  }

  vk_swapchainClaim(swapchain);
  result = vkAcquireNextImageKHR(swapchain->device,
                                 swapchain->swapchain,
                                 timeout,
                                 sync->imageAvailable,
                                 VK_NULL_HANDLE,
                                 imageIndex);
  vk_swapchainUnclaim(swapchain);

  return result;
}

GPU_HIDE
bool
vk_restoreFrameFence(GPUSwapchainVk *swapchain, GPUFrameSyncVk *sync) {
  VkFenceCreateInfo info = {0};
  VkFence           replacement;

  if (!swapchain || !sync) {
    return false;
  }

  replacement = VK_NULL_HANDLE;
  info.sType  = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  info.flags  = VK_FENCE_CREATE_SIGNALED_BIT;

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

  if (!swapchain || swapchain->imageCount == 0u) {
    swapchainSetStatus(swapchainObj, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (swapchain->frameActive) {
    return NULL;
  }

  /* a view the system doesn't show gives MoltenVK no drawable for up to a
   * second (taken at the frame's first pass), and from the background the
   * acquire's own semaphore signal is a command buffer the system refuses:
   * no frame there, at once */
  if (vk_framesRefused(swapchain)) {
    swapchainSetStatus(swapchainObj, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    return NULL;
  }

  sync = &swapchain->frameSync[swapchain->frameIndex];

  if (!sync->fence) {
    vk_setSwapchainStatus(swapchain, VK_ERROR_UNKNOWN);
    return NULL;
  }

  result = vkWaitForFences(swapchain->device,
                           1u,
                           &sync->fence,
                           VK_TRUE,
                           UINT64_MAX);

  if (result != VK_SUCCESS) {
    vk_setSwapchainStatus(swapchain, result);
    return NULL;
  }

  if (!vk_waitFrameCompletion(sync)) {
    vk_setSwapchainStatus(swapchain, VK_ERROR_UNKNOWN);
    return NULL;
  }

  imageIndex = 0u;
  result     = vk__takeImage(swapchain, sync, UINT64_MAX, &imageIndex);
  vk_setSwapchainStatus(swapchain, result);

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
  swapchain->imageAcquired      = true;
  swapchain->frameActive        = true;
  swapchain->frameScheduled     = false;
  swapchain->frameSubmitted     = false;

  return frame;
}

/* the display behind the latency: a present older than the latency's not yet
 * shown (VK_KHR_present_wait). Frames queue one per refresh, so once behind,
 * drawing on keeps it behind for good; a frame left out drains it. A refresh's
 * report gets 2 ms to arrive. The swapchain is the caller's alone meanwhile
 * (the present thread's claim, or no frame of it being presented elsewhere):
 * no queue lock, so submits of other threads go on */
static bool
vk__displayBehind(GPUSwapchainVk *swapchain, uint32_t latency) {
#if defined(VK_KHR_present_id) && defined(VK_KHR_present_wait)
  GPUDeviceVk *device;
  VkResult     result;

  device = swapchain->gpuDevice ? swapchain->gpuDevice->_priv : NULL;
  if (!device || !device->presentWait || !device->waitForPresent || !vk_presenterIdle(swapchain)
      || swapchain->lastPresentId <= latency) {
    return false; /* a present still being made: no look now */
  }

  vk_swapchainClaim(swapchain);
  result = device->waitForPresent(swapchain->device, swapchain->swapchain,
                                  swapchain->lastPresentId - latency, 2000000u);
  vk_swapchainUnclaim(swapchain);

  return result == VK_TIMEOUT;
#else
  GPU__UNUSED(swapchain);
  GPU__UNUSED(latency);
  return false;
#endif
}

/* a pacer's tick: nothing waited for. The slot's last frame still on the GPU
 * means the display is a whole swapchain behind, and the frame before still
 * with the present thread a refresh later means its present is held back
 * (Android's queueBuffer waits for the GPU to finish the frame before it):
 * no frame this refresh, the thread left free instead of waiting at the
 * acquire. The
 * image is acquired at the frame's first use (vk_acquireDrawable): Vulkan gives
 * an acquired image back only by presenting it, and a tick that draws nothing
 * would show a stale one */
static GPUFrame*
vk_beginDrawable(GPUApi *api, GPUSwapchain *swapchainObj, void *pacer) {
  GPUSwapchainVk *swapchain;
  GPUFrameSyncVk *sync;
  GPUFrame       *frame;

  GPU__UNUSED(api);

  swapchain = swapchainObj ? swapchainObj->_priv : NULL;

  if (!swapchain || swapchain->imageCount == 0u) {
    swapchainSetStatus(swapchainObj, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (swapchain->frameActive) {
    return NULL;
  }

  /* a display link ticks where the view isn't shown and in the background:
   * no frame there (vk_beginFrame) */
  if (vk_framesRefused(swapchain)) {
    swapchainSetStatus(swapchainObj, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    return NULL;
  }

  sync = &swapchain->frameSync[swapchain->frameIndex];

  if (!sync->fence || vkGetFenceStatus(swapchain->device, sync->fence) != VK_SUCCESS
      || !vk_frameCompletionDone(sync) || !vk_presenterIdle(swapchain)
      || vk__displayBehind(swapchain, ((GPUPacer *)pacer)->latencyFrames)) {
    return NULL;
  }

  frame = &swapchain->frame;
  memset(frame, 0, sizeof(*frame));
  frame->_priv       = swapchain;
  frame->drawable    = swapchain;
  frame->targetState = GPU_FRAME_TARGET_PENDING;

  swapchain->imageAcquired  = false;
  swapchain->frameActive    = true;
  swapchain->frameScheduled = false;
  swapchain->frameSubmitted = false;

  return frame;
}

static bool
vk_acquireDrawable(GPUApi *api, GPUFrame *frame) {
  GPUSwapchainVk *swapchain;
  GPUFrameSyncVk *sync;
  VkResult        result;
  uint64_t        timeout;
  uint32_t        imageIndex;
  double          left;

  GPU__UNUSED(api);

  swapchain = frame->_priv;

  if (!swapchain || !swapchain->frameActive || swapchain->imageAcquired) {
    return swapchain && swapchain->imageAcquired;
  }

  /* gone to the background, or out of view, since its tick */
  if (vk_framesRefused(swapchain)) {
    swapchainSetStatus(swapchain->gpuSwapchain, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    return false;
  }

  /* a paced frame was begun because the display can take it (the tick's
   * checks): its image is waited for as a drawn frame's must be, until a
   * backstop of a few refreshes past its deadline (Android's queue frees a
   * buffer only at SurfaceFlinger's next latch) */
  timeout = UINT64_MAX;
  if (frame->deadline > 0.0) {
    left    = frame->deadline - pacerHostSeconds();
    timeout = (uint64_t)(((left > 0.0 ? left : 0.0) + 0.1) * 1e9);
  }

  sync       = &swapchain->frameSync[swapchain->frameIndex];
  imageIndex = 0u;
  result     = vk__takeImage(swapchain, sync, timeout, &imageIndex);

  if (result == VK_TIMEOUT || result == VK_NOT_READY) {
    return false;
  }

  vk_setSwapchainStatus(swapchain, result);

  if ((result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) || imageIndex >= swapchain->imageCount) {
    return false;
  }

  frame->target                 = &swapchain->textures[imageIndex];
  frame->targetView             = &swapchain->textureViews[imageIndex];
  swapchain->acquiredImageIndex = imageIndex;
  swapchain->imageAcquired      = true;

  return true;
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

  /* a paced frame that never took its image left the slot unused. One whose
   * submit failed (refused from the background, say), or one ended in the
   * background, keeps its image, slot and signalled semaphore for the next
   * frame: presenting it would show an image never drawn, or be refused */
  if (swapchain->imageAcquired) {
    if (!swapchain->frameSubmitted && (swapchain->frameScheduled || vk_gpuRefused())) {
      swapchain->imageHeld = true;
    } else {
      if (!swapchain->frameSubmitted) {
        (void)vk__presentAcquiredFrame(swapchain);
      }

      swapchain->frameIndex = (swapchain->frameIndex + 1u) % swapchain->imageCount;
    }
  }

  swapchain->imageAcquired  = false;
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

  if (!command || !swapchain || !swapchain->frameActive || !swapchain->imageAcquired
      || swapchain->frameScheduled || command->presentSwapchain
      || command->owner != swapchain->queue) {
    return false;
  }

  command->presentSwapchain  = swapchain;
  command->presentImageIndex = swapchain->acquiredImageIndex;
  command->presentFrameIndex = swapchain->frameIndex;
  swapchain->frameScheduled  = true;

  return true;
}

/* frame feedback: the display's time for a watched frame's present comes back
 * through VK_GOOGLE_display_timing (swapchain.c) */
static GPUResult
vk_observeFrame(GPUFrame *frame, GPUFrameFeedback *feedback) {
  GPUDeviceVk *device;

  if (!frame || !frame->drawable || !feedback || !frame->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device = frame->device->_priv;
  return device && device->getPastTiming ? GPU_OK : GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
vk_initFrame(GPUFrameApi *api) {
  api->observe         = vk_observeFrame;
  api->beginFrame      = vk_beginFrame;
  api->endFrame        = vk_endFrame;
  api->beginDrawable   = vk_beginDrawable;
  api->acquireDrawable = vk_acquireDrawable;
  framePacerUseClock(api);
}

GPU_HIDE
void
vk_initCmdbuf(GPUCommandBufferApi *api) {
  api->presentDrawable = vk_schedulePresent;
}
