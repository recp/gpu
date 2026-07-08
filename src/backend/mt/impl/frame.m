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
              GPUSwapChain *__restrict swapChain) {
  GPUFrame           *frame;
  GPUTexture         *target;
  GPUTextureView     *targetView;
  GPUSwapChainMetal  *swapChainMtl;
  id<CAMetalDrawable> drawable;

  swapChainMtl = swapChain->_priv;
  drawable     = [swapChainMtl->layer nextDrawable];
  if (!drawable) {
    return NULL;
  }

  [drawable retain];
  frame = calloc(1, sizeof(*frame));
  target = calloc(1, sizeof(*target));
  targetView = calloc(1, sizeof(*targetView));
  if (!frame || !target || !targetView) {
    free(targetView);
    free(target);
    free(frame);
    [drawable release];
    return NULL;
  }

  target->_priv = drawable.texture;
  target->format = (GPUFormat)drawable.texture.pixelFormat;
  target->dimension = GPU_TEXTURE_DIMENSION_2D;
  target->width = (uint32_t)drawable.texture.width;
  target->height = (uint32_t)drawable.texture.height;
  target->depthOrLayers = 1;
  target->mipLevelCount = 1;
  target->sampleCount = 1;
  target->_ownsNative = false;

  targetView->_priv = drawable.texture;
  targetView->_texture = target;
  targetView->_ownsNative = false;

  frame->_priv      = drawable;
  frame->target     = target;
  frame->targetView = targetView;
  frame->drawable   = drawable;

  return frame;
}

GPU_HIDE
void
mt_endFrame(GPUApi   *__restrict api,
            GPUFrame *__restrict frame) {
  (void)api;

  if (!frame)
    return;

  free(frame->targetView);
  free(frame->target);
  [(id<CAMetalDrawable>)frame->drawable release];
  free(frame);
}

GPU_HIDE
void
mt_initFrame(GPUApiFrame *api) {
  api->beginFrame = mt_beginFrame;
  api->endFrame   = mt_endFrame;
}
