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
#include "device_internal.h"
#include "swapchain_internal.h"

static GPUSwapChain*
gpuCreateSwapchainInternal(GPUDevice              * __restrict device,
                           struct GPUCommandQueue * __restrict cmdQue,
                           const GPUSwapchainCreateInfo * __restrict info) {
  GPUApi       *api;
  GPUSwapChain *swapChain;

  if (!(api = gpuDeviceApi(device)))
    return NULL;
  if (!api->swapchain.createSwapChain)
    return NULL;

  swapChain = api->swapchain.createSwapChain(api,
                                             device,
                                             cmdQue,
                                             info);
  if (swapChain) {
    swapChain->device = device;
    swapChain->width  = info->width;
    swapChain->height = info->height;
  }

  return swapChain;
}

GPU_EXPORT
GPUResult
GPUCreateSwapchain(GPUDevice                    * __restrict device,
                   const GPUSwapchainCreateInfo * __restrict info,
                   GPUSwapchain                ** __restrict outSwapchain) {
  GPUCommandQueue *queue;

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

  *outSwapchain = gpuCreateSwapchainInternal(device,
                                             queue,
                                             info);
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
void
GPUDestroySwapchain(GPUSwapchain * __restrict swapChain) {
  GPUApi *api;

  if (!swapChain) {
    return;
  }

  if (!(api = gpuDeviceApi(swapChain->device))) {
    return;
  }

  if (api->swapchain.destroySwapChain) {
    api->swapchain.destroySwapChain(swapChain);
  }
}

GPU_EXPORT
GPUResult
GPUResizeSwapchain(GPUSwapchain * __restrict swapChain,
                   uint32_t                  width,
                   uint32_t                  height) {
  GPUApi      *api;
  GPUExtent2D  size;
  GPUResult    result;

  if (!swapChain || width == 0 || height == 0)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!(api = gpuDeviceApi(swapChain->device)))
    return GPU_ERROR_BACKEND_FAILURE;

  if (!api->swapchain.resizeSwapChain)
    return GPU_ERROR_UNSUPPORTED;

  size.width  = width;
  size.height = height;
  result = api->swapchain.resizeSwapChain(swapChain, size);
  if (result == GPU_OK) {
    swapChain->width  = width;
    swapChain->height = height;
  }

  return result;
}
