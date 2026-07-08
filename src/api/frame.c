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

  if (!swapchain)
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->frame.beginFrame)
    return NULL;

  return api->frame.beginFrame(api, swapchain);
}

GPU_EXPORT
GPUTexture*
GPUFrameGetTarget(GPUFrame *frame) {
  return frame ? frame->target : NULL;
}

GPU_EXPORT
GPUTextureView*
GPUFrameGetTargetView(GPUFrame *frame) {
  return frame ? frame->targetView : NULL;
}

GPU_EXPORT
void
GPUEndFrame(GPUFrame* frame) {
  GPUApi *api;

  if (!frame)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->frame.endFrame)
    return;

  api->frame.endFrame(api, frame);
}

GPU_EXPORT
GPUResult
GPUFinishFrame(GPUCommandQueue  * __restrict cmdq,
               GPUCommandBuffer * __restrict cmdb,
               GPUFrame         * __restrict frame) {
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUResult result;

  if (!cmdq || !cmdb || !frame)
    return GPU_ERROR_INVALID_ARGUMENT;

  GPUSchedulePresent(cmdb, frame);

  buffers[0] = cmdb;
  submitInfo.commandBufferCount = 1;
  submitInfo.ppCommandBuffers = buffers;
  result = GPUQueueSubmit(cmdq, &submitInfo);

  GPUEndFrame(frame);
  return result;
}
