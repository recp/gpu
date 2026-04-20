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
mt_newRenderPipeline(GPUPixelFormat pixelFormat) {
  GPURenderPipeline *pipeline;
  MTLRenderPipelineDescriptor *renderDesc;

  renderDesc = [MTLRenderPipelineDescriptor new];
  renderDesc.colorAttachments[0].pixelFormat = (MTLPixelFormat)pixelFormat;
  pipeline   = calloc(1, sizeof(*pipeline));
  
  pipeline->_priv = renderDesc;
  
  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
mt_newRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPUDeviceMT            *deviceMT;
  GPURenderPipelineState *rederPipline;
  id<MTLRenderPipelineState> mtRederPipline;
  NSError *error;
  
  deviceMT       = device->_priv;
  error = nil;
  mtRederPipline = [deviceMT->device newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)pipeline->_priv
                                                                    error:&error];
  if (!mtRederPipline) {
    NSLog(@"Failed to create render pipeline state: %@", error);
    return NULL;
  }
  rederPipline   = calloc(1, sizeof(*rederPipline));
  
  rederPipline->_priv = mtRederPipline;
  
  return rederPipline;
}

GPU_HIDE
void
mt_setFunction(GPURenderPipeline * __restrict pipline,
               GPUFunction       * __restrict func,
               GPUFunctionType                functype) {
  MTLRenderPipelineDescriptor *desc;
  desc = pipline->_priv;
  switch (functype) {
    case GPU_FUNCTION_VERT:
      desc.vertexFunction = (id<MTLFunction>)func->_priv;
      break;
    case GPU_FUNCTION_FRAG:
      desc.fragmentFunction = (id<MTLFunction>)func->_priv;
      break;
  }
}

GPU_HIDE
void
mt_colorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                 index,
               GPUPixelFormat           pixelFormat) {
  ((MTLRenderPipelineDescriptor *)pipline->_priv).colorAttachments[index].pixelFormat = (MTLPixelFormat)pixelFormat;
}

GPU_HIDE
void
mt_depthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat) {
  ((MTLRenderPipelineDescriptor *)pipline->_priv).depthAttachmentPixelFormat = (MTLPixelFormat)pixelFormat;
}

GPU_HIDE
void
mt_stencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat) {
  ((MTLRenderPipelineDescriptor *)pipline->_priv).stencilAttachmentPixelFormat = (MTLPixelFormat)pixelFormat;
}

GPU_HIDE
void
mt_sampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount) {
  ((MTLRenderPipelineDescriptor *)pipline->_priv).sampleCount = sampleCount;
}

GPU_HIDE
void
mt_initRenderPipeline(GPUApiRender *api) {
  api->newRenderPipeline = mt_newRenderPipeline;
  api->newRenderState    = mt_newRenderState;
  api->setFunction       = mt_setFunction;
  api->colorFormat       = mt_colorFormat;
  api->depthFormat       = mt_depthFormat;
  api->stencilFormat     = mt_stencilFormat;
  api->sampleCount       = mt_sampleCount;
}
