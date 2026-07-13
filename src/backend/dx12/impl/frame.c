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

GPU_HIDE
GPUFrame*
dx12_beginFrame(GPUApi       * __restrict api,
                GPUSwapChain * __restrict swapChain) {
  GPUSwapChainDX12 *native;
  GPUFrameDX12     *frame;
  UINT              frameIndex;

  GPU__UNUSED(api);

  native = swapChain ? swapChain->_priv : NULL;
  if (!native || !native->swapChain || !native->frames ||
      native->imageCount == 0u || native->frameActive) {
    return NULL;
  }

  frameIndex = native->swapChain->lpVtbl->GetCurrentBackBufferIndex(
    native->swapChain
  );
  if (frameIndex >= native->imageCount) {
    return NULL;
  }

  frame = &native->frames[frameIndex];
  if (!dx12_waitQueueFence(native->queue,
                           frame->fenceValue,
                           native->frameEvent)) {
    return NULL;
  }

  native->frameIndex     = frameIndex;
  native->frameActive    = true;
  native->frameScheduled = false;
  frame->frame._priv      = native;
  frame->frame.target     = &frame->target;
  frame->frame.targetView = &frame->targetView;
  frame->frame.drawable   = frame;
  return &frame->frame;
}

GPU_HIDE
void
dx12_endFrame(GPUApi   * __restrict api,
              GPUFrame * __restrict frame) {
  GPUSwapChainDX12 *native;

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
void
dx12_schedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUCommandBufferDX12 *command;
  GPUSwapChainDX12     *swapchain;

  command   = cmdb ? cmdb->_priv : NULL;
  swapchain = frame ? frame->_priv : NULL;
  if (!command || !swapchain || !frame->drawable ||
      !swapchain->frameActive || swapchain->frameScheduled ||
      command->presentSwapchain || command->owner != swapchain->queue) {
    return;
  }

  command->presentSwapchain  = swapchain;
  swapchain->frameScheduled  = true;
}

GPU_HIDE
void
dx12_initFrame(GPUApiFrame *api) {
  api->beginFrame = dx12_beginFrame;
  api->endFrame   = dx12_endFrame;
}

GPU_HIDE
void
dx12_initCmdbuf(GPUApiCommandBuffer *api) {
  api->presentDrawable = dx12_schedulePresent;
}
