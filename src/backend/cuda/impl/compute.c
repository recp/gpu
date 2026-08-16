/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static bool
cuda__validComputeInterface(const GPUDevice                    *device,
                            const GPUComputePipelineCreateInfo *info,
                            GPUShaderPTXEntryView               *outPTX) {
  GPUShaderReflection   reflection;
  GPUShaderPTXEntryView ptx;
  GPUShaderStageFlags   stage;

  memset(&ptx, 0, sizeof(ptx));
  if (!info || !info->library || !info->entryPoint || !info->entryPoint[0] ||
      !gpuShaderEntryView(info->library,
                          info->entryPoint,
                          &stage,
                          &reflection) ||
      !gpuGetShaderLibraryPTXEntry(info->library, info->entryPoint, &ptx) ||
      stage != GPU_SHADER_STAGE_COMPUTE_BIT ||
      reflection.pushConstantSizeBytes != 0u ||
      reflection.pushConstantStages != 0u ||
      ptx.paramCount > GPU_SHADER_PTX_MAX_PARAM_COUNT ||
      ptx.paramDataSize > GPU_SHADER_PTX_MAX_PARAM_BYTES) {
    return false;
  }

  for (uint32_t i = 0u; i < reflection.resourceCount; i++) {
    const GPUShaderResourceReflection *resource;
    bool                               supported;

    resource = &reflection.pResources[i];
    supported = resource->bindingType == GPU_BINDING_UNIFORM_BUFFER ||
                resource->bindingType == GPU_BINDING_STORAGE_BUFFER ||
                resource->bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
                resource->bindingType == GPU_BINDING_SAMPLED_TEXTURE ||
                resource->bindingType == GPU_BINDING_SAMPLER;
    if (resource->bindingType == GPU_BINDING_STORAGE_TEXTURE) {
      supported = resource->storageTexture.viewType == GPU_TEXTURE_VIEW_2D &&
                  resource->storageTexture.format == GPU_FORMAT_RGBA32_FLOAT;
    }
    if (!supported || resource->arrayCount == 0u ||
        (resource->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u ||
        (resource->arrayCount > 1u &&
         !GPUIsFeatureEnabled(device, GPU_FEATURE_DESCRIPTOR_INDEXING))) {
      return false;
    }
  }
  for (uint32_t i = 0u; i < ptx.paramCount; i++) {
    const GPUShaderPTXParamInfo *param;
    uint32_t                     size;

    param = &ptx.params[i];
    size  = cuda_ptxParamSize(param->kind);
    if ((param->kind != GPUShaderPTXParamBuffer &&
         param->kind != GPUShaderPTXParamSurface &&
         param->kind != GPUShaderPTXParamTexture &&
         param->kind != GPUShaderPTXParamSampledTexture &&
         param->kind != GPUShaderPTXParamTextureMetadata) ||
        (param->kind == GPUShaderPTXParamBuffer &&
         param->bindingType != GPU_BINDING_UNIFORM_BUFFER &&
         param->bindingType != GPU_BINDING_STORAGE_BUFFER &&
         param->bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER) ||
        (param->kind == GPUShaderPTXParamSurface &&
         param->bindingType != GPU_BINDING_STORAGE_TEXTURE) ||
        (param->kind == GPUShaderPTXParamTexture &&
         param->bindingType != GPU_BINDING_SAMPLED_TEXTURE) ||
        (param->kind == GPUShaderPTXParamSampledTexture &&
         param->bindingType != GPU_BINDING_SAMPLED_TEXTURE) ||
        (param->kind == GPUShaderPTXParamTextureMetadata &&
         param->bindingType != GPU_BINDING_STORAGE_TEXTURE &&
         param->bindingType != GPU_BINDING_SAMPLED_TEXTURE) ||
        size == 0u || ptx.paramDataSize < size ||
        param->dataOffset > ptx.paramDataSize - size) {
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
  GPUComputePipelineCuda           *native;
  GPUDeviceCuda                    *deviceNative;
  GPUShaderLibraryCuda             *library;
  GPUCudaModule                    *module;
  const GPUShaderStaticSamplerInfo *staticSamplers;
  GPUShaderPTXEntryView             ptx;
  uint64_t                          entryBit;
  uint32_t                          block[3];
  uint64_t                          threadCount;
  size_t                            nativeSize;
  size_t                            paramBytes;
  size_t                            samplerBytes;
  uint32_t                          staticSamplerCount;
  CUresult                          result;

  deviceNative = cuda_device(device);
  library      = info && info->library ? info->library->_priv : NULL;
  module       = library ? library->module : NULL;
  if (!deviceNative || !pipeline || !module ||
      !cuda__validComputeInterface(device, info, &ptx)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!gpuGetShaderLibraryComputeWorkgroupSize(info->library,
                                                info->entryPoint,
                                                block)) {
    block[0] = block[1] = block[2] = 1u;
  }
  threadCount = (uint64_t)block[0] * block[1] * block[2];
  if (block[0] == 0u || block[1] == 0u || block[2] == 0u ||
      block[0] > deviceNative->maxBlockDim[0] ||
      block[1] > deviceNative->maxBlockDim[1] ||
      block[2] > deviceNative->maxBlockDim[2] ||
      threadCount > deviceNative->maxThreadsPerBlock) {
    return GPU_ERROR_UNSUPPORTED;
  }

  staticSamplers = gpuGetShaderLibraryStaticSamplers(info->library,
                                                      &staticSamplerCount);
  entryBit       = gpuShaderEntryBit(info->library, info->entryPoint);
  paramBytes     = (size_t)ptx.paramCount * sizeof(native->params[0]);
  samplerBytes   = (size_t)staticSamplerCount * sizeof(GPUStaticSamplerDesc);
  if ((ptx.paramCount > 0u && paramBytes / sizeof(native->params[0]) !=
                              ptx.paramCount) ||
      (staticSamplerCount > 0u &&
       samplerBytes / sizeof(GPUStaticSamplerDesc) != staticSamplerCount) ||
      paramBytes > SIZE_MAX - sizeof(*native) ||
      samplerBytes > SIZE_MAX - sizeof(*native) - paramBytes) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  nativeSize = sizeof(*native) + paramBytes + samplerBytes;
  native = calloc(1, nativeSize);
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->pipeline = pipeline;
  result = cuda_getModuleFunction(module, info->entryPoint, &native->function);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "PTX entry lookup");
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cuda_retainModule(module);
  native->module        = module;
  native->paramCount    = ptx.paramCount;
  native->paramDataSize = ptx.paramDataSize;
  native->staticSamplers = (GPUStaticSamplerDesc *)
    ((uint8_t *)native->params + paramBytes);
  if (ptx.paramCount > 0u) {
    memcpy(native->params,
           ptx.params,
           (size_t)ptx.paramCount * sizeof(native->params[0]));
  }
  if (staticSamplerCount > 0u && (!staticSamplers || entryBit == 0u)) {
    cuda_releaseModule(native->module);
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }
  for (uint32_t i = 0u; i < staticSamplerCount; i++) {
    if ((staticSamplers[i].entryMask & entryBit) == 0u) {
      continue;
    }
    if (!cuda_staticSamplerDescSupported(&staticSamplers[i].desc)) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    native->staticSamplers[native->staticSamplerCount++] =
      staticSamplers[i].desc;
  }
  for (uint32_t i = 0u; i < ptx.paramCount; i++) {
    GPUShaderPTXParamInfo *param;
    uint32_t               samplerIndex;
    uint32_t               sourceIndex;

    param = &native->params[i];
    if (param->kind != GPUShaderPTXParamSampledTexture ||
        param->staticSamplerId == UINT32_MAX) {
      continue;
    }
    sourceIndex = param->staticSamplerId;
    if (sourceIndex >= staticSamplerCount ||
        (staticSamplers[sourceIndex].entryMask & entryBit) == 0u) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    samplerIndex = 0u;
    for (uint32_t j = 0u; j < sourceIndex; j++) {
      samplerIndex += (staticSamplers[j].entryMask & entryBit) != 0u;
    }
    if (samplerIndex >= native->staticSamplerCount) {
      cuda_releaseModule(native->module);
      free(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    param->staticSamplerId = samplerIndex;
  }

  native->base._priv = native;
  native->base.workgroupSize[0] = block[0];
  native->base.workgroupSize[1] = block[1];
  native->base.workgroupSize[2] = block[2];
  pipeline->_priv    = native;
  pipeline->_state   = &native->base;
  return GPU_OK;
}

static void
cuda_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineCuda *native;

  native = pipeline ? pipeline->_state : NULL;
  if (native) cuda_releaseModule(native->module);
  free(native);
  free(pipeline);
}

static GPUComputePassEncoder *
cuda_computeCommandEncoder(GPUCommandBuffer               *cmdb,
                           const GPUComputePassCreateInfo *info) {
  GPUCommandCuda *command;

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
  command->pipeline = NULL;
  memset(command->boundParamMask, 0, sizeof(command->boundParamMask));
  return &command->compute;
}

static void
cuda_setComputePipeline(GPUComputePassEncoder   *encoder,
                        GPUComputePipelineState *state) {
  GPUCommandCuda         *command;
  GPUComputePipelineCuda *native;

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
cuda__paramsBound(const GPUCommandCuda *command) {
  const GPUComputePipelineCuda *pipeline;

  pipeline = command ? command->pipeline : NULL;
  if (!pipeline) {
    return false;
  }
  for (uint32_t i = 0u; i < pipeline->paramCount; i++) {
    if ((command->boundParamMask[i / 64u] &
         (UINT64_C(1) << (i % 64u))) == 0u) {
      return false;
    }
  }
  return true;
}

static uint8_t *
cuda__reserveParamData(GPUComputePassEncoder *encoder,
                       GPUCommandCuda        *command,
                       uint32_t               size) {
  uint8_t *data;
  uint32_t capacity;
  uint32_t oldCapacity;

  if (!encoder || !command || size == 0u ||
      command->paramDataCount > UINT32_MAX - size) {
    return NULL;
  }
  if (command->paramDataCount + size <= command->paramDataCapacity) {
    data = command->paramData + command->paramDataCount;
    command->paramDataCount += size;
    return data;
  }

  capacity = command->paramDataCapacity
               ? command->paramDataCapacity
               : GPU_SHADER_PTX_MAX_PARAM_BYTES;
  while (capacity < command->paramDataCount + size) {
    if (capacity > UINT32_MAX / 2u) {
      return NULL;
    }
    capacity *= 2u;
  }
  oldCapacity = command->paramDataCapacity;
  data = realloc(command->paramData, capacity);
  if (!data) {
    return NULL;
  }
  command->paramData         = data;
  command->paramDataCapacity = capacity;
  data += command->paramDataCount;
  command->paramDataCount += size;
  gpuDeviceRecordHotPathAlloc(encoder->_device, capacity - oldCapacity);
  return data;
}

static void
cuda_dispatch(GPUComputePassEncoder *encoder,
              uint32_t               x,
              uint32_t               y,
              uint32_t               z) {
  GPUCommandCuda  *command;
  GPUDeviceCuda   *device;
  GPUDispatchCuda *dispatch;
  GPUDispatchCuda *dispatches;
  uint8_t         *paramData;
  uint32_t         capacity;
  size_t           size;

  command = encoder ? encoder->_priv : NULL;
  device  = encoder ? cuda_device(encoder->_device) : NULL;
  if (!command || command->recordResult != GPU_OK || !command->pipeline ||
      !cuda__paramsBound(command) || !device ||
      encoder->_workgroupSize[0] == 0u ||
      encoder->_workgroupSize[1] == 0u ||
      encoder->_workgroupSize[2] == 0u ||
      x > device->maxGridDim[0] || y > device->maxGridDim[1] ||
      z > device->maxGridDim[2]) {
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
    size       = (size_t)capacity * sizeof(*dispatches);
    dispatches = realloc(command->dispatches, size);
    if (!dispatches) {
      command->recordResult = GPU_ERROR_OUT_OF_MEMORY;
      return;
    }
    command->dispatches       = dispatches;
    command->dispatchCapacity = capacity;
    gpuDeviceRecordHotPathAlloc(encoder->_device, size);
  }

  dispatch = &command->dispatches[command->dispatchCount];
  memset(dispatch, 0, sizeof(*dispatch));
  dispatch->paramDataSize = command->pipeline->paramDataSize;
  if (dispatch->paramDataSize <= sizeof(dispatch->inlineParams)) {
    paramData = dispatch->inlineParams;
  } else {
    dispatch->paramDataOffset = command->paramDataCount;
    paramData = cuda__reserveParamData(encoder,
                                       command,
                                       dispatch->paramDataSize);
    if (!paramData) {
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
  gpuRetainComputePipeline(dispatch->pipeline);
}

static void
cuda_endComputePass(GPUComputePassEncoder *encoder) {
  GPU__UNUSED(encoder);
}

void
cuda_initCompute(GPUApiCompute *api) {
  api->createPipeline          = cuda_createComputePipeline;
  api->destroyComputePipeline = cuda_destroyComputePipeline;
  api->computeCommandEncoder   = cuda_computeCommandEncoder;
  api->setComputePipelineState = cuda_setComputePipeline;
  api->buffer                  = cuda_setComputeBuffer;
  api->texture                 = cuda_setComputeTexture;
  api->dispatch                = cuda_dispatch;
  api->endEncoding             = cuda_endComputePass;
}
