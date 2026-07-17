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

#if MT_HAS_METAL4
static void
mt_fillBlendDescriptor4(MTL4RenderPipelineColorAttachmentDescriptor *desc,
                        const GPUBlendState                          *blend) {
  desc.blendingState             = blend->enabled
                                     ? MTL4BlendStateEnabled
                                     : MTL4BlendStateDisabled;
  desc.sourceRGBBlendFactor      = mt_blendFactor(blend->color.srcFactor);
  desc.destinationRGBBlendFactor = mt_blendFactor(blend->color.dstFactor);
  desc.rgbBlendOperation         = mt_blendOperation(blend->color.op);
  desc.sourceAlphaBlendFactor    = mt_blendFactor(blend->alpha.srcFactor);
  desc.destinationAlphaBlendFactor = mt_blendFactor(blend->alpha.dstFactor);
  desc.alphaBlendOperation         = mt_blendOperation(blend->alpha.op);
  desc.writeMask                   = mt_colorWriteMask(blend->writeMask);
}

static MTL4LibraryFunctionDescriptor *
mt_functionDescriptor4(const MTShaderFunction *function) {
  if (!function || !function->library || !function->name) {
    return nil;
  }
  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTL4LibraryFunctionDescriptor *descriptor;

    descriptor         = [MTL4LibraryFunctionDescriptor new];
    descriptor.library = function->library;
    descriptor.name    = function->name;
    return descriptor;
  }
  return nil;
}

static id
mt_renderDescriptor4(const GPURenderPipeline    *pipeline,
                     const MTRenderPipelineDesc *native) {
  uint32_t i;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if (pipeline->_mesh) {
      MTL4MeshRenderPipelineDescriptor *descriptor;

      descriptor                            = [MTL4MeshRenderPipelineDescriptor new];
      descriptor.objectFunctionDescriptor   = native->taskFunction;
      descriptor.meshFunctionDescriptor     = native->meshFunction;
      descriptor.fragmentFunctionDescriptor = native->fragmentFunction;
      descriptor.rasterSampleCount          = pipeline->_sampleCount;
      descriptor.alphaToCoverageState       =
        pipeline->_alphaToCoverageEnable
          ? MTL4AlphaToCoverageStateEnabled
          : MTL4AlphaToCoverageStateDisabled;
      descriptor.maxTotalThreadsPerMeshThreadgroup =
        pipeline->_meshWorkgroupSize[0] *
        pipeline->_meshWorkgroupSize[1] *
        pipeline->_meshWorkgroupSize[2];
      if (pipeline->_task) {
        descriptor.maxTotalThreadsPerObjectThreadgroup =
          pipeline->_taskWorkgroupSize[0] *
          pipeline->_taskWorkgroupSize[1] *
          pipeline->_taskWorkgroupSize[2];
      }
      descriptor.payloadMemoryLength = pipeline->_payloadSizeBytes;
      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        MTL4RenderPipelineColorAttachmentDescriptor *attachment;

        attachment             = descriptor.colorAttachments[i];
        attachment.pixelFormat = mt_format(pipeline->_colorTargetFormats[i]);
        mt_fillBlendDescriptor4(attachment,
                                &pipeline->_colorTargetBlends[i]);
      }
      return descriptor;
    } else {
      MTL4RenderPipelineDescriptor *descriptor;
      MTLRenderPipelineDescriptor  *classic;

      classic                               = native->classic;
      descriptor                            = [MTL4RenderPipelineDescriptor new];
      descriptor.vertexFunctionDescriptor   = native->vertexFunction;
      descriptor.fragmentFunctionDescriptor = native->fragmentFunction;
      descriptor.vertexDescriptor           = classic.vertexDescriptor;
      descriptor.rasterSampleCount          = pipeline->_sampleCount;
      descriptor.inputPrimitiveTopology     =
        mt_topologyClass(pipeline->_primitiveTopology);
      descriptor.alphaToCoverageState       =
        pipeline->_alphaToCoverageEnable
          ? MTL4AlphaToCoverageStateEnabled
          : MTL4AlphaToCoverageStateDisabled;
      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        MTL4RenderPipelineColorAttachmentDescriptor *attachment;

        attachment             = descriptor.colorAttachments[i];
        attachment.pixelFormat = mt_format(pipeline->_colorTargetFormats[i]);
        mt_fillBlendDescriptor4(attachment,
                                &pipeline->_colorTargetBlends[i]);
      }
      return descriptor;
    }
  }
  return nil;
}
#endif

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
  MTRenderPipelineDesc *native;
  GPURenderPipeline    *pipeline;
  id                    renderDesc;

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
  native   = calloc(1, sizeof(*native));
  pipeline = calloc(1, sizeof(*pipeline));
  if (!native || !pipeline) {
    free(native);
    free(pipeline);
    [renderDesc release];
    return NULL;
  }
  native->classic  = renderDesc;
  pipeline->_priv = native;
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
  MTRenderPipelineDesc        *pipelineDesc;
  MTLRenderPipelineDescriptor *renderDesc;
  MTLMeshRenderPipelineDescriptor *meshDesc;
  MTLDepthStencilDescriptor   *depthDesc;
  MTLStencilDescriptor        *frontDesc;
  MTLStencilDescriptor        *backDesc;
  NSError                     *error;
  uint32_t                     i;
  bool                         usesArchive;
