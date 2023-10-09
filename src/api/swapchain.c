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

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChain(GPUDevice              * __restrict device,
                   struct GPUCommandQueue * __restrict cmdQue,
                   struct GPUSurface      * __restrict surface,
                   GPUExtent2D                         size,
                   bool                                autoResize) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->swapchain.createSwapChain(api, device, cmdQue, surface, size, autoResize);
}

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChainForView(GPUDevice              * __restrict device,
                          struct GPUCommandQueue * __restrict cmdQue,
                          void                   * __restrict viewHandle,
                          GPUWindowType                       viewHandleType,
                          float                               backingScaleFactor,
                          uint32_t                            width,
                          uint32_t                            height,
                          bool                                autoResize) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->swapchain.createSwapChainForView(api, device, cmdQue, viewHandle, viewHandleType, backingScaleFactor, width, height, autoResize);
}

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChainForLayer(GPUDevice              * __restrict device,
                           struct GPUCommandQueue * __restrict cmdQue,
                           float                               backingScaleFactor,
                           uint32_t                            width,
                           uint32_t                            height,
                           bool                                autoResize) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->swapchain.createSwapChainForLayer(api, device, cmdQue, backingScaleFactor, width, height, autoResize);
}

GPU_EXPORT
void
GPUSwapChainAttachToLayer(GPUSwapChain* swapChain, void* targetLayer, bool autoResize) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  api->swapchain.attachToLayer(swapChain, targetLayer, autoResize);
}

GPU_EXPORT
void
GPUSwapChainAttachToView(GPUSwapChain* swapChain, void *viewHandle, bool autoResize, bool replace) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  api->swapchain.attachToView(swapChain, viewHandle, autoResize, replace);
}
