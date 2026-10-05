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

static const MTLCompareFunction mt_compareFunctions[] = {
  [GPU_COMPARE_NEVER]         = MTLCompareFunctionNever,
  [GPU_COMPARE_LESS]          = MTLCompareFunctionLess,
  [GPU_COMPARE_EQUAL]         = MTLCompareFunctionEqual,
  [GPU_COMPARE_LESS_EQUAL]    = MTLCompareFunctionLessEqual,
  [GPU_COMPARE_GREATER]       = MTLCompareFunctionGreater,
  [GPU_COMPARE_NOT_EQUAL]     = MTLCompareFunctionNotEqual,
  [GPU_COMPARE_GREATER_EQUAL] = MTLCompareFunctionGreaterEqual,
  [GPU_COMPARE_ALWAYS]        = MTLCompareFunctionAlways
};

static const MTLStencilOperation mt_stencilOperations[] = {
  [GPU_STENCIL_OP_KEEP]            = MTLStencilOperationKeep,
  [GPU_STENCIL_OP_ZERO]            = MTLStencilOperationZero,
  [GPU_STENCIL_OP_REPLACE]         = MTLStencilOperationReplace,
  [GPU_STENCIL_OP_INCREMENT_CLAMP] = MTLStencilOperationIncrementClamp,
  [GPU_STENCIL_OP_DECREMENT_CLAMP] = MTLStencilOperationDecrementClamp,
  [GPU_STENCIL_OP_INVERT]          = MTLStencilOperationInvert,
  [GPU_STENCIL_OP_INCREMENT_WRAP]  = MTLStencilOperationIncrementWrap,
  [GPU_STENCIL_OP_DECREMENT_WRAP]  = MTLStencilOperationDecrementWrap
};

