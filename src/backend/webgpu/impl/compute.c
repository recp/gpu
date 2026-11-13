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
#include "../../../api/compile_pool_internal.h"
#include "constants.h"

static const uint8_t webgpu_zeroPushConstants[GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT];

/* background compiles on Dawn (GPUSetPipelineCompileThreads): its device
 * only one thread may call, so WebGPU's own async creation compiles them
 * (on Dawn's workers) and the pipeline is waited for at its first bind,
 * GPUWaitComputePipeline or destroy. a browser's pipelines already compile
 * outside the calling thread. */
#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
#  define GPU_WEBGPU_ASYNC_COMPUTE 1

typedef struct GPUComputePipelineWebGPUAsync {
  GPUComputePipelineWebGPU      state; /* first: what the rest of the backend sees */
  GPUComputePipeline           *owner;
  WGPUInstance                  instance;
  WGPUFuture                    future;
  WGPUComputePipeline           created;
  WGPUCreatePipelineAsyncStatus status;
  bool                          pending;
} GPUComputePipelineWebGPUAsync;

#  define GPU_WEBGPU_COMPUTE_STATE_SIZE sizeof(GPUComputePipelineWebGPUAsync)

/* runs inside the wait for its future (WaitAnyOnly) */
static void
webgpu_computeCreated(WGPUCreatePipelineAsyncStatus status,
                      WGPUComputePipeline           created,
                      WGPUStringView                message,
                      void                         *userData,
                      void                         *unused) {
  GPUComputePipelineWebGPUAsync *async;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  async          = userData;
  async->status  = status;
  async->created = created;
}

static void
webgpu_computeResolve(GPUComputePipelineWebGPU *state) {
  GPUComputePipelineWebGPUAsync *async;
  WGPUFutureWaitInfo             wait = WGPU_FUTURE_WAIT_INFO_INIT;

  async = (GPUComputePipelineWebGPUAsync *)state;

  if (!async->pending)
    return;

  wait.future = async->future;

  /* not completed: its callback may still come, so the state stays */
  if (wgpuInstanceWaitAny(async->instance, 1u, &wait, UINT64_MAX) != WGPUWaitStatus_Success
      || !wait.completed)
    return;

  async->pending = false;

  if (async->status == WGPUCreatePipelineAsyncStatus_Success && async->created) {
    state->pipeline      = async->created;
    state->base._priv    = async->created;
    async->owner->_priv  = async->created;
  } else if (async->created) {
    wgpuComputePipelineRelease(async->created);
  }

  async->created = NULL;
}
#else
#  define GPU_WEBGPU_ASYNC_COMPUTE 0
#  define GPU_WEBGPU_COMPUTE_STATE_SIZE sizeof(GPUComputePipelineWebGPU)
#endif

static GPUCommandWebGPU*
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
  GPUWebGPUPipelineError         error;
#endif
  GPUComputePipelineWebGPU     *state;
  GPUDeviceWebGPU              *native;
#if GPU_WEBGPU_ASYNC_COMPUTE
  WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo = WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
  GPUComputePipelineWebGPUAsync             *async;
  GPUInstanceWebGPU                         *instance;
#endif
  uint64_t                      entryMask;
  uint32_t                      automaticGroupMask;
  GPUResult                     result;

  native = webgpuDevice(device);

  if (!native || !native->device || !info || !info->library
      || !info->library->_priv || !info->layout || !info->layout->_native
      || !info->entryPoint || !info->entryPoint[0] || !pipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(state = calloc(1, GPU_WEBGPU_COMPUTE_STATE_SIZE))) {
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

  state->base.workgroupSize[0] = 1u;
  state->base.workgroupSize[1] = 1u;
  state->base.workgroupSize[2] = 1u;

#if GPU_WEBGPU_ASYNC_COMPUTE
  if (compilePoolActive(device)
      && (instance = webgpuInstance(device->inst)) && instance->instance && instance->timedWaitAny) {
    async                  = (GPUComputePipelineWebGPUAsync *)state;
    async->owner           = pipeline;
    async->instance        = instance->instance;
    callbackInfo.mode      = WGPUCallbackMode_WaitAnyOnly;
    callbackInfo.callback  = webgpu_computeCreated;
    callbackInfo.userdata1 = async;
    async->future          = wgpuDeviceCreateComputePipelineAsync(native->device, &descriptor, callbackInfo);

    if (async->future.id) {
      async->pending   = true;
      pipeline->_state = &state->base;
      return GPU_OK;
    }
  }
#endif

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

  state->base._priv = state->pipeline;
  pipeline->_priv   = state->pipeline;
  pipeline->_state  = &state->base;

  return GPU_OK;
}

static GPUResult
webgpu_waitComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineWebGPU *state;

  if (!pipeline || !(state = (GPUComputePipelineWebGPU *)pipeline->_state))
    return GPU_ERROR_INVALID_ARGUMENT;

#if GPU_WEBGPU_ASYNC_COMPUTE
  webgpu_computeResolve(state);
#endif

  return state->pipeline ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyComputePipeline(GPUComputePipeline *pipeline) {
  GPUComputePipelineWebGPU *state;

  if (!pipeline) {
    return;
  }

  state = (GPUComputePipelineWebGPU *)pipeline->_state;

  if (state) {
#if GPU_WEBGPU_ASYNC_COMPUTE
    webgpu_computeResolve(state);
#endif

    if (state->pipeline) {
      wgpuComputePipelineRelease(state->pipeline);
    }

    webgpuDestroyPipelineLayout(&state->layout);

#if GPU_WEBGPU_ASYNC_COMPUTE
    /* a wait that failed: its callback may still write here */
    if (((GPUComputePipelineWebGPUAsync *)state)->pending)
      state = NULL;
#endif
  }

  free(state);
  free(pipeline);
}

static GPUComputePassEncoder*
webgpu_computeCommandEncoder(GPUCommandBuffer               *cmdb,
                             const GPUComputePassCreateInfo *info) {
  WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
  GPUCommandWebGPU         *command;

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
  GPUCommandWebGPU *command;
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
                          GPUComputePipelineState *state) {
  GPUComputePipelineWebGPU *nativeState;
  GPUCommandWebGPU         *command;

  command     = webgpu_computeCommand(encoder);
  nativeState = (GPUComputePipelineWebGPU *)state;

#if GPU_WEBGPU_ASYNC_COMPUTE
  if (nativeState)
    webgpu_computeResolve(nativeState);
#endif

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
webgpu_initCompute(GPUComputeApi *api) {
  api->createPipeline          = webgpu_createComputePipeline;
  api->destroyComputePipeline  = webgpu_destroyComputePipeline;
  api->waitPipeline            = webgpu_waitComputePipeline;
  api->computeCommandEncoder   = webgpu_computeCommandEncoder;
  api->setComputePipelineState = webgpu_setComputePipeline;
  api->pushConstants           = webgpu_computePushConstants;
  api->dispatch                = webgpu_dispatch;
  api->dispatchIndirect        = webgpu_dispatchIndirect;
  api->endEncoding             = webgpu_endComputeEncoding;
}
