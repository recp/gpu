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

static bool
cuda__validComputeInterface(const GPUDevice                    *device,
                            const GPUComputePipelineCreateInfo *info,
                            ShaderPTXEntryView                 *outPTX) {
  GPUShaderReflection                reflection;
  ShaderPTXEntryView                 ptx;
  const GPUShaderResourceReflection *resource;
  const ShaderPTXParamInfo          *param;
  GPUShaderStageFlags                stage;
  uint32_t                           i;
  uint32_t                           size;
  bool                               supported;

  memset(&ptx, 0, sizeof(ptx));

  if (!info || !info->library || !info->entryPoint || !info->entryPoint[0]
      || !shaderEntryView(info->library,
                          info->entryPoint,
                          &stage,
                          &reflection)
      || !getShaderLibraryPTXEntry(info->library, info->entryPoint, &ptx)
      || stage != GPU_SHADER_STAGE_COMPUTE_BIT
      || reflection.pushConstantSizeBytes != 0u
      || reflection.pushConstantStages != 0u
      || ptx.paramCount > GPU_SHADER_PTX_MAX_PARAM_COUNT
      || ptx.paramDataSize > GPU_SHADER_PTX_MAX_PARAM_BYTES) {
    return false;
  }

  for (i = 0u; i < reflection.resourceCount; i++) {
    CudaFormatInfo    format;

    resource  = &reflection.pResources[i];
    supported = resource->bindingType == GPU_BINDING_UNIFORM_BUFFER
                || resource->bindingType == GPU_BINDING_STORAGE_BUFFER
                || resource->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                || resource->bindingType == GPU_BINDING_SAMPLED_TEXTURE
                || resource->bindingType == GPU_BINDING_SAMPLER;

    if (resource->bindingType == GPU_BINDING_STORAGE_TEXTURE) {
      supported = cuda_textureStorageViewSupported(resource->storageTexture.viewType)
                  && cuda_formatInfo(resource->storageTexture.format, &format)
                  && (format.flags & GPU_CUDA_FORMAT_STORAGE_BIT) != 0u;
    }

    if (!supported || resource->arrayCount == 0u
        || (resource->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u
        || (resource->arrayCount > 1u
            && !GPUIsFeatureEnabled(device, GPU_FEATURE_DESCRIPTOR_INDEXING))) {
      return false;
    }
  }

  for (i = 0u; i < ptx.paramCount; i++) {
    param = &ptx.params[i];
    size  = cuda_ptxParamSize(param->kind);

    if ((param->kind != GPUShaderPTXParamBuffer
         && param->kind != GPUShaderPTXParamSurface
         && param->kind != GPUShaderPTXParamTexture
         && param->kind != GPUShaderPTXParamSampledTexture
         && param->kind != GPUShaderPTXParamTextureMetadata)
        || (param->kind == GPUShaderPTXParamBuffer
            && param->bindingType != GPU_BINDING_UNIFORM_BUFFER
            && param->bindingType != GPU_BINDING_STORAGE_BUFFER
            && param->bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER)
        || (param->kind == GPUShaderPTXParamSurface
            && param->bindingType != GPU_BINDING_STORAGE_TEXTURE)
        || (param->kind == GPUShaderPTXParamTexture
            && param->bindingType != GPU_BINDING_SAMPLED_TEXTURE)
        || (param->kind == GPUShaderPTXParamSampledTexture
            && param->bindingType != GPU_BINDING_SAMPLED_TEXTURE)
        || (param->kind == GPUShaderPTXParamTextureMetadata
            && param->bindingType != GPU_BINDING_STORAGE_TEXTURE
            && param->bindingType != GPU_BINDING_SAMPLED_TEXTURE)
        || size == 0u || ptx.paramDataSize < size
        || param->dataOffset > ptx.paramDataSize - size) {
      return false;
    }
  }

  if (outPTX) {
    *outPTX = ptx;
  }

  return true;
}

static GPUResult
cuda_createComputePipeline(GPUDevice                          *device,
                           const GPUComputePipelineCreateInfo *info,
                           GPUComputePipeline                 *pipeline) {
  ShaderPTXEntryView                ptx;
  ShaderSourceBlob                  source = {0};
  uint32_t                          block[3];
  ComputePipelineCuda              *native;
  DeviceCuda                       *deviceNative;
  ShaderLibraryCuda                *library;
  CudaModule                       *module;
  const GPUPipelineConstants        *constants;
  const ShaderStaticSamplerInfo    *staticSamplers;
  ShaderPTXParamInfo               *param;
  uint64_t                          entryBit;
  uint64_t                          threadCount;
  size_t                            nativeSize;
  size_t                            paramBytes;
  size_t                            samplerBytes;
  uint32_t                          entryStaticSamplerCount;
  uint32_t                          staticSamplerCount;
  uint32_t                          i;
  uint32_t                          j;
  uint32_t                          samplerIndex;
  uint32_t                          sourceIndex;
  CUresult                          result;
  GPUResult                         compiled;

  deviceNative = cuda_device(device);
  library      = info && info->library ? info->library->_priv : NULL;
  module       = library ? library->module : NULL;

  if (!deviceNative || !pipeline || !module
      || !cuda__validComputeInterface(device, info, &ptx)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!getShaderLibraryComputeWorkgroupSize(info->library,
                                            info->entryPoint,
                                            block)) {
    block[0] = block[1] = block[2] = 1u;
  }

  threadCount = (uint64_t)block[0] * block[1] * block[2];

  if (block[0] == 0u || block[1] == 0u || block[2] == 0u
      || block[0] > deviceNative->maxBlockDim[0]
      || block[1] > deviceNative->maxBlockDim[1]
      || block[2] > deviceNative->maxBlockDim[2]
      || threadCount > deviceNative->maxThreadsPerBlock) {
    return GPU_ERROR_UNSUPPORTED;
  }

  staticSamplers = getShaderLibraryStaticSamplers(info->library,
                                                  &staticSamplerCount);
  entryBit       = shaderEntryBit(info->library, info->entryPoint);

  if (staticSamplerCount > 0u && (!staticSamplers || entryBit == 0u)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  entryStaticSamplerCount = 0u;

  for (i = 0u; i < staticSamplerCount; i++) {
    entryStaticSamplerCount += (staticSamplers[i].entryMask & entryBit) != 0u;
  }

  paramBytes   = (size_t)ptx.paramCount * sizeof(native->params[0]);
  samplerBytes = (size_t)entryStaticSamplerCount * sizeof(CUDA_TEXTURE_DESC);

  if ((ptx.paramCount > 0u && paramBytes / sizeof(native->params[0]) != ptx.paramCount)
      || (entryStaticSamplerCount > 0u
          && samplerBytes / sizeof(CUDA_TEXTURE_DESC) != entryStaticSamplerCount)
      || paramBytes > SIZE_MAX - sizeof(*native)
      || samplerBytes > SIZE_MAX - sizeof(*native) - paramBytes) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  nativeSize = sizeof(*native) + paramBytes + samplerBytes;

  if (!(native = calloc(1, nativeSize))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->pipeline = pipeline;

  constants = pipelineConstants(info->chain.pNext);

  if (constants && constants->constantCount != 0u) {
    compiled = compileShaderLibraryEntry(info->library, info->entryPoint, constants, &source);

    if (compiled != GPU_OK) {
      free(native);
      return compiled;
    }

    module = cuda_createModule(device, source.data, source.size);
    freeShaderSourceBlob(&source);

    if (!module) {
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  } else {
    cuda_retainModule(module);
  }

  result = cuda_getModuleFunction(module, info->entryPoint, &native->function);

  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "PTX entry lookup");
    cuda_releaseModule(module);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->module         = module;
  native->paramCount     = ptx.paramCount;
  native->paramDataSize  = ptx.paramDataSize;
  native->staticSamplers = (CUDA_TEXTURE_DESC *)((uint8_t *)native->params + paramBytes);

  if (ptx.paramCount > 0u) {
    memcpy(native->params,
           ptx.params,
           (size_t)ptx.paramCount * sizeof(native->params[0]));
  }

  for (i = 0u; i < staticSamplerCount; i++) {
    if ((staticSamplers[i].entryMask & entryBit) == 0u) {
      continue;
    }

    if (!cuda_staticSamplerTextureDesc(&staticSamplers[i].desc,
                                       &native->staticSamplers[native->staticSamplerCount])) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    native->staticSamplerCount++;
  }

  for (i = 0u; i < ptx.paramCount; i++) {
    param = &native->params[i];

    if (param->kind != GPUShaderPTXParamSampledTexture
        || param->staticSamplerId == UINT32_MAX) {
      continue;
    }

    sourceIndex = param->staticSamplerId;

    if (sourceIndex >= staticSamplerCount
        || (staticSamplers[sourceIndex].entryMask & entryBit) == 0u) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    samplerIndex = 0u;

    for (j = 0u; j < sourceIndex; j++) {
      samplerIndex += (staticSamplers[j].entryMask & entryBit) != 0u;
    }

    if (samplerIndex >= native->staticSamplerCount) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    param->staticSamplerId = samplerIndex;
  }

  native->base._priv            = native;
  native->base.workgroupSize[0] = block[0];
  native->base.workgroupSize[1] = block[1];
  native->base.workgroupSize[2] = block[2];
  pipeline->_priv               = native;
  pipeline->_state              = &native->base;

  return GPU_OK;
}

static void
cuda_destroyComputePipeline(GPUComputePipeline *pipeline) {
  ComputePipelineCuda    *native;

  native = pipeline ? pipeline->_state : NULL;

  if (native)
    cuda_releaseModule(native->module);

  free(native);
  free(pipeline);
}

static GPUComputePassEncoder*
cuda_computeCommandEncoder(GPUCommandBuffer               *cmdb,
                           const GPUComputePassCreateInfo *info) {
  CommandCuda    *command;

  GPU__UNUSED(info);
  command = cuda_command(cmdb);

  if (!command || command->recordResult != GPU_OK) {
    return NULL;
  }

  memset(&command->compute, 0, sizeof(command->compute));
  command->compute._priv             = command;
  command->compute._workgroupSize[0] = 1u;
  command->compute._workgroupSize[1] = 1u;
  command->compute._workgroupSize[2] = 1u;
  command->pipeline                  = NULL;
  memset(command->boundParamMask, 0, sizeof(command->boundParamMask));

  return &command->compute;
}

static void
cuda_setComputePipeline(GPUComputePassEncoder   *encoder,
                        ComputePipelineState    *state) {
  CommandCuda            *command;
  ComputePipelineCuda    *native;

  command = encoder ? encoder->_priv : NULL;
  native  = state ? state->_priv : NULL;

  if (!command || !native || !native->function) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }

  if (command->pipeline != native) {
    memset(command->boundParamMask, 0, sizeof(command->boundParamMask));
  }

  command->pipeline          = native;
  encoder->_workgroupSize[0] = state->workgroupSize[0];
  encoder->_workgroupSize[1] = state->workgroupSize[1];
  encoder->_workgroupSize[2] = state->workgroupSize[2];
  cuda_rebindComputeGroups(encoder);
}

static bool
cuda__paramsBound(const CommandCuda    *command) {
  const ComputePipelineCuda    *pipeline;
  uint32_t                      i;

  pipeline = command ? command->pipeline : NULL;

  if (!pipeline) {
    return false;
  }

  for (i = 0u; i < pipeline->paramCount; i++) {
    if ((command->boundParamMask[i / 64u] & (UINT64_C(1) << (i % 64u))) == 0u) {
      return false;
    }
  }

  return true;
}

static uint8_t*
cuda__reserveParamData(GPUComputePassEncoder *encoder,
                       CommandCuda           *command,
                       uint32_t               size) {
  uint8_t *data;
  uint32_t capacity;
  uint32_t oldCapacity;

  if (!encoder || !command || size == 0u
      || command->paramDataCount > UINT32_MAX - size) {
    return NULL;
  }

  if (command->paramDataCount + size <= command->paramDataCapacity) {
    data = command->paramData + command->paramDataCount;
    command->paramDataCount += size;
    return data;
  }

  capacity = command->paramDataCapacity ? command->paramDataCapacity : GPU_SHADER_PTX_MAX_PARAM_BYTES;

  while (capacity < command->paramDataCount + size) {
    if (capacity > UINT32_MAX / 2u) {
      return NULL;
    }

    capacity *= 2u;
  }

  oldCapacity = command->paramDataCapacity;

  if (!(data = realloc(command->paramData, capacity))) {
    return NULL;
  }

  command->paramData         = data;
  command->paramDataCapacity = capacity;
  data += command->paramDataCount;
  command->paramDataCount += size;
  deviceRecordHotPathAlloc(encoder->_device, capacity - oldCapacity);

  return data;
}

static void
cuda_dispatch(GPUComputePassEncoder *encoder,
              uint32_t               x,
              uint32_t               y,
              uint32_t               z) {
  CommandCuda     *command;
  DeviceCuda      *device;
  DispatchCuda    *dispatch;
  DispatchCuda    *dispatches;
  uint8_t         *paramData;
  size_t           size;
  uint32_t         capacity;

  command = encoder ? encoder->_priv : NULL;
  device  = encoder ? cuda_device(encoder->_device) : NULL;

  if (!command || command->recordResult != GPU_OK || !command->pipeline
      || !cuda__paramsBound(command) || !device
      || encoder->_workgroupSize[0] == 0u
      || encoder->_workgroupSize[1] == 0u
      || encoder->_workgroupSize[2] == 0u
      || x > device->maxGridDim[0] || y > device->maxGridDim[1]
      || z > device->maxGridDim[2]) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }

  if (command->dispatchCount == command->dispatchCapacity) {
    if (command->dispatchCapacity > UINT32_MAX / 2u) {
      command->recordResult = GPU_ERROR_OUT_OF_MEMORY;
      return;
    }

    capacity = command->dispatchCapacity * 2u;

    if ((size_t)capacity > SIZE_MAX / sizeof(*dispatches)) {
      command->recordResult = GPU_ERROR_OUT_OF_MEMORY;
      return;
    }

    size = (size_t)capacity * sizeof(*dispatches);

    if (!(dispatches = realloc(command->dispatches, size))) {
      command->recordResult = GPU_ERROR_OUT_OF_MEMORY;
      return;
    }

    command->dispatches       = dispatches;
    command->dispatchCapacity = capacity;
    deviceRecordHotPathAlloc(encoder->_device, size);
  }

  dispatch = &command->dispatches[command->dispatchCount];
  memset(dispatch, 0, sizeof(*dispatch));
  dispatch->paramDataSize = command->pipeline->paramDataSize;

  if (dispatch->paramDataSize <= sizeof(dispatch->inlineParams)) {
    paramData = dispatch->inlineParams;
  } else {
    dispatch->paramDataOffset = command->paramDataCount;

    if (!(paramData = cuda__reserveParamData(encoder,
                                             command,
                                             dispatch->paramDataSize))) {
      command->recordResult = GPU_ERROR_OUT_OF_MEMORY;
      return;
    }
  }

  if (dispatch->paramDataSize > 0u) {
    memcpy(paramData, command->boundParams, dispatch->paramDataSize);
  }

  command->dispatchCount++;
  dispatch->pipeline = command->pipeline->pipeline;
  dispatch->grid[0]  = x;
  dispatch->grid[1]  = y;
  dispatch->grid[2]  = z;
  dispatch->block[0] = encoder->_workgroupSize[0];
  dispatch->block[1] = encoder->_workgroupSize[1];
  dispatch->block[2] = encoder->_workgroupSize[2];
  retainComputePipeline(dispatch->pipeline);
}

static void
cuda_endComputePass(GPUComputePassEncoder *encoder) {
  GPU__UNUSED(encoder);
}

void
cuda_initCompute(ApiCompute    *api) {
  api->createPipeline          = cuda_createComputePipeline;
  api->destroyComputePipeline  = cuda_destroyComputePipeline;
  api->computeCommandEncoder   = cuda_computeCommandEncoder;
  api->setComputePipelineState = cuda_setComputePipeline;
  api->buffer                  = cuda_setComputeBuffer;
  api->texture                 = cuda_setComputeTexture;
  api->dispatch                = cuda_dispatch;
  api->endEncoding             = cuda_endComputePass;
}
