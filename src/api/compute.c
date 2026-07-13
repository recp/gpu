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
#include "device_internal.h"
#include "library_internal.h"
#include "pipeline_cache_internal.h"

static GPUDevice *
gpu_computePassDevice(const GPUComputePassEncoder *pass) {
  if (!pass) {
    return NULL;
  }
  if (pass->_cmdb && pass->_cmdb->_queue) {
    return pass->_cmdb->_queue->_device;
  }

  return pass->_pipelineLayout ? pass->_pipelineLayout->_device : NULL;
}

static GPUApi *
gpu_computePassApi(const GPUComputePassEncoder *pass) {
  return gpuDeviceApi(gpu_computePassDevice(pass));
}

#if GPU_BUILD_WITH_VALIDATION
static void
gpu_computeValidationError(const GPUComputePassEncoder *pass,
                           const char                    *message) {
  gpuDeviceRecordValidationError(gpu_computePassDevice(pass), message);
}

static inline bool
gpu_computeBindingsComplete(const GPUComputePassEncoder *pass) {
  if (!gpuDeviceValidationEnabled(gpu_computePassDevice(pass))) {
    return true;
  }

  return gpuPipelineLayoutMaskIsBound(pass->_pipelineLayout,
                                      pass->_boundGroupLayouts,
                                      GPU_ENCODER_MAX_BIND_GROUPS,
                                      pass->_requiredBindGroupMask);
}
#else
#  define gpu_computeValidationError(pass, message) ((void)0)
#  define gpu_computeBindingsComplete(pass) true
#endif

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
gpu_validIndirectBatch(GPUBuffer *argsBuffer,
                       uint64_t argsOffset,
                       uint32_t commandCount,
                       uint32_t strideBytes,
                       uint32_t commandSize) {
  uint64_t maxCommandIndex;
  uint64_t lastCommandOffset;

  if (!gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      (argsOffset & 3u) != 0u ||
      commandCount == 0u ||
      strideBytes < commandSize ||
      (strideBytes & 3u) != 0u ||
      commandSize > UINT64_MAX - argsOffset) {
    return false;
  }

  maxCommandIndex = (uint64_t)commandCount - 1u;
  if (maxCommandIndex >
      (UINT64_MAX - argsOffset - commandSize) / strideBytes) {
    return false;
  }

  lastCommandOffset = argsOffset + maxCommandIndex * strideBytes;
  return gpuBufferRangeValid(argsBuffer, lastCommandOffset, commandSize);
}

