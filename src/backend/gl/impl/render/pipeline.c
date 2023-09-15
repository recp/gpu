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

GPU_HIDE
GPURenderPipeline*
gl_newPipeline(GPUPixelFormat pixelFormat) {
  GPURenderPipeline *pipeline;

  pipeline = calloc(1, sizeof(*pipeline));

  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
gl_newRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPURenderPipelineState *rederPipline;
  
  rederPipline   = calloc(1, sizeof(*rederPipline));
  
  
  return rederPipline;
}

GPU_HIDE
void
gl_setFunction(GPURenderPipeline * __restrict pipline,
               GPUFunction       * __restrict func,
               GPUFunctionType                functype) {
  
}

GPU_HIDE
void
gl_colorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUPixelFormat                 pixelFormat) {
  
}

GPU_HIDE
void
gl_depthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat                 pixelFormat) {
  
}

GPU_HIDE
void
gl_stencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat) {
  
}

GPU_HIDE
void
gl_sampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                       sampleCount) {
  
}

GPU_HIDE
void
gl_initRenderPipeline(GPUApiRender *api) {
  api->newRenderPipeline    = gl_newPipeline;
  api->newRenderState = gl_newRenderState;
  api->setFunction    = gl_setFunction;
  api->colorFormat    = gl_colorFormat;
  api->depthFormat    = gl_depthFormat;
  api->stencilFormat  = gl_stencilFormat;
  api->sampleCount    = gl_sampleCount;
}
