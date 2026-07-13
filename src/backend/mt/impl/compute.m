/*
 * Copyright (C) 2020 Recep Aslantas
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
#include "../../../api/compute_internal.h"

typedef struct MTComputePipelineDesc {
  id<MTLFunction> function;
} MTComputePipelineDesc;

static MTComputeEncoder *
mt_computeEncoder(GPUComputePassEncoder *enc) {
  return enc ? enc->_priv : NULL;
}

static id<MTLBuffer>
mt_nativeBuffer(GPUBuffer *buffer) {
  return buffer ? (id<MTLBuffer>)buffer->_priv : nil;
}

GPU_HIDE
GPUComputePipeline*
mt_newComputePipeline(void) {
  GPUComputePipeline *pipeline;
  MTComputePipelineDesc *desc;

  pipeline = calloc(1, sizeof(*pipeline));
  desc = calloc(1, sizeof(*desc));
  if (!pipeline || !desc) {
    free(desc);
    free(pipeline);
    return NULL;
  }

  pipeline->_priv = desc;
  return pipeline;
}

GPU_HIDE
void
mt_setComputeFunction(GPUComputePipeline *pipeline, GPUFunction *func) {
  MTComputePipelineDesc *desc;

  if (!pipeline || !pipeline->_priv || !func) {
    return;
  }

  desc = pipeline->_priv;
  desc->function = (id<MTLFunction>)func->_priv;
}

GPU_HIDE
GPUComputePipelineState*
mt_newComputeState(GPUDevice *device, GPUComputePipeline *pipeline) {
  GPUDeviceMT *deviceMT;
  MTComputePipelineDesc *desc;
  GPUComputePipelineState *state;
  id<MTLComputePipelineState> mtState;
  NSError *error;

  if (!device || !pipeline || !pipeline->_priv) {
    return NULL;
  }

  deviceMT = device->_priv;
  desc = pipeline->_priv;
  if (!deviceMT || !desc->function) {
    return NULL;
  }

  error = nil;
  mtState = [deviceMT->device newComputePipelineStateWithFunction:desc->function
                                                            error:&error];
  if (!mtState) {
    NSLog(@"Failed to create compute pipeline state: %@", error);
    return NULL;
  }

  state = calloc(1, sizeof(*state));
  if (!state) {
    [mtState release];
    return NULL;
  }

  state->_priv = mtState;
  state->workgroupSize[0] = 1u;
  state->workgroupSize[1] = 1u;
  state->workgroupSize[2] = 1u;
  pipeline->_state = state;
  return state;
}

GPU_HIDE
void
mt_destroyComputePipeline(GPUComputePipeline *pipeline) {
  MTComputePipelineDesc *desc;

  if (!pipeline) {
    return;
  }

  desc = pipeline->_priv;
  if (pipeline->_state) {
    GPUComputePipelineState *state = pipeline->_state;
    if (state->_priv) {
      [(id<MTLComputePipelineState>)state->_priv release];
    }
    free(state);
  }
  free(desc);
  free(pipeline);
}

GPU_HIDE
GPUComputePassEncoder*
mt_computeCommandEncoder(GPUCommandBuffer *cmdb, const char *label) {
  MTCommandBuffer       *commandState;
  MTComputeEncoder      *nativeState;
  GPUComputePassEncoder *enc;

  if (!cmdb) {
    return NULL;
  }

  commandState = mt_commandBuffer(cmdb);
  if (!commandState) {
    return NULL;
  }
  enc = &commandState->computeEncoder;
  nativeState = &commandState->computeState;
  memset(enc, 0, sizeof(*enc));
  memset(nativeState, 0, sizeof(*nativeState));

  if (commandState && commandState->mode == MTCommandMode4) {
    if (!mt_prepareArgumentState(cmdb,
                                 &commandState->computeArguments,
                                 gpuDeviceDebugLabel(
                                   gpuCommandBufferDevice(cmdb),
                                   "gpu-metal4-compute-arguments"))) {
      return NULL;
    }
    if (@available(macOS 26.0, iOS 26.0, *)) {
      nativeState->modern = [commandState->modern computeCommandEncoder];
      mt_applyPendingBarrier(cmdb, nativeState->modern);
      nativeState->arguments = &commandState->computeArguments;
      [nativeState->modern setArgumentTable:nativeState->arguments->table];
    }
  } else {
    nativeState->classic = [mt_classicCommandBuffer(cmdb) computeCommandEncoder];
  }

  if (!nativeState->classic && !nativeState->modern) {
    return NULL;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    NSString *nativeLabel = [NSString stringWithUTF8String:label];

    nativeState->classic.label = nativeLabel;
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4ComputeCommandEncoder>)nativeState->modern setLabel:nativeLabel];
    }
  }
#else
  GPU__UNUSED(label);
#endif

  enc->_priv = nativeState;
  enc->_workgroupSize[0] = 1u;
  enc->_workgroupSize[1] = 1u;
  enc->_workgroupSize[2] = 1u;
  return enc;
}

GPU_HIDE
void
mt_setComputePipelineState(GPUComputePassEncoder *enc,
                           GPUComputePipelineState *state) {
  MTComputeEncoder *native;

  if (!enc || !state || !state->_priv) {
    return;
  }

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  if (native && native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern setComputePipelineState:(id<MTLComputePipelineState>)state->_priv];
    }
  } else {
    [native->classic setComputePipelineState:(id<MTLComputePipelineState>)state->_priv];
  }
  enc->_workgroupSize[0] = state->workgroupSize[0] ? state->workgroupSize[0] : 1u;
  enc->_workgroupSize[1] = state->workgroupSize[1] ? state->workgroupSize[1] : 1u;
  enc->_workgroupSize[2] = state->workgroupSize[2] ? state->workgroupSize[2] : 1u;
}

GPU_HIDE
void
mt_computeBuffer(GPUComputePassEncoder *enc,
                 GPUBuffer             *buf,
                 size_t                 off,
                 uint32_t               index) {
  MTComputeEncoder *native;
  id<MTLBuffer>     buffer;

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  buffer = mt_nativeBuffer(buf);
  if (native && native->modern) {
    mt_setArgumentBuffer(enc->_cmdb, native->arguments, buffer, off, index);
    return;
  }
  [native->classic setBuffer:buffer offset:(NSUInteger)off atIndex:index];
}

GPU_HIDE
void
mt_computeTexture(GPUComputePassEncoder *enc,
                  GPUTextureView        *view,
                  uint32_t               index) {
  MTComputeEncoder *native;
  id<MTLTexture>    texture;

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  texture = view ? (id<MTLTexture>)view->_priv : nil;
  if (native && native->modern) {
    mt_setArgumentTexture(enc->_cmdb, native->arguments, texture, index);
    return;
  }
  [native->classic setTexture:texture atIndex:index];
}

GPU_HIDE
void
mt_computeSampler(GPUComputePassEncoder *enc,
                  GPUSampler            *sampler,
                  uint32_t               index) {
  MTComputeEncoder    *native;
  id<MTLSamplerState>  samplerState;

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  samplerState = sampler ? (id<MTLSamplerState>)sampler->_priv : nil;
  if (native && native->modern) {
    mt_setArgumentSampler(native->arguments, samplerState, index);
    return;
  }
  [native->classic setSamplerState:samplerState atIndex:index];
}

GPU_HIDE
void
mt_computePushConstants(GPUComputePassEncoder *enc,
                        const void            *data,
                        uint32_t               sizeBytes) {
  MTComputeEncoder *native;

  if (!data || sizeBytes == 0u) {
    return;
  }

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  if (native && native->modern) {
    uint64_t address;

    if (mt_uploadConstants(enc->_cmdb, data, sizeBytes, &address)) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        [(id<MTL4ArgumentTable>)native->arguments->table
          setAddress:address
             atIndex:MT_PUSH_CONSTANT_INDEX];
      }
      native->arguments->bufferMask |= 1u << MT_PUSH_CONSTANT_INDEX;
    }
    return;
  }
  [native->classic setBytes:data
                     length:(NSUInteger)sizeBytes
                    atIndex:MT_PUSH_CONSTANT_INDEX];
}

GPU_HIDE
void
mt_dispatch(GPUComputePassEncoder *enc,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  MTComputeEncoder *native;
  MTLSize groups;
  MTLSize threads;

  groups = MTLSizeMake(x, y, z);
  threads = MTLSizeMake(enc->_workgroupSize[0] ? enc->_workgroupSize[0] : 1u,
                        enc->_workgroupSize[1] ? enc->_workgroupSize[1] : 1u,
                        enc->_workgroupSize[2] ? enc->_workgroupSize[2] : 1u);
  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern dispatchThreadgroups:groups
                       threadsPerThreadgroup:threads];
    }
    return;
  }
  [native->classic dispatchThreadgroups:groups threadsPerThreadgroup:threads];
}

GPU_HIDE
void
mt_dispatchIndirect(GPUComputePassEncoder *enc,
                    GPUBuffer             *argsBuffer,
                    uint64_t               argsOffset) {
  MTComputeEncoder *native;
  id<MTLBuffer>     args;
  MTLSize           threads;

  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  args = mt_nativeBuffer(argsBuffer);
  threads = MTLSizeMake(enc->_workgroupSize[0] ? enc->_workgroupSize[0] : 1u,
                        enc->_workgroupSize[1] ? enc->_workgroupSize[1] : 1u,
                        enc->_workgroupSize[2] ? enc->_workgroupSize[2] : 1u);
  if (native && native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(enc->_cmdb, args);
      [native->modern dispatchThreadgroupsWithIndirectBuffer:args.gpuAddress + argsOffset
                                       threadsPerThreadgroup:threads];
    }
    return;
  }
  [native->classic dispatchThreadgroupsWithIndirectBuffer:args
                                      indirectBufferOffset:(NSUInteger)argsOffset
                                    threadsPerThreadgroup:threads];
}

GPU_HIDE
void
mt_endComputeEncoding(GPUComputePassEncoder *enc) {
  MTComputeEncoder *native;

  if (!enc) {
    return;
  }
  native = mt_computeEncoder(enc);
  if (!native) {
    return;
  }
  if (native && native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern endEncoding];
    }
  } else {
    [native->classic endEncoding];
  }
  native->classic = nil;
  native->modern = nil;
}

GPU_HIDE
void
mt_initCompute(GPUApiCompute *api) {
  api->newComputePipeline = mt_newComputePipeline;
  api->setFunction = mt_setComputeFunction;
  api->newComputeState = mt_newComputeState;
  api->destroyComputePipeline = mt_destroyComputePipeline;
  api->computeCommandEncoder = mt_computeCommandEncoder;
  api->setComputePipelineState = mt_setComputePipelineState;
  api->buffer = mt_computeBuffer;
  api->texture = mt_computeTexture;
  api->sampler = mt_computeSampler;
  api->pushConstants = mt_computePushConstants;
  api->dispatch = mt_dispatch;
  api->dispatchIndirect = mt_dispatchIndirect;
  api->endEncoding = mt_endComputeEncoding;
}