static GPUComputePipelineState *
gpuCompileComputePipelineState(GPUDevice *device, GPUComputePipeline *pipeline) {
  GPUApi *api;

  if (!(api = gpuDeviceApi(device)) || !api->compute.newComputeState) {
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
  GPUComputePipeline      *pipeline;
  GPUFunction             *function;
  GPUApi                  *api;
  uint32_t                 requiredBindGroupMask;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;
  if (!device || !info || !info->layout || !info->library || !info->entryPoint) {
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
  if (info->cache && !info->chain.pNext) {
    GPUResult result;

    result = gpuPipelineCacheFindCompute(info->cache, info, &pipeline);
    if (result != GPU_OK) {
      return result;
    }
    if (pipeline) {
      *outPipeline = pipeline;
      return GPU_OK;
    }
  }
  {
    GPUShaderStageFlags stage;

    if (gpuGetShaderLibraryEntryStage(info->library, info->entryPoint, &stage) &&
        stage != GPU_SHADER_STAGE_COMPUTE_BIT) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }
  {
    const char *entries[] = {info->entryPoint};

    if (!gpuPipelineLayoutMatchesShaderEntries(info->layout,
                                               info->library,
                                               entries,
                                               (uint32_t)GPU_ARRAY_LEN(entries),
                                               GPU_SHADER_STAGE_COMPUTE_BIT,
                                               &requiredBindGroupMask)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (api->compute.createPipeline) {
    GPUResult result;

    pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    pipeline->_api      = api;
    pipeline->_device   = device;
    pipeline->_refCount = 1u;
    result = api->compute.createPipeline(device, info, pipeline);
    if (result != GPU_OK) {
      free(pipeline);
      return result;
    }
    state = pipeline->_state;
    if (!state) {
      GPUDestroyComputePipeline(pipeline);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  } else {
    if (!api->compute.newComputePipeline || !api->compute.setFunction) {
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

    pipeline->_api      = api;
    pipeline->_device   = device;
    pipeline->_refCount = 1u;
    api->compute.setFunction(pipeline, function);
    state = gpuCompileComputePipelineState(device, pipeline);
    if (!state) {
      GPUDestroyComputePipeline(pipeline);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  if (!gpuGetShaderLibraryComputeWorkgroupSize(info->library,
                                               info->entryPoint,
                                               state->workgroupSize)) {
    state->workgroupSize[0] = 1u;
    state->workgroupSize[1] = 1u;
    state->workgroupSize[2] = 1u;
  }
  pipeline->_layout = info->layout;
  pipeline->_requiredBindGroupMask = requiredBindGroupMask;
  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &pipeline->_pushConstantSizeBytes,
                                    &pipeline->_pushConstantStages);
  if (info->cache && !info->chain.pNext) {
    pipeline = gpuPipelineCacheStoreCompute(info->cache, info, pipeline);
  } else {
    gpuRecordPipelineCompile(device, info->cache);
  }
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

  if (!gpuReleaseComputePipeline(pipeline)) {
    return;
  }

  if ((api = pipeline->_api) && api->compute.destroyComputePipeline) {
    api->compute.destroyComputePipeline(pipeline);
    return;
  }

  free(pipeline);
}

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePass(GPUCommandBuffer *cmdb, const char *label) {
  GPUDevice *device;
  GPUApi    *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return NULL;
  }
  device = cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!(api = gpuDeviceApi(device)) || !api->compute.computeCommandEncoder) {
    return NULL;
  }

  {
    GPUComputePassEncoder *pass;

    label = gpuDeviceDebugLabel(device, label);
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
  GPUApi                  *api;

  if (!pass || pass->_ended || !pipeline || !pipeline->_state) {
    return;
  }
  if (pipeline->_device != gpu_computePassDevice(pass) ||
      pipeline->_api != gpu_computePassApi(pass)) {
    gpu_computeValidationError(pass,
                               "GPUBindComputePipeline skipped: device mismatch");
    return;
  }
  if (!(api = gpu_computePassApi(pass)) ||
      !api->compute.setComputePipelineState) {
    return;
  }

  gpuDeviceRecordBindRequest(pipeline->_device);
  if (pass->_pipeline == pipeline) {
    return;
  }

  if (pass->_pipelineLayout != pipeline->_layout) {
    memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
    memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
    memset(pass->_boundDynamicOffsetCounts,
           0,
           sizeof(pass->_boundDynamicOffsetCounts));
  }

  state = pipeline->_state;
  api->compute.setComputePipelineState(pass, state);
  gpuDeviceRecordBindEmission(pipeline->_device);
  pass->_hasPipeline             = true;
  pass->_pipeline                = pipeline;
  pass->_pipelineLayout          = pipeline->_layout;
  pass->_requiredBindGroupMask   = pipeline->_requiredBindGroupMask;
  pass->_pushConstantSizeBytes   = pipeline->_pushConstantSizeBytes;
  pass->_pushConstantStages      = pipeline->_pushConstantStages &
                                   GPU_SHADER_STAGE_COMPUTE_BIT;
  pass->_pushConstantsEmitted    = false;
  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
}

GPU_HIDE
void
gpuSetComputeBuffer(GPUComputePassEncoder *pass,
                    GPUBuffer             *buf,
                    uint64_t               off,
                    uint32_t               index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf) {
    return;
  }
  if (!(api = gpu_computePassApi(pass)) || !api->compute.buffer) {
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
  if (!(api = gpu_computePassApi(pass)) || !api->compute.texture) {
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
  if (!(api = gpu_computePassApi(pass)) || !api->compute.sampler) {
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
                        binding->offset,
                        binding->binding + binding->arrayIndex);
  } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
    gpuSetComputeTexture(bindCtx->pass,
                         binding->textureView,
                         binding->binding + binding->arrayIndex);
  } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
    gpuSetComputeSampler(bindCtx->pass,
                         binding->sampler,
                         binding->binding + binding->arrayIndex);
  }
}

GPU_EXPORT
void
GPUBindComputeGroup(GPUComputePassEncoder *pass,
                    uint32_t               groupIndex,
                    GPUBindGroup          *bindGroup,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *pDynamicOffsets) {
  GPUBindComputeContext ctx;
  GPUDevice            *device;
  GPUApi               *api;

  if (!pass || pass->_ended ||
      !bindGroup ||
      gpuBindGroupGetDevice(bindGroup) != gpu_computePassDevice(pass) ||
      groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS ||
      !gpuPipelineLayoutAcceptsBindGroup(pass->_pipelineLayout, groupIndex, bindGroup)) {
    return;
  }
  device = gpuBindGroupGetDevice(bindGroup);
  gpuDeviceRecordBindRequest(device);
  if (gpuBindGroupShadowMatches(
        pass->_boundGroups[groupIndex],
        pass->_boundDynamicOffsetCounts[groupIndex],
        pass->_boundDynamicOffsets[groupIndex],
        bindGroup,
        dynamicOffsetCount,
        pDynamicOffsets)) {
    return;
  }

  api = gpu_computePassApi(pass);
  if (api && api->descriptor.bindComputeGroup) {
    if (gpuValidateBindGroupDynamicOffsets(pass->_pipelineLayout,
                                           groupIndex,
                                           bindGroup,
                                           dynamicOffsetCount,
                                           pDynamicOffsets) &&
        api->descriptor.bindComputeGroup(pass,
                                         pass->_pipelineLayout,
                                         groupIndex,
                                         bindGroup,
                                         dynamicOffsetCount,
                                         pDynamicOffsets)) {
      pass->_boundGroups[groupIndex] = bindGroup;
      pass->_boundGroupLayouts[groupIndex] = gpuBindGroupGetLayout(bindGroup);
      gpuStoreBindGroupShadow(
        &pass->_boundDynamicOffsetCounts[groupIndex],
        pass->_boundDynamicOffsets[groupIndex],
        dynamicOffsetCount,
        pDynamicOffsets);
      gpuDeviceRecordBindEmission(device);
    }
    return;
  }

  ctx.pass = pass;
  if (gpuForEachBindGroupBindingWithDynamicOffsets(pass->_pipelineLayout,
                                                   groupIndex,
                                                   bindGroup,
                                                   dynamicOffsetCount,
                                                   pDynamicOffsets,
                                                   gpuBindComputeBinding,
                                                   &ctx)) {
    pass->_boundGroups[groupIndex] = bindGroup;
    pass->_boundGroupLayouts[groupIndex] = gpuBindGroupGetLayout(bindGroup);
    gpuStoreBindGroupShadow(
      &pass->_boundDynamicOffsetCounts[groupIndex],
      pass->_boundDynamicOffsets[groupIndex],
      dynamicOffsetCount,
      pDynamicOffsets);
    gpuDeviceRecordBindEmission(device);
  }
}

GPU_EXPORT
void
GPUSetComputePushConstants(GPUComputePassEncoder *pass,
                           uint32_t               offset,
                           uint32_t               sizeBytes,
                           const void            *data) {
  GPUDevice *device;
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
  device = gpu_computePassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if (pass->_pushConstantsEmitted &&
      memcmp(pass->_pushConstants + offset, data, sizeBytes) == 0) {
    return;
  }
  if (!(api = gpu_computePassApi(pass)) || !api->compute.pushConstants) {
    return;
  }

  memcpy(pass->_pushConstants + offset, data, sizeBytes);
  api->compute.pushConstants(pass,
                             pass->_pushConstants,
                             pass->_pushConstantSizeBytes);
  pass->_pushConstantsEmitted = true;
  gpuDeviceRecordStateEmission(device);
}

GPU_EXPORT
void
GPUDispatch(GPUComputePassEncoder *pass,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  if (!pass->_hasPipeline) {
    gpu_computeValidationError(pass, "GPUDispatch skipped: no compute pipeline bound");
    return;
  }
  if (!gpu_computeBindingsComplete(pass)) {
    gpu_computeValidationError(pass, "GPUDispatch skipped: missing compute bind group");
    return;
  }
  if (x == 0 || y == 0 || z == 0) {
    gpu_computeValidationError(pass, "GPUDispatch skipped: zero dispatch size");
    return;
  }
  if (!(api = gpu_computePassApi(pass)) || !api->compute.dispatch) {
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

  if (!pass || pass->_ended) {
    return;
  }
  if (!pass->_hasPipeline) {
    gpu_computeValidationError(pass, "GPUDispatchIndirect skipped: no compute pipeline bound");
    return;
  }
  if (!gpu_computeBindingsComplete(pass)) {
    gpu_computeValidationError(pass, "GPUDispatchIndirect skipped: missing compute bind group");
    return;
  }
  if (!gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      (argsOffset & 3u) != 0u ||
      !gpuBufferRangeValid(argsBuffer, argsOffset, 12u)) {
    gpu_computeValidationError(pass, "GPUDispatchIndirect skipped: invalid indirect buffer");
    return;
  }
  if (!(api = gpu_computePassApi(pass)) || !api->compute.dispatchIndirect) {
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
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  if (!pass->_hasPipeline) {
    gpu_computeValidationError(
      pass,
      "GPUMultiDispatchIndirect skipped: no compute pipeline bound"
    );
    return;
  }
  if (!gpu_computeBindingsComplete(pass)) {
    gpu_computeValidationError(
      pass,
      "GPUMultiDispatchIndirect skipped: missing compute bind group"
    );
    return;
  }
  if (!gpu_validIndirectBatch(argsBuffer,
                              argsOffset,
                              dispatchCount,
                              strideBytes,
                              12u)) {
    gpu_computeValidationError(
      pass,
      "GPUMultiDispatchIndirect skipped: invalid indirect batch"
    );
    return;
  }
  if (!(api = gpu_computePassApi(pass))) {
    return;
  }
  if (api->compute.multiDispatchIndirect &&
      api->compute.multiDispatchIndirect(pass,
                                         argsBuffer,
                                         argsOffset,
                                         dispatchCount,
                                         strideBytes)) {
    return;
  }
  if (!api->compute.dispatchIndirect) {
    return;
  }

  for (uint32_t i = 0; i < dispatchCount; i++) {
    api->compute.dispatchIndirect(pass,
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
  if (!(api = gpu_computePassApi(pass)) || !api->compute.endEncoding) {
    return;
  }

  api->compute.endEncoding(pass);
}
