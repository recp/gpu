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
#include "pipeline_internal.h"
#include "../descr/descriptor_internal.h"
#include "../library_internal.h"
#include "../pipeline_cache_internal.h"
#include "../vertex_internal.h"

#define GPU_RENDER_PIPELINE_MAX_COLOR_TARGETS 8u

static bool
gpu_blendStateIsValid(const GPUBlendState *blend) {
  return (uint32_t)blend->color.srcFactor <=
           GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
         (uint32_t)blend->color.dstFactor <=
           GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
         (uint32_t)blend->alpha.srcFactor <=
           GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
         (uint32_t)blend->alpha.dstFactor <=
           GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
         (uint32_t)blend->color.op <= GPU_BLEND_OP_MAX &&
         (uint32_t)blend->alpha.op <= GPU_BLEND_OP_MAX &&
         (blend->writeMask & ~GPU_COLOR_WRITE_ALL) == 0u;
}

static bool
gpu_depthStencilStateIsValid(GPUFormat                    format,
                             const GPUDepthStencilState *state) {
  bool hasDepth;
  bool hasStencil;

  hasDepth = format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
             format == GPU_FORMAT_DEPTH32_FLOAT ||
             format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  if (format != GPU_FORMAT_UNDEFINED && !hasDepth) {
    return false;
  }
  if (!state) {
    return true;
  }
  if ((uint32_t)state->depthCompare > GPU_COMPARE_ALWAYS ||
      (uint32_t)state->front.compare > GPU_COMPARE_ALWAYS ||
      (uint32_t)state->back.compare > GPU_COMPARE_ALWAYS ||
      (uint32_t)state->front.failOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      (uint32_t)state->front.depthFailOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      (uint32_t)state->front.passOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      (uint32_t)state->back.failOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      (uint32_t)state->back.depthFailOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      (uint32_t)state->back.passOp > GPU_STENCIL_OP_DECREMENT_WRAP ||
      state->stencilReadMask > UINT8_MAX ||
      state->stencilWriteMask > UINT8_MAX) {
    return false;
  }

  if (!state->depthTestEnable && !state->depthWriteEnable &&
      !state->stencilTestEnable) {
    return true;
  }
  if (!hasDepth) {
    return false;
  }

  hasStencil = format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
               format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  return !state->stencilTestEnable || hasStencil;
}

static bool
gpu_vertexFormatIsValid(GPUVertexFormat format) {
  return format != GPUUnknown && format <= GPUVertexFormatHalf;
}

static bool
gpu_vertexStepModeIsValid(GPUVertexStepMode mode) {
  return mode == GPU_VERTEX_STEP_MODE_VERTEX ||
         mode == GPU_VERTEX_STEP_MODE_INSTANCE;
}

static bool
gpu_vertexStateIsValid(const GPUVertexState *state) {
  if (!state) {
    return false;
  }
  if (state->bufferLayoutCount == 0) {
    return true;
  }
  if (!state->pBufferLayouts) {
    return false;
  }

  for (uint32_t i = 0; i < state->bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *layout = &state->pBufferLayouts[i];

    if (!gpu_vertexStepModeIsValid(layout->stepMode)) {
      return false;
    }
    if (layout->attributeCount > 0 && !layout->pAttributes) {
      return false;
    }
    for (uint32_t j = 0; j < layout->attributeCount; j++) {
      if (!gpu_vertexFormatIsValid(layout->pAttributes[j].format)) {
        return false;
      }
    }
  }

  return true;
}

static GPUVertexStepFunction
gpu_vertexStepFunction(GPUVertexStepMode mode) {
  switch (mode) {
    case GPU_VERTEX_STEP_MODE_INSTANCE:
      return GPUPerInstance;
    case GPU_VERTEX_STEP_MODE_VERTEX:
    default:
      return GPUPerVertex;
  }
}

