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

static bool
gpu_blendStateIsDefault(const GPUBlendState *blend) {
  return !blend->enabled &&
         (blend->writeMask == 0 || blend->writeMask == GPU_COLOR_WRITE_ALL);
}

static bool
gpu_depthStencilStateIsDefault(const GPUDepthStencilState *state) {
  return !state ||
         (!state->depthTestEnable &&
          !state->depthWriteEnable &&
          !state->stencilTestEnable &&
          state->stencilReadMask == 0 &&
          state->stencilWriteMask == 0);
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
gpu_createVertexDescriptorFromState(const GPUVertexState *state) {
  GPUVertexDescriptor *desc;
  uint32_t i, j;

  if (state->bufferLayoutCount == 0)
    return NULL;
  if (!state->pBufferLayouts)
    return NULL;

  desc = GPUNewVertexDesc();
  if (!desc)
    return NULL;

  for (i = 0; i < state->bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *layout = &state->pBufferLayouts[i];

    if (layout->attributeCount > 0 && !layout->pAttributes)
      return NULL;

    GPULayout(desc, i, layout->strideBytes, 1, gpu_vertexStepFunction(layout->stepMode));
    for (j = 0; j < layout->attributeCount; j++) {
      const GPUVertexAttribute *attr = &layout->pAttributes[j];

      GPUAttrib(desc, attr->shaderLocation, attr->format, attr->offset, i);
    }
  }

  return desc;
}

static bool
gpu_pipelineInfoIsSupported(const GPURenderPipelineCreateInfo *info) {
  uint32_t i;

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO) {
    return false;
  }
  if (info->primitiveTopology != GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    return false;
  if (info->multisample.alphaToCoverageEnable)
    return false;
  if (info->multisample.sampleMask != 0 &&
      info->multisample.sampleMask != 0xffffffffu)
    return false;
  if (!gpu_depthStencilStateIsDefault(info->pDepthStencilState))
    return false;
  for (i = 0; i < info->colorTargetCount; i++) {
    if (!gpu_blendStateIsDefault(&info->pColorTargets[i].blend))
      return false;
  }

  return true;
}

GPU_EXPORT
GPUResult
GPUCreateRenderPipeline(GPUDevice                         * __restrict device,
                        const GPURenderPipelineCreateInfo * __restrict info,
                        GPURenderPipeline                ** __restrict outPipeline) {
  GPURenderPipelineState *state;
  GPURenderPipeline      *pipeline;
  GPUVertexDescriptor    *vertexDesc;
  GPUFunction            *vertexFunc;
  GPUFunction            *fragmentFunc;
  GPUFormat               colorFormat;
  uint32_t                i;
  uint32_t                sampleCount;

  if (!outPipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outPipeline = NULL;

  if (!device || !info || !info->library || !info->vertexEntry || !info->fragmentEntry)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (info->colorTargetCount == 0 || !info->pColorTargets)
    return GPU_ERROR_INVALID_ARGUMENT;
  if (!gpu_pipelineInfoIsSupported(info))
    return GPU_ERROR_INVALID_ARGUMENT;

  vertexFunc = GPUShaderFunction(info->library, info->vertexEntry);
  fragmentFunc = GPUShaderFunction(info->library, info->fragmentEntry);
  if (!vertexFunc || !fragmentFunc)
    return GPU_ERROR_INVALID_ARGUMENT;

  colorFormat = info->pColorTargets[0].format;
  pipeline = GPUNewRenderPipeline(colorFormat);
  if (!pipeline)
    return GPU_ERROR_BACKEND_FAILURE;

  GPUSetFunction(pipeline, vertexFunc, GPU_FUNCTION_VERT);
  GPUSetFunction(pipeline, fragmentFunc, GPU_FUNCTION_FRAG);

  vertexDesc = gpu_createVertexDescriptorFromState(&info->vertex);
  if (info->vertex.bufferLayoutCount > 0 && !vertexDesc) {
    GPUDestroyRenderPipeline(pipeline);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (vertexDesc)
    GPUSetVertexDesc(pipeline, vertexDesc);

  for (i = 0; i < info->colorTargetCount; i++)
    GPUColorFormat(pipeline, i, info->pColorTargets[i].format);

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    GPUDepthFormat(pipeline, info->depthStencilFormat);
    GPUStencilFormat(pipeline, info->depthStencilFormat);
  }

  sampleCount = info->multisample.sampleCount > 0 ? info->multisample.sampleCount : 1;
  GPUSampleCount(pipeline, sampleCount);

  state = GPUNewRenderState(device, pipeline);
  if (!state) {
    GPUDestroyRenderPipeline(pipeline);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  free(state);
  pipeline->_primitiveTopology = info->primitiveTopology;
  pipeline->_cullMode = info->cullMode;
  pipeline->_frontFace = info->frontFace;
  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyRenderPipeline(GPURenderPipeline *pipeline) {
  free(pipeline);
}

GPU_EXPORT
GPURenderPipeline*
GPUNewRenderPipeline(GPUPixelFormat pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->render.newRenderPipeline(pixelFormat);
}

GPU_EXPORT
GPURenderPipelineState*
GPUNewRenderState(GPUDevice         * __restrict device,
                  GPURenderPipeline * __restrict pipeline) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  
  return api->render.newRenderState(device, pipeline);
}

GPU_EXPORT
void
GPUSetFunction(GPURenderPipeline * __restrict pipline,
               GPUFunction       * __restrict func,
               GPUFunctionType                functype) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.setFunction(pipline, func, functype);
}

GPU_EXPORT
void
GPUColorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.colorFormat(pipline, index, pixelFormat);
}

GPU_EXPORT
void
GPUDepthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat                 pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.depthFormat(pipline, pixelFormat);
}

GPU_EXPORT
void
GPUStencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat                pixelFormat) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  api->render.stencilFormat(pipline, pixelFormat);
}

GPU_EXPORT
void
GPUSampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                       sampleCount) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->render.sampleCount(pipline, sampleCount);
}
