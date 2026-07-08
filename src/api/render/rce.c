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
#include "pipeline_internal.h"
#include "rce_internal.h"

static GPUPrimitiveType
gpu_primitiveTypeFromTopology(GPUPrimitiveTopology topology) {
  switch (topology) {
    case GPU_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return GPUPrimitiveTypePoint;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return GPUPrimitiveTypeLine;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return GPUPrimitiveTypeLineStrip;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return GPUPrimitiveTypeTriangleStrip;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    default:
      return GPUPrimitiveTypeTriangle;
  }
}

static bool
gpu_validIndexType(GPUIndexType indexType) {
  return indexType == GPUIndexTypeUInt16 ||
         indexType == GPUIndexTypeUInt32;
}

static bool
gpu_validDynamicStateApplyInfo(const GPUDynamicStateApplyInfo *info) {
  if (!info) {
    return false;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO) {
    return false;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return false;
  }

  return true;
}

GPU_EXPORT
void
GPUBindRenderPipeline(GPURenderPassEncoder *pass, GPURenderPipeline *pipeline) {
  GPURenderPipelineState state;
  GPUApi *api;

  if (!pass || pass->_ended || !pipeline || !pipeline->_state)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;
  if (!api->rce.setRenderPipelineState)
    return;

  state._priv = pipeline->_state;
  api->rce.setRenderPipelineState(pass, &state);
  pass->_hasPipeline = true;
  pass->_primitiveType = gpu_primitiveTypeFromTopology(pipeline->_primitiveTopology);
  if (api->rce.cullMode)
    api->rce.cullMode(pass, pipeline->_cullMode);
  if (api->rce.frontFace)
    api->rce.frontFace(pass, pipeline->_frontFace);
}

GPU_HIDE
void
gpuSetRenderVertexBuffer(GPURenderPassEncoder *pass,
                         GPUBuffer            *buf,
                         size_t                off,
                         uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.vertexBuffer)
    return;
  
  api->rce.vertexBuffer(pass, buf, off, index);
}

GPU_EXPORT
void
GPUBindVertexBuffers(GPURenderPassEncoder   *pass,
                     uint32_t                firstSlot,
                     uint32_t                count,
                     const GPUBufferBinding *bindings) {
  GPUApi *api;
  uint32_t i;

  if (!pass || pass->_ended || !bindings)
    return;
  if (count == 0 || firstSlot > UINT32_MAX - (count - 1u))
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
GPUBindIndexBuffer(GPURenderPassEncoder *pass,
                   GPUBuffer            *indexBuffer,
                   uint64_t              offset,
                   GPUIndexType          indexType) {
  if (!pass || pass->_ended || !indexBuffer)
    return;
  if (!gpu_validIndexType(indexType))
    return;

  pass->_indexBuffer       = indexBuffer;
  pass->_indexBufferOffset = offset;
  pass->_indexType         = indexType;
  pass->_hasIndexBuffer    = true;
}

GPU_EXPORT
void
GPUSetViewport(GPURenderPassEncoder *pass, const GPUViewport *viewport) {
  GPUApi *api;

  if (!pass || pass->_ended || !viewport)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.viewport)
    return;

  api->rce.viewport(pass, viewport);
}

GPU_EXPORT
void
GPUSetScissor(GPURenderPassEncoder *pass, const GPUScissorRect *scissor) {
  GPUApi *api;

  if (!pass || pass->_ended || !scissor)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.scissor)
    return;

  api->rce.scissor(pass, scissor);
}

GPU_EXPORT
void
GPUSetBlendConstant(GPURenderPassEncoder *pass, const float rgba[4]) {
  GPUApi *api;

  if (!pass || pass->_ended || !rgba)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.blendConstant)
    return;

  api->rce.blendConstant(pass, rgba);
}

GPU_EXPORT
void
GPUSetStencilReference(GPURenderPassEncoder *pass, uint32_t reference) {
  GPUApi *api;

  if (!pass || pass->_ended)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.stencilReference)
    return;

  api->rce.stencilReference(pass, reference);
}

GPU_HIDE
void
gpuSetRenderVertexTexture(GPURenderPassEncoder *pass,
                          GPUTextureView       *view,
                          uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !view)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setVertexTexture) {
    api->rce.setVertexTexture(pass, view, index);
  }
}

GPU_HIDE
void
gpuSetRenderVertexSampler(GPURenderPassEncoder *pass,
                          GPUSampler           *sampler,
                          uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !sampler)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setVertexSampler) {
    api->rce.setVertexSampler(pass, sampler, index);
  }
}

GPU_HIDE
void
gpuSetRenderFragmentBuffer(GPURenderPassEncoder *pass,
                           GPUBuffer            *buf,
                           size_t                off,
                           uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.fragmentBuffer)
    return;
  
  api->rce.fragmentBuffer(pass, buf, off, index);
}

GPU_HIDE
void
gpuSetRenderFragmentTexture(GPURenderPassEncoder *pass,
                            GPUTextureView       *view,
                            uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !view)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.setFragmentTexture)
    return;
  
  api->rce.setFragmentTexture(pass, view, index);
}

GPU_HIDE
void
gpuSetRenderFragmentSampler(GPURenderPassEncoder *pass,
                            GPUSampler           *sampler,
                            uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !sampler)
    return;
  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->rce.setFragmentSampler) {
    api->rce.setFragmentSampler(pass, sampler, index);
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

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      vertexCount == 0 || instanceCount == 0)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.drawPrimitives)
    return;

  api->rce.drawPrimitives(pass,
                          pass->_primitiveType,
                          firstVertex,
                          vertexCount,
                          instanceCount,
                          firstInstance);
}

GPU_EXPORT
void
GPUDrawIndexed(GPURenderPassEncoder *pass,
               uint32_t              indexCount,
               uint32_t              instanceCount,
               uint32_t              firstIndex,
               int32_t               vertexOffset,
               uint32_t              firstInstance) {
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      indexCount == 0 || instanceCount == 0 || !pass->_hasIndexBuffer)
    return;
  if (!(api = gpuActiveGPUApi()) || !api->rce.drawIndexedPrims)
    return;
  
  api->rce.drawIndexedPrims(pass,
                            indexCount,
                            instanceCount,
                            firstIndex,
                            vertexOffset,
                            firstInstance);
}

GPU_EXPORT
void
GPUApplyDynamicState(GPURenderPassEncoder *pass,
                     const GPUDynamicStateApplyInfo *info) {
  if (!pass || pass->_ended || !gpu_validDynamicStateApplyInfo(info))
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
