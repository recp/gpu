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

#include "../../common.h"

GPU_EXPORT
void
GPUBindRenderPipeline(GPURenderPassEncoder *pass, GPURenderPipeline *pipeline) {
  GPURenderPipelineState state;
  GPUApi *api;

  if (!pass || !pipeline || !pipeline->_state)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->rce.setRenderPipelineState)
    return;

  state._priv = pipeline->_state;
  api->rce.setRenderPipelineState(pass, &state);
  if (api->rce.cullMode)
    api->rce.cullMode(pass, pipeline->_cullMode);
  if (api->rce.frontFace)
    api->rce.frontFace(pass, pipeline->_frontFace);
}

GPU_EXPORT
void
GPUSetVertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.vertexBuffer(rce, buf, off, index);
}

GPU_EXPORT
void
GPUBindVertexBuffers(GPURenderPassEncoder   *pass,
                     uint32_t                firstSlot,
                     uint32_t                count,
                     const GPUBufferBinding *bindings) {
  GPUApi *api;
  uint32_t i;

  if (!pass || !bindings)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->rce.vertexBuffer)
    return;

  for (i = 0; i < count; i++) {
    if (bindings[i].buffer) {
      api->rce.vertexBuffer(pass,
                            bindings[i].buffer,
                            bindings[i].offset,
                            firstSlot + i);
    }
  }
}

GPU_EXPORT
void
GPUSetViewport(GPURenderPassEncoder *pass, const GPUViewport *viewport) {
  GPUApi *api;

  if (!pass || !viewport)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.viewport)
    return;

  api->rce.viewport(pass, viewport);
}

GPU_EXPORT
void
GPUSetScissor(GPURenderPassEncoder *pass, const GPUScissorRect *scissor) {
  GPUApi *api;

  if (!pass || !scissor)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.scissor)
    return;

  api->rce.scissor(pass, scissor);
}

GPU_EXPORT
void
GPUSetBlendConstant(GPURenderPassEncoder *pass, const float rgba[4]) {
  GPUApi *api;

  if (!pass || !rgba)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.blendConstant)
    return;

  api->rce.blendConstant(pass, rgba);
}

GPU_EXPORT
void
GPUSetStencilReference(GPURenderPassEncoder *pass, uint32_t reference) {
  GPUApi *api;

  if (!pass)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.stencilReference)
    return;

  api->rce.stencilReference(pass, reference);
}

GPU_EXPORT
void
GPUSetVertexTexture(GPURenderCommandEncoder *rce,
                    GPUTextureView          *view,
                    uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setVertexTexture) {
    api->rce.setVertexTexture(rce, view, index);
  }
}

GPU_EXPORT
void
GPUSetVertexSampler(GPURenderCommandEncoder *rce,
                    GPUSampler              *sampler,
                    uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setVertexSampler) {
    api->rce.setVertexSampler(rce, sampler, index);
  }
}

GPU_EXPORT
void
GPUSetFragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.fragmentBuffer(rce, buf, off, index);
}

GPU_EXPORT
void
GPUSetFragmentTexture(GPURenderCommandEncoder *rce,
                      GPUTextureView           *view,
                      uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.setFragmentTexture(rce, view, index);
}

GPU_EXPORT
void
GPUSetFragmentSampler(GPURenderCommandEncoder *rce,
                      GPUSampler              *sampler,
                      uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setFragmentSampler) {
    api->rce.setFragmentSampler(rce, sampler, index);
  }
}

GPU_EXPORT
void
GPUDraw(GPURenderPassEncoder *pass,
        uint32_t              vertexCount,
        uint32_t              instanceCount,
        uint32_t              firstVertex,
        uint32_t              firstInstance) {
  GPUApi *api;
  uint32_t i;

  if (!pass || vertexCount == 0 || instanceCount == 0 || firstInstance != 0)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.drawPrimitives)
    return;

  for (i = 0; i < instanceCount; i++) {
    api->rce.drawPrimitives(pass,
                            GPUPrimitiveTypeTriangle,
                            firstVertex,
                            vertexCount);
  }
}

GPU_EXPORT
void
GPUDrawIndexed(GPURenderCommandEncoder *rce,
               GPUPrimitiveType         type,
               uint32_t                 indexCount,
               GPUIndexType             indexType,
               GPUBuffer               *indexBuffer,
               uint32_t                 indexBufferOffset) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.drawIndexedPrims(rce,
                            type,
                            indexCount,
                            indexType,
                            indexBuffer,
                            indexBufferOffset);
}

GPU_EXPORT
void
GPUApplyDynamicState(GPURenderPassEncoder *pass,
                     const GPUDynamicStateApplyInfo *info) {
  if (!pass || !info)
    return;

  if (info->mask & GPU_DYNAMIC_STATE_VIEWPORT_BIT)
    GPUSetViewport(pass, &info->viewport);
  if (info->mask & GPU_DYNAMIC_STATE_SCISSOR_BIT)
    GPUSetScissor(pass, &info->scissor);
  if (info->mask & GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT)
    GPUSetBlendConstant(pass, info->blendConstant);
  if (info->mask & GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT)
    GPUSetStencilReference(pass, info->stencilReference);
}
