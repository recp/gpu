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

  if (!dx12_compileShader(deviceDX12,
                          library,
                          info->entryPoint,
                          GPU_SHADER_STAGE_COMPUTE_BIT,
                          &shaderCode)) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  desc.pRootSignature      = layout->rootSignature;
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
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->rootSignature = layout->rootSignature;
  native->rootSignature->lpVtbl->AddRef(native->rootSignature);
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
  GPUCommandBufferDX12    *command;
  GPUComputePassEncoder   *encoder;
  GPUComputeEncoderDX12   *native;

  GPU__UNUSED(label);

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->commandList) {
    return NULL;
  }

  encoder = &command->computeEncoder;
  native  = &command->computeState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->commandList        = command->commandList;
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

GPU_HIDE
void
dx12_endComputeEncoding(GPUComputePassEncoder *encoder) {
  GPUComputeEncoderDX12 *native;

  native = dx12__computeEncoder(encoder);
  if (!native) {
    return;
  }

  native->commandList   = NULL;
  native->rootSignature = NULL;
  native->resourceHeap  = NULL;
  native->samplerHeap   = NULL;
}

GPU_HIDE
void
dx12_initCompute(GPUApiCompute *api) {
  api->createPipeline          = dx12_createComputePipeline;
  api->destroyComputePipeline  = dx12_destroyComputePipeline;
  api->computeCommandEncoder   = dx12_computeCommandEncoder;
  api->setComputePipelineState = dx12_setComputePipelineState;
  api->dispatch                = dx12_dispatch;
  api->endEncoding             = dx12_endComputeEncoding;
}
