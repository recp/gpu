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
#include "../impl.h"
#include "constants.h"

static const uint8_t webgpu_zeroPushConstants[GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT];

static CommandWebGPU*
webgpu_computeCommand(GPUComputePassEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

static GPUResult
webgpu_createComputePipeline(GPUDevice                          *device,
                             const GPUComputePipelineCreateInfo *info,
                             GPUComputePipeline                 *pipeline) {
  WGPUConstantEntry              constantEntries[USL_RUNTIME_MAX_SPEC_CONSTANTS];
  char                          constantIDs[USL_RUNTIME_MAX_SPEC_CONSTANTS][11];
  WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  WebGPUPipelineError            error;
#endif
  ComputePipelineWebGPU        *state;
  DeviceWebGPU                 *native;
  uint64_t                      entryMask;
  uint32_t                      automaticGroupMask;
  GPUResult                     result;

  native = webgpuDevice(device);

  if (!native || !native->device || !info || !info->library
      || !info->library->_priv || !info->layout || !info->layout->_native
      || !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(state = calloc(1, sizeof(*state)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  automaticGroupMask = 0u;

  if (shaderLibraryHasEntryResourceInfo(info->library)) {
    entryMask          = shaderEntryBit(info->library, info->entryPoint);
    automaticGroupMask = shaderWGSLStaticGroups(info->library, entryMask);

    if (entryMask == 0u || automaticGroupMask == UINT32_MAX) {
      free(state);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  automaticGroupMask &= ~pipeline->_requiredBindGroupMask;

  if (webgpuCreatePipelineLayout(device,
                                 info->layout,
                                 pipeline->_requiredBindGroupMask,
                                 automaticGroupMask,
                                 &state->layout) != GPU_OK) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  descriptor.label              = webgpuString(info->label);
  descriptor.layout             = state->layout.layout;
  descriptor.compute.module     = info->library->_priv;
  descriptor.compute.entryPoint = webgpuString(info->entryPoint);

  descriptor.compute.constantCount = webgpu_pipelineConstants(info->chain.pNext, constantEntries, constantIDs);
  descriptor.compute.constants     = descriptor.compute.constantCount ? constantEntries : NULL;

  result = GPU_OK;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  webgpuBeginPipelineError(device, &error);
#endif
  state->pipeline = wgpuDeviceCreateComputePipeline(native->device, &descriptor);
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  result = webgpuEndPipelineError(&error);
#endif

  if (!state->pipeline || result != GPU_OK) {
    if (state->pipeline)
      wgpuComputePipelineRelease(state->pipeline);

    webgpuDestroyPipelineLayout(&state->layout);
    free(state);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
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
  ComputePipelineWebGPU    *state;

  if (!pipeline) {
    return;
  }

  state = pipeline->_state;

  if (state) {
    if (state->pipeline) {
      wgpuComputePipelineRelease(state->pipeline);
    }

    webgpuDestroyPipelineLayout(&state->layout);
  }

  free(state);
  free(pipeline);
}

static GPUComputePassEncoder*
webgpu_computeCommandEncoder(GPUCommandBuffer               *cmdb,
                             const GPUComputePassCreateInfo *info) {
  WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
  CommandWebGPU            *command;

  command = webgpuCommand(cmdb);

  if (!command || !command->encoder || command->computeEncoder) {
    return NULL;
  }

  memset(&command->compute, 0, sizeof(command->compute));
  descriptor.label = webgpuString(info->label);

  if (info->timestampWrites) {
    command->timestampWrites = (WGPUPassTimestampWrites)WGPU_PASS_TIMESTAMP_WRITES_INIT;

    command->timestampWrites.querySet                  = info->timestampWrites->querySet->_priv;
    command->timestampWrites.beginningOfPassWriteIndex = info->timestampWrites->beginIndex;
    command->timestampWrites.endOfPassWriteIndex       = info->timestampWrites->endIndex;
    descriptor.timestampWrites                         = &command->timestampWrites;
  }

  if (!(command->computeEncoder = wgpuCommandEncoderBeginComputePass(command->encoder,
                                                                     &descriptor))) {
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
  CommandWebGPU    *command;
  uint32_t          dynamicOffset;

  command = webgpu_computeCommand(encoder);

  if (!command || !command->computeEncoder
      || !webgpuUploadPushConstants(command,
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
                          ComputePipelineState    *state) {
  ComputePipelineWebGPU    *nativeState;
  CommandWebGPU            *command;

  command     = webgpu_computeCommand(encoder);
  nativeState = (ComputePipelineWebGPU *)state;

  if (!command || !command->computeEncoder || !nativeState
      || !nativeState->pipeline) {
    return;
  }

  wgpuComputePassEncoderSetPipeline(command->computeEncoder,
                                    nativeState->pipeline);
  webgpuBindComputeAutomaticGroups(encoder, &nativeState->layout);

  if (nativeState->layout.pushConstantSizeBytes > 0u) {
    webgpu_computePushConstants(encoder,
                                webgpu_zeroPushConstants,
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
  CommandWebGPU    *command;

  command = webgpu_computeCommand(encoder);

  if (command && command->computeEncoder) {
    wgpuComputePassEncoderDispatchWorkgroups(command->computeEncoder, x, y, z);
  }
}

static void
webgpu_dispatchIndirect(GPUComputePassEncoder *encoder,
                        GPUBuffer             *argsBuffer,
                        uint64_t               argsOffset) {
  CommandWebGPU    *command;

  command = webgpu_computeCommand(encoder);

  if (command && command->computeEncoder && argsBuffer && argsBuffer->_priv) {
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(command->computeEncoder,
                                                     argsBuffer->_priv,
                                                     argsOffset);
  }
}

static void
webgpu_endComputeEncoding(GPUComputePassEncoder *encoder) {
  CommandWebGPU    *command;

  command = webgpu_computeCommand(encoder);

  if (!command || !command->computeEncoder) {
    return;
  }

  wgpuComputePassEncoderEnd(command->computeEncoder);
  wgpuComputePassEncoderRelease(command->computeEncoder);
  command->computeEncoder = NULL;
}

void
webgpu_initCompute(ApiCompute    *api) {
  api->createPipeline          = webgpu_createComputePipeline;
  api->destroyComputePipeline  = webgpu_destroyComputePipeline;
  api->computeCommandEncoder   = webgpu_computeCommandEncoder;
  api->setComputePipelineState = webgpu_setComputePipeline;
  api->pushConstants           = webgpu_computePushConstants;
  api->dispatch                = webgpu_dispatch;
  api->dispatchIndirect        = webgpu_dispatchIndirect;
  api->endEncoding             = webgpu_endComputeEncoding;
}
