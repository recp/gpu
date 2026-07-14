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

#ifndef gpu_swapchain_internal_h
#define gpu_swapchain_internal_h

#include "../common.h"

struct GPUSwapchain {
  GPUDevice          *device;
  void               *_priv;
  void               *target;
  float               backingScaleFactor;
  uint32_t            width;
  uint32_t            height;
  GPUFormat           format;
  GPUSwapchainStatus  status;
};

static inline void
gpuSwapchainSetStatus(GPUSwapchain       *swapchain,
                      GPUSwapchainStatus  status) {
  if (!swapchain) {
    return;
  }
  if (swapchain->status >= GPU_SWAPCHAIN_STATUS_SUBOPTIMAL &&
      status < swapchain->status) {
    return;
  }
  swapchain->status = status;
}

static inline void
gpuSwapchainResetStatus(GPUSwapchain *swapchain) {
  if (swapchain) {
    swapchain->status = GPU_SWAPCHAIN_STATUS_READY;
  }
}

#endif /* gpu_swapchain_internal_h */
