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
#include "impl/impl.c"

GPUApi mt = {
  .initialized = false,
  .backend     = GPU_BACKEND_METAL,
};

GPU_HIDE
GPUApi*
backend_metal(void) {
  if (!mt.initialized) {
    mt_initDevice(&mt.device);
    mt_initRenderPipeline(&mt.render);
    mt_initRCE(&mt.rce);
    mt_initCmdBuff(&mt.cmdbuf);
    mt_initCmdQue(&mt.cmdque);
    mt_initBuff(&mt.buf);
    mt_initDepthStencil(&mt.depthStencil);
    mt_initVertex(&mt.vertex);
    mt_initLibrary(&mt.library);
    mt_initRenderPass(&mt.renderPass);
    mt_initSwapChain(&mt.swapchain);
    mt_initFrame(&mt.frame);
    mt_initInstance(&mt.instance);
    mt_initSurface(&mt.surface);

    mt.initialized = true;
  }
  return &mt;
}
