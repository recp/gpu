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
GPURenderPipeline*
mt_newPipeline(GPUPixelFormat pixelFormat) {
  GPURenderPipeline *pipeline;
  MtRenderDesc      *renderDesc;

  renderDesc = mtNewRenderPipeline((MtPixelFormat)pixelFormat);
  pipeline   = calloc(1, sizeof(*pipeline));
  
  pipeline->priv = renderDesc;
  
  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
mt_newRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPURenderPipelineState *rederPipline;
  MtRenderPipeline       *mtRederPipline;
  
  mtRederPipline = mtNewRenderState(device->priv, pipeline->priv, NULL);
  rederPipline   = calloc(1, sizeof(*rederPipline));
  
  rederPipline->priv = mtRederPipline;
  
  return rederPipline;
}

GPU_HIDE
void
mt_setFunction(GPURenderPipeline    * __restrict pipline,
               GPUFunction    * __restrict func,
               GPUFunctionType             functype) {
  mtSetFunc(pipline->priv, func->priv, (MtFuncType)functype);
}

GPU_HIDE
void
mt_colorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                 index,
               GPUPixelFormat           pixelFormat) {
  mtColorPixelFormat(pipline->priv, index, (MtPixelFormat)pixelFormat);
}

GPU_HIDE
void
mt_depthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat) {
  mtDepthPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_HIDE
void
mt_stencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat) {
  mtStencilPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_HIDE
void
mt_sampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount) {
  mtSampleCount(pipline->priv, sampleCount);
}

GPU_HIDE
void
mt_initRenderPipeline(GPUApiRender *api) {
  api->newPipeline    = mt_newPipeline;
  api->newRenderState = mt_newRenderState;
  api->setFunction    = mt_setFunction;
  api->colorFormat    = mt_colorFormat;
  api->depthFormat    = mt_depthFormat;
  api->stencilFormat  = mt_stencilFormat;
  api->sampleCount    = mt_sampleCount;
}
