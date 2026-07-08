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

GPU_EXPORT
GPURenderPassDesc*
gpuNewPass(void) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->renderPass.newPass();
}

GPU_EXPORT
GPURenderPassEncoder*
GPUBeginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  GPURenderPassDesc    *desc;
  GPURenderPassEncoder *encoder;
  GPUApi               *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!cmdb || !info || !api->renderPass.beginRenderPass || !api->rce.renderCommandEncoder)
    return NULL;

  desc = api->renderPass.beginRenderPass(info);
  if (!desc)
    return NULL;

  encoder = api->rce.renderCommandEncoder(cmdb, desc);
  GPUDestroyRenderPass(desc);

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

GPU_EXPORT
void
GPUDestroyRenderPass(GPURenderPassDesc *pass) {
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
