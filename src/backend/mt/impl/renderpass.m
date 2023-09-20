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
GPURenderPassDesc*
mt_newPass(void) {
  return mtNewPass();
}

GPU_HIDE
GPURenderPassDesc*
mt_beginRenderPass(GPUTexture *target) {
  GPURenderPassDesc       *renderPass;
  MTLRenderPassDescriptor *rpd;
  id<CAMetalDrawable>      drawable;
  id<MTLTexture>           texture;

  texture  = target;

  rpd = [MTLRenderPassDescriptor renderPassDescriptor];
  rpd.colorAttachments[0].texture     = texture;
  rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
  rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
  rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

//  MTLPixelFormatDepth32Float_Stencil8
  renderPass = calloc(1, sizeof(*renderPass));
  renderPass->_priv = rpd;

  return renderPass;
}

GPU_HIDE
void
mt_endRenderPass(GPURenderPassDesc *pass) {

}

GPU_HIDE
void
mt_initRenderPass(GPUApiRenderPass *api) {
  api->newPass         = mt_newPass;
  api->beginRenderPass = mt_beginRenderPass;
  api->endRenderPass   = mt_endRenderPass;
}
