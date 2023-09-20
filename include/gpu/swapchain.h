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

typedef struct GPUSwapChain {
  void *_priv;
  void *target; /* draw target */
  float backingScaleFactor;
} GPUSwapChain;

GPU_EXPORT
GPUSwapChain*
GPUCreateSwapChain(GPUDevice* device, float backingScaleFactor);

GPU_EXPORT
void
GPUSwapChainAttachToLayer(GPUSwapChain* swapChain, void* targetLayer, bool autoResize);

GPU_EXPORT
void
GPUSwapChainAttachToView(GPUSwapChain  *swapChain,
                         void          *viewHandle,
                         bool           autoResize,
                         bool           replace);

#ifdef __cplusplus
}
#endif
#endif /* gpu_swapchain_h */
