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
GPUResult
GPUCreateRenderPipeline(GPUDevice                         * __restrict device,
                        const GPURenderPipelineCreateInfo * __restrict info,
                        GPURenderPipeline                ** __restrict outPipeline) {
  GPURenderPipelineState *state;
  GPURenderPipeline      *pipeline;
  GPUFunction            *vertexFunc;
  GPUFunction            *fragmentFunc;

  if (!outPipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outPipeline = NULL;

  if (!device || !info || !info->library || !info->vertexEntry || !info->fragmentEntry)
    return GPU_ERROR_INVALID_ARGUMENT;

  vertexFunc = GPUShaderFunction(info->library, info->vertexEntry);
  fragmentFunc = GPUShaderFunction(info->library, info->fragmentEntry);
  if (!vertexFunc || !fragmentFunc)
    return GPU_ERROR_INVALID_ARGUMENT;

  pipeline = GPUNewRenderPipeline(info->colorFormat);
  if (!pipeline)
    return GPU_ERROR_BACKEND_FAILURE;

  GPUSetFunction(pipeline, vertexFunc, GPU_FUNCTION_VERT);
  GPUSetFunction(pipeline, fragmentFunc, GPU_FUNCTION_FRAG);

  if (info->vertexDesc)
    GPUSetVertexDesc(pipeline, info->vertexDesc);
  if (info->colorFormat != GPUPixelFormatInvalid)
    GPUColorFormat(pipeline, 0, info->colorFormat);
  if (info->depthFormat != GPUPixelFormatInvalid)
    GPUDepthFormat(pipeline, info->depthFormat);
  if (info->stencilFormat != GPUPixelFormatInvalid)
    GPUStencilFormat(pipeline, info->stencilFormat);
  if (info->sampleCount > 0)
    GPUSampleCount(pipeline, info->sampleCount);

  state = GPUNewRenderState(device, pipeline);
  if (!state) {
    GPUDestroyRenderPipeline(pipeline);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  free(state);
  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyRenderPipeline(GPURenderPipeline *pipeline) {
  free(pipeline);
}

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
