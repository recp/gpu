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
GPUResult
GPUCreateSwapchain(GPUDevice                    * __restrict device,
                   const GPUSwapchainCreateInfo * __restrict info,
                   GPUSwapchain                ** __restrict outSwapchain) {
  GPUCommandQueue *queue;
  GPUExtent2D      size;

  if (!outSwapchain)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outSwapchain = NULL;

  if (!device || !info || !info->surface || info->width == 0 || info->height == 0)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info))
    return GPU_ERROR_INVALID_ARGUMENT;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue)
    return GPU_ERROR_BACKEND_FAILURE;

  size.width      = info->width;
  size.height     = info->height;
  *outSwapchain = GPUCreateSwapChain(device,
                                     queue,
                                     info->surface,
                                     size,
                                     true);
  if (!*outSwapchain)
    return GPU_ERROR_BACKEND_FAILURE;

  return GPU_OK;
}

GPU_EXPORT
GPUSwapchain*
GPUCreateSwapchainDefault(GPUDevice         * __restrict device,
                          struct GPUSurface * __restrict surface,
                          uint32_t                       width,
                          uint32_t                       height) {
  GPUSwapchainCreateInfo info = {0};
  GPUSwapchain          *swapchain;

  info.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.surface          = surface;
  info.width            = width;
  info.height           = height;
  info.format           = GPU_FORMAT_BGRA8_UNORM;
  info.imageCount       = 3;
  info.presentMode      = GPU_PRESENT_MODE_FIFO;

  if (GPUCreateSwapchain(device, &info, &swapchain) != GPU_OK)
    return NULL;

  return swapchain;
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
GPUDestroySwapChain(GPUSwapChain * __restrict swapChain) {
  GPUApi *api;

  if (!swapChain) {
    return;
  }

  if (!(api = gpuActiveGPUApi())) {
    return;
  }

  if (api->swapchain.destroySwapChain) {
    api->swapchain.destroySwapChain(swapChain);
  }
}

GPU_EXPORT
void
GPUDestroySwapchain(GPUSwapchain * __restrict swapChain) {
  GPUDestroySwapChain(swapChain);
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
