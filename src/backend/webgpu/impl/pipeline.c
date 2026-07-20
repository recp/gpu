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

static GPUResult
webgpu_createPipeline(GPUDevice                         *device,
                      const GPURenderPipelineCreateInfo *info,
                      uint32_t                           requiredBindGroupMask,
                      GPURenderPipeline                 *pipeline) {
  WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  WGPUFragmentState            fragment = WGPU_FRAGMENT_STATE_INIT;
  WGPUColorTargetState         targets[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  WGPUBlendState               blends[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUDeviceWebGPU             *native;
  WGPUShaderModule             module;

  GPU__UNUSED(requiredBindGroupMask);
  native = gpu_webgpuDevice(device);
  module = info && info->library ? info->library->_priv : NULL;
  if (!native || !native->device || !module || !info->layout->_native ||
      info->vertex.bufferLayoutCount != 0u ||
      info->depthStencilFormat != GPU_FORMAT_UNDEFINED ||
      info->pDepthStencilState) {
    return GPU_ERROR_UNSUPPORTED;
  }

  memset(targets, 0, sizeof(targets));
  memset(blends, 0, sizeof(blends));
  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    targets[i] = (WGPUColorTargetState)WGPU_COLOR_TARGET_STATE_INIT;
    targets[i].format    = gpu_webgpuFormat(info->pColorTargets[i].format);
    targets[i].writeMask =
      webgpu_colorWriteMask(info->pColorTargets[i].blend.writeMask);
    if (targets[i].format == WGPUTextureFormat_Undefined) {
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

  fragment.module     = module;
  fragment.entryPoint = gpu_webgpuString(info->fragmentEntry);
  fragment.targetCount = info->colorTargetCount;
  fragment.targets     = targets;
  descriptor.fragment  = &fragment;

  pipeline->_priv = wgpuDeviceCreateRenderPipeline(native->device,
                                                    &descriptor);
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
