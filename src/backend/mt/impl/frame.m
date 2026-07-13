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

GPU_HIDE
GPUFrame*
mt_beginFrame(GPUApi       *__restrict api,
              GPUSwapchain *__restrict swapchain) {
  GPUFrame           *frame;
  GPUTexture         *target;
  GPUTextureView     *targetView;
  GPUSwapchainMetal  *swapchainMtl;
  id<CAMetalDrawable> drawable;

  swapchainMtl = swapchain->_priv;
  if (!swapchainMtl || swapchainMtl->frameActive) {
    return NULL;
  }
  drawable     = [swapchainMtl->layer nextDrawable];
  if (!drawable) {
    return NULL;
  }

  [drawable retain];
  frame = &swapchainMtl->frame;
  target = &swapchainMtl->target;
  targetView = &swapchainMtl->targetView;
  memset(frame, 0, sizeof(*frame));
  memset(target, 0, sizeof(*target));
  memset(targetView, 0, sizeof(*targetView));

  target->_priv = drawable.texture;
  target->format = mt_formatFromNative(drawable.texture.pixelFormat);
  target->dimension = GPU_TEXTURE_DIMENSION_2D;
  target->width = (uint32_t)drawable.texture.width;
  target->height = (uint32_t)drawable.texture.height;
  target->depthOrLayers = 1;
  target->mipLevelCount = 1;
  target->sampleCount = 1;
  target->usage = GPU_TEXTURE_USAGE_COLOR_TARGET;
  target->_ownsNative = false;

  targetView->_priv = drawable.texture;
  targetView->_texture = target;
  targetView->format = target->format;
  targetView->viewType = GPU_TEXTURE_VIEW_2D;
  targetView->baseMipLevel = 0;
  targetView->mipLevelCount = 1;
  targetView->baseArrayLayer = 0;
  targetView->arrayLayerCount = 1;
  targetView->_ownsNative = false;

  frame->_priv      = swapchainMtl;
  frame->target     = target;
  frame->targetView = targetView;
  frame->drawable   = drawable;
  swapchainMtl->frameActive = true;

  return frame;
}

GPU_HIDE
void
mt_endFrame(GPUApi   *__restrict api,
            GPUFrame *__restrict frame) {
  GPUSwapchainMetal *swapchainMtl;

  (void)api;

  if (!frame)
    return;

  swapchainMtl = frame->_priv;
  [(id<CAMetalDrawable>)frame->drawable release];
  frame->drawable = NULL;
  if (swapchainMtl) {
    swapchainMtl->frameActive = false;
  }
}

GPU_HIDE
void
mt_initFrame(GPUApiFrame *api) {
  api->beginFrame = mt_beginFrame;
  api->endFrame   = mt_endFrame;
}
