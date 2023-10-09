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

#ifndef gpu_gpudef_swapchain_h
#define gpu_gpudef_swapchain_h
#ifdef __cplusplus
extern "C" {
#endif

#include "../common.h"
#include "../gpu.h"

struct GPUApi;

typedef struct GPUApiSwapChain {
  GPUSwapChain*
  (*createSwapChain)(struct GPUApi          * __restrict api,
                     struct GPUDevice       * __restrict device,
                     struct GPUCommandQueue * __restrict cmdQue,
                     struct GPUSurface      * __restrict surface,
                     GPUExtent2D                         size,
                     bool                                autoResize);

  GPUSwapChain*
  (*createSwapChainForView)(struct GPUApi          * __restrict api,
                            struct GPUDevice       * __restrict device,
                            struct GPUCommandQueue * __restrict cmdQue,
                            void                   * __restrict viewHandle,
                            GPUWindowType                       viewHandleType,
                            float                               backingScaleFactor,
                            uint32_t                            width,
                            uint32_t                            height,
                            bool                                autoResize);

  GPUSwapChain*
  (*createSwapChainForLayer)(struct GPUApi          * __restrict api,
                             struct GPUDevice       * __restrict device,
                             struct GPUCommandQueue * __restrict cmdQue,
                             float                               backingScaleFactor,
                             uint32_t                            width,
                             uint32_t                            height,
                             bool                                autoResize);

  void (*attachToLayer)(GPUSwapChain* swapChain, void* targetLayer, bool autoResize);
  void (*attachToView)(GPUSwapChain* swapChain, void *viewHandle, bool autoResize, bool replace);
} GPUApiSwapChain;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_swapchain_h */