static GPUVertexDescriptor *
gpu_createVertexDescriptorFromState(GPUApi              *api,
                                    const GPUVertexState *state) {
  GPUVertexDescriptor *desc;
  uint32_t i, j;

  if (state->bufferLayoutCount == 0)
    return NULL;
  if (!api || !state->pBufferLayouts ||
      !api->vertex.newVertexDesc ||
      !api->vertex.destroyVertexDesc ||
      !api->vertex.attrib ||
      !api->vertex.layout ||
      !api->vertex.vertexDesc)
    return NULL;

  desc = gpuCreateVertexDesc(api);
  if (!desc)
    return NULL;

  for (i = 0; i < state->bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *layout = &state->pBufferLayouts[i];

    if (layout->attributeCount > 0 && !layout->pAttributes) {
      gpuDestroyVertexDesc(api, desc);
      return NULL;
    }

    gpuVertexDescLayout(api,
                        desc,
                        i,
                        layout->strideBytes,
                        1,
                        gpu_vertexStepFunction(layout->stepMode));
    for (j = 0; j < layout->attributeCount; j++) {
      const GPUVertexAttribute *attr = &layout->pAttributes[j];

      gpuVertexDescAttrib(api,
                          desc,
                          attr->shaderLocation,
                          attr->format,
                          attr->offset,
                          i);
    }
  }

  return desc;
}

static bool
gpu_primitiveTopologyIsValid(GPUPrimitiveTopology topology) {
  return topology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ||
         topology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
         topology == GPU_PRIMITIVE_TOPOLOGY_LINE_LIST ||
         topology == GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
         topology == GPU_PRIMITIVE_TOPOLOGY_POINT_LIST;
}

static bool
gpu_cullModeIsValid(GPUCullMode mode) {
  return mode == GPU_CULL_MODE_NONE ||
         mode == GPU_CULL_MODE_FRONT ||
         mode == GPU_CULL_MODE_BACK;
}

static bool
gpu_frontFaceIsValid(GPUFrontFace face) {
  return face == GPU_FRONT_FACE_CCW ||
         face == GPU_FRONT_FACE_CW;
}

static bool
gpu_renderPipelineEntriesMatchStages(const GPURenderPipelineCreateInfo *info) {
  GPUShaderStageFlags stage;

  if (gpuGetShaderLibraryEntryStage(info->library, info->vertexEntry, &stage) &&
      stage != GPU_SHADER_STAGE_VERTEX_BIT) {
    return false;
  }
  if (gpuGetShaderLibraryEntryStage(info->library, info->fragmentEntry, &stage) &&
      stage != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    return false;
  }

  return true;
}

static bool
gpu_pipelineInfoIsSupported(const GPURenderPipelineCreateInfo *info) {
  uint32_t i;

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO) {
    return false;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info))
    return false;
  if (info->colorTargetCount == 0 ||
      info->colorTargetCount > GPU_RENDER_PIPELINE_MAX_COLOR_TARGETS ||
      !info->pColorTargets)
    return false;
  if (!gpu_vertexStateIsValid(&info->vertex))
    return false;
  if (!gpu_primitiveTopologyIsValid(info->primitiveTopology) ||
      !gpu_cullModeIsValid(info->cullMode) ||
      !gpu_frontFaceIsValid(info->frontFace))
    return false;
  if (info->primitiveTopology != GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    return false;
  if (info->multisample.sampleCount != 0u &&
      info->multisample.sampleCount != 1u &&
      info->multisample.sampleCount != 2u &&
      info->multisample.sampleCount != 4u &&
      info->multisample.sampleCount != 8u)
    return false;
  if (info->multisample.sampleMask != 0u &&
      info->multisample.sampleMask != UINT32_MAX)
    return false;
  if (!gpu_depthStencilStateIsValid(info->depthStencilFormat,
                                    info->pDepthStencilState))
    return false;
  for (i = 0; i < info->colorTargetCount; i++) {
    if (info->pColorTargets[i].format == GPU_FORMAT_UNDEFINED)
      return false;
    if (!gpu_blendStateIsValid(&info->pColorTargets[i].blend))
      return false;
  }

  return true;
}

