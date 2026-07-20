/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPUPrimitiveTopology
webgpu_topology(GPUPrimitiveTopology topology) {
  static const WGPUPrimitiveTopology topologies[] = {
    [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST]  = WGPUPrimitiveTopology_TriangleList,
    [GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP] = WGPUPrimitiveTopology_TriangleStrip,
    [GPU_PRIMITIVE_TOPOLOGY_LINE_LIST]      = WGPUPrimitiveTopology_LineList,
    [GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP]     = WGPUPrimitiveTopology_LineStrip,
    [GPU_PRIMITIVE_TOPOLOGY_POINT_LIST]     = WGPUPrimitiveTopology_PointList
  };

  return (uint32_t)topology < GPU_ARRAY_LEN(topologies)
           ? topologies[topology]
           : WGPUPrimitiveTopology_Undefined;
}

static WGPUCullMode
webgpu_cullMode(GPUCullMode mode) {
  static const WGPUCullMode modes[] = {
    [GPU_CULL_MODE_NONE]  = WGPUCullMode_None,
    [GPU_CULL_MODE_FRONT] = WGPUCullMode_Front,
    [GPU_CULL_MODE_BACK]  = WGPUCullMode_Back
  };

  return (uint32_t)mode < GPU_ARRAY_LEN(modes)
           ? modes[mode]
           : WGPUCullMode_None;
}

static WGPUFrontFace
webgpu_frontFace(GPUFrontFace face) {
  return face == GPU_FRONT_FACE_CW ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
}

static WGPUVertexFormat
webgpu_vertexFormat(GPUVertexFormat format) {
  static const WGPUVertexFormat formats[GPU_VERTEX_FORMAT_COUNT] = {
    [GPU_VERTEX_FORMAT_UINT8]             = WGPUVertexFormat_Uint8,
    [GPU_VERTEX_FORMAT_UINT8X2]           = WGPUVertexFormat_Uint8x2,
    [GPU_VERTEX_FORMAT_UINT8X4]           = WGPUVertexFormat_Uint8x4,
    [GPU_VERTEX_FORMAT_SINT8]             = WGPUVertexFormat_Sint8,
    [GPU_VERTEX_FORMAT_SINT8X2]           = WGPUVertexFormat_Sint8x2,
    [GPU_VERTEX_FORMAT_SINT8X4]           = WGPUVertexFormat_Sint8x4,
    [GPU_VERTEX_FORMAT_UNORM8]            = WGPUVertexFormat_Unorm8,
    [GPU_VERTEX_FORMAT_UNORM8X2]          = WGPUVertexFormat_Unorm8x2,
    [GPU_VERTEX_FORMAT_UNORM8X4]          = WGPUVertexFormat_Unorm8x4,
    [GPU_VERTEX_FORMAT_SNORM8]            = WGPUVertexFormat_Snorm8,
    [GPU_VERTEX_FORMAT_SNORM8X2]          = WGPUVertexFormat_Snorm8x2,
    [GPU_VERTEX_FORMAT_SNORM8X4]          = WGPUVertexFormat_Snorm8x4,
    [GPU_VERTEX_FORMAT_UINT16]            = WGPUVertexFormat_Uint16,
    [GPU_VERTEX_FORMAT_UINT16X2]          = WGPUVertexFormat_Uint16x2,
    [GPU_VERTEX_FORMAT_UINT16X4]          = WGPUVertexFormat_Uint16x4,
    [GPU_VERTEX_FORMAT_SINT16]            = WGPUVertexFormat_Sint16,
    [GPU_VERTEX_FORMAT_SINT16X2]          = WGPUVertexFormat_Sint16x2,
    [GPU_VERTEX_FORMAT_SINT16X4]          = WGPUVertexFormat_Sint16x4,
    [GPU_VERTEX_FORMAT_UNORM16]           = WGPUVertexFormat_Unorm16,
    [GPU_VERTEX_FORMAT_UNORM16X2]         = WGPUVertexFormat_Unorm16x2,
    [GPU_VERTEX_FORMAT_UNORM16X4]         = WGPUVertexFormat_Unorm16x4,
    [GPU_VERTEX_FORMAT_SNORM16]           = WGPUVertexFormat_Snorm16,
    [GPU_VERTEX_FORMAT_SNORM16X2]         = WGPUVertexFormat_Snorm16x2,
    [GPU_VERTEX_FORMAT_SNORM16X4]         = WGPUVertexFormat_Snorm16x4,
    [GPU_VERTEX_FORMAT_FLOAT16]           = WGPUVertexFormat_Float16,
    [GPU_VERTEX_FORMAT_FLOAT16X2]         = WGPUVertexFormat_Float16x2,
    [GPU_VERTEX_FORMAT_FLOAT16X4]         = WGPUVertexFormat_Float16x4,
    [GPU_VERTEX_FORMAT_FLOAT32]           = WGPUVertexFormat_Float32,
    [GPU_VERTEX_FORMAT_FLOAT32X2]         = WGPUVertexFormat_Float32x2,
    [GPU_VERTEX_FORMAT_FLOAT32X3]         = WGPUVertexFormat_Float32x3,
    [GPU_VERTEX_FORMAT_FLOAT32X4]         = WGPUVertexFormat_Float32x4,
    [GPU_VERTEX_FORMAT_SINT32]            = WGPUVertexFormat_Sint32,
    [GPU_VERTEX_FORMAT_SINT32X2]          = WGPUVertexFormat_Sint32x2,
    [GPU_VERTEX_FORMAT_SINT32X3]          = WGPUVertexFormat_Sint32x3,
    [GPU_VERTEX_FORMAT_SINT32X4]          = WGPUVertexFormat_Sint32x4,
    [GPU_VERTEX_FORMAT_UINT32]            = WGPUVertexFormat_Uint32,
    [GPU_VERTEX_FORMAT_UINT32X2]          = WGPUVertexFormat_Uint32x2,
    [GPU_VERTEX_FORMAT_UINT32X3]          = WGPUVertexFormat_Uint32x3,
    [GPU_VERTEX_FORMAT_UINT32X4]          = WGPUVertexFormat_Uint32x4,
    [GPU_VERTEX_FORMAT_UNORM10_10_10_2]   = WGPUVertexFormat_Unorm10_10_10_2,
    [GPU_VERTEX_FORMAT_UNORM8X4_BGRA]     = WGPUVertexFormat_Unorm8x4BGRA
  };

  return (uint32_t)format < GPU_ARRAY_LEN(formats) ? formats[format] : 0;
}

