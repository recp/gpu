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
#include "../../../../api/render/pipeline_internal.h"

static MTLCompareFunction
mt_compareFunction(GPUCompareOp op) {
  static const MTLCompareFunction functions[] = {
    [GPU_COMPARE_NEVER]         = MTLCompareFunctionNever,
    [GPU_COMPARE_LESS]          = MTLCompareFunctionLess,
    [GPU_COMPARE_EQUAL]         = MTLCompareFunctionEqual,
    [GPU_COMPARE_LESS_EQUAL]    = MTLCompareFunctionLessEqual,
    [GPU_COMPARE_GREATER]       = MTLCompareFunctionGreater,
    [GPU_COMPARE_NOT_EQUAL]     = MTLCompareFunctionNotEqual,
    [GPU_COMPARE_GREATER_EQUAL] = MTLCompareFunctionGreaterEqual,
    [GPU_COMPARE_ALWAYS]        = MTLCompareFunctionAlways
  };

  return (uint32_t)op < GPU_ARRAY_LEN(functions)
           ? functions[op]
           : MTLCompareFunctionNever;
}

static MTLStencilOperation
mt_stencilOperation(GPUStencilOp op) {
  static const MTLStencilOperation operations[] = {
    [GPU_STENCIL_OP_KEEP]            = MTLStencilOperationKeep,
    [GPU_STENCIL_OP_ZERO]            = MTLStencilOperationZero,
    [GPU_STENCIL_OP_REPLACE]         = MTLStencilOperationReplace,
    [GPU_STENCIL_OP_INCREMENT_CLAMP] = MTLStencilOperationIncrementClamp,
    [GPU_STENCIL_OP_DECREMENT_CLAMP] = MTLStencilOperationDecrementClamp,
    [GPU_STENCIL_OP_INVERT]          = MTLStencilOperationInvert,
    [GPU_STENCIL_OP_INCREMENT_WRAP]  = MTLStencilOperationIncrementWrap,
    [GPU_STENCIL_OP_DECREMENT_WRAP]  = MTLStencilOperationDecrementWrap
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : MTLStencilOperationKeep;
}

static void
mt_fillStencilDescriptor(MTLStencilDescriptor      *desc,
                         const GPUStencilFaceState *state,
                         uint32_t                   readMask,
                         uint32_t                   writeMask) {
  desc.stencilCompareFunction    = mt_compareFunction(state->compare);
  desc.stencilFailureOperation   = mt_stencilOperation(state->failOp);
  desc.depthFailureOperation     = mt_stencilOperation(state->depthFailOp);
  desc.depthStencilPassOperation = mt_stencilOperation(state->passOp);
  desc.readMask                  = readMask;
  desc.writeMask                 = writeMask;
}

GPU_HIDE
GPURenderPipeline*
mt_newRenderPipeline(GPUPixelFormat pixelFormat) {
  GPURenderPipeline            *pipeline;
  MTLRenderPipelineDescriptor *renderDesc;

  renderDesc = [MTLRenderPipelineDescriptor new];
  renderDesc.colorAttachments[0].pixelFormat = (MTLPixelFormat)pixelFormat;
  pipeline = calloc(1, sizeof(*pipeline));
  pipeline->_priv = renderDesc;
  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
mt_newRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPUDeviceMT                *deviceMT;
  GPURenderPipelineState     *renderPipeline;
  MTRenderPipelineState      *native;
  MTLDepthStencilDescriptor  *depthDesc;
  MTLStencilDescriptor       *frontDesc;
  MTLStencilDescriptor       *backDesc;
  NSError                    *error;
  
  deviceMT = device->_priv;
  error    = nil;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  native->render = [deviceMT->device
    newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)pipeline->_priv
                                   error:&error];
  if (!native->render) {
    NSLog(@"Failed to create render pipeline state: %@", error);
    free(native);
    return NULL;
  }

  depthDesc = [MTLDepthStencilDescriptor new];
  depthDesc.depthCompareFunction = pipeline->_depthStencilState.depthTestEnable
                                     ? mt_compareFunction(
                                         pipeline->_depthStencilState.depthCompare
                                       )
                                     : MTLCompareFunctionAlways;
  depthDesc.depthWriteEnabled = pipeline->_depthStencilState.depthWriteEnable;
  if (pipeline->_depthStencilState.stencilTestEnable) {
    frontDesc = [MTLStencilDescriptor new];
    backDesc  = [MTLStencilDescriptor new];
    mt_fillStencilDescriptor(frontDesc,
                             &pipeline->_depthStencilState.front,
                             pipeline->_depthStencilState.stencilReadMask,
                             pipeline->_depthStencilState.stencilWriteMask);
    mt_fillStencilDescriptor(backDesc,
                             &pipeline->_depthStencilState.back,
                             pipeline->_depthStencilState.stencilReadMask,
                             pipeline->_depthStencilState.stencilWriteMask);
    depthDesc.frontFaceStencil = frontDesc;
    depthDesc.backFaceStencil  = backDesc;
    [frontDesc release];
    [backDesc release];
  }
  native->depthStencil = [deviceMT->device
    newDepthStencilStateWithDescriptor:depthDesc];
  [depthDesc release];
  if (!native->depthStencil) {
    [native->render release];
    free(native);
    return NULL;
  }

  renderPipeline = calloc(1, sizeof(*renderPipeline));
  if (!renderPipeline) {
    [native->depthStencil release];
    [native->render release];
    free(native);
    return NULL;
  }

  renderPipeline->_priv = native;
  pipeline->_state      = native;
  return renderPipeline;
}

GPU_HIDE
void
mt_destroyRenderPipeline(GPURenderPipeline *pipeline) {
  if (!pipeline) {
    return;
  }

  if (pipeline->_state) {
    MTRenderPipelineState *native = pipeline->_state;

    [native->depthStencil release];
    [native->render release];
    free(native);
  }
  if (pipeline->_priv) {
    [(id)pipeline->_priv release];
  }
  free(pipeline);
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
  ((MTLRenderPipelineDescriptor *)pipline->_priv).rasterSampleCount =
    sampleCount;
}

GPU_HIDE
void
mt_initRenderPipeline(GPUApiRender *api) {
  api->newRenderPipeline     = mt_newRenderPipeline;
  api->newRenderState        = mt_newRenderState;
  api->destroyRenderPipeline = mt_destroyRenderPipeline;
  api->setFunction           = mt_setFunction;
  api->colorFormat           = mt_colorFormat;
  api->depthFormat           = mt_depthFormat;
  api->stencilFormat         = mt_stencilFormat;
  api->sampleCount           = mt_sampleCount;
}
