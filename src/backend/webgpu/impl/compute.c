/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUCommandWebGPU *
webgpu_computeCommand(GPUComputePassEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

static GPUResult
webgpu_createComputePipeline(GPUDevice                          *device,
                             const GPUComputePipelineCreateInfo *info,
                             GPUComputePipeline                 *pipeline) {
  WGPUComputePipelineDescriptor descriptor =
    WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
  GPUComputePipelineWebGPU *state;
  GPUDeviceWebGPU          *native;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !info->library ||
      !info->library->_priv || !info->layout || !info->layout->_native ||
      !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  state = calloc(1, sizeof(*state));
  if (!state) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  if (gpu_webgpuCreatePipelineLayout(device,
                                     info->layout,
                                     pipeline->_requiredBindGroupMask,
                                     &state->layout) != GPU_OK) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  descriptor.label              = gpu_webgpuString(info->label);
  descriptor.layout             = state->layout.layout;
  descriptor.compute.module     = info->library->_priv;
  descriptor.compute.entryPoint = gpu_webgpuString(info->entryPoint);
  state->pipeline = wgpuDeviceCreateComputePipeline(native->device, &descriptor);
  if (!state->pipeline) {
    gpu_webgpuDestroyPipelineLayout(&state->layout);
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state->base._priv            = state->pipeline;
  state->base.workgroupSize[0] = 1u;
  state->base.workgroupSize[1] = 1u;
  state->base.workgroupSize[2] = 1u;
  pipeline->_priv              = state->pipeline;
  pipeline->_state             = &state->base;
  return GPU_OK;
}

static void
webgpu_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineWebGPU *state;

  if (!pipeline) {
    return;
  }
  state = pipeline->_state;
  if (state) {
    if (state->pipeline) {
      wgpuComputePipelineRelease(state->pipeline);
    }
    gpu_webgpuDestroyPipelineLayout(&state->layout);
  }
  free(state);
  free(pipeline);
}

static GPUComputePassEncoder *
webgpu_computeCommandEncoder(GPUCommandBuffer               *cmdb,
                             const GPUComputePassCreateInfo *info) {
  WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
  GPUCommandWebGPU         *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder || command->computeEncoder) {
    return NULL;
  }

  memset(&command->compute, 0, sizeof(command->compute));
  descriptor.label = gpu_webgpuString(info->label);
  if (info->timestampWrites) {
    command->timestampWrites =
      (WGPUPassTimestampWrites)WGPU_PASS_TIMESTAMP_WRITES_INIT;
    command->timestampWrites.querySet =
      info->timestampWrites->querySet->_priv;
    command->timestampWrites.beginningOfPassWriteIndex =
      info->timestampWrites->beginIndex;
    command->timestampWrites.endOfPassWriteIndex =
      info->timestampWrites->endIndex;
    descriptor.timestampWrites = &command->timestampWrites;
  }
  command->computeEncoder = wgpuCommandEncoderBeginComputePass(command->encoder,
                                                                &descriptor);
  if (!command->computeEncoder) {
    return NULL;
  }

  command->compute._priv             = command;
  command->compute._workgroupSize[0] = 1u;
  command->compute._workgroupSize[1] = 1u;
  command->compute._workgroupSize[2] = 1u;
  return &command->compute;
}

static void
webgpu_computePushConstants(GPUComputePassEncoder *encoder,
                            const void            *data,
                            uint32_t               sizeBytes) {
  GPUCommandWebGPU *command;
  uint32_t          dynamicOffset;

  command = webgpu_computeCommand(encoder);
  if (!command || !command->computeEncoder ||
      !gpu_webgpuUploadPushConstants(command,
                                     data,
                                     sizeBytes,
                                     &dynamicOffset)) {
    return;
  }
  wgpuComputePassEncoderSetBindGroup(command->computeEncoder,
                                     GPU_WEBGPU_PUSH_CONSTANT_GROUP,
                                     command->pushConstantGroup,
                                     1u,
                                     &dynamicOffset);
}

static void
webgpu_setComputePipeline(GPUComputePassEncoder   *encoder,
                          GPUComputePipelineState *state) {
  GPUComputePipelineWebGPU *nativeState;
  GPUCommandWebGPU         *command;

  command     = webgpu_computeCommand(encoder);
  nativeState = (GPUComputePipelineWebGPU *)state;
  if (!command || !command->computeEncoder || !nativeState ||
      !nativeState->pipeline) {
    return;
  }

  wgpuComputePassEncoderSetPipeline(command->computeEncoder,
                                    nativeState->pipeline);
  gpu_webgpuBindComputeAutomaticGroups(encoder, &nativeState->layout);
  if (nativeState->layout.pushConstantSizeBytes > 0u) {
    static const uint8_t zero[GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT];

    webgpu_computePushConstants(encoder,
                                zero,
                                nativeState->layout.pushConstantSizeBytes);
  }
  encoder->_workgroupSize[0] = state->workgroupSize[0];
  encoder->_workgroupSize[1] = state->workgroupSize[1];
  encoder->_workgroupSize[2] = state->workgroupSize[2];
}

static void
webgpu_dispatch(GPUComputePassEncoder *encoder,
                uint32_t               x,
                uint32_t               y,
                uint32_t               z) {
  GPUCommandWebGPU *command;

  command = webgpu_computeCommand(encoder);
  if (command && command->computeEncoder) {
    wgpuComputePassEncoderDispatchWorkgroups(command->computeEncoder, x, y, z);
  }
}

static void
webgpu_dispatchIndirect(GPUComputePassEncoder *encoder,
                        GPUBuffer             *argsBuffer,
                        uint64_t               argsOffset) {
  GPUCommandWebGPU *command;

  command = webgpu_computeCommand(encoder);
  if (command && command->computeEncoder && argsBuffer && argsBuffer->_priv) {
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(command->computeEncoder,
                                                      argsBuffer->_priv,
                                                      argsOffset);
  }
}

static void
webgpu_endComputeEncoding(GPUComputePassEncoder *encoder) {
  GPUCommandWebGPU *command;

  command = webgpu_computeCommand(encoder);
  if (!command || !command->computeEncoder) {
    return;
  }

  wgpuComputePassEncoderEnd(command->computeEncoder);
  wgpuComputePassEncoderRelease(command->computeEncoder);
  command->computeEncoder = NULL;
}

void
webgpu_initCompute(GPUApiCompute *api) {
  api->createPipeline          = webgpu_createComputePipeline;
  api->destroyComputePipeline  = webgpu_destroyComputePipeline;
  api->computeCommandEncoder   = webgpu_computeCommandEncoder;
  api->setComputePipelineState = webgpu_setComputePipeline;
  api->pushConstants           = webgpu_computePushConstants;
  api->dispatch                = webgpu_dispatch;
  api->dispatchIndirect        = webgpu_dispatchIndirect;
  api->endEncoding             = webgpu_endComputeEncoding;
}
