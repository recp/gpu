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

#include "../../common.h"

GPU_EXPORT
GPURenderPipeline*
GPUNewRenderPipeline(GPUPixelFormat pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->render.newRenderPipeline(pixelFormat);
}

GPU_EXPORT
GPURenderPipelineState*
GPUNewRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  
  return api->render.newRenderState(device, pipeline);
}

GPU_EXPORT
void
GPUSetFunction(GPURenderPipeline * __restrict pipline,
               GPUFunction       * __restrict func,
               GPUFunctionType                functype) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.setFunction(pipline, func, functype);
}

GPU_EXPORT
void
GPUColorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.colorFormat(pipline, index, pixelFormat);
}

GPU_EXPORT
void
GPUDepthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.depthFormat(pipline, pixelFormat);
}

GPU_EXPORT
void
GPUStencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat                pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  api->render.stencilFormat(pipline, pixelFormat);
}

GPU_EXPORT
void
GPUSampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                       sampleCount) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.sampleCount(pipline, sampleCount);
}