static const MTLBlendFactor mt_blendFactors[] = {
  [GPU_BLEND_FACTOR_ZERO]                = MTLBlendFactorZero,
  [GPU_BLEND_FACTOR_ONE]                 = MTLBlendFactorOne,
  [GPU_BLEND_FACTOR_SRC_ALPHA]           = MTLBlendFactorSourceAlpha,
  [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = MTLBlendFactorOneMinusSourceAlpha
};

static const MTLBlendOperation mt_blendOperations[] = {
  [GPU_BLEND_OP_ADD]              = MTLBlendOperationAdd,
  [GPU_BLEND_OP_SUBTRACT]         = MTLBlendOperationSubtract,
  [GPU_BLEND_OP_REVERSE_SUBTRACT] = MTLBlendOperationReverseSubtract,
  [GPU_BLEND_OP_MIN]              = MTLBlendOperationMin,
  [GPU_BLEND_OP_MAX]              = MTLBlendOperationMax
};

static MTLCompareFunction
mt_compareFunction(GPUCompareOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(mt_compareFunctions) ? mt_compareFunctions[op] : MTLCompareFunctionNever;
}

static MTLStencilOperation
mt_stencilOperation(GPUStencilOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(mt_stencilOperations) ? mt_stencilOperations[op] : MTLStencilOperationKeep;
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
  return (uint32_t)factor < GPU_ARRAY_LEN(mt_blendFactors) ? mt_blendFactors[factor] : MTLBlendFactorZero;
}

static MTLBlendOperation
mt_blendOperation(GPUBlendOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(mt_blendOperations) ? mt_blendOperations[op] : MTLBlendOperationAdd;
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

  if ((mask & GPU_COLOR_WRITE_R) != 0u)
    result |= MTLColorWriteMaskRed;

  if ((mask & GPU_COLOR_WRITE_G) != 0u)
    result |= MTLColorWriteMaskGreen;

  if ((mask & GPU_COLOR_WRITE_B) != 0u)
    result |= MTLColorWriteMaskBlue;

  if ((mask & GPU_COLOR_WRITE_A) != 0u)
    result |= MTLColorWriteMaskAlpha;

  return result;
}

static void
mt_fillBlendDescriptor(MTLRenderPipelineColorAttachmentDescriptor *desc,
                       const GPUBlendState                        *blend) {
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
                        const GPUBlendState                         *blend) {
  desc.blendingState               = blend->enabled ? MTL4BlendStateEnabled : MTL4BlendStateDisabled;
  desc.sourceRGBBlendFactor        = mt_blendFactor(blend->color.srcFactor);
  desc.destinationRGBBlendFactor   = mt_blendFactor(blend->color.dstFactor);
  desc.rgbBlendOperation           = mt_blendOperation(blend->color.op);
  desc.sourceAlphaBlendFactor      = mt_blendFactor(blend->alpha.srcFactor);
  desc.destinationAlphaBlendFactor = mt_blendFactor(blend->alpha.dstFactor);
  desc.alphaBlendOperation         = mt_blendOperation(blend->alpha.op);
  desc.writeMask                   = mt_colorWriteMask(blend->writeMask);
}

static MTL4LibraryFunctionDescriptor*
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
  MTLRenderPipelineDescriptor *classic;
  uint32_t                     i;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if (pipeline->_mesh) {
      MTL4MeshRenderPipelineDescriptor *meshDescriptor;

      meshDescriptor                            = [MTL4MeshRenderPipelineDescriptor new];
      meshDescriptor.objectFunctionDescriptor   = native->taskFunction;
      meshDescriptor.meshFunctionDescriptor     = native->meshFunction;
      meshDescriptor.fragmentFunctionDescriptor = native->fragmentFunction;

      if (native->fragmentIntersectionFunctions4.count > 0u) {
        MTL4StaticLinkingDescriptor *meshLinking;

        meshLinking                                    = [MTL4StaticLinkingDescriptor new];
        meshLinking.functionDescriptors                = native->fragmentIntersectionFunctions4;
        meshDescriptor.fragmentStaticLinkingDescriptor = meshLinking;
        [meshLinking release];
      }

      meshDescriptor.rasterSampleCount                 = pipeline->_sampleCount;
      meshDescriptor.alphaToCoverageState              = pipeline->_alphaToCoverageEnable
                                                         ? MTL4AlphaToCoverageStateEnabled
                                                         : MTL4AlphaToCoverageStateDisabled;
      meshDescriptor.maxTotalThreadsPerMeshThreadgroup = pipeline->_meshWorkgroupSize[0] *
                                                         pipeline->_meshWorkgroupSize[1] *
                                                         pipeline->_meshWorkgroupSize[2];

      if (pipeline->_task) {
        meshDescriptor.maxTotalThreadsPerObjectThreadgroup = pipeline->_taskWorkgroupSize[0] *
                                                             pipeline->_taskWorkgroupSize[1] *
                                                             pipeline->_taskWorkgroupSize[2];
      }

      meshDescriptor.payloadMemoryLength = pipeline->_payloadSizeBytes;

      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        MTL4RenderPipelineColorAttachmentDescriptor *meshAttachment;

        meshAttachment             = meshDescriptor.colorAttachments[i];
        meshAttachment.pixelFormat = mt_format(pipeline->_colorTargetFormats[i]);
        mt_fillBlendDescriptor4(meshAttachment,
                                &pipeline->_colorTargetBlends[i]);
      }

      return meshDescriptor;
    } else {
      MTL4RenderPipelineDescriptor *vertexDescriptor;

      classic                                     = native->classic;
      vertexDescriptor                            = [MTL4RenderPipelineDescriptor new];
      vertexDescriptor.vertexFunctionDescriptor   = native->vertexFunction;
      vertexDescriptor.fragmentFunctionDescriptor = native->fragmentFunction;

      if (native->vertexIntersectionFunctions4.count > 0u) {
        MTL4StaticLinkingDescriptor *vertexLinking;

        vertexLinking                                  = [MTL4StaticLinkingDescriptor new];
        vertexLinking.functionDescriptors              = native->vertexIntersectionFunctions4;
        vertexDescriptor.vertexStaticLinkingDescriptor = vertexLinking;
        [vertexLinking release];
      }

      if (native->fragmentIntersectionFunctions4.count > 0u) {
        MTL4StaticLinkingDescriptor *fragmentLinking;

        fragmentLinking                                  = [MTL4StaticLinkingDescriptor new];
        fragmentLinking.functionDescriptors              = native->fragmentIntersectionFunctions4;
        vertexDescriptor.fragmentStaticLinkingDescriptor = fragmentLinking;
        [fragmentLinking release];
      }

      vertexDescriptor.vertexDescriptor       = classic.vertexDescriptor;
      vertexDescriptor.rasterSampleCount      = pipeline->_sampleCount;
      vertexDescriptor.inputPrimitiveTopology = mt_topologyClass(pipeline->_primitiveTopology);
      vertexDescriptor.alphaToCoverageState   = pipeline->_alphaToCoverageEnable
                                                ? MTL4AlphaToCoverageStateEnabled
                                                : MTL4AlphaToCoverageStateDisabled;

      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        MTL4RenderPipelineColorAttachmentDescriptor *vertexAttachment;

        vertexAttachment             = vertexDescriptor.colorAttachments[i];
        vertexAttachment.pixelFormat = mt_format(pipeline->_colorTargetFormats[i]);
        mt_fillBlendDescriptor4(vertexAttachment,
                                &pipeline->_colorTargetBlends[i]);
      }

      return vertexDescriptor;
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

