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
mt_beginRenderPass(const GPURenderPassCreateInfo *info) {
  const GPURenderPassColorAttachment        *color;
  const GPURenderPassDepthStencilAttachment *depthStencil;
  GPURenderPassDesc                         *renderPass;
  MTLRenderPassDescriptor                   *rpd;
  uint32_t                                   i;

  if (!info || (info->colorAttachmentCount > 0 && !info->pColorAttachments))
    return NULL;

  rpd = [MTLRenderPassDescriptor renderPassDescriptor];

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];
    if (!color->view)
      return NULL;

    rpd.colorAttachments[i].texture        = (id<MTLTexture>)color->view->_priv;
    rpd.colorAttachments[i].resolveTexture = color->resolveView ?
      (id<MTLTexture>)color->resolveView->_priv : nil;

    switch (color->loadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.colorAttachments[i].loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.colorAttachments[i].loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.colorAttachments[i].loadAction = MTLLoadActionLoad;
        break;
    }

    switch (color->storeOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.colorAttachments[i].storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.colorAttachments[i].storeAction = MTLStoreActionStore;
        break;
    }

    rpd.colorAttachments[i].clearColor = MTLClearColorMake(color->clearColor.float32[0],
                                                           color->clearColor.float32[1],
                                                           color->clearColor.float32[2],
                                                           color->clearColor.float32[3]);
  }

  depthStencil = info->pDepthStencilAttachment;
  if (depthStencil && depthStencil->view) {
    rpd.depthAttachment.texture   = (id<MTLTexture>)depthStencil->view->_priv;
    rpd.stencilAttachment.texture = (id<MTLTexture>)depthStencil->view->_priv;

    switch (depthStencil->depthLoadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.depthAttachment.loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.depthAttachment.loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.depthAttachment.loadAction = MTLLoadActionLoad;
        break;
    }

    switch (depthStencil->depthStoreOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.depthAttachment.storeAction = MTLStoreActionStore;
        break;
    }

    switch (depthStencil->stencilLoadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.stencilAttachment.loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.stencilAttachment.loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.stencilAttachment.loadAction = MTLLoadActionLoad;
        break;
    }

    switch (depthStencil->stencilStoreOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.stencilAttachment.storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.stencilAttachment.storeAction = MTLStoreActionStore;
        break;
    }

    rpd.depthAttachment.clearDepth       = depthStencil->clearDepth;
    rpd.stencilAttachment.clearStencil   = depthStencil->clearStencil;
  }

  renderPass = calloc(1, sizeof(*renderPass));
  if (!renderPass)
    return NULL;
  renderPass->_priv = rpd;

  return renderPass;
}

GPU_HIDE
void
mt_destroyRenderPass(GPURenderPassDesc *pass) {
  free(pass);
}

GPU_HIDE
void
mt_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = mt_beginRenderPass;
  api->destroyRenderPass = mt_destroyRenderPass;
}
