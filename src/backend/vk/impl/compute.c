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
#include "../../../api/buffer_internal.h"
#include "../../../api/compute_internal.h"
#include "../../../api/descr/descriptor_internal.h"
#include "../../../api/library_internal.h"
#include "pipeline_cache.h"

static GPUComputeEncoderVk*
vk__computeEncoder(GPUComputePassEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

GPU_HIDE
GPUResult
vk_createComputePipeline(GPUDevice                          *device,
                         const GPUComputePipelineCreateInfo *info,
                         GPUComputePipeline                 *pipeline) {
  GPUDeviceVk                    *deviceVk;
  GPUShaderLibraryVk                   *library;
  GPUPipelineLayoutVk            *layout;
  GPUComputePipelineState        *state;
  GPUComputePipelineVk           *native;
  VkPipelineShaderStageCreateInfo stage        = {0};
  VkComputePipelineCreateInfo     pipelineInfo = {0};
  VkPipelineCache                 pipelineCache;
  VkResult                        result;

  deviceVk = device ? device->_priv : NULL;
  library  = info && info->library ? info->library->_priv : NULL;
  layout   = info && info->layout ? info->layout->_native : NULL;
  if (!deviceVk || !library || !library->module ||
      library->device != deviceVk->device ||
      !layout || !layout->layout || layout->device != deviceVk->device ||
      !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  state = calloc(1, sizeof(*state) + sizeof(*native));
  if (!state) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native           = (GPUComputePipelineVk *)(state + 1);
  native->device   = deviceVk->device;
  if (vk_createShaderLayout(device,
                            info->layout,
                            info->library,
                            &native->shaderLayout) != GPU_OK) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  stage.sType      = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage.stage      = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module     = library->module;
  stage.pName      = info->entryPoint;
  pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage  = stage;
  pipelineInfo.layout = native->shaderLayout.layout;
#ifdef VK_EXT_descriptor_buffer
  if (native->shaderLayout.descriptorBuffer) {
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
  }
#endif
  pipelineCache = vk_lockCache(info->cache);
  result = vkCreateComputePipelines(native->device,
                                    pipelineCache,
                                    1u,
                                    &pipelineInfo,
                                    NULL,
                                    &native->pipeline);
  vk_unlockCache(info->cache);
  if (result != VK_SUCCESS) {
    vk_destroyShaderLayout(&native->shaderLayout);
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

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
vk_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineState *state;
  GPUComputePipelineVk    *native;

  if (!pipeline) {
    return;
  }

  state  = pipeline->_state;
  native = state ? state->_priv : NULL;
  if (native && native->device && native->pipeline) {
    vkDestroyPipeline(native->device, native->pipeline, NULL);
  }
  if (native) {
    vk_destroyShaderLayout(&native->shaderLayout);
  }
  free(state);
  free(pipeline);
}

GPU_HIDE
GPUComputePassEncoder*
vk_computeCommandEncoder(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferVk     *command;
  GPUComputePassEncoder  *encoder;
  GPUComputeEncoderVk    *native;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->command) {
    return NULL;
  }

  encoder = &command->computeEncoder;
  native  = &command->computeState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->command          = command->command;
  native->debugLabelActive = vk_beginDebugLabel(
    gpuCommandBufferDevice(cmdb),
    native->command,
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
vk_setComputePipelineState(GPUComputePassEncoder   *encoder,
                           GPUComputePipelineState *pipelineState) {
  GPUComputeEncoderVk  *native;
  GPUComputePipelineVk *pipeline;

  native   = vk__computeEncoder(encoder);
  pipeline = pipelineState ? pipelineState->_priv : NULL;
  if (!native || !native->command || !pipeline || !pipeline->pipeline) {
    return;
  }

  vkCmdBindPipeline(native->command,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline->pipeline);
  if (native->descriptorPipelineLayout != pipeline->shaderLayout.baseLayout) {
    memset(native->descriptorGroups, 0, sizeof(native->descriptorGroups));
  }
  vk_bindShaderSamplers(native->command,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        &pipeline->shaderLayout);
  native->pipelineLayout     = pipeline->shaderLayout.layout;
  native->descriptorPipelineLayout = pipeline->shaderLayout.baseLayout;
  encoder->_workgroupSize[0] = pipelineState->workgroupSize[0];
  encoder->_workgroupSize[1] = pipelineState->workgroupSize[1];
  encoder->_workgroupSize[2] = pipelineState->workgroupSize[2];
}

GPU_HIDE
void
vk_computePushConstants(GPUComputePassEncoder *encoder,
                        const void            *data,
                        uint32_t               sizeBytes) {
  GPUComputeEncoderVk *native;

  native = vk__computeEncoder(encoder);
  if (!native || !native->command || !native->pipelineLayout ||
      !data || sizeBytes == 0u) {
    return;
  }

  vkCmdPushConstants(native->command,
                     native->pipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0u,
                     sizeBytes,
                     data);
}

GPU_HIDE
void
vk_dispatch(GPUComputePassEncoder *encoder,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  GPUComputeEncoderVk *native;

  native = vk__computeEncoder(encoder);
  if (!native || !native->command) {
    return;
  }

  vkCmdDispatch(native->command, x, y, z);
}

GPU_HIDE
void
vk_dispatchIndirect(GPUComputePassEncoder *encoder,
                    GPUBuffer            *argsBuffer,
                    uint64_t              argsOffset) {
  GPUComputeEncoderVk *native;
  GPUBufferVk         *buffer;

  native = vk__computeEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->command || !buffer || !buffer->buffer) {
    return;
  }

  vkCmdDispatchIndirect(native->command, buffer->buffer, argsOffset);
}

GPU_HIDE
void
vk_endComputeEncoding(GPUComputePassEncoder *encoder) {
  GPUComputeEncoderVk *native;

  native = vk__computeEncoder(encoder);
  if (!native) {
    return;
  }

  if (native->debugLabelActive) {
    vk_endDebugLabel(gpuCommandBufferDevice(encoder->_cmdb), native->command);
  }

  native->command        = VK_NULL_HANDLE;
  native->pipelineLayout = VK_NULL_HANDLE;
  native->debugLabelActive = false;
}

GPU_HIDE
void
vk_initCompute(GPUApiCompute *api) {
  api->createPipeline          = vk_createComputePipeline;
  api->destroyComputePipeline  = vk_destroyComputePipeline;
  api->computeCommandEncoder   = vk_computeCommandEncoder;
  api->setComputePipelineState = vk_setComputePipelineState;
  api->pushConstants           = vk_computePushConstants;
  api->dispatch                = vk_dispatch;
  api->dispatchIndirect        = vk_dispatchIndirect;
  api->endEncoding             = vk_endComputeEncoding;
}
