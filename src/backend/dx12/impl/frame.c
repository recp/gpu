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
#include "../impl.h"
#include "../../../api/pacer_internal.h"

static GPUFrame*
dx12__activateFrame(GPUSwapchain     *swapchain,
                    GPUSwapchainDX12 *native,
                    UINT              frameIndex) {
  GPUFrameDX12 *frame;

  frame = &native->frames[frameIndex];
  swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_READY);

  native->frameIndex      = frameIndex;
  native->frameActive     = true;
  native->frameScheduled  = false;
  frame->frame._priv      = native;
  frame->frame.target     = &frame->target;
  frame->frame.targetView = &frame->targetView;
  frame->frame.drawable   = frame;

  return &frame->frame;
}

GPU_HIDE
GPUFrame*
dx12_beginFrame(GPUApi       *__restrict api,
                GPUSwapchain *__restrict swapchain) {
  GPUSwapchainDX12 *native;
  GPUFrameDX12     *frame;
  UINT              frameIndex;

  GPU__UNUSED(api);

  native = swapchain ? swapchain->_priv : NULL;

  if (!native || !native->swapchain || !native->frames
      || native->imageCount == 0u) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (native->frameActive) {
    return NULL;
  }

  frameIndex = native->swapchain->lpVtbl->GetCurrentBackBufferIndex(native->swapchain);

  if (frameIndex >= native->imageCount) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  frame = &native->frames[frameIndex];

  if (!dx12_waitQueueFence(native->queue,
                           frame->fenceValue,
                           native->frameEvent)) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    return NULL;
  }

  return dx12__activateFrame(swapchain, native, frameIndex);
}

/* a pacer's tick: nothing waited for. The back buffer still on the GPU, or
 * DXGI's queue more than a frame past the latency (the display behind: a
 * frame drawn now would keep it behind), gives no frame this refresh. DXGI
 * names the back buffer without acquiring it, so the target is set now and a
 * frame left undrawn is simply not presented */
static GPUFrame*
dx12_beginDrawable(GPUApi       *__restrict api,
                   GPUSwapchain *__restrict swapchain,
                   void         *pacer) {
  DXGI_FRAME_STATISTICS stats;
  GPUSwapchainDX12     *native;
  GPUFrameDX12         *frame;
  UINT64                completed;
  UINT                  frameIndex, lastPresent;

  GPU__UNUSED(api);

  native = swapchain ? swapchain->_priv : NULL;

  if (!native || !native->swapchain || !native->frames
      || native->imageCount == 0u) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  if (native->frameActive) {
    return NULL;
  }

  frameIndex = native->swapchain->lpVtbl->GetCurrentBackBufferIndex(native->swapchain);

  if (frameIndex >= native->imageCount) {
    swapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
    return NULL;
  }

  frame = &native->frames[frameIndex];
  if (frame->fenceValue) {
    completed = native->queue->completionFence
                  ? native->queue->completionFence->lpVtbl->GetCompletedValue(native->queue->completionFence)
                  : 0u;
    if (completed == UINT64_MAX || completed < frame->fenceValue) {
      return NULL;
    }
  }

  if (SUCCEEDED(native->swapchain->lpVtbl->GetLastPresentCount(native->swapchain, &lastPresent))
      && SUCCEEDED(native->swapchain->lpVtbl->GetFrameStatistics(native->swapchain, &stats))
      && lastPresent - stats.PresentCount > ((GPUPacer *)pacer)->latencyFrames + 1u) {
    return NULL;
  }

  return dx12__activateFrame(swapchain, native, frameIndex);
}

GPU_HIDE
void
dx12_endFrame(GPUApi   *__restrict api,
              GPUFrame *__restrict frame) {
  GPUSwapchainDX12 *native;

  GPU__UNUSED(api);

  native = frame ? frame->_priv : NULL;

  if (!native || !native->frameActive) {
    return;
  }

  frame->drawable        = NULL;
  native->frameActive    = false;
  native->frameScheduled = false;
}

GPU_HIDE
bool
dx12_schedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUCommandBufferDX12 *command;
  GPUSwapchainDX12     *swapchain;

  command   = cmdb ? cmdb->_priv : NULL;
  swapchain = frame ? frame->_priv : NULL;

  if (!command || !swapchain || !frame->drawable
      || !swapchain->frameActive || swapchain->frameScheduled
      || command->presentSwapchain || command->owner != swapchain->queue) {
    return false;
  }

  command->presentSwapchain = swapchain;
  swapchain->frameScheduled = true;

  return true;
}

GPU_HIDE
void
dx12_initFrame(GPUFrameApi *api) {
  api->beginFrame    = dx12_beginFrame;
  api->endFrame      = dx12_endFrame;
  api->beginDrawable = dx12_beginDrawable;
  framePacerUseClock(api);
}

GPU_HIDE
void
dx12_initCmdbuf(GPUCommandBufferApi *api) {
  api->presentDrawable = dx12_schedulePresent;
}
