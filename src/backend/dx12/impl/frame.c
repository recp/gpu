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

typedef struct GPUFrameDX12 {
  IDXGISwapChain3 *swapChain;
} GPUFrameDX12;

GPU_HIDE
GPUFrame*
dx12_beginFrame(GPUApi       *__restrict api,
                GPUSwapChain *__restrict swapChain) {
  return NULL;
}

GPU_HIDE
void
dx12_endFrame(GPUApi *__restrict api, GPUFrame *__restrict frame) {
}

GPU_HIDE
void
dx12_initFrame(GPUApiFrame *apiFrame) {
  apiFrame->beginFrame = dx12_beginFrame;
  apiFrame->endFrame   = dx12_endFrame;
}
