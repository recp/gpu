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

#ifndef gpu_swapchain_h
#define gpu_swapchain_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "format.h"

typedef struct GPUDevice GPUDevice;
struct GPUSurface;

typedef struct GPUSwapchain GPUSwapchain;

typedef enum GPUPresentMode {
  GPU_PRESENT_MODE_FIFO = 0,
  GPU_PRESENT_MODE_MAILBOX = 1,
  GPU_PRESENT_MODE_IMMEDIATE = 2
} GPUPresentMode;

typedef struct GPUSwapchainCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  struct GPUSurface *surface;
  uint32_t         width;
  uint32_t         height;
  GPUFormat        format;
  uint32_t         imageCount;
  GPUPresentMode   presentMode;
} GPUSwapchainCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateSwapchain(GPUDevice                        * __restrict device,
                   const GPUSwapchainCreateInfo     * __restrict info,
                   GPUSwapchain                    ** __restrict outSwapchain);

GPU_EXPORT
GPUSwapchain*
GPUCreateSwapchainDefault(GPUDevice          * __restrict device,
                          struct GPUSurface  * __restrict surface,
                          uint32_t                        width,
                          uint32_t                        height);

GPU_EXPORT
void
GPUDestroySwapchain(GPUSwapchain * __restrict swapchain);

GPU_EXPORT
GPUResult
GPUResizeSwapchain(GPUSwapchain * __restrict swapchain,
                   uint32_t                  width,
                   uint32_t                  height);

#ifdef __cplusplus
}
#endif
#endif /* gpu_swapchain_h */