GPU_EXPORT
GPUResult
GPUCreateRenderPipeline(GPUDevice                         * __restrict device,
                        const GPURenderPipelineCreateInfo * __restrict info,
                        GPURenderPipeline                ** __restrict outPipeline) {
  GPUApi                 *api;
  GPURenderPipelineState *state;
  GPURenderPipeline      *pipeline;
  GPUVertexDescriptor    *vertexDesc;
  GPUFunction            *vertexFunc;
  GPUFunction            *fragmentFunc;
  GPUFormat               colorFormat;
  uint32_t                i;
  uint32_t                requiredBindGroupMask;
  uint32_t                sampleCount;

  if (!outPipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outPipeline = NULL;

  if (!device || !info || !info->layout || !info->library ||
      !info->vertexEntry || !info->fragmentEntry)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (info->cache && info->cache->device != device)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (!gpu_pipelineInfoIsSupported(info))
    return GPU_ERROR_INVALID_ARGUMENT;
  if (info->cache && !info->chain.pNext) {
    GPUResult result;

    result = gpuPipelineCacheFindRender(info->cache, info, &pipeline);
    if (result != GPU_OK) {
      return result;
    }
    if (pipeline) {
      *outPipeline = pipeline;
      return GPU_OK;
    }
  }
  if (!gpu_renderPipelineEntriesMatchStages(info))
    return GPU_ERROR_INVALID_ARGUMENT;
  api = gpuDeviceApi(device);
  if (!api)
    return GPU_ERROR_BACKEND_FAILURE;
  {
    const char *entries[] = {info->vertexEntry, info->fragmentEntry};

    if (!gpuPipelineLayoutMatchesShaderEntries(info->layout,
                                               info->library,
                                               entries,
                                               (uint32_t)GPU_ARRAY_LEN(entries),
                                               GPU_SHADER_STAGE_VERTEX_BIT |
                                                 GPU_SHADER_STAGE_FRAGMENT_BIT,
                                               &requiredBindGroupMask))
      return GPU_ERROR_INVALID_ARGUMENT;
  }

  sampleCount = info->multisample.sampleCount > 0 ?
    info->multisample.sampleCount : 1u;
  if (api->render.createPipeline) {
    GPUResult result;

    pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline)
      return GPU_ERROR_OUT_OF_MEMORY;

    pipeline->_api      = api;
    pipeline->_refCount = 1u;
    result = api->render.createPipeline(device,
                                        info,
                                        requiredBindGroupMask,
                                        pipeline);
    if (result != GPU_OK) {
      free(pipeline);
      return result;
    }
    goto ready;
  }

  vertexFunc = GPUShaderFunction(info->library, info->vertexEntry);
  fragmentFunc = GPUShaderFunction(info->library, info->fragmentEntry);
  if (!vertexFunc || !fragmentFunc)
    return GPU_ERROR_INVALID_ARGUMENT;

  colorFormat = info->pColorTargets[0].format;
  pipeline = gpuCreateRenderPipelineDesc(api, colorFormat);
  if (!pipeline)
    return GPU_ERROR_BACKEND_FAILURE;

  pipeline->_api      = api;
  pipeline->_refCount = 1u;

  gpuPipelineSetFunction(pipeline, vertexFunc, GPU_FUNCTION_VERT);
  gpuPipelineSetFunction(pipeline, fragmentFunc, GPU_FUNCTION_FRAG);

  vertexDesc = gpu_createVertexDescriptorFromState(api, &info->vertex);
  if (info->vertex.bufferLayoutCount > 0 && !vertexDesc) {
    GPUDestroyRenderPipeline(pipeline);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (vertexDesc)
    gpuPipelineSetVertexDesc(pipeline, vertexDesc);
  gpuDestroyVertexDesc(api, vertexDesc);

  for (i = 0; i < info->colorTargetCount; i++)
    gpuPipelineSetColorFormat(pipeline, i, info->pColorTargets[i].format);

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    gpuPipelineSetDepthFormat(pipeline, info->depthStencilFormat);
    if (info->depthStencilFormat == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
        info->depthStencilFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)
      gpuPipelineSetStencilFormat(pipeline, info->depthStencilFormat);
  }
  if (info->pDepthStencilState)
    pipeline->_depthStencilState = *info->pDepthStencilState;

  pipeline->_sampleCount = sampleCount;
  pipeline->_alphaToCoverageEnable =
    info->multisample.alphaToCoverageEnable;
  pipeline->_colorTargetCount = info->colorTargetCount;
  for (i = 0; i < info->colorTargetCount; i++) {
    pipeline->_colorTargetFormats[i] = info->pColorTargets[i].format;
    pipeline->_colorTargetBlends[i] = info->pColorTargets[i].blend;
  }
  gpuPipelineSetSampleCount(pipeline, sampleCount);

  state = gpuCompileRenderPipelineState(device, pipeline);
  if (!state) {
    GPUDestroyRenderPipeline(pipeline);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  free(state);

ready:
  pipeline->_layout = info->layout;
  pipeline->_requiredBindGroupMask = requiredBindGroupMask;
  pipeline->_colorTargetCount = info->colorTargetCount;
  for (i = 0; i < info->colorTargetCount; i++) {
    pipeline->_colorTargetFormats[i] = info->pColorTargets[i].format;
    pipeline->_colorTargetBlends[i] = info->pColorTargets[i].blend;
  }
  pipeline->_depthStencilFormat = info->depthStencilFormat;
  pipeline->_sampleCount = sampleCount;
  pipeline->_alphaToCoverageEnable =
    info->multisample.alphaToCoverageEnable;
  pipeline->_primitiveTopology = info->primitiveTopology;
  pipeline->_cullMode = info->cullMode;
  pipeline->_frontFace = info->frontFace;
  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &pipeline->_pushConstantSizeBytes,
                                    &pipeline->_pushConstantStages);
  if (info->cache && !info->chain.pNext) {
    pipeline = gpuPipelineCacheStoreRender(info->cache, info, pipeline);
  } else {
    gpuRecordPipelineCompile(device, info->cache);
  }
  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyRenderPipeline(GPURenderPipeline *pipeline) {
  GPUApi *api;

  if (!pipeline)
    return;

  if (!gpuReleaseRenderPipeline(pipeline)) {
    return;
  }

  if ((api = pipeline->_api) && api->render.destroyRenderPipeline) {
    api->render.destroyRenderPipeline(pipeline);
    return;
  }

  free(pipeline);
}

GPU_HIDE
GPURenderPipeline *
gpuCreateRenderPipelineDesc(GPUApi *api, GPUPixelFormat pixelFormat) {
  if (!api || !api->render.newRenderPipeline)
    return NULL;

  return api->render.newRenderPipeline(pixelFormat);
}

GPU_HIDE
GPURenderPipelineState *
gpuCompileRenderPipelineState(GPUDevice         * __restrict device,
                              GPURenderPipeline * __restrict pipeline) {
  GPUApi *api;

  if (!device || !pipeline || !(api = gpuDeviceApi(device)) ||
      pipeline->_api != api || !api->render.newRenderState)
    return NULL;
  
  return api->render.newRenderState(device, pipeline);
}

GPU_HIDE
void
gpuPipelineSetFunction(GPURenderPipeline * __restrict pipeline,
                       GPUFunction       * __restrict func,
                       GPUFunctionType                functionType) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.setFunction)
    return;
  
  api->render.setFunction(pipeline, func, functionType);
}

GPU_HIDE
void
gpuPipelineSetColorFormat(GPURenderPipeline * __restrict pipeline,
                          uint32_t                       index,
                          GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.colorFormat)
    return;
  
  api->render.colorFormat(pipeline, index, pixelFormat);
}

GPU_HIDE
void
gpuPipelineSetDepthFormat(GPURenderPipeline * __restrict pipeline,
                          GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.depthFormat)
    return;
  
  api->render.depthFormat(pipeline, pixelFormat);
}

GPU_HIDE
void
gpuPipelineSetStencilFormat(GPURenderPipeline * __restrict pipeline,
                            GPUPixelFormat                pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.stencilFormat)
    return;

  api->render.stencilFormat(pipeline, pixelFormat);
}

GPU_HIDE
void
gpuPipelineSetSampleCount(GPURenderPipeline * __restrict pipeline,
                          uint32_t                       sampleCount) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.sampleCount)
    return;
  
  api->render.sampleCount(pipeline, sampleCount);
}
