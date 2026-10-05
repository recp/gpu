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

#include "common.h"
#include "impl.h"

static GPUApi webgpu = {
  .backend = GPU_BACKEND_WEBGPU
};

GPU_HIDE
GPUApi*
backend_webgpu(void) {
  if (!webgpu.initialized) {
    webgpu_initDevice(&webgpu.device);
    webgpu_initInstance(&webgpu.instance);
    webgpu_initSurface(&webgpu.surface);
    webgpu_initSwapchain(&webgpu.swapchain);
    webgpu_initFrame(&webgpu.frame);
    webgpu_initCommandQueue(&webgpu.cmdque);
    webgpu_initCommandBuffer(&webgpu.cmdbuf);
    webgpu_initQuery(&webgpu.cmdbuf);
    webgpu_initBuffer(&webgpu.buf);
    webgpu_initTexture(&webgpu.texture);
    webgpu_initSampler(&webgpu.sampler);
    webgpu_initLibrary(&webgpu.library);
    webgpu_initDescriptor(&webgpu.descriptor);
    webgpu_initPipeline(&webgpu.render);
    webgpu_initCompute(&webgpu.compute);
    webgpu_initRenderPass(&webgpu.renderPass);
    webgpu_initRenderEncoder(&webgpu.rce);
    webgpu.initialized = true;
  }

  return &webgpu;
}
