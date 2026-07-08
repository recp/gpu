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
#include "compute_internal.h"
#include "descr/descriptor_internal.h"
#include "library_internal.h"

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

  if (!cmdb) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.computeCommandEncoder) {
    return NULL;
  }

  return api->compute.computeCommandEncoder(cmdb, label);
}

GPU_EXPORT
void
GPUBindComputePipeline(GPUComputePassEncoder *pass,
                       GPUComputePipeline    *pipeline) {
  GPUComputePipelineState *state;
  GPUApi *api;

  if (!pass || !pipeline || !pipeline->_state) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.setComputePipelineState) {
    return;
  }

  state = pipeline->_state;
  api->compute.setComputePipelineState(pass, state);
}

GPU_HIDE
void
gpuSetComputeBuffer(GPUComputePassEncoder *pass,
                    GPUBuffer             *buf,
                    size_t                 off,
                    uint32_t               index) {
  GPUApi *api;

  if (!pass || !buf) {
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

  if (!pass || !view) {
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

  if (!pass || !sampler) {
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

  (void)pDynamicOffsets;

  if (!pass || setIndex != 0 || dynamicOffsetCount != 0 || !bindGroup) {
    return;
  }

  ctx.pass = pass;
  gpuForEachBindGroupBinding(bindGroup, gpuBindComputeBinding, &ctx);
}

GPU_EXPORT
void
GPUDispatch(GPUComputePassEncoder *pass,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  GPUApi *api;

  if (!pass || x == 0 || y == 0 || z == 0) {
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

  if (!pass || !argsBuffer) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.dispatchIndirect) {
    return;
  }

  api->compute.dispatchIndirect(pass, argsBuffer, argsOffset);
}

GPU_EXPORT
void
GPUEndComputePass(GPUComputePassEncoder *pass) {
  GPUApi *api;

  if (!pass) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->compute.endEncoding) {
    return;
  }

  api->compute.endEncoding(pass);
}
