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
mt_beginFrame(GPUSwapChain* swapChain) {
  GPUFrame           *frame;
  GPUSwapChainMetal  *swapChainMtl;
  id<CAMetalDrawable> drawable;

  swapChainMtl = swapChain->_priv;
  drawable     = [swapChainMtl->layer nextDrawable];
  if (!drawable) {
    // Handle error, e.g. skip this frame
    return NULL;
  }

  frame           = calloc(1, sizeof(*frame));
  frame->_priv    = drawable;
  frame->target   = drawable.texture; // wrap with GPUTexture later.
  frame->drawable = drawable;

  // Here, you might set up your MTLRenderPassDescriptor using the drawable's texture.
  // Then create a command buffer and render command encoder and proceed with rendering.
  // ...

  return frame;
}

GPU_HIDE
void
mt_endFrame(GPUFrame* frame) {

}

GPU_HIDE
void
mt_initFrame(GPUApiFrame *api) {
  api->beginFrame = mt_beginFrame;
  api->endFrame   = mt_endFrame;
}
