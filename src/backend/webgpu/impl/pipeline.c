/*
 * Copyright (C) 2026 Recep Aslantas
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
#include "../impl.h"
#include "constants.h"

static const WGPUPrimitiveTopology webgpu_topologies[] = {
  [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST]  = WGPUPrimitiveTopology_TriangleList,
  [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP] = WGPUPrimitiveTopology_TriangleStrip,
  [GPU_PRIMITIVE_TOPOLOGY_LINE_LIST]      = WGPUPrimitiveTopology_LineList,
  [GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP]     = WGPUPrimitiveTopology_LineStrip,
  [GPU_PRIMITIVE_TOPOLOGY_POINT_LIST]     = WGPUPrimitiveTopology_PointList
};

static const WGPUCullMode webgpu_cullModes[] = {
  [GPU_CULL_MODE_NONE]  = WGPUCullMode_None,
  [GPU_CULL_MODE_FRONT] = WGPUCullMode_Front,
  [GPU_CULL_MODE_BACK]  = WGPUCullMode_Back
};

static const WGPUVertexFormat webgpu_vertexFormats[GPU_VERTEX_FORMAT_COUNT] = {
  [GPU_VERTEX_FORMAT_UINT8]           = WGPUVertexFormat_Uint8,
  [GPU_VERTEX_FORMAT_UINT8X2]         = WGPUVertexFormat_Uint8x2,
  [GPU_VERTEX_FORMAT_UINT8X4]         = WGPUVertexFormat_Uint8x4,
  [GPU_VERTEX_FORMAT_SINT8]           = WGPUVertexFormat_Sint8,
  [GPU_VERTEX_FORMAT_SINT8X2]         = WGPUVertexFormat_Sint8x2,
  [GPU_VERTEX_FORMAT_SINT8X4]         = WGPUVertexFormat_Sint8x4,
  [GPU_VERTEX_FORMAT_UNORM8]          = WGPUVertexFormat_Unorm8,
  [GPU_VERTEX_FORMAT_UNORM8X2]        = WGPUVertexFormat_Unorm8x2,
  [GPU_VERTEX_FORMAT_UNORM8X4]        = WGPUVertexFormat_Unorm8x4,
  [GPU_VERTEX_FORMAT_SNORM8]          = WGPUVertexFormat_Snorm8,
  [GPU_VERTEX_FORMAT_SNORM8X2]        = WGPUVertexFormat_Snorm8x2,
  [GPU_VERTEX_FORMAT_SNORM8X4]        = WGPUVertexFormat_Snorm8x4,
  [GPU_VERTEX_FORMAT_UINT16]          = WGPUVertexFormat_Uint16,
  [GPU_VERTEX_FORMAT_UINT16X2]        = WGPUVertexFormat_Uint16x2,
  [GPU_VERTEX_FORMAT_UINT16X4]        = WGPUVertexFormat_Uint16x4,
  [GPU_VERTEX_FORMAT_SINT16]          = WGPUVertexFormat_Sint16,
  [GPU_VERTEX_FORMAT_SINT16X2]        = WGPUVertexFormat_Sint16x2,
  [GPU_VERTEX_FORMAT_SINT16X4]        = WGPUVertexFormat_Sint16x4,
  [GPU_VERTEX_FORMAT_UNORM16]         = WGPUVertexFormat_Unorm16,
  [GPU_VERTEX_FORMAT_UNORM16X2]       = WGPUVertexFormat_Unorm16x2,
  [GPU_VERTEX_FORMAT_UNORM16X4]       = WGPUVertexFormat_Unorm16x4,
  [GPU_VERTEX_FORMAT_SNORM16]         = WGPUVertexFormat_Snorm16,
  [GPU_VERTEX_FORMAT_SNORM16X2]       = WGPUVertexFormat_Snorm16x2,
  [GPU_VERTEX_FORMAT_SNORM16X4]       = WGPUVertexFormat_Snorm16x4,
  [GPU_VERTEX_FORMAT_FLOAT16]         = WGPUVertexFormat_Float16,
  [GPU_VERTEX_FORMAT_FLOAT16X2]       = WGPUVertexFormat_Float16x2,
  [GPU_VERTEX_FORMAT_FLOAT16X4]       = WGPUVertexFormat_Float16x4,
  [GPU_VERTEX_FORMAT_FLOAT32]         = WGPUVertexFormat_Float32,
  [GPU_VERTEX_FORMAT_FLOAT32X2]       = WGPUVertexFormat_Float32x2,
  [GPU_VERTEX_FORMAT_FLOAT32X3]       = WGPUVertexFormat_Float32x3,
  [GPU_VERTEX_FORMAT_FLOAT32X4]       = WGPUVertexFormat_Float32x4,
  [GPU_VERTEX_FORMAT_SINT32]          = WGPUVertexFormat_Sint32,
  [GPU_VERTEX_FORMAT_SINT32X2]        = WGPUVertexFormat_Sint32x2,
  [GPU_VERTEX_FORMAT_SINT32X3]        = WGPUVertexFormat_Sint32x3,
  [GPU_VERTEX_FORMAT_SINT32X4]        = WGPUVertexFormat_Sint32x4,
  [GPU_VERTEX_FORMAT_UINT32]          = WGPUVertexFormat_Uint32,
  [GPU_VERTEX_FORMAT_UINT32X2]        = WGPUVertexFormat_Uint32x2,
  [GPU_VERTEX_FORMAT_UINT32X3]        = WGPUVertexFormat_Uint32x3,
  [GPU_VERTEX_FORMAT_UINT32X4]        = WGPUVertexFormat_Uint32x4,
  [GPU_VERTEX_FORMAT_UNORM10_10_10_2] = WGPUVertexFormat_Unorm10_10_10_2,
  [GPU_VERTEX_FORMAT_UNORM8X4_BGRA]   = WGPUVertexFormat_Unorm8x4BGRA
};

static const WGPUBlendFactor webgpu_blendFactors[] = {
  [GPU_BLEND_FACTOR_ZERO]                = WGPUBlendFactor_Zero,
  [GPU_BLEND_FACTOR_ONE]                 = WGPUBlendFactor_One,
  [GPU_BLEND_FACTOR_SRC_ALPHA]           = WGPUBlendFactor_SrcAlpha,
  [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = WGPUBlendFactor_OneMinusSrcAlpha
};

static const WGPUBlendOperation webgpu_blendOps[] = {
  [GPU_BLEND_OP_ADD]              = WGPUBlendOperation_Add,
  [GPU_BLEND_OP_SUBTRACT]         = WGPUBlendOperation_Subtract,
  [GPU_BLEND_OP_REVERSE_SUBTRACT] = WGPUBlendOperation_ReverseSubtract,
  [GPU_BLEND_OP_MIN]              = WGPUBlendOperation_Min,
  [GPU_BLEND_OP_MAX]              = WGPUBlendOperation_Max
};

static const WGPUCompareFunction webgpu_compareFunctions[] = {
  [GPU_COMPARE_NEVER]         = WGPUCompareFunction_Never,
  [GPU_COMPARE_LESS]          = WGPUCompareFunction_Less,
  [GPU_COMPARE_EQUAL]         = WGPUCompareFunction_Equal,
  [GPU_COMPARE_LESS_EQUAL]    = WGPUCompareFunction_LessEqual,
  [GPU_COMPARE_GREATER]       = WGPUCompareFunction_Greater,
  [GPU_COMPARE_NOT_EQUAL]     = WGPUCompareFunction_NotEqual,
  [GPU_COMPARE_GREATER_EQUAL] = WGPUCompareFunction_GreaterEqual,
  [GPU_COMPARE_ALWAYS]        = WGPUCompareFunction_Always
};

static const WGPUStencilOperation webgpu_stencilOps[] = {
  [GPU_STENCIL_OP_KEEP]            = WGPUStencilOperation_Keep,
  [GPU_STENCIL_OP_ZERO]            = WGPUStencilOperation_Zero,
  [GPU_STENCIL_OP_REPLACE]         = WGPUStencilOperation_Replace,
  [GPU_STENCIL_OP_INCREMENT_CLAMP] = WGPUStencilOperation_IncrementClamp,
  [GPU_STENCIL_OP_DECREMENT_CLAMP] = WGPUStencilOperation_DecrementClamp,
  [GPU_STENCIL_OP_INVERT]          = WGPUStencilOperation_Invert,
  [GPU_STENCIL_OP_INCREMENT_WRAP]  = WGPUStencilOperation_IncrementWrap,
  [GPU_STENCIL_OP_DECREMENT_WRAP]  = WGPUStencilOperation_DecrementWrap
};

static WGPUPrimitiveTopology
webgpu_topology(GPUPrimitiveTopology topology) {
  return (uint32_t)topology < GPU_ARRAY_LEN(webgpu_topologies)
           ? webgpu_topologies[topology]
           : WGPUPrimitiveTopology_Undefined;
}

static WGPUCullMode
webgpu_cullMode(GPUCullMode mode) {
  return (uint32_t)mode < GPU_ARRAY_LEN(webgpu_cullModes) ? webgpu_cullModes[mode] : WGPUCullMode_None;
}

static WGPUFrontFace
webgpu_frontFace(GPUFrontFace face) {
  return face == GPU_FRONT_FACE_CW ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
}

static WGPUVertexFormat
webgpu_vertexFormat(GPUVertexFormat format) {
  return (uint32_t)format < GPU_ARRAY_LEN(webgpu_vertexFormats) ? webgpu_vertexFormats[format] : 0;
}

static WGPUBlendFactor
webgpu_blendFactor(GPUBlendFactor factor) {
  return (uint32_t)factor < GPU_ARRAY_LEN(webgpu_blendFactors) ? webgpu_blendFactors[factor] : WGPUBlendFactor_Zero;
}

static WGPUBlendOperation
webgpu_blendOperation(GPUBlendOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(webgpu_blendOps) ? webgpu_blendOps[op] : WGPUBlendOperation_Add;
}

static WGPUColorWriteMask
webgpu_colorWriteMask(GPUColorWriteMaskFlags mask) {
  WGPUColorWriteMask result;

  if (mask == GPU_COLOR_WRITE_DEFAULT) {
    return WGPUColorWriteMask_All;
  }

  if (mask == GPU_COLOR_WRITE_NONE) {
    return WGPUColorWriteMask_None;
  }

  result = WGPUColorWriteMask_None;

  if ((mask & GPU_COLOR_WRITE_R) != 0u)
    result |= WGPUColorWriteMask_Red;

  if ((mask & GPU_COLOR_WRITE_G) != 0u)
    result |= WGPUColorWriteMask_Green;

  if ((mask & GPU_COLOR_WRITE_B) != 0u)
    result |= WGPUColorWriteMask_Blue;

  if ((mask & GPU_COLOR_WRITE_A) != 0u)
    result |= WGPUColorWriteMask_Alpha;

  return result;
}

static void
webgpu_fillBlend(WGPUBlendState *outBlend, const GPUBlendState *blend) {
  outBlend->color.srcFactor = webgpu_blendFactor(blend->color.srcFactor);
  outBlend->color.dstFactor = webgpu_blendFactor(blend->color.dstFactor);
  outBlend->color.operation = webgpu_blendOperation(blend->color.op);
  outBlend->alpha.srcFactor = webgpu_blendFactor(blend->alpha.srcFactor);
  outBlend->alpha.dstFactor = webgpu_blendFactor(blend->alpha.dstFactor);
  outBlend->alpha.operation = webgpu_blendOperation(blend->alpha.op);
}

static WGPUCompareFunction
webgpu_compareFunction(GPUCompareOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(webgpu_compareFunctions)
           ? webgpu_compareFunctions[op]
           : WGPUCompareFunction_Always;
}

static WGPUStencilOperation
webgpu_stencilOperation(GPUStencilOp op) {
  return (uint32_t)op < GPU_ARRAY_LEN(webgpu_stencilOps) ? webgpu_stencilOps[op] : WGPUStencilOperation_Keep;
}

static void
webgpu_fillStencilFace(WGPUStencilFaceState      *target,
                       const GPUStencilFaceState *source) {
  target->compare     = webgpu_compareFunction(source->compare);
  target->failOp      = webgpu_stencilOperation(source->failOp);
  target->depthFailOp = webgpu_stencilOperation(source->depthFailOp);
  target->passOp      = webgpu_stencilOperation(source->passOp);
}

static GPUResult
webgpu_createPipeline(GPUDevice                         *device,
                      const GPURenderPipelineCreateInfo *info,
                      uint32_t                           requiredBindGroupMask,
                      GPURenderPipeline                 *pipeline) {
  WGPUConstantEntry            constantEntries[USL_RUNTIME_MAX_SPEC_CONSTANTS];
  char                        constantIDs[USL_RUNTIME_MAX_SPEC_CONSTANTS][11];
  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  WGPUFragmentState            fragment   = WGPU_FRAGMENT_STATE_INIT;
  WGPUColorTargetState         targets[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPUBlendState               blends[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPUDepthStencilState        depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  WebGPUPipelineError          error;
#endif
  WGPUVertexBufferLayout      *vertexBuffers;
  WGPUVertexAttribute         *vertexAttributes;
  DeviceWebGPU                *native;
  RenderPipelineWebGPU        *state;
  WGPUShaderModule             module;
  const GPUVertexBufferLayout *vertexSource;
  WGPUVertexAttribute         *attribute;
  const GPUDepthStencilState  *depthSource;
  uint64_t                     entryMask;
  uint64_t                     fragmentEntryMask;
  uint64_t                     vertexEntryMask;
  uint32_t                     attributeCount;
  uint32_t                     attributeCursor;
  uint32_t                     automaticGroupMask;
  uint32_t                     countIndex;
  uint32_t                     j;
  uint32_t                     targetIndex;
  GPUResult                    result;

  native = webgpuDevice(device);
  module = info && info->library ? info->library->_priv : NULL;

  if (!native || !native->device || !info || !module || !info->layout
      || !info->layout->_native) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (info->multisample.sampleCount != 0u
      && info->multisample.sampleCount != 1u
      && info->multisample.sampleCount != 4u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  attributeCount = 0u;

  for (countIndex = 0u; countIndex < info->vertex.bufferLayoutCount; countIndex++) {
    if (info->vertex.pBufferLayouts[countIndex].attributeCount >
        UINT32_MAX - attributeCount) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    attributeCount += info->vertex.pBufferLayouts[countIndex].attributeCount;
  }

  vertexBuffers    = info->vertex.bufferLayoutCount
                       ? calloc(info->vertex.bufferLayoutCount,
                                sizeof(*vertexBuffers))
                       : NULL;
  vertexAttributes = attributeCount
                       ? calloc(attributeCount, sizeof(*vertexAttributes))
                       : NULL;

  if ((info->vertex.bufferLayoutCount && !vertexBuffers)
      || (attributeCount && !vertexAttributes)) {
    free(vertexBuffers);
    free(vertexAttributes);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  attributeCursor = 0u;

  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    vertexSource                    = &info->vertex.pBufferLayouts[i];
    vertexBuffers[i]                = (WGPUVertexBufferLayout)WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBuffers[i].arrayStride    = vertexSource->strideBytes;
    vertexBuffers[i].stepMode       = vertexSource->stepMode == GPU_VERTEX_STEP_MODE_INSTANCE
                                        ? WGPUVertexStepMode_Instance
                                        : WGPUVertexStepMode_Vertex;
    vertexBuffers[i].attributeCount = vertexSource->attributeCount;
    vertexBuffers[i].attributes     = &vertexAttributes[attributeCursor];

    for (j = 0u; j < vertexSource->attributeCount; j++) {
      attribute                 = &vertexAttributes[attributeCursor++];
      *attribute                = (WGPUVertexAttribute)WGPU_VERTEX_ATTRIBUTE_INIT;
      attribute->format         = webgpu_vertexFormat(vertexSource->pAttributes[j].format);
      attribute->offset         = vertexSource->pAttributes[j].offset;
      attribute->shaderLocation = vertexSource->pAttributes[j].shaderLocation;

      if (!attribute->format) {
        free(vertexBuffers);
        free(vertexAttributes);
        return GPU_ERROR_UNSUPPORTED;
      }
    }
  }

  memset(targets, 0, sizeof(targets));
  memset(blends, 0, sizeof(blends));

  for (targetIndex = 0u; targetIndex < info->colorTargetCount; targetIndex++) {
    targets[targetIndex]           = (WGPUColorTargetState)WGPU_COLOR_TARGET_STATE_INIT;
    targets[targetIndex].format    = webgpuFormat(info->pColorTargets[targetIndex].format);
    targets[targetIndex].writeMask = webgpu_colorWriteMask(info->pColorTargets[targetIndex].blend.writeMask);

    if (targets[targetIndex].format == WGPUTextureFormat_Undefined) {
      free(vertexBuffers);
      free(vertexAttributes);
      return GPU_ERROR_UNSUPPORTED;
    }

    if (info->pColorTargets[targetIndex].blend.enabled) {
      webgpu_fillBlend(&blends[targetIndex], &info->pColorTargets[targetIndex].blend);
      targets[targetIndex].blend = &blends[targetIndex];
    }
  }

  if (!(state = calloc(1, sizeof(*state)))) {
    free(vertexBuffers);
    free(vertexAttributes);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  automaticGroupMask = 0u;

  if (shaderLibraryHasEntryResourceInfo(info->library)) {
    vertexEntryMask    = shaderEntryBit(info->library, info->vertexEntry);
    fragmentEntryMask  = shaderEntryBit(info->library, info->fragmentEntry);
    entryMask          = vertexEntryMask | fragmentEntryMask;
    automaticGroupMask = shaderWGSLStaticGroups(info->library, entryMask);

    if (vertexEntryMask == 0u || fragmentEntryMask == 0u
        || automaticGroupMask == UINT32_MAX) {
      free(state);
      free(vertexBuffers);
      free(vertexAttributes);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  automaticGroupMask &= ~requiredBindGroupMask;

  if (webgpuCreatePipelineLayout(device,
                                 info->layout,
                                 requiredBindGroupMask,
                                 automaticGroupMask,
                                 &state->layout) != GPU_OK) {
    free(state);
    free(vertexBuffers);
    free(vertexAttributes);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  descriptor.label               = webgpuString(info->label);
  descriptor.layout              = state->layout.layout;
  descriptor.vertex.module       = module;
  descriptor.vertex.entryPoint   = webgpuString(info->vertexEntry);
  descriptor.vertex.bufferCount  = info->vertex.bufferLayoutCount;
  descriptor.vertex.buffers      = vertexBuffers;
  descriptor.primitive.topology  = webgpu_topology(info->primitiveTopology);
  descriptor.primitive.frontFace = webgpu_frontFace(info->frontFace);
  descriptor.primitive.cullMode  = webgpu_cullMode(info->cullMode);
  descriptor.multisample.count   = info->multisample.sampleCount
                                     ? info->multisample.sampleCount
                                     : 1u;
  descriptor.multisample.mask    = info->multisample.sampleMask
                                     ? info->multisample.sampleMask
                                     : UINT32_MAX;
  descriptor.multisample.alphaToCoverageEnabled = info->multisample.alphaToCoverageEnable;

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    depthStencil.format = webgpuFormat(info->depthStencilFormat);

    if (depthStencil.format == WGPUTextureFormat_Undefined) {
      free(vertexBuffers);
      free(vertexAttributes);
      return GPU_ERROR_UNSUPPORTED;
    }

    depthSource                    = info->pDepthStencilState;
    depthStencil.depthWriteEnabled = depthSource && depthSource->depthWriteEnable
                                       ? WGPUOptionalBool_True
                                       : WGPUOptionalBool_False;
    depthStencil.depthCompare      = depthSource && depthSource->depthTestEnable
                                       ? webgpu_compareFunction(depthSource->depthCompare)
                                       : WGPUCompareFunction_Always;

    if (depthSource && depthSource->stencilTestEnable) {
      webgpu_fillStencilFace(&depthStencil.stencilFront, &depthSource->front);
      webgpu_fillStencilFace(&depthStencil.stencilBack, &depthSource->back);
      depthStencil.stencilReadMask  = depthSource->stencilReadMask;
      depthStencil.stencilWriteMask = depthSource->stencilWriteMask;
    } else {
      depthStencil.stencilFront.compare     = WGPUCompareFunction_Always;
      depthStencil.stencilFront.failOp      = WGPUStencilOperation_Keep;
      depthStencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
      depthStencil.stencilFront.passOp      = WGPUStencilOperation_Keep;
      depthStencil.stencilBack              = depthStencil.stencilFront;
    }

    descriptor.depthStencil = &depthStencil;
  }

  fragment.module      = module;
  fragment.entryPoint  = webgpuString(info->fragmentEntry);
  fragment.targetCount = info->colorTargetCount;
  fragment.targets     = targets;
  descriptor.fragment  = &fragment;

  descriptor.vertex.constantCount = webgpu_pipelineConstants(info->chain.pNext, constantEntries, constantIDs);
  descriptor.vertex.constants     = descriptor.vertex.constantCount ? constantEntries : NULL;
  fragment.constantCount          = descriptor.vertex.constantCount;
  fragment.constants              = descriptor.vertex.constants;

  result = GPU_OK;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  webgpuBeginPipelineError(device, &error);
#endif
  state->pipeline = wgpuDeviceCreateRenderPipeline(native->device, &descriptor);
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  result = webgpuEndPipelineError(&error);
#endif
  free(vertexBuffers);
  free(vertexAttributes);

  if (!state->pipeline || result != GPU_OK) {
    if (state->pipeline)
      wgpuRenderPipelineRelease(state->pipeline);

    webgpuDestroyPipelineLayout(&state->layout);
    free(state);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  pipeline->_priv  = state->pipeline;
  pipeline->_state = state;

  return GPU_OK;
}

static void
webgpu_destroyPipeline(GPURenderPipeline *pipeline) {
  RenderPipelineWebGPU    *state;

  state = pipeline ? pipeline->_state : NULL;

  if (state) {
    if (state->pipeline) {
      wgpuRenderPipelineRelease(state->pipeline);
    }

    webgpuDestroyPipelineLayout(&state->layout);
    free(state);
  }

  free(pipeline);
}

void
webgpu_initPipeline(ApiRender    *api) {
  api->createPipeline        = webgpu_createPipeline;
  api->destroyRenderPipeline = webgpu_destroyPipeline;
}
