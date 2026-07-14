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
#include "../pipeline_cache.h"

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

static MTLPrimitiveTopologyClass
mt_topologyClass(GPUPrimitiveTopology topology) {
  switch (topology) {
    case GPU_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MTLPrimitiveTopologyClassPoint;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MTLPrimitiveTopologyClassLine;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    default:
      return MTLPrimitiveTopologyClassTriangle;
  }
}

static MTLBlendFactor
mt_blendFactor(GPUBlendFactor factor) {
  static const MTLBlendFactor factors[] = {
    [GPU_BLEND_FACTOR_ZERO]                = MTLBlendFactorZero,
    [GPU_BLEND_FACTOR_ONE]                 = MTLBlendFactorOne,
    [GPU_BLEND_FACTOR_SRC_ALPHA]           = MTLBlendFactorSourceAlpha,
    [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = MTLBlendFactorOneMinusSourceAlpha
  };

  return (uint32_t)factor < GPU_ARRAY_LEN(factors)
           ? factors[factor]
           : MTLBlendFactorZero;
}

static MTLBlendOperation
mt_blendOperation(GPUBlendOp op) {
  static const MTLBlendOperation operations[] = {
    [GPU_BLEND_OP_ADD]              = MTLBlendOperationAdd,
    [GPU_BLEND_OP_SUBTRACT]         = MTLBlendOperationSubtract,
    [GPU_BLEND_OP_REVERSE_SUBTRACT] = MTLBlendOperationReverseSubtract,
    [GPU_BLEND_OP_MIN]              = MTLBlendOperationMin,
    [GPU_BLEND_OP_MAX]              = MTLBlendOperationMax
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : MTLBlendOperationAdd;
}

static MTLColorWriteMask
mt_colorWriteMask(GPUColorWriteMaskFlags mask) {
  MTLColorWriteMask result;

  if (mask == GPU_COLOR_WRITE_DEFAULT) {
    return MTLColorWriteMaskAll;
  }
  if (mask == GPU_COLOR_WRITE_NONE) {
    return MTLColorWriteMaskNone;
  }

  result = MTLColorWriteMaskNone;
  if ((mask & GPU_COLOR_WRITE_R) != 0u) result |= MTLColorWriteMaskRed;
  if ((mask & GPU_COLOR_WRITE_G) != 0u) result |= MTLColorWriteMaskGreen;
  if ((mask & GPU_COLOR_WRITE_B) != 0u) result |= MTLColorWriteMaskBlue;
  if ((mask & GPU_COLOR_WRITE_A) != 0u) result |= MTLColorWriteMaskAlpha;
  return result;
}

static void
mt_fillBlendDescriptor(MTLRenderPipelineColorAttachmentDescriptor *desc,
                       const GPUBlendState                         *blend) {
  desc.blendingEnabled             = blend->enabled;
  desc.sourceRGBBlendFactor        = mt_blendFactor(blend->color.srcFactor);
  desc.destinationRGBBlendFactor   = mt_blendFactor(blend->color.dstFactor);
  desc.rgbBlendOperation           = mt_blendOperation(blend->color.op);
  desc.sourceAlphaBlendFactor      = mt_blendFactor(blend->alpha.srcFactor);
  desc.destinationAlphaBlendFactor = mt_blendFactor(blend->alpha.dstFactor);
  desc.alphaBlendOperation         = mt_blendOperation(blend->alpha.op);
  desc.writeMask                   = mt_colorWriteMask(blend->writeMask);
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
mt_newRenderPipeline(GPUFormat pixelFormat, bool mesh) {
  GPURenderPipeline *pipeline;
  id                 renderDesc;

  if (mesh) {
    if (@available(macOS 13.0, iOS 16.0, *)) {
      renderDesc = [MTLMeshRenderPipelineDescriptor new];
    } else {
      return NULL;
    }
  } else {
    renderDesc = [MTLRenderPipelineDescriptor new];
  }
  if (pixelFormat != GPU_FORMAT_UNDEFINED) {
    if (mesh) {
      ((MTLMeshRenderPipelineDescriptor *)renderDesc)
        .colorAttachments[0].pixelFormat = mt_format(pixelFormat);
    } else {
      ((MTLRenderPipelineDescriptor *)renderDesc)
        .colorAttachments[0].pixelFormat = mt_format(pixelFormat);
    }
  }
  pipeline = calloc(1, sizeof(*pipeline));
  if (!pipeline) {
    [renderDesc release];
    return NULL;
  }
  pipeline->_priv = renderDesc;
  pipeline->_mesh = mesh;
  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
mt_newRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPUDeviceMT                 *deviceMT;
  GPURenderPipelineState      *renderPipeline;
  MTRenderPipelineState       *native;
  MTLRenderPipelineDescriptor *renderDesc;
  MTLMeshRenderPipelineDescriptor *meshDesc;
  MTLDepthStencilDescriptor   *depthDesc;
  MTLStencilDescriptor        *frontDesc;
  MTLStencilDescriptor        *backDesc;
  NSError                     *error;
  uint32_t                     i;
  bool                         usesArchive;
  
  deviceMT = device->_priv;
  error    = nil;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  renderDesc  = pipeline->_mesh ? nil : pipeline->_priv;
  meshDesc    = pipeline->_mesh ? pipeline->_priv : nil;
  usesArchive = false;
  if (meshDesc) {
    if (@available(macOS 13.0, iOS 16.0, *)) {
      meshDesc.alphaToCoverageEnabled = pipeline->_alphaToCoverageEnable;
      meshDesc.maxTotalThreadsPerMeshThreadgroup =
        pipeline->_meshWorkgroupSize[0] *
        pipeline->_meshWorkgroupSize[1] *
        pipeline->_meshWorkgroupSize[2];
      if (pipeline->_task) {
        meshDesc.maxTotalThreadsPerObjectThreadgroup =
          pipeline->_taskWorkgroupSize[0] *
          pipeline->_taskWorkgroupSize[1] *
          pipeline->_taskWorkgroupSize[2];
      }
      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        mt_fillBlendDescriptor(meshDesc.colorAttachments[i],
                               &pipeline->_colorTargetBlends[i]);
      }
      native->render = [deviceMT->device
        newRenderPipelineStateWithMeshDescriptor:meshDesc
                                         options:MTLPipelineOptionNone
                                      reflection:nil
                                           error:&error];
    }
  } else {
    renderDesc.inputPrimitiveTopology =
      mt_topologyClass(pipeline->_primitiveTopology);
    renderDesc.alphaToCoverageEnabled = pipeline->_alphaToCoverageEnable;
    for (i = 0u; i < pipeline->_colorTargetCount; i++) {
      mt_fillBlendDescriptor(renderDesc.colorAttachments[i],
                             &pipeline->_colorTargetBlends[i]);
    }
    usesArchive = mt_useRenderCache(pipeline->_cache, renderDesc);
    if (usesArchive) {
      native->render = [deviceMT->device
        newRenderPipelineStateWithDescriptor:renderDesc
                                     options:
                                       MTLPipelineOptionFailOnBinaryArchiveMiss
                                  reflection:nil
                                       error:&error];
      if (!native->render) {
        mt_addRenderCache(pipeline->_cache, renderDesc);
        error = nil;
      }
    }
    if (!native->render) {
      native->render = [deviceMT->device
        newRenderPipelineStateWithDescriptor:renderDesc
                                       error:&error];
    }
  }
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
  native->mesh          = pipeline->_mesh;
  native->task          = pipeline->_task;
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
               GPUShaderFunction * __restrict func,
               GPUFunctionType                functype) {
  MTLRenderPipelineDescriptor     *desc;
  MTLMeshRenderPipelineDescriptor *meshDesc;

  desc     = pipline->_mesh ? nil : pipline->_priv;
  meshDesc = pipline->_mesh ? pipline->_priv : nil;
  switch (functype) {
    case GPU_FUNCTION_VERT:
      desc.vertexFunction = (id<MTLFunction>)func->_priv;
      break;
    case GPU_FUNCTION_FRAG:
      if (meshDesc) {
        meshDesc.fragmentFunction = (id<MTLFunction>)func->_priv;
      } else {
        desc.fragmentFunction = (id<MTLFunction>)func->_priv;
      }
      break;
    case GPU_FUNCTION_TASK:
      meshDesc.objectFunction = (id<MTLFunction>)func->_priv;
      break;
    case GPU_FUNCTION_MESH:
      meshDesc.meshFunction = (id<MTLFunction>)func->_priv;
      break;
  }
}

GPU_HIDE
void
mt_colorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUFormat                      pixelFormat) {
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)pipline->_priv)
      .colorAttachments[index].pixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)pipline->_priv)
      .colorAttachments[index].pixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_depthFormat(GPURenderPipeline * __restrict pipline,
               GPUFormat                      pixelFormat) {
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)pipline->_priv)
      .depthAttachmentPixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)pipline->_priv)
      .depthAttachmentPixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_stencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUFormat                      pixelFormat) {
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)pipline->_priv)
      .stencilAttachmentPixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)pipline->_priv)
      .stencilAttachmentPixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_sampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount) {
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)pipline->_priv).rasterSampleCount =
      sampleCount;
  } else {
    ((MTLRenderPipelineDescriptor *)pipline->_priv).rasterSampleCount =
      sampleCount;
  }
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