static GPUResult
mt_setIntersectionFunctions(GPURenderPipeline         *pipeline,
                            GPUShaderFunction  *const *functions,
                            const GPUShaderStageFlags *stages,
                            uint32_t                   functionCount) {
  MTRenderPipelineDesc        *native;
  NSMutableArray              *vertexFunctions;
  NSMutableArray              *fragmentFunctions;
  NSMutableArray              *vertexFunctions4;
  NSMutableArray              *fragmentFunctions4;
  MTShaderFunction            *function;
  NSMutableArray              *target;
  NSMutableArray              *target4;
  MTLLinkedFunctions          *meshLinked;
  MTLRenderPipelineDescriptor *descriptor;
  MTLLinkedFunctions          *vertexLinked;
  MTLLinkedFunctions          *fragmentLinked;
  uint32_t                     i;

  if (!pipeline || !(native = pipeline->_priv) || !functions || !stages
      || functionCount == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  vertexFunctions    = [[NSMutableArray alloc] initWithCapacity:functionCount];
  fragmentFunctions  = [[NSMutableArray alloc] initWithCapacity:functionCount];
  vertexFunctions4   = nil;
  fragmentFunctions4 = nil;
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    vertexFunctions4   = [[NSMutableArray alloc] initWithCapacity:functionCount];
    fragmentFunctions4 = [[NSMutableArray alloc] initWithCapacity:functionCount];
  }
#endif

  for (i = 0u; i < functionCount; i++) {
    function = functions[i] ? functions[i]->_priv : NULL;

    if (!function || !function->function
        || (stages[i] != GPU_SHADER_STAGE_VERTEX_BIT
            && stages[i] != GPU_SHADER_STAGE_FRAGMENT_BIT)
        || (pipeline->_mesh && stages[i] == GPU_SHADER_STAGE_VERTEX_BIT)) {
      [fragmentFunctions4 release];
      [vertexFunctions4 release];
      [fragmentFunctions release];
      [vertexFunctions release];
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    target  = stages[i] == GPU_SHADER_STAGE_VERTEX_BIT ? vertexFunctions : fragmentFunctions;
    target4 = stages[i] == GPU_SHADER_STAGE_VERTEX_BIT ? vertexFunctions4 : fragmentFunctions4;
    [target addObject:function->function];
#if MT_HAS_METAL4
    if (target4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        MTL4LibraryFunctionDescriptor *function4;

        function4         = [MTL4LibraryFunctionDescriptor new];
        function4.library = function->library;
        function4.name    = function->name;
        [target4 addObject:function4];
        [function4 release];
      }
    }
#endif
  }

  if (pipeline->_mesh && fragmentFunctions.count > 0u) {
    if (@available(macOS 14.0, iOS 17.0, *)) {
      meshLinked           = [MTLLinkedFunctions new];
      meshLinked.functions = fragmentFunctions;
      ((MTLMeshRenderPipelineDescriptor *)native->classic)
        .fragmentLinkedFunctions = meshLinked;
      [meshLinked release];
    } else {
      [fragmentFunctions4 release];
      [vertexFunctions4 release];
      [fragmentFunctions release];
      [vertexFunctions release];
      return GPU_ERROR_UNSUPPORTED;
    }
  } else if (!pipeline->_mesh) {
    descriptor = native->classic;

    if (vertexFunctions.count > 0u) {
      vertexLinked                     = [MTLLinkedFunctions new];
      vertexLinked.functions           = vertexFunctions;
      descriptor.vertexLinkedFunctions = vertexLinked;
      [vertexLinked release];
    }

    if (fragmentFunctions.count > 0u) {
      fragmentLinked                     = [MTLLinkedFunctions new];
      fragmentLinked.functions           = fragmentFunctions;
      descriptor.fragmentLinkedFunctions = fragmentLinked;
      [fragmentLinked release];
    }
  }

  [native->fragmentIntersectionFunctions4 release];
  [native->vertexIntersectionFunctions4 release];
  [native->fragmentIntersectionFunctions release];
  [native->vertexIntersectionFunctions release];
  native->vertexIntersectionFunctions    = [vertexFunctions copy];
  native->fragmentIntersectionFunctions  = [fragmentFunctions copy];
  native->vertexIntersectionFunctions4   = [vertexFunctions4 copy];
  native->fragmentIntersectionFunctions4 = [fragmentFunctions4 copy];
  [fragmentFunctions4 release];
  [vertexFunctions4 release];
  [fragmentFunctions release];
  [vertexFunctions release];

  return GPU_OK;
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

  native->classic = renderDesc;
  pipeline->_priv = native;
  pipeline->_mesh = mesh;

  return pipeline;
}

