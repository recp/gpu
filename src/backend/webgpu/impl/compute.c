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
  GPUComputePipelineState *state;
  GPUDeviceWebGPU         *native;
  uint32_t                 pushConstantSize;
  GPUShaderStageFlags      pushConstantStages;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !info->library ||
      !info->library->_priv || !info->layout || !info->layout->_native ||
      !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  gpuGetPipelineLayoutPushConstants(info->layout,
                                    &pushConstantSize,
                                    &pushConstantStages);
  GPU__UNUSED(pushConstantStages);
  if (pushConstantSize != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  state = calloc(1, sizeof(*state));
  if (!state) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label              = gpu_webgpuString(info->label);
  descriptor.layout             = info->layout->_native;
  descriptor.compute.module     = info->library->_priv;
  descriptor.compute.entryPoint = gpu_webgpuString(info->entryPoint);
  state->_priv = wgpuDeviceCreateComputePipeline(native->device, &descriptor);
  if (!state->_priv) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state->workgroupSize[0] = 1u;
  state->workgroupSize[1] = 1u;
  state->workgroupSize[2] = 1u;
  pipeline->_priv         = state->_priv;
  pipeline->_state        = state;
  return GPU_OK;
}

static void
webgpu_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineState *state;

  if (!pipeline) {
    return;
  }
  state = pipeline->_state;
  if (state && state->_priv) {
    wgpuComputePipelineRelease(state->_priv);
  }
  free(state);
  free(pipeline);
}

static GPUComputePassEncoder *
webgpu_computeCommandEncoder(GPUCommandBuffer *cmdb, const char *label) {
  WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
  GPUCommandWebGPU         *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder || command->computeEncoder) {
    return NULL;
  }

  memset(&command->compute, 0, sizeof(command->compute));
  descriptor.label = gpu_webgpuString(label);
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
webgpu_setComputePipeline(GPUComputePassEncoder   *encoder,
                          GPUComputePipelineState *state) {
  GPUCommandWebGPU *command;

  command = webgpu_computeCommand(encoder);
  if (!command || !command->computeEncoder || !state || !state->_priv) {
    return;
  }

  wgpuComputePassEncoderSetPipeline(command->computeEncoder, state->_priv);
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
  api->dispatch                = webgpu_dispatch;
  api->dispatchIndirect        = webgpu_dispatchIndirect;
  api->endEncoding             = webgpu_endComputeEncoding;
}
