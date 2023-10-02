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

GPUApi dx12 = {
  .initialized = false,
  .backend     = GPU_BACKEND_DIRECTX,
  .reserved    = &(GPU__DX12){0}
};

GPU_HIDE
GPUApi*
backend_dx12(void) {
  // TODO: init
  if (!dx12.initialized) {
    dx12_initDevice(&dx12.device);
    // dx12_initRenderPipeline(&dx12.render);
    // dx12_initRCE(&dx12.rce);
    // dx12_initCmdBuff(&dx12.cmdbuf);
    dx12_initCmdQue(&dx12.cmdque);
    // dx12_initBuff(&dx12.buf);
    // dx12_initDepthStencil(&dx12.depthStencil);
    // dx12_initVertex(&dx12.vertex);
    // dx12_initLibrary(&dx12.library);
    dx12_initSwapChain(&dx12.swapchain);
    dx12_initFrame(&dx12.frame);
    dx12_initDescriptor(&dx12.descriptor);
    dx12_initInstance(&dx12.instance);

    dx12.initialized = true;
  }
  return &dx12;
}