static WGPUBlendFactor
webgpu_blendFactor(GPUBlendFactor factor) {
  static const WGPUBlendFactor factors[] = {
    [GPU_BLEND_FACTOR_ZERO]                = WGPUBlendFactor_Zero,
    [GPU_BLEND_FACTOR_ONE]                 = WGPUBlendFactor_One,
    [GPU_BLEND_FACTOR_SRC_ALPHA]           = WGPUBlendFactor_SrcAlpha,
    [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = WGPUBlendFactor_OneMinusSrcAlpha
  };

  return (uint32_t)factor < GPU_ARRAY_LEN(factors)
           ? factors[factor]
           : WGPUBlendFactor_Zero;
}

static WGPUBlendOperation
webgpu_blendOperation(GPUBlendOp op) {
  static const WGPUBlendOperation operations[] = {
    [GPU_BLEND_OP_ADD]              = WGPUBlendOperation_Add,
    [GPU_BLEND_OP_SUBTRACT]         = WGPUBlendOperation_Subtract,
    [GPU_BLEND_OP_REVERSE_SUBTRACT] = WGPUBlendOperation_ReverseSubtract,
    [GPU_BLEND_OP_MIN]              = WGPUBlendOperation_Min,
    [GPU_BLEND_OP_MAX]              = WGPUBlendOperation_Max
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : WGPUBlendOperation_Add;
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
  if ((mask & GPU_COLOR_WRITE_R) != 0u) result |= WGPUColorWriteMask_Red;
  if ((mask & GPU_COLOR_WRITE_G) != 0u) result |= WGPUColorWriteMask_Green;
  if ((mask & GPU_COLOR_WRITE_B) != 0u) result |= WGPUColorWriteMask_Blue;
  if ((mask & GPU_COLOR_WRITE_A) != 0u) result |= WGPUColorWriteMask_Alpha;
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
  static const WGPUCompareFunction functions[] = {
    [GPU_COMPARE_NEVER]         = WGPUCompareFunction_Never,
    [GPU_COMPARE_LESS]          = WGPUCompareFunction_Less,
    [GPU_COMPARE_EQUAL]         = WGPUCompareFunction_Equal,
    [GPU_COMPARE_LESS_EQUAL]    = WGPUCompareFunction_LessEqual,
    [GPU_COMPARE_GREATER]       = WGPUCompareFunction_Greater,
    [GPU_COMPARE_NOT_EQUAL]     = WGPUCompareFunction_NotEqual,
    [GPU_COMPARE_GREATER_EQUAL] = WGPUCompareFunction_GreaterEqual,
    [GPU_COMPARE_ALWAYS]        = WGPUCompareFunction_Always
  };

  return (uint32_t)op < GPU_ARRAY_LEN(functions)
           ? functions[op]
           : WGPUCompareFunction_Always;
}

static WGPUStencilOperation
webgpu_stencilOperation(GPUStencilOp op) {
  static const WGPUStencilOperation operations[] = {
    [GPU_STENCIL_OP_KEEP]            = WGPUStencilOperation_Keep,
    [GPU_STENCIL_OP_ZERO]            = WGPUStencilOperation_Zero,
    [GPU_STENCIL_OP_REPLACE]         = WGPUStencilOperation_Replace,
    [GPU_STENCIL_OP_INCREMENT_CLAMP] = WGPUStencilOperation_IncrementClamp,
    [GPU_STENCIL_OP_DECREMENT_CLAMP] = WGPUStencilOperation_DecrementClamp,
    [GPU_STENCIL_OP_INVERT]          = WGPUStencilOperation_Invert,
    [GPU_STENCIL_OP_INCREMENT_WRAP]  = WGPUStencilOperation_IncrementWrap,
    [GPU_STENCIL_OP_DECREMENT_WRAP]  = WGPUStencilOperation_DecrementWrap
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : WGPUStencilOperation_Keep;
}

static void
webgpu_fillStencilFace(WGPUStencilFaceState       *target,
                       const GPUStencilFaceState  *source) {
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
  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  WGPUFragmentState            fragment = WGPU_FRAGMENT_STATE_INIT;
  WGPUColorTargetState         targets[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPUBlendState               blends[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPUDepthStencilState        depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
  WGPUVertexBufferLayout      *vertexBuffers;
  WGPUVertexAttribute         *vertexAttributes;
  GPUDeviceWebGPU             *native;
  WGPUShaderModule             module;
  uint32_t                     attributeCount;
  uint32_t                     attributeCursor;

  GPU__UNUSED(requiredBindGroupMask);
  native = gpu_webgpuDevice(device);
  module = info && info->library ? info->library->_priv : NULL;
  if (!native || !native->device || !info || !module || !info->layout ||
      !info->layout->_native) {
    return GPU_ERROR_UNSUPPORTED;
  }

  attributeCount = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount >
        UINT32_MAX - attributeCount) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    attributeCount += info->vertex.pBufferLayouts[i].attributeCount;
  }
  vertexBuffers = info->vertex.bufferLayoutCount
                    ? calloc(info->vertex.bufferLayoutCount,
                             sizeof(*vertexBuffers))
                    : NULL;
  vertexAttributes = attributeCount
                       ? calloc(attributeCount, sizeof(*vertexAttributes))
                       : NULL;
  if ((info->vertex.bufferLayoutCount && !vertexBuffers) ||
      (attributeCount && !vertexAttributes)) {
    free(vertexBuffers);
    free(vertexAttributes);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  attributeCursor = 0u;
  for (uint32_t i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *source;

    source = &info->vertex.pBufferLayouts[i];
    vertexBuffers[i] = (WGPUVertexBufferLayout)
                         WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBuffers[i].arrayStride    = source->strideBytes;
    vertexBuffers[i].stepMode       = source->stepMode ==
                                        GPU_VERTEX_STEP_MODE_INSTANCE
                                          ? WGPUVertexStepMode_Instance
                                          : WGPUVertexStepMode_Vertex;
    vertexBuffers[i].attributeCount = source->attributeCount;
    vertexBuffers[i].attributes     = &vertexAttributes[attributeCursor];
    for (uint32_t j = 0u; j < source->attributeCount; j++) {
      WGPUVertexAttribute *attribute;

      attribute = &vertexAttributes[attributeCursor++];
      *attribute = (WGPUVertexAttribute)WGPU_VERTEX_ATTRIBUTE_INIT;
      attribute->format = webgpu_vertexFormat(source->pAttributes[j].format);
      attribute->offset = source->pAttributes[j].offset;
      attribute->shaderLocation = source->pAttributes[j].shaderLocation;
      if (!attribute->format) {
        free(vertexBuffers);
        free(vertexAttributes);
        return GPU_ERROR_UNSUPPORTED;
      }
    }
  }

  memset(targets, 0, sizeof(targets));
  memset(blends, 0, sizeof(blends));
  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    targets[i] = (WGPUColorTargetState)WGPU_COLOR_TARGET_STATE_INIT;
    targets[i].format    = gpu_webgpuFormat(info->pColorTargets[i].format);
    targets[i].writeMask =
      webgpu_colorWriteMask(info->pColorTargets[i].blend.writeMask);
    if (targets[i].format == WGPUTextureFormat_Undefined) {
      free(vertexBuffers);
      free(vertexAttributes);
      return GPU_ERROR_UNSUPPORTED;
    }
    if (info->pColorTargets[i].blend.enabled) {
      webgpu_fillBlend(&blends[i], &info->pColorTargets[i].blend);
      targets[i].blend = &blends[i];
    }
  }

  descriptor.label              = gpu_webgpuString(info->label);
  descriptor.layout             = info->layout->_native;
  descriptor.vertex.module      = module;
  descriptor.vertex.entryPoint  = gpu_webgpuString(info->vertexEntry);
  descriptor.vertex.bufferCount = info->vertex.bufferLayoutCount;
  descriptor.vertex.buffers     = vertexBuffers;
  descriptor.primitive.topology = webgpu_topology(info->primitiveTopology);
  descriptor.primitive.frontFace = webgpu_frontFace(info->frontFace);
  descriptor.primitive.cullMode  = webgpu_cullMode(info->cullMode);
  descriptor.multisample.count   = info->multisample.sampleCount
                                     ? info->multisample.sampleCount
                                     : 1u;
  descriptor.multisample.mask    = info->multisample.sampleMask
                                     ? info->multisample.sampleMask
                                     : UINT32_MAX;
  descriptor.multisample.alphaToCoverageEnabled =
    info->multisample.alphaToCoverageEnable;

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    const GPUDepthStencilState *source;

    depthStencil.format = gpu_webgpuFormat(info->depthStencilFormat);
    if (depthStencil.format == WGPUTextureFormat_Undefined) {
      free(vertexBuffers);
      free(vertexAttributes);
      return GPU_ERROR_UNSUPPORTED;
    }
    source = info->pDepthStencilState;
    depthStencil.depthWriteEnabled = source && source->depthWriteEnable
                                       ? WGPUOptionalBool_True
                                       : WGPUOptionalBool_False;
    depthStencil.depthCompare = source && source->depthTestEnable
                                  ? webgpu_compareFunction(source->depthCompare)
                                  : WGPUCompareFunction_Always;
    if (source && source->stencilTestEnable) {
      webgpu_fillStencilFace(&depthStencil.stencilFront, &source->front);
      webgpu_fillStencilFace(&depthStencil.stencilBack, &source->back);
      depthStencil.stencilReadMask  = source->stencilReadMask;
      depthStencil.stencilWriteMask = source->stencilWriteMask;
    } else {
      depthStencil.stencilFront.compare     = WGPUCompareFunction_Always;
      depthStencil.stencilFront.failOp      = WGPUStencilOperation_Keep;
      depthStencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
      depthStencil.stencilFront.passOp      = WGPUStencilOperation_Keep;
      depthStencil.stencilBack              = depthStencil.stencilFront;
    }
    descriptor.depthStencil = &depthStencil;
  }

  fragment.module     = module;
  fragment.entryPoint = gpu_webgpuString(info->fragmentEntry);
  fragment.targetCount = info->colorTargetCount;
  fragment.targets     = targets;
  descriptor.fragment  = &fragment;

  pipeline->_priv = wgpuDeviceCreateRenderPipeline(native->device,
                                                    &descriptor);
  free(vertexBuffers);
  free(vertexAttributes);
  pipeline->_state = pipeline->_priv;
  return pipeline->_priv ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyPipeline(GPURenderPipeline *pipeline) {
  if (pipeline && pipeline->_priv) {
    wgpuRenderPipelineRelease(pipeline->_priv);
  }
  free(pipeline);
}

void
webgpu_initPipeline(GPUApiRender *api) {
  api->createPipeline        = webgpu_createPipeline;
  api->destroyRenderPipeline = webgpu_destroyPipeline;
}
