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

GPU_HIDE
GPUSwapChain*
vk_createSwapChain(GPUApi          * __restrict api,
                   GPUDevice       * __restrict device,
                   GPUCommandQueue * __restrict cmdQue,
                   GPUSurface      * __restrict surface,
                   GPUExtent2D                  size,
                   bool                         autoResize) {
  return NULL;
}

GPU_HIDE
void
vk_initSwapChain(GPUApiSwapChain* apiSwapChain) {
  apiSwapChain->createSwapChain = vk_createSwapChain;
}