GPU_HIDE
GPURenderPipelineState*
mt_newRenderState(GPUDevice         *__restrict device,
                  GPURenderPipeline *__restrict pipeline) {
  GPUDeviceMT                     *deviceMT;
  GPURenderPipelineState          *renderPipeline;
  MTRenderPipelineState           *native;
  MTRenderPipelineDesc            *pipelineDesc;
  MTLRenderPipelineDescriptor     *renderDesc;
  MTLMeshRenderPipelineDescriptor *meshDesc;
  MTLDepthStencilDescriptor       *depthDesc;
  MTLStencilDescriptor            *frontDesc;
  MTLStencilDescriptor            *backDesc;
  NSError                         *error;
#if MT_HAS_METAL4
  id                               renderDesc4;
#endif
  uint32_t                         i;
  bool                             usesArchive;

  deviceMT = device->_priv;
  error    = nil;

  if (!(native = calloc(1, sizeof(*native)))) {
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
    renderDesc4    = mt_renderDescriptor4(pipeline, pipelineDesc);
    native->render = mt_compileRenderPipeline4(pipeline->_cache,
                                               deviceMT,
                                               renderDesc4,
                                               &error);
    [renderDesc4 release];
#endif
  } else if (meshDesc) {
    if (@available(macOS 13.0, iOS 16.0, *)) {
      meshDesc.alphaToCoverageEnabled            = pipeline->_alphaToCoverageEnable;
      meshDesc.maxTotalThreadsPerMeshThreadgroup = pipeline->_meshWorkgroupSize[0] *
                                                   pipeline->_meshWorkgroupSize[1] *
                                                   pipeline->_meshWorkgroupSize[2];

      if (pipeline->_task) {
        meshDesc.maxTotalThreadsPerObjectThreadgroup = pipeline->_taskWorkgroupSize[0] *
                                                       pipeline->_taskWorkgroupSize[1] *
                                                       pipeline->_taskWorkgroupSize[2];
      }

      meshDesc.payloadMemoryLength = pipeline->_payloadSizeBytes;

      for (i = 0u; i < pipeline->_colorTargetCount; i++) {
        mt_fillBlendDescriptor(meshDesc.colorAttachments[i],
                               &pipeline->_colorTargetBlends[i]);
      }

      native->render = [deviceMT->device newRenderPipelineStateWithMeshDescriptor:meshDesc
                                                                          options:MTLPipelineOptionNone
                                                                       reflection:nil
                                                                            error:&error];
    }
  } else {
    renderDesc.inputPrimitiveTopology = mt_topologyClass(pipeline->_primitiveTopology);
    renderDesc.alphaToCoverageEnabled = pipeline->_alphaToCoverageEnable;

    for (i = 0u; i < pipeline->_colorTargetCount; i++) {
      mt_fillBlendDescriptor(renderDesc.colorAttachments[i],
                             &pipeline->_colorTargetBlends[i]);
    }

    usesArchive = mt_useRenderCache(pipeline->_cache, renderDesc);

    if (usesArchive) {
      native->render = [deviceMT->device newRenderPipelineStateWithDescriptor:renderDesc
                                                                      options:MTLPipelineOptionFailOnBinaryArchiveMiss
                                                                   reflection:nil
                                                                        error:&error];

      mt_addRenderCache(pipeline->_cache, renderDesc, native->render == nil);

      if (!native->render) {
        error = nil;
      }
    }

    if (!native->render) {
      native->render = [deviceMT->device newRenderPipelineStateWithDescriptor:renderDesc
                                                                        error:&error];
    }
  }

  if (!native->render) {
    NSLog(@"Failed to create render pipeline state: %@", error);
    free(native);
    return NULL;
  }

  depthDesc                      = [MTLDepthStencilDescriptor new];
  depthDesc.depthCompareFunction = pipeline->_depthStencilState.depthTestEnable
                                   ? mt_compareFunction(pipeline->_depthStencilState.depthCompare)
                                   : MTLCompareFunctionAlways;
  depthDesc.depthWriteEnabled    = pipeline->_depthStencilState.depthWriteEnable;

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

  native->depthStencil = [deviceMT->device newDepthStencilStateWithDescriptor:depthDesc];
  [depthDesc release];

  if (!native->depthStencil) {
    [native->render release];
    free(native);
    return NULL;
  }

  if (!(renderPipeline = calloc(1, sizeof(*renderPipeline)))) {
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
  MTRenderPipelineState *state;
  MTRenderPipelineDesc  *descriptor;

  if (!pipeline) {
    return;
  }

  if (pipeline->_state) {
    state = pipeline->_state;

    [state->depthStencil release];
    [state->render release];
    free(state);
  }

  if (pipeline->_priv) {
    descriptor = pipeline->_priv;
    [descriptor->fragmentIntersectionFunctions4 release];
    [descriptor->vertexIntersectionFunctions4 release];
    [descriptor->fragmentIntersectionFunctions release];
    [descriptor->vertexIntersectionFunctions release];
    [descriptor->meshFunction release];
    [descriptor->taskFunction release];
    [descriptor->fragmentFunction release];
    [descriptor->vertexFunction release];
    [descriptor->classic release];
    free(descriptor);
  }

  free(pipeline);
}

GPU_HIDE
void
mt_setFunction(GPURenderPipeline *__restrict pipline,
               GPUShaderFunction *__restrict func,
               GPUFunctionType               functype) {
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
mt_colorFormat(GPURenderPipeline *__restrict pipline,
               uint32_t                      index,
               GPUFormat                     pixelFormat) {
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
mt_depthFormat(GPURenderPipeline *__restrict pipline,
               GPUFormat                     pixelFormat) {
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
mt_stencilFormat(GPURenderPipeline *__restrict pipline,
                 GPUFormat                     pixelFormat) {
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
mt_sampleCount(GPURenderPipeline *__restrict pipline,
               uint32_t                      sampleCount) {
  MTRenderPipelineDesc *native;

  native = pipline->_priv;

  if (pipline->_mesh) {
    ((MTLMeshRenderPipelineDescriptor *)native->classic).rasterSampleCount = sampleCount;
  } else {
    ((MTLRenderPipelineDescriptor *)native->classic).rasterSampleCount = sampleCount;
  }
}

GPU_HIDE
void
mt_initRenderPipeline(GPUApiRender *api) {
  api->newRenderPipeline     = mt_newRenderPipeline;
  api->newRenderState        = mt_newRenderState;
  api->destroyRenderPipeline = mt_destroyRenderPipeline;
  api->setFunction           = mt_setFunction;

  api->setIntersectionFunctions = mt_setIntersectionFunctions;

  api->colorFormat   = mt_colorFormat;
  api->depthFormat   = mt_depthFormat;
  api->stencilFormat = mt_stencilFormat;
  api->sampleCount   = mt_sampleCount;
}
