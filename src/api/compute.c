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

#include "../common.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"

static bool
gpu_validPushConstantRange(uint32_t limit,
                           uint32_t offset,
                           uint32_t sizeBytes,
                           const void *data) {
  if (sizeBytes == 0u) {
    return true;
  }
  if (!data || offset > limit) {
    return false;
  }

  return sizeBytes <= limit - offset;
}

static bool
gpu_validIndirectBatch(uint64_t argsOffset,
                       uint32_t commandCount,
                       uint32_t strideBytes) {
  uint64_t maxCommandIndex;

  if (commandCount == 0u || strideBytes == 0u) {
    return false;
  }

  maxCommandIndex = (uint64_t)commandCount - 1u;
  return maxCommandIndex <= (UINT64_MAX - argsOffset) / strideBytes;
}

static GPUComputePipelineState *
gpuCompileComputePipelineState(GPUDevice *device, GPUComputePipeline *pipeline) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()) || !api->compute.newComputeState) {
    return NULL;
  }

  return api->compute.newComputeState(device, pipeline);
}

GPU_EXPORT
GPUResult
GPUCreateComputePipeline(GPUDevice                          * __restrict device,
                         const GPUComputePipelineCreateInfo * __restrict info,
                         GPUComputePipeline                ** __restrict outPipeline) {
  GPUComputePipelineState *state;
  GPUComputePipeline *pipeline;
  GPUFunction *function;
  GPUApi *api;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;
  if (!device || !info || !info->library || !info->entryPoint) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->cache && info->cache->device != device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  {
    GPUShaderStageFlags stage;

    if (gpuGetShaderLibraryEntryStage(info->library, info->entryPoint, &stage) &&
        stage != GPU_SHADER_STAGE_COMPUTE_BIT) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (!(api = gpuActiveGPUApi()) ||
      !api->compute.newComputePipeline ||
      !api->compute.setFunction) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  function = GPUShaderFunction(info->library, info->entryPoint);
  if (!function) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  pipeline = api->compute.newComputePipeline();
  if (!pipeline) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  api->compute.setFunction(pipeline, function);
  state = gpuCompileComputePipelineState(device, pipeline);
  if (!state) {
    GPUDestroyComputePipeline(pipeline);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!gpuGetShaderLibraryComputeWorkgroupSize(info->library,
                                               info->entryPoint,
                                               state->workgroupSize)) {
    state->workgroupSize[0] = 1u;
    state->workgroupSize[1] = 1u;
    state->workgroupSize[2] = 1u;
  }
  gpuRecordPipelineCompile(device, info->cache);
  pipeline->_layout = info->layout;
  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &pipeline->_pushConstantSizeBytes,
                                    &pipeline->_pushConstantStages);
  *outPipeline = pipeline;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUApi *api;

  if (!pipeline) {
    return;
  }

  if ((api = gpuActiveGPUApi()) && api->compute.destroyComputePipeline) {
    api->compute.destroyComputePipeline(pipeline);
    return;
  }

  free(pipeline);
}

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePass(GPUCommandBuffer *cmdb, const char *label) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.computeCommandEncoder) {
    return NULL;
  }

  {
    GPUComputePassEncoder *pass;

    pass = api->compute.computeCommandEncoder(cmdb, label);
    if (pass) {
      pass->_cmdb = cmdb;
      cmdb->_activeEncoder = true;
    }
    return pass;
  }
}

GPU_EXPORT
void
GPUBindComputePipeline(GPUComputePassEncoder *pass,
                       GPUComputePipeline    *pipeline) {
  GPUComputePipelineState *state;
  GPUApi *api;

  if (!pass || pass->_ended || !pipeline || !pipeline->_state) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.setComputePipelineState) {
    return;
  }

  if (pass->_pipelineLayout != pipeline->_layout) {
    memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
    memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
  }

  state = pipeline->_state;
  api->compute.setComputePipelineState(pass, state);
  pass->_hasPipeline = true;
  pass->_pipelineLayout = pipeline->_layout;
  pass->_pushConstantSizeBytes = pipeline->_pushConstantSizeBytes;
  pass->_pushConstantStages = pipeline->_pushConstantStages & GPU_SHADER_STAGE_COMPUTE_BIT;
  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
}

GPU_HIDE
void
gpuSetComputeBuffer(GPUComputePassEncoder *pass,
                    GPUBuffer             *buf,
                    size_t                 off,
                    uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.buffer) {
    return;
  }

  api->compute.buffer(pass, buf, off, index);
}

