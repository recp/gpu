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

static id<MTLComputeCommandEncoder>
mt_nativeCCE(GPUComputePassEncoder *enc) {
  return enc ? (__bridge id<MTLComputeCommandEncoder>)enc->_priv : nil;
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
  pipeline->_state = mtState;
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
    [(id<MTLComputePipelineState>)pipeline->_state release];
  }
  free(desc);
  free(pipeline);
}

GPU_HIDE
GPUComputePassEncoder*
mt_computeCommandEncoder(GPUCommandBuffer *cmdb, const char *label) {
  id<MTLComputeCommandEncoder> native;
  GPUComputePassEncoder *enc;

  if (!cmdb) {
    return NULL;
  }

  native = [(id<MTLCommandBuffer>)cmdb->_priv computeCommandEncoder];
  if (!native) {
    return NULL;
  }
  if (label) {
    native.label = [NSString stringWithUTF8String:label];
  }

  enc = calloc(1, sizeof(*enc));
  if (!enc) {
    [native endEncoding];
    return NULL;
  }

  enc->_priv = (__bridge void *)native;
  return enc;
}

GPU_HIDE
void
mt_setComputePipelineState(GPUComputePassEncoder *enc,
                           GPUComputePipelineState *state) {
  if (!enc || !state || !state->_priv) {
    return;
  }

  [mt_nativeCCE(enc) setComputePipelineState:(id<MTLComputePipelineState>)state->_priv];
}

GPU_HIDE
void
mt_computeBuffer(GPUComputePassEncoder *enc,
                 GPUBuffer             *buf,
                 size_t                 off,
                 uint32_t               index) {
  [mt_nativeCCE(enc) setBuffer:(id<MTLBuffer>)buf
                        offset:(NSUInteger)off
                       atIndex:index];
}

GPU_HIDE
void
mt_computeTexture(GPUComputePassEncoder *enc,
                  GPUTextureView        *view,
                  uint32_t               index) {
  [mt_nativeCCE(enc) setTexture:view ? (id<MTLTexture>)view->_priv : nil
                        atIndex:index];
}

GPU_HIDE
void
mt_computeSampler(GPUComputePassEncoder *enc,
                  GPUSampler            *sampler,
                  uint32_t               index) {
  [mt_nativeCCE(enc) setSamplerState:sampler ? (id<MTLSamplerState>)sampler->_priv : nil
                             atIndex:index];
}

GPU_HIDE
void
mt_dispatch(GPUComputePassEncoder *enc,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z) {
  MTLSize groups;
  MTLSize threads;

  groups = MTLSizeMake(x, y, z);
  threads = MTLSizeMake(1, 1, 1);
  [mt_nativeCCE(enc) dispatchThreadgroups:groups
                    threadsPerThreadgroup:threads];
}

GPU_HIDE
void
mt_dispatchIndirect(GPUComputePassEncoder *enc,
                    GPUBuffer             *argsBuffer,
                    uint64_t               argsOffset) {
  [mt_nativeCCE(enc) dispatchThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)argsBuffer
                                               indirectBufferOffset:(NSUInteger)argsOffset
                                             threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
}

GPU_HIDE
void
mt_endComputeEncoding(GPUComputePassEncoder *enc) {
  [mt_nativeCCE(enc) endEncoding];
  free(enc);
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
  api->dispatch = mt_dispatch;
  api->dispatchIndirect = mt_dispatchIndirect;
  api->endEncoding = mt_endComputeEncoding;
}
