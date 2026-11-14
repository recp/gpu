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

struct GPUSurface;
typedef struct GPUDevice    GPUDevice;
typedef struct GPUSwapchain GPUSwapchain;

typedef enum GPUPresentMode {
  GPU_PRESENT_MODE_FIFO      = 0,
  GPU_PRESENT_MODE_MAILBOX   = 1,
  GPU_PRESENT_MODE_IMMEDIATE = 2
} GPUPresentMode;

typedef enum GPUSwapchainStatus {
  GPU_SWAPCHAIN_STATUS_READY        = 0,
  GPU_SWAPCHAIN_STATUS_UNAVAILABLE  = 1,
  GPU_SWAPCHAIN_STATUS_SUBOPTIMAL   = 2,
  GPU_SWAPCHAIN_STATUS_OUT_OF_DATE  = 3,
  GPU_SWAPCHAIN_STATUS_SURFACE_LOST = 4
} GPUSwapchainStatus;

typedef enum GPUColorSpace {
  GPU_COLOR_SPACE_DEFAULT    = 0,
  GPU_COLOR_SPACE_SRGB       = 1,
  GPU_COLOR_SPACE_DISPLAY_P3 = 2
} GPUColorSpace;

/* optional GPUSwapchainCreateInfo chain. explicit colour spaces describe the
 * rendered content; they do not convert shader values or change the format's
 * transfer function. enable GPU_FEATURE_SWAPCHAIN_COLOR_SPACE for SRGB/P3 and
 * GPU_FEATURE_SWAPCHAIN_TRANSACTIONS for transaction presentation.
 */
typedef struct GPUSwapchainOptions {
  GPUChainedStruct chain;
  GPUColorSpace    colorSpace;
  bool             presentsWithTransaction;
} GPUSwapchainOptions;

typedef struct GPUSwapchainCreateInfo {
  GPUChainedStruct   chain;
  const char        *label;
  struct GPUSurface *surface;
  uint32_t           width;
  uint32_t           height;
  GPUFormat          format;
  uint32_t           imageCount;
  GPUPresentMode     presentMode;
} GPUSwapchainCreateInfo;

/* one device/graphics queue may serve multiple independent swapchains. finish
 * each frame before beginning another on that device: transient allocation and
 * frame statistics are device-owned. GPU work for different windows may remain
 * in flight. serialize frame, resize and scale calls on the rendering thread.
 * Apple view/layer creation, resize, scale and destruction stay on the main
 * thread. transaction swapchains also encode/submit/present on that thread in
 * the caller's current Core Animation transaction. classic Metal waits for GPU
 * scheduling in this opt-in mode, never GPU completion. default presentation
 * remains asynchronous. pause an attached pacer before reconfiguration.
 */

GPU_EXPORT
GPUResult
GPUCreateSwapchain(GPUDevice                    *__restrict device,
                   const GPUSwapchainCreateInfo *__restrict info,
                   GPUSwapchain                **__restrict outSwapchain);

GPU_EXPORT
GPUSwapchain*
GPUCreateSwapchainDefault(GPUDevice         *__restrict device,
                          struct GPUSurface *__restrict surface,
                          uint32_t                      width,
                          uint32_t                      height);

GPU_EXPORT
GPUFormat
GPUGetSwapchainFormat(GPUSwapchain *__restrict swapchain);

GPU_EXPORT
GPUSwapchainStatus
GPUGetSwapchainStatus(GPUSwapchain *__restrict swapchain);

GPU_EXPORT
void
GPUDestroySwapchain(GPUSwapchain *__restrict swapchain);

GPU_EXPORT
GPUResult
GPUResizeSwapchain(GPUSwapchain *__restrict swapchain,
                   uint32_t                 width,
                   uint32_t                 height);

/* enable GPU_FEATURE_SWAPCHAIN_SCALE. sizes use logical units; the scale maps
 * them to drawable pixels. positive finite values only; no active frame.
 * failure preserves the previous scale. does not change the parent surface.
 */
GPU_EXPORT
GPUResult
GPUSetSwapchainScale(GPUSwapchain *swapchain, float scale);

#ifdef __cplusplus
}
#endif
#endif /* gpu_swapchain_h */
