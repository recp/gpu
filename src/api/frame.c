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
GPUFrame*
GPUBeginFrame(GPUSwapChain* swapchain) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->frame.beginFrame(swapchain);
}

GPU_EXPORT
void
GPUEndFrame(GPUFrame* frame) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  return api->frame.endFrame(frame);
}

GPU_EXPORT
void
GPUFinishFrame(GPUCommandBuffer * __restrict cmdb, GPUFrame * __restrict frame) {
  GPUPresent(cmdb, frame);
  GPUCommit(cmdb);
  GPUEndFrame(frame);
}
