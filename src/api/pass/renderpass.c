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

#include "../../common.h"

#define GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS 8u

static bool
gpu_validLoadOp(GPULoadOp op) {
  return op == GPU_LOAD_OP_LOAD ||
         op == GPU_LOAD_OP_CLEAR ||
         op == GPU_LOAD_OP_DONT_CARE;
}

static bool
gpu_validStoreOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE ||
         op == GPU_STORE_OP_DONT_CARE;
}

static bool
gpu_validRenderPassCreateInfo(const GPURenderPassCreateInfo *info) {
  const GPURenderPassDepthStencilAttachment *depthStencil;

  if (!info) {
    return false;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO) {
    return false;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return false;
  }
  if (info->colorAttachmentCount > GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS) {
    return false;
  }
  if (info->colorAttachmentCount > 0 && !info->pColorAttachments) {
    return false;
  }

  for (uint32_t i = 0; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *color;

    color = &info->pColorAttachments[i];
    if (!color->view ||
        !gpu_validLoadOp(color->loadOp) ||
        !gpu_validStoreOp(color->storeOp)) {
      return false;
    }
  }

  depthStencil = info->pDepthStencilAttachment;
  if (depthStencil) {
    if (!depthStencil->view ||
        !gpu_validLoadOp(depthStencil->depthLoadOp) ||
        !gpu_validStoreOp(depthStencil->depthStoreOp) ||
        !gpu_validLoadOp(depthStencil->stencilLoadOp) ||
        !gpu_validStoreOp(depthStencil->stencilStoreOp)) {
      return false;
    }
  }

  return info->colorAttachmentCount > 0 || depthStencil != NULL;
}

static void
gpu_destroyRenderPass(GPURenderPassDesc *pass) {
  GPUApi *api;

  if (!pass) {
    return;
  }

  api = gpuActiveGPUApi();
  if (api && api->renderPass.destroyRenderPass) {
    api->renderPass.destroyRenderPass(pass);
    return;
  }

  free(pass);
}

GPU_EXPORT
GPURenderPassEncoder*
GPUBeginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  GPURenderPassDesc    *desc;
  GPURenderPassEncoder *encoder;
  GPUApi               *api;

  if (!cmdb || cmdb->_submitted || !gpu_validRenderPassCreateInfo(info))
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->renderPass.beginRenderPass || !api->rce.renderCommandEncoder)
    return NULL;

  desc = api->renderPass.beginRenderPass(info);
  if (!desc)
    return NULL;

  encoder = api->rce.renderCommandEncoder(cmdb, desc);
  gpu_destroyRenderPass(desc);

  return encoder;
}

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassEncoder *pass) {
  GPUApi *api;

  if (!pass || !(api = gpuActiveGPUApi()) || !api->rce.endEncoding)
    return;

  api->rce.endEncoding(pass);
}
