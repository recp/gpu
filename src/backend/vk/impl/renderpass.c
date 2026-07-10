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
GPURenderPassDesc*
vk_beginRenderPass(GPUCommandBuffer              *cmdb,
                   const GPURenderPassCreateInfo *info) {
  const GPURenderPassColorAttachment *color;
  GPUCommandBufferVk                  *command;
  GPUTextureViewVk                    *view;
  GPUSwapChainVk                      *swapchain;
  GPURenderPassDesc                   *pass;
  GPURenderPassVk                     *native;

  if (!cmdb || !info || info->colorAttachmentCount != 1u ||
      !info->pColorAttachments || info->pDepthStencilAttachment) {
    return NULL;
  }

  color     = &info->pColorAttachments[0];
  command   = cmdb->_priv;
  view      = color->view ? color->view->_priv : NULL;
  swapchain = view ? view->swapchain : NULL;
  if (!command || !view || !swapchain || !swapchain->frameActive ||
      color->resolveView ||
      view->imageIndex != swapchain->acquiredImageIndex) {
    return NULL;
  }

  pass   = &command->renderPass;
  native = &command->renderPassState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  native->swapchain   = swapchain;
  native->renderPass  = swapchain->renderPasses[color->loadOp][color->storeOp];
  native->framebuffer = swapchain->framebuffers[view->imageIndex];
  native->extent      = swapchain->extent;
  native->clearValue.color.float32[0] = color->clearColor.float32[0];
  native->clearValue.color.float32[1] = color->clearColor.float32[1];
  native->clearValue.color.float32[2] = color->clearColor.float32[2];
  native->clearValue.color.float32[3] = color->clearColor.float32[3];

  pass->_priv = native;
  pass->label = info->label;
  return pass;
}

GPU_HIDE
void
vk_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

GPU_HIDE
void
vk_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = vk_beginRenderPass;
  api->destroyRenderPass = vk_destroyRenderPass;
}
