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
dx12_beginRenderPass(GPUCommandBuffer             *cmdb,
                     const GPURenderPassCreateInfo *info) {
  GPUCommandBufferDX12 *command;
  GPURenderPassDX12    *renderPass;
  GPURenderPassDesc    *desc;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->commandList || !info ||
      info->colorAttachmentCount == 0u ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS ||
      !info->pColorAttachments || info->pDepthStencilAttachment) {
    return NULL;
  }

  renderPass = &command->renderPass;
  desc       = &command->renderPassDesc;
  memset(renderPass, 0, sizeof(*renderPass));
  memset(desc, 0, sizeof(*desc));

  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *attachment;
    GPUTextureViewDX12                 *view;

    attachment = &info->pColorAttachments[i];
    view       = attachment->view ? attachment->view->_priv : NULL;
    if (!view || !view->resource || !view->state || attachment->resolveView ||
        view->width == 0u || view->height == 0u ||
        (i > 0u &&
         (view->width != renderPass->width ||
          view->height != renderPass->height))) {
      return NULL;
    }

    renderPass->colorViews[i]     = view;
    renderPass->loadOps[i]        = attachment->loadOp;
    renderPass->storeOps[i]       = attachment->storeOp;
    renderPass->clearColors[i][0] = attachment->clearColor.float32[0];
    renderPass->clearColors[i][1] = attachment->clearColor.float32[1];
    renderPass->clearColors[i][2] = attachment->clearColor.float32[2];
    renderPass->clearColors[i][3] = attachment->clearColor.float32[3];
    renderPass->width             = view->width;
    renderPass->height            = view->height;
  }

  renderPass->colorCount = info->colorAttachmentCount;
  desc->_priv            = renderPass;
  desc->label            = info->label;
  return desc;
}

GPU_HIDE
void
dx12_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

GPU_HIDE
void
dx12_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = dx12_beginRenderPass;
  api->destroyRenderPass = dx12_destroyRenderPass;
}
