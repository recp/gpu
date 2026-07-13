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
#include "../../../api/compute_internal.h"

static GPUComputeEncoderDX12 *
dx12__computeEncoder(GPUComputePassEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

GPU_HIDE
GPUResult
dx12_createComputePipeline(GPUDevice                          *device,
                           const GPUComputePipelineCreateInfo *info,
                           GPUComputePipeline                 *pipeline) {
  GPUDeviceDX12           *deviceDX12;
  GPULibraryDX12          *library;
  GPUPipelineLayoutDX12   *layout;
  GPUComputePipelineState *state;
  GPUComputePipelineDX12  *native;
  ID3D12RootSignature     *rootSignature;
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {0};
  DX12ShaderCode           shaderCode = {0};
  HRESULT                  result;

  deviceDX12 = device ? device->_priv : NULL;
  library    = info && info->library ? info->library->_priv : NULL;
  layout     = info && info->layout ? info->layout->_native : NULL;
  if (!deviceDX12 || !deviceDX12->d3dDevice || !library ||
      !library->source || !layout || !layout->rootSignature ||
      !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  state = calloc(1, sizeof(*state) + sizeof(*native));
  if (!state) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native = (GPUComputePipelineDX12 *)(state + 1);
  rootSignature = NULL;
  if (dx12_createShaderRootSignature(device,
                                     info->layout,
                                     info->library,
                                     &rootSignature) != GPU_OK) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!dx12_compileShader(deviceDX12,
                          library,
                          info->entryPoint,
                          GPU_SHADER_STAGE_COMPUTE_BIT,
                          &shaderCode)) {
    rootSignature->lpVtbl->Release(rootSignature);
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  desc.pRootSignature      = rootSignature;
  desc.CS.pShaderBytecode  = shaderCode.data;
  desc.CS.BytecodeLength   = shaderCode.size;
  result = deviceDX12->d3dDevice->lpVtbl->CreateComputePipelineState(
    deviceDX12->d3dDevice,
    &desc,
    &IID_ID3D12PipelineState,
    (void **)&native->pipelineState
  );
  dx12_freeShaderCode(&shaderCode);
  if (FAILED(result) || !native->pipelineState) {
    rootSignature->lpVtbl->Release(rootSignature);
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->rootSignature = rootSignature;
  state->_priv            = native;
  state->workgroupSize[0] = 1u;
  state->workgroupSize[1] = 1u;
  state->workgroupSize[2] = 1u;
  pipeline->_priv         = native;
  pipeline->_state        = state;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineState *state;
  GPUComputePipelineDX12  *native;

  if (!pipeline) {
    return;
  }

  state  = pipeline->_state;
  native = state ? state->_priv : NULL;
  if (native) {
    if (native->pipelineState) {
      native->pipelineState->lpVtbl->Release(native->pipelineState);
    }
    if (native->rootSignature) {
      native->rootSignature->lpVtbl->Release(native->rootSignature);
    }
  }
  free(state);
  free(pipeline);
}

GPU_HIDE
GPUComputePassEncoder *
dx12_computeCommandEncoder(GPUCommandBuffer *cmdb, const char *label) {
  GPUDeviceDX12           *device;
  GPUCommandBufferDX12    *command;
  GPUComputePassEncoder   *encoder;
  GPUComputeEncoderDX12   *native;

  device  = cmdb && cmdb->_queue && cmdb->_queue->_device
              ? cmdb->_queue->_device->_priv
              : NULL;
  command = cmdb ? cmdb->_priv : NULL;
  if (!device || !command || !command->commandList) {
    return NULL;
  }

  encoder = &command->computeEncoder;
  native  = &command->computeState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->device           = device;
  native->commandList      = command->commandList;
  native->debugEventActive = dx12_beginDebugEvent(
    gpuCommandBufferDevice(cmdb),
    native->commandList,
    label
  );
  encoder->_priv             = native;
  encoder->_workgroupSize[0] = 1u;
  encoder->_workgroupSize[1] = 1u;
  encoder->_workgroupSize[2] = 1u;
  return encoder;
}

GPU_HIDE
void
dx12_setComputePipelineState(GPUComputePassEncoder   *encoder,
                             GPUComputePipelineState *pipelineState) {
  GPUComputeEncoderDX12  *native;
  GPUComputePipelineDX12 *pipeline;

  native   = dx12__computeEncoder(encoder);
  pipeline = pipelineState ? pipelineState->_priv : NULL;
  if (!native || !native->commandList || !pipeline ||
      !pipeline->pipelineState || !pipeline->rootSignature) {
    return;
  }

  native->commandList->lpVtbl->SetComputeRootSignature(native->commandList,
                                                       pipeline->rootSignature);
  native->commandList->lpVtbl->SetPipelineState(native->commandList,
                                                pipeline->pipelineState);
  native->rootSignature      = pipeline->rootSignature;
  encoder->_workgroupSize[0] = pipelineState->workgroupSize[0];
  encoder->_workgroupSize[1] = pipelineState->workgroupSize[1];
  encoder->_workgroupSize[2] = pipelineState->workgroupSize[2];
}

GPU_HIDE
void
dx12_computePushConstants(GPUComputePassEncoder *encoder,
                          const void            *data,
                          uint32_t               sizeBytes) {
  GPUComputeEncoderDX12 *native;
  GPUPipelineLayoutDX12 *layout;

  native = dx12__computeEncoder(encoder);
  layout = encoder && encoder->_pipelineLayout
             ? encoder->_pipelineLayout->_native
             : NULL;
  if (!native || !native->commandList || !layout || !data ||
      layout->pushConstantRootParameter == UINT32_MAX ||
      sizeBytes != layout->pushConstantDwordCount * 4u) {
    return;
  }

  native->commandList->lpVtbl->SetComputeRoot32BitConstants(
    native->commandList,
    layout->pushConstantRootParameter,
    layout->pushConstantDwordCount,
    data,
    0u
  );
}

GPU_HIDE
void
dx12_dispatch(GPUComputePassEncoder *encoder,
              uint32_t               x,
              uint32_t               y,
              uint32_t               z) {
  GPUComputeEncoderDX12 *native;

  native = dx12__computeEncoder(encoder);
  if (!native || !native->commandList) {
    return;
  }

  native->commandList->lpVtbl->Dispatch(native->commandList, x, y, z);
}

static bool
dx12__dispatchIndirect(GPUComputePassEncoder *encoder,
                       GPUBuffer             *argsBuffer,
                       uint64_t               argsOffset,
                       uint32_t               dispatchCount,
                       uint32_t               strideBytes) {
  GPUComputeEncoderDX12 *native;
  GPUBufferDX12         *buffer;

  native = dx12__computeEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->device || !native->commandList || !buffer ||
      !buffer->resource || !native->device->dispatchSignature ||
      !dx12_transitionBuffer(native->commandList,
                             buffer,
                             D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) ||
      strideBytes != (uint32_t)sizeof(D3D12_DISPATCH_ARGUMENTS)) {
    return false;
  }

  native->commandList->lpVtbl->ExecuteIndirect(
    native->commandList,
    native->device->dispatchSignature,
    dispatchCount,
    buffer->resource,
    argsOffset,
    NULL,
    0u
  );
  return true;
}

GPU_HIDE
void
dx12_dispatchIndirect(GPUComputePassEncoder *encoder,
                      GPUBuffer             *argsBuffer,
                      uint64_t               argsOffset) {
  (void)dx12__dispatchIndirect(encoder,
                               argsBuffer,
                               argsOffset,
                               1u,
                               (uint32_t)sizeof(D3D12_DISPATCH_ARGUMENTS));
}

GPU_HIDE
bool
dx12_multiDispatchIndirect(GPUComputePassEncoder *encoder,
                           GPUBuffer             *argsBuffer,
                           uint64_t               argsOffset,
                           uint32_t               dispatchCount,
                           uint32_t               strideBytes) {
  return dx12__dispatchIndirect(encoder,
                                argsBuffer,
                                argsOffset,
                                dispatchCount,
                                strideBytes);
}

GPU_HIDE
void
dx12_endComputeEncoding(GPUComputePassEncoder *encoder) {
  GPUComputeEncoderDX12 *native;

  native = dx12__computeEncoder(encoder);
  if (!native) {
    return;
  }

  if (native->debugEventActive) {
    dx12_endDebugEvent(gpuCommandBufferDevice(encoder->_cmdb),
                       native->commandList);
  }

  native->device           = NULL;
  native->commandList      = NULL;
  native->rootSignature    = NULL;
  native->resourceHeap     = NULL;
  native->samplerHeap      = NULL;
  native->debugEventActive = false;
}

GPU_HIDE
void
dx12_initCompute(GPUApiCompute *api) {
  api->createPipeline          = dx12_createComputePipeline;
  api->destroyComputePipeline  = dx12_destroyComputePipeline;
  api->computeCommandEncoder   = dx12_computeCommandEncoder;
  api->setComputePipelineState = dx12_setComputePipelineState;
  api->pushConstants           = dx12_computePushConstants;
  api->dispatch                = dx12_dispatch;
  api->dispatchIndirect        = dx12_dispatchIndirect;
  api->multiDispatchIndirect   = dx12_multiDispatchIndirect;
  api->endEncoding             = dx12_endComputeEncoding;
}