#if MT_HAS_METAL4
  id                           renderDesc4;
#endif
  
  deviceMT = device->_priv;
  error    = nil;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return NULL;
  }
  pipelineDesc = pipeline->_priv;
  if (!deviceMT || !pipelineDesc) {
    free(native);
    return NULL;
  }
  renderDesc  = pipeline->_mesh ? nil : pipelineDesc->classic;
  meshDesc    = pipeline->_mesh ? pipelineDesc->classic : nil;
  usesArchive = false;
  if (deviceMT->commandMode == MTCommandMode4) {
#if MT_HAS_METAL4
    renderDesc4 = mt_renderDescriptor4(pipeline, pipelineDesc);
    native->render = mt_compileRenderPipeline4(pipeline->_cache,
                                                deviceMT,
                                                renderDesc4,
                                                &error);
    [renderDesc4 release];
#endif
  } else if (meshDesc) {
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
      meshDesc.payloadMemoryLength = pipeline->_payloadSizeBytes;
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
    MTRenderPipelineDesc *native;

    native = pipeline->_priv;
    [native->meshFunction release];
    [native->taskFunction release];
    [native->fragmentFunction release];
    [native->vertexFunction release];
    [native->classic release];
    free(native);
  }
  free(pipeline);
}

GPU_HIDE
void
mt_setFunction(GPURenderPipeline * __restrict pipline,
               GPUShaderFunction * __restrict func,
               GPUFunctionType                functype) {
  MTRenderPipelineDesc            *native;
  MTShaderFunction                *function;
  MTLRenderPipelineDescriptor     *desc;
  MTLMeshRenderPipelineDescriptor *meshDesc;

  native   = pipline->_priv;
  function = func->_priv;
  desc     = pipline->_mesh ? nil : native->classic;
  meshDesc = pipline->_mesh ? native->classic : nil;
  switch (functype) {
    case GPU_FUNCTION_VERT:
      desc.vertexFunction = function->function;
#if MT_HAS_METAL4
      [native->vertexFunction release];
      native->vertexFunction = mt_functionDescriptor4(function);
#endif
      break;
    case GPU_FUNCTION_FRAG:
      if (meshDesc) {
        meshDesc.fragmentFunction = function->function;
      } else {
        desc.fragmentFunction = function->function;
      }
#if MT_HAS_METAL4
      [native->fragmentFunction release];
      native->fragmentFunction = mt_functionDescriptor4(function);
#endif
      break;
    case GPU_FUNCTION_TASK:
      meshDesc.objectFunction = function->function;
#if MT_HAS_METAL4
      [native->taskFunction release];
      native->taskFunction = mt_functionDescriptor4(function);
#endif
      break;
    case GPU_FUNCTION_MESH:
      meshDesc.meshFunction = function->function;
#if MT_HAS_METAL4
      [native->meshFunction release];
      native->meshFunction = mt_functionDescriptor4(function);
#endif
      break;
  }
}

GPU_HIDE
void
mt_colorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUFormat                      pixelFormat) {
  MTRenderPipelineDesc *native;

  native = pipline->_priv;
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)native->classic)
      .colorAttachments[index].pixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)native->classic)
      .colorAttachments[index].pixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_depthFormat(GPURenderPipeline * __restrict pipline,
               GPUFormat                      pixelFormat) {
  MTRenderPipelineDesc *native;

  native = pipline->_priv;
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)native->classic)
      .depthAttachmentPixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)native->classic)
      .depthAttachmentPixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_stencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUFormat                      pixelFormat) {
  MTRenderPipelineDesc *native;

  native = pipline->_priv;
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)native->classic)
      .stencilAttachmentPixelFormat = mt_format(pixelFormat);
  } else {
    ((MTLRenderPipelineDescriptor *)native->classic)
      .stencilAttachmentPixelFormat = mt_format(pixelFormat);
  }
}

GPU_HIDE
void
mt_sampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount) {
  MTRenderPipelineDesc *native;

  native = pipline->_priv;
  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)native->classic).rasterSampleCount =
      sampleCount;
  } else {
    ((MTLRenderPipelineDescriptor *)native->classic).rasterSampleCount =
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