GPU_HIDE
void
gpuSetComputeTexture(GPUComputePassEncoder *pass,
                     GPUTextureView        *view,
                     uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !view) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.texture) {
    return;
  }

  api->compute.texture(pass, view, index);
}

GPU_HIDE
void
gpuSetComputeSampler(GPUComputePassEncoder *pass,
                     GPUSampler            *sampler,
                     uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !sampler) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.sampler) {
    return;
  }

  api->compute.sampler(pass, sampler, index);
}

typedef struct GPUBindComputeContext {
  GPUComputePassEncoder *pass;
} GPUBindComputeContext;

static void
gpuBindComputeBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  GPUBindComputeContext *bindCtx;

  if (!ctx || !binding ||
      (binding->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u) {
    return;
  }

  bindCtx = ctx;
  if (binding->kind == GPUBindKindBuffer && binding->buffer) {
    gpuSetComputeBuffer(bindCtx->pass,
                        binding->buffer,
                        (size_t)binding->offset,
                        binding->binding);
  } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
    gpuSetComputeTexture(bindCtx->pass,
                         binding->textureView,
                         binding->binding);
  } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
    gpuSetComputeSampler(bindCtx->pass,
                         binding->sampler,
                         binding->binding);
  }
}

GPU_EXPORT
void
GPUBindComputeGroup(GPUComputePassEncoder *pass,
                    uint32_t               setIndex,
                    GPUBindGroup          *bindGroup,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *pDynamicOffsets) {
  GPUBindComputeContext ctx;

  if (!pass || pass->_ended ||
      setIndex != 0 || !bindGroup ||
      setIndex >= GPU_ENCODER_MAX_BIND_GROUPS ||
      !gpuPipelineLayoutAcceptsBindGroup(pass->_pipelineLayout, setIndex, bindGroup)) {
    return;
  }
  if (dynamicOffsetCount == 0u && pass->_boundGroups[setIndex] == bindGroup) {
    return;
  }

  ctx.pass = pass;
  if (gpuForEachBindGroupBindingWithDynamicOffsets(bindGroup,
                                                   dynamicOffsetCount,
                                                   pDynamicOffsets,
                                                   gpuBindComputeBinding,
                                                   &ctx)) {
    pass->_boundGroups[setIndex] = bindGroup;
    pass->_boundGroupLayouts[setIndex] = gpuBindGroupGetLayout(bindGroup);
  }
}

GPU_EXPORT
void
GPUSetComputePushConstants(GPUComputePassEncoder *pass,
                           uint32_t               offset,
                           uint32_t               sizeBytes,
                           const void            *data) {
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      pass->_pushConstantSizeBytes == 0u ||
      pass->_pushConstantStages == 0u) {
    return;
  }
  if (!gpu_validPushConstantRange(pass->_pushConstantSizeBytes,
                                  offset,
                                  sizeBytes,
                                  data)) {
    return;
  }
  if (sizeBytes == 0u) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.pushConstants) {
    return;
  }

  memcpy(pass->_pushConstants + offset, data, sizeBytes);
  api->compute.pushConstants(pass,
                             pass->_pushConstants,
                             pass->_pushConstantSizeBytes);
}

GPU_EXPORT
void
GPUDispatch(GPUComputePassEncoder *pass,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      x == 0 || y == 0 || z == 0) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.dispatch) {
    return;
  }

  api->compute.dispatch(pass, x, y, z);
}

GPU_EXPORT
void
GPUDispatchIndirect(GPUComputePassEncoder *pass,
                    GPUBuffer            *argsBuffer,
                    uint64_t              argsOffset) {
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      !gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      !gpuBufferRangeValid(argsBuffer, argsOffset, 12u)) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.dispatchIndirect) {
    return;
  }

  api->compute.dispatchIndirect(pass, argsBuffer, argsOffset);
}

GPU_EXPORT
void
GPUMultiDispatchIndirect(GPUComputePassEncoder *pass,
                         GPUBuffer            *argsBuffer,
                         uint64_t              argsOffset,
                         uint32_t              dispatchCount,
                         uint32_t              strideBytes) {
  if (!gpu_validIndirectBatch(argsOffset, dispatchCount, strideBytes)) {
    return;
  }

  for (uint32_t i = 0; i < dispatchCount; i++) {
    GPUDispatchIndirect(pass,
                        argsBuffer,
                        argsOffset + (uint64_t)i * strideBytes);
  }
}

GPU_EXPORT
void
GPUEndComputePass(GPUComputePassEncoder *pass) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  pass->_ended = true;
  if (pass->_cmdb) {
    pass->_cmdb->_activeEncoder = false;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.endEncoding) {
    return;
  }

  api->compute.endEncoding(pass);
}
