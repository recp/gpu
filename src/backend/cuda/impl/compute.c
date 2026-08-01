/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static bool
cuda__validComputeInterface(const GPUComputePipelineCreateInfo *info) {
  GPUShaderReflection reflection;
  GPUShaderStageFlags stage;

  if (!info || !info->library || !info->entryPoint || !info->entryPoint[0] ||
      !gpuShaderEntryView(info->library,
                          info->entryPoint,
                          &stage,
                          &reflection) ||
      stage != GPU_SHADER_STAGE_COMPUTE_BIT ||
      reflection.resourceCount != 1u || !reflection.pResources ||
      reflection.pushConstantSizeBytes != 0u ||
      reflection.pushConstantStages != 0u) {
    return false;
  }

  return reflection.pResources[0].groupIndex == 0u &&
         reflection.pResources[0].binding == 0u &&
         (reflection.pResources[0].bindingType ==
            GPU_BINDING_STORAGE_BUFFER ||
          reflection.pResources[0].bindingType ==
            GPU_BINDING_READ_ONLY_STORAGE_BUFFER) &&
         (reflection.pResources[0].visibility &
            GPU_SHADER_STAGE_COMPUTE_BIT) != 0u &&
         reflection.pResources[0].arrayCount == 1u &&
         !reflection.pResources[0].hasDynamicOffset;
}

static GPUResult
cuda_createComputePipeline(GPUDevice                          *device,
                           const GPUComputePipelineCreateInfo *info,
                           GPUComputePipeline                 *pipeline) {
  GPUComputePipelineCuda *native;
  GPUDeviceCuda          *deviceNative;
  GPUShaderLibraryCuda   *library;
  uint32_t                block[3];
  uint64_t                threadCount;
  CUresult                result;

  deviceNative = cuda_device(device);
  library      = info && info->library ? info->library->_priv : NULL;
  if (!deviceNative || !pipeline || !library || !library->source ||
      !cuda__validComputeInterface(info)) {
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

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->pipeline = pipeline;
  native->driver   = deviceNative->driver;
  native->context  = deviceNative->context;
  if (cuda_push(native->driver, native->context) != GPU_OK) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = native->driver->moduleLoadData(&native->module,
                                           library->source,
                                           0u,
                                           NULL,
                                           NULL);
  if (result == CUDA_SUCCESS) {
    result = native->driver->moduleGetFunction(&native->function,
                                                native->module,
                                                info->entryPoint);
  }
  cuda_pop(native->driver);
  if (result != CUDA_SUCCESS) {
    if (native->module &&
        cuda_push(native->driver, native->context) == GPU_OK) {
      (void)native->driver->moduleUnload(native->module);
      cuda_pop(native->driver);
    }
    cuda_report(device, result, "PTX module creation");
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
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
  if (native && native->module &&
      cuda_push(native->driver, native->context) == GPU_OK) {
    (void)native->driver->moduleUnload(native->module);
    cuda_pop(native->driver);
  }
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
  command->pipeline                  = NULL;
  command->buffer                    = NULL;
  command->bufferOffset              = 0u;
  return &command->compute;
}

static void
cuda_setComputePipeline(GPUComputePassEncoder   *encoder,
                        GPUComputePipelineState *state) {
  GPUCommandCuda        *command;
  GPUComputePipelineCuda *native;

  command = encoder ? encoder->_priv : NULL;
  native  = state ? state->_priv : NULL;
  if (!command || !native || !native->function) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }
  command->pipeline          = native;
  encoder->_workgroupSize[0] = state->workgroupSize[0];
  encoder->_workgroupSize[1] = state->workgroupSize[1];
  encoder->_workgroupSize[2] = state->workgroupSize[2];
}

static void
cuda_setComputeBuffer(GPUComputePassEncoder *encoder,
                      GPUBuffer             *buffer,
                      uint64_t               offset,
                      uint32_t               index) {
  GPUCommandCuda *command;
  GPUBufferCuda  *native;

  command = encoder ? encoder->_priv : NULL;
  native  = buffer ? buffer->_priv : NULL;
  if (!command || !native || index != 0u ||
      buffer->device != encoder->_device ||
      !gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_STORAGE) ||
      !gpuBufferOffsetValid(buffer, offset) || offset == buffer->sizeBytes) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }
  command->buffer       = native;
  command->bufferOffset = offset;
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
  uint32_t         capacity;
  size_t           size;

  command = encoder ? encoder->_priv : NULL;
  device  = encoder ? cuda_device(encoder->_device) : NULL;
  if (!command || command->recordResult != GPU_OK || !command->pipeline ||
      !command->buffer || !device || encoder->_workgroupSize[0] == 0u ||
      encoder->_workgroupSize[1] == 0u ||
      encoder->_workgroupSize[2] == 0u ||
      x > device->maxGridDim[0] || y > device->maxGridDim[1] ||
      z > device->maxGridDim[2] ||
      command->buffer->address > UINT64_MAX - command->bufferOffset) {
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

  dispatch          = &command->dispatches[command->dispatchCount++];
  dispatch->pipeline = command->pipeline->pipeline;
  dispatch->buffer   = command->buffer->address + command->bufferOffset;
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
  api->dispatch                = cuda_dispatch;
  api->endEncoding             = cuda_endComputePass;
}
