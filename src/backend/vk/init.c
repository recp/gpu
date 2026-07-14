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

#include "common.h"
#include "impl.h"

GPUApi vk = {
  .initialized = false,
  .backend     = GPU_BACKEND_VULKAN
};

GPU_HIDE
GPUApi*
backend_vk(void) {
  // TODO: init
  if (!vk.initialized) {
    vk_initInstance(&vk.instance);
    vk_initDevice(&vk.device);
    vk_initBuff(&vk.buf);
    vk_initTexture(&vk.texture);
    vk_initSampler(&vk.sampler);
    vk_initSurface(&vk.surface);
    vk_initCmdQue(&vk.cmdque);
    vk_initCmdbuf(&vk.cmdbuf);
    vk_initQuery(&vk.cmdbuf);
    vk_initLibrary(&vk.library);
    vk_initRenderPipeline(&vk.render);
    vk_initRenderPass(&vk.renderPass);
    vk_initRCE(&vk.rce);
    vk_initCompute(&vk.compute);
    vk_initSwapchain(&vk.swapchain);
    vk_initFrame(&vk.frame);
    vk_initDescriptor(&vk.descriptor);
    vk_initPipelineCache(&vk.pipelineCache);

    vk.initialized = true;
  }
  return &vk;
}
