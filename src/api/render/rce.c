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
#include "../buffer_internal.h"
#include "../cmdqueue_internal.h"
#include "../descr/descriptor_internal.h"
#include "../device_internal.h"
#include "pipeline_internal.h"
#include "rce_internal.h"

#if GPU_BUILD_WITH_VALIDATION
#  include <math.h>
#endif

static GPUDevice *
gpu_renderPassDevice(const GPURenderPassEncoder *pass) {
  if (!pass) {
    return NULL;
  }
  if (pass->_device) {
    return pass->_device;
  }
  if (!pass->_cmdb || !pass->_cmdb->_queue) {
    return NULL;
  }

  return pass->_cmdb->_queue->_device;
}

static GPUApi *
gpu_renderPassApi(const GPURenderPassEncoder *pass) {
  if (pass && pass->_api) {
    return pass->_api;
  }
  return gpuDeviceApi(gpu_renderPassDevice(pass));
}

#if GPU_BUILD_WITH_VALIDATION
static bool
gpu_validViewport(const GPUViewport *viewport) {
  return viewport &&
         isfinite(viewport->x) &&
         isfinite(viewport->y) &&
         isfinite(viewport->width) &&
         isfinite(viewport->height) &&
         isfinite(viewport->minDepth) &&
         isfinite(viewport->maxDepth) &&
         viewport->width > 0.0f &&
         viewport->height > 0.0f &&
         viewport->minDepth >= 0.0f &&
         viewport->minDepth <= 1.0f &&
         viewport->maxDepth >= 0.0f &&
         viewport->maxDepth <= 1.0f;
}
#endif

#if GPU_BUILD_WITH_VALIDATION
static void
gpu_renderValidationError(const GPURenderPassEncoder *pass,
                          const char                   *message) {
  gpuDeviceRecordValidationError(gpu_renderPassDevice(pass), message);
}

static inline bool
gpu_renderBindingsComplete(const GPURenderPassEncoder *pass) {
  if (!gpuDeviceValidationEnabled(gpu_renderPassDevice(pass))) {
    return true;
  }

  return gpuPipelineLayoutMaskIsBound(pass->_pipelineLayout,
                                      pass->_boundGroupLayouts,
                                      GPU_ENCODER_MAX_BIND_GROUPS,
                                      pass->_requiredBindGroupMask);
}
#else
#  define gpu_renderValidationError(pass, message) ((void)0)
#  define gpu_renderBindingsComplete(pass) true
#endif

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
  return indexType == GPU_INDEX_TYPE_UINT16 ||
         indexType == GPU_INDEX_TYPE_UINT32;
}

static bool
gpu_validDynamicStateApplyInfo(const GPUDynamicStateApplyInfo *info) {
  const GPUDynamicStateMask validMask =
    GPU_DYNAMIC_STATE_VIEWPORT_BIT |
    GPU_DYNAMIC_STATE_SCISSOR_BIT |
    GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT |
    GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;

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

  return (info->mask & ~validMask) == 0u;
}

static bool
gpu_validPushConstantRange(uint32_t limit,
                           uint32_t offset,
                           uint32_t sizeBytes,
                           const void *data) {
  if (sizeBytes == 0u) {
    return true;
  }
  if (!data || offset > limit) {
    return false;
  }

  return sizeBytes <= limit - offset;
}

static bool
gpu_renderPipelineMatchesPass(const GPURenderPassEncoder *pass,
                              const GPURenderPipeline *pipeline) {
  if (pipeline->_colorTargetCount != pass->_colorAttachmentCount) {
    return false;
  }

  for (uint32_t i = 0; i < pipeline->_colorTargetCount; i++) {
    if (pipeline->_colorTargetFormats[i] != pass->_colorAttachmentFormats[i] ||
        pipeline->_sampleCount != pass->_colorAttachmentSampleCounts[i]) {
      return false;
    }
    if (pass->_colorAttachmentHasResolve[i] && pipeline->_sampleCount <= 1u) {
      return false;
    }
  }

  if (pipeline->_depthStencilFormat != pass->_depthStencilFormat) {
    return false;
  }
  if (pass->_depthStencilFormat != GPU_FORMAT_UNDEFINED &&
      pipeline->_sampleCount != pass->_depthStencilSampleCount) {
    return false;
  }

  return true;
}

static bool
gpu_validIndirectBatch(GPUBuffer *argsBuffer,
                       uint64_t argsOffset,
                       uint32_t commandCount,
                       uint32_t strideBytes,
                       uint32_t commandSize) {
  uint64_t maxCommandIndex;
  uint64_t lastCommandOffset;

  if (!gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      (argsOffset & 3u) != 0u ||
      commandCount == 0u ||
      strideBytes < commandSize ||
      (strideBytes & 3u) != 0u ||
      commandSize > UINT64_MAX - argsOffset) {
    return false;
  }

  maxCommandIndex = (uint64_t)commandCount - 1u;
  if (maxCommandIndex >
      (UINT64_MAX - argsOffset - commandSize) / strideBytes) {
    return false;
  }

  lastCommandOffset = argsOffset + maxCommandIndex * strideBytes;
  return gpuBufferRangeValid(argsBuffer, lastCommandOffset, commandSize);
}

static bool
gpu_validIndexRange(GPUBuffer *buffer,
                    uint64_t   baseOffset,
                    GPUIndexType indexType,
                    uint32_t   firstIndex,
                    uint32_t   indexCount) {
  uint64_t indexSize;
  uint64_t firstByte;
  uint64_t byteCount;

  indexSize = indexType == GPU_INDEX_TYPE_UINT32 ? 4u : 2u;
  if (firstIndex > UINT64_MAX / indexSize ||
      indexCount > UINT64_MAX / indexSize) {
    return false;
  }

  firstByte = (uint64_t)firstIndex * indexSize;
  byteCount = (uint64_t)indexCount * indexSize;
  if (baseOffset > UINT64_MAX - firstByte) {
    return false;
  }

  return gpuBufferRangeValid(buffer, baseOffset + firstByte, byteCount);
}

GPU_EXPORT
void
GPUBindRenderPipeline(GPURenderPassEncoder *pass, GPURenderPipeline *pipeline) {
  GPUDevice             *device;
  GPUApi                *api;
  GPURenderPipelineState state;

  if (!pass || pass->_ended || !pipeline || !pipeline->_state)
    return;
  if (!gpu_renderPipelineMatchesPass(pass, pipeline))
    return;
  device = gpu_renderPassDevice(pass);
  if (!(api = gpuDeviceApi(device)))
    return;
  if (pipeline->_api != api || !pipeline->_layout ||
      pipeline->_layout->_device != device) {
    gpu_renderValidationError(pass,
                              "GPUBindRenderPipeline skipped: device mismatch");
    return;
  }
  if (!api->rce.setRenderPipelineState)
    return;

  gpuDeviceRecordBindRequest(gpu_renderPassDevice(pass));
  if (pass->_pipeline == pipeline)
    return;

  if (pass->_pipelineLayout != pipeline->_layout) {
    memset(pass->_boundGroups, 0, sizeof(pass->_boundGroups));
    memset(pass->_boundGroupLayouts, 0, sizeof(pass->_boundGroupLayouts));
    memset(pass->_boundDynamicOffsetCounts,
           0,
           sizeof(pass->_boundDynamicOffsetCounts));
  }

  state._priv = pipeline->_state;
  api->rce.setRenderPipelineState(pass, &state);
  gpuDeviceRecordBindEmission(gpu_renderPassDevice(pass));
  pass->_hasPipeline           = true;
  pass->_pipeline              = pipeline;
  pass->_pipelineLayout        = pipeline->_layout;
  pass->_requiredBindGroupMask = pipeline->_requiredBindGroupMask;
  pass->_primitiveType = gpu_primitiveTypeFromTopology(pipeline->_primitiveTopology);
  pass->_pushConstantSizeBytes = pipeline->_pushConstantSizeBytes;
  pass->_pushConstantStages    = pipeline->_pushConstantStages &
                                 (GPU_SHADER_STAGE_VERTEX_BIT |
                                  GPU_SHADER_STAGE_FRAGMENT_BIT);
  pass->_pushConstantsEmitted  = false;
  if (pass->_pushConstantSizeBytes > 0u) {
    memset(pass->_pushConstants, 0, pass->_pushConstantSizeBytes);
  }
  if (api->rce.cullMode)
    api->rce.cullMode(pass, pipeline->_cullMode);
  if (api->rce.frontFace)
    api->rce.frontFace(pass, pipeline->_frontFace);
}

static void
gpu_bindRenderVertexBuffer(GPURenderPassEncoder *pass,
                           GPUApi               *api,
                           GPUBuffer            *buf,
                           uint64_t              off,
                           uint32_t              index) {
  GPUDevice *device;
  uint32_t slotBit;

  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordBindRequest(device);
  if (index < GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT) {
    slotBit = 1u << index;
    if ((pass->_vertexBufferMask & slotBit) != 0u &&
        pass->_vertexBuffers[index] == buf &&
        pass->_vertexBufferOffsets[index] == off) {
      return;
    }
  } else {
    slotBit = 0u;
  }

  api->rce.vertexInputBuffer(pass, buf, off, index);
  gpuDeviceRecordBindEmission(device);
  if (slotBit != 0u) {
    pass->_vertexBuffers[index]       = buf;
    pass->_vertexBufferOffsets[index] = off;
    pass->_vertexBufferMask          |= slotBit;
  }
}

GPU_HIDE
void
gpuSetRenderVertexBuffer(GPURenderPassEncoder *pass,
                         GPUBuffer            *buf,
                         uint64_t              off,
                         uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.vertexBuffer)
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
  if (!(api = gpu_renderPassApi(pass)))
    return;
  if (!api->rce.vertexInputBuffer)
    return;

  for (i = 0; i < count; i++) {
    if (bindings[i].buffer &&
        gpuBufferHasUsage(bindings[i].buffer, GPU_BUFFER_USAGE_VERTEX) &&
        gpuBufferOffsetValid(bindings[i].buffer, bindings[i].offset)) {
      gpu_bindRenderVertexBuffer(pass,
                                 api,
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
  if (!gpuBufferHasUsage(indexBuffer, GPU_BUFFER_USAGE_INDEX) ||
      !gpuBufferOffsetValid(indexBuffer, offset))
    return;

  pass->_indexBuffer       = indexBuffer;
  pass->_indexBufferOffset = offset;
  pass->_indexType         = indexType;
  pass->_hasIndexBuffer    = true;
}

GPU_EXPORT
void
GPUSetViewport(GPURenderPassEncoder *pass, const GPUViewport *viewport) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended || !viewport)
    return;
#if GPU_BUILD_WITH_VALIDATION
  if (!gpu_validViewport(viewport)) {
    gpu_renderValidationError(pass, "GPUSetViewport ignored invalid viewport");
    return;
  }
#endif
  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if ((pass->_dynamicStateMask & GPU_DYNAMIC_STATE_VIEWPORT_BIT) != 0u &&
      memcmp(&pass->_viewport, viewport, sizeof(*viewport)) == 0)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.viewport)
    return;

  api->rce.viewport(pass, viewport);
  pass->_viewport = *viewport;
  pass->_dynamicStateMask |= GPU_DYNAMIC_STATE_VIEWPORT_BIT;
  gpuDeviceRecordStateEmission(device);
}

GPU_EXPORT
void
GPUSetScissor(GPURenderPassEncoder *pass, const GPUScissorRect *scissor) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended || !scissor)
    return;
  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if ((pass->_dynamicStateMask & GPU_DYNAMIC_STATE_SCISSOR_BIT) != 0u &&
      memcmp(&pass->_scissor, scissor, sizeof(*scissor)) == 0)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.scissor)
    return;

  api->rce.scissor(pass, scissor);
  pass->_scissor = *scissor;
  pass->_dynamicStateMask |= GPU_DYNAMIC_STATE_SCISSOR_BIT;
  gpuDeviceRecordStateEmission(device);
}

GPU_EXPORT
void
GPUSetBlendConstant(GPURenderPassEncoder *pass, const float rgba[4]) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended || !rgba)
    return;
  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if ((pass->_dynamicStateMask & GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT) != 0u &&
      memcmp(pass->_blendConstant, rgba, sizeof(pass->_blendConstant)) == 0)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.blendConstant)
    return;

  api->rce.blendConstant(pass, rgba);
  memcpy(pass->_blendConstant, rgba, sizeof(pass->_blendConstant));
  pass->_dynamicStateMask |= GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT;
  gpuDeviceRecordStateEmission(device);
}

GPU_EXPORT
void
GPUSetStencilReference(GPURenderPassEncoder *pass, uint32_t reference) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended)
    return;
  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if ((pass->_dynamicStateMask & GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT) != 0u &&
      pass->_stencilReference == reference)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.stencilReference)
    return;

  api->rce.stencilReference(pass, reference);
  pass->_stencilReference = reference;
  pass->_dynamicStateMask |= GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  gpuDeviceRecordStateEmission(device);
}

GPU_HIDE
void
gpuSetRenderVertexTexture(GPURenderPassEncoder *pass,
                          GPUTextureView       *view,
                          uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !view)
    return;
  if (!(api = gpu_renderPassApi(pass)))
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
  if (!(api = gpu_renderPassApi(pass)))
    return;

  if (api->rce.setVertexSampler) {
    api->rce.setVertexSampler(pass, sampler, index);
  }
}

GPU_HIDE
void
gpuSetRenderFragmentBuffer(GPURenderPassEncoder *pass,
                           GPUBuffer            *buf,
                           uint64_t              off,
                           uint32_t              index) {
  GPUApi *api;

  if (!pass || pass->_ended || !buf)
    return;
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.fragmentBuffer)
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
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.setFragmentTexture)
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
  if (!(api = gpu_renderPassApi(pass)))
    return;

  if (api->rce.setFragmentSampler) {
    api->rce.setFragmentSampler(pass, sampler, index);
  }
}

GPU_EXPORT
void
GPUSetRenderPushConstants(GPURenderPassEncoder *pass,
                          uint32_t              offset,
                          uint32_t              sizeBytes,
                          const void           *data) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended || !pass->_hasPipeline ||
      pass->_pushConstantSizeBytes == 0u ||
      pass->_pushConstantStages == 0u) {
    return;
  }
  if (!gpu_validPushConstantRange(pass->_pushConstantSizeBytes,
                                  offset,
                                  sizeBytes,
                                  data)) {
    return;
  }
  if (sizeBytes == 0u) {
    return;
  }
  device = gpu_renderPassDevice(pass);
  gpuDeviceRecordStateRequest(device);
  if (pass->_pushConstantsEmitted &&
      memcmp(pass->_pushConstants + offset, data, sizeBytes) == 0) {
    return;
  }
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.pushConstants) {
    return;
  }

  memcpy(pass->_pushConstants + offset, data, sizeBytes);
  api->rce.pushConstants(pass,
                         pass->_pushConstantStages,
                         pass->_pushConstants,
                         pass->_pushConstantSizeBytes);
  pass->_pushConstantsEmitted = true;
  gpuDeviceRecordStateEmission(device);
}

GPU_EXPORT
void
GPUDraw(GPURenderPassEncoder *pass,
        uint32_t              vertexCount,
        uint32_t              instanceCount,
        uint32_t              firstVertex,
        uint32_t              firstInstance) {
  GPUApi *api;

  if (!pass || pass->_ended)
    return;
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(pass, "GPUDraw skipped: no render pipeline bound");
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(pass, "GPUDraw skipped: missing render bind group");
    return;
  }
  if (vertexCount == 0 || instanceCount == 0) {
    gpu_renderValidationError(pass, "GPUDraw skipped: zero draw count");
    return;
  }
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.drawPrimitives)
    return;

  api->rce.drawPrimitives(pass,
                          pass->_primitiveType,
                          firstVertex,
                          vertexCount,
                          instanceCount,
                          firstInstance);
  gpuDeviceRecordDraws(gpu_renderPassDevice(pass), 1u);
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

  if (!pass || pass->_ended)
    return;
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(pass, "GPUDrawIndexed skipped: no render pipeline bound");
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(pass, "GPUDrawIndexed skipped: missing render bind group");
    return;
  }
  if (indexCount == 0 || instanceCount == 0) {
    gpu_renderValidationError(pass, "GPUDrawIndexed skipped: zero draw count");
    return;
  }
  if (!pass->_hasIndexBuffer ||
      !gpu_validIndexRange(pass->_indexBuffer,
                           pass->_indexBufferOffset,
                           pass->_indexType,
                           firstIndex,
                           indexCount)) {
    gpu_renderValidationError(pass, "GPUDrawIndexed skipped: invalid index buffer");
    return;
  }
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.drawIndexedPrims)
    return;
  
  api->rce.drawIndexedPrims(pass,
                            indexCount,
                            instanceCount,
                            firstIndex,
                            vertexOffset,
                            firstInstance);
  gpuDeviceRecordDraws(gpu_renderPassDevice(pass), 1u);
}

GPU_EXPORT
void
GPUDrawIndirect(GPURenderPassEncoder *pass,
                GPUBuffer            *argsBuffer,
                uint64_t              argsOffset) {
  GPUApi *api;

  if (!pass || pass->_ended)
    return;
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(pass, "GPUDrawIndirect skipped: no render pipeline bound");
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(pass, "GPUDrawIndirect skipped: missing render bind group");
    return;
  }
  if (!gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      (argsOffset & 3u) != 0u ||
      !gpuBufferRangeValid(argsBuffer, argsOffset, 16u)) {
    gpu_renderValidationError(pass, "GPUDrawIndirect skipped: invalid indirect buffer");
    return;
  }
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.drawPrimitivesIndirect)
    return;

  api->rce.drawPrimitivesIndirect(pass,
                                  pass->_primitiveType,
                                  argsBuffer,
                                  argsOffset);
  gpuDeviceRecordDraws(gpu_renderPassDevice(pass), 1u);
}

GPU_EXPORT
void
GPUDrawIndexedIndirect(GPURenderPassEncoder *pass,
                       GPUBuffer            *argsBuffer,
                       uint64_t              argsOffset) {
  GPUApi *api;

  if (!pass || pass->_ended)
    return;
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(pass, "GPUDrawIndexedIndirect skipped: no render pipeline bound");
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(pass, "GPUDrawIndexedIndirect skipped: missing render bind group");
    return;
  }
  if (!pass->_hasIndexBuffer ||
      !gpuBufferHasUsage(argsBuffer, GPU_BUFFER_USAGE_INDIRECT) ||
      (argsOffset & 3u) != 0u ||
      !gpuBufferRangeValid(argsBuffer, argsOffset, 20u)) {
    gpu_renderValidationError(pass, "GPUDrawIndexedIndirect skipped: invalid indirect/index buffer");
    return;
  }
  if (!(api = gpu_renderPassApi(pass)) || !api->rce.drawIndexedPrimsIndirect)
    return;

  api->rce.drawIndexedPrimsIndirect(pass, argsBuffer, argsOffset);
  gpuDeviceRecordDraws(gpu_renderPassDevice(pass), 1u);
}

GPU_EXPORT
void
GPUMultiDrawIndirect(GPURenderPassEncoder *pass,
                     GPUBuffer            *argsBuffer,
                     uint64_t              argsOffset,
                     uint32_t              drawCount,
                     uint32_t              strideBytes) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(pass,
                              "GPUMultiDrawIndirect skipped: no render pipeline bound");
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(pass,
                              "GPUMultiDrawIndirect skipped: missing render bind group");
    return;
  }
  if (!gpu_validIndirectBatch(argsBuffer,
                              argsOffset,
                              drawCount,
                              strideBytes,
                              16u)) {
    gpu_renderValidationError(pass,
                              "GPUMultiDrawIndirect skipped: invalid indirect batch");
    return;
  }
  if (!(api = gpu_renderPassApi(pass))) {
    return;
  }
  device = gpu_renderPassDevice(pass);
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW) &&
      api->rce.multiDrawPrimitivesIndirect &&
      api->rce.multiDrawPrimitivesIndirect(pass,
                                           pass->_primitiveType,
                                           argsBuffer,
                                           argsOffset,
                                           drawCount,
                                           strideBytes)) {
    gpuDeviceRecordDraws(device, drawCount);
    return;
  }
  if (!api->rce.drawPrimitivesIndirect) {
    return;
  }

  for (uint32_t i = 0; i < drawCount; i++) {
    api->rce.drawPrimitivesIndirect(pass,
                                    pass->_primitiveType,
                                    argsBuffer,
                                    argsOffset + (uint64_t)i * strideBytes);
  }
  gpuDeviceRecordDraws(device, drawCount);
}

GPU_EXPORT
void
GPUMultiDrawIndexedIndirect(GPURenderPassEncoder *pass,
                            GPUBuffer            *argsBuffer,
                            uint64_t              argsOffset,
                            uint32_t              drawCount,
                            uint32_t              strideBytes) {
  GPUDevice *device;
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  if (!pass->_hasPipeline) {
    gpu_renderValidationError(
      pass,
      "GPUMultiDrawIndexedIndirect skipped: no render pipeline bound"
    );
    return;
  }
  if (!gpu_renderBindingsComplete(pass)) {
    gpu_renderValidationError(
      pass,
      "GPUMultiDrawIndexedIndirect skipped: missing render bind group"
    );
    return;
  }
  if (!pass->_hasIndexBuffer ||
      !gpu_validIndirectBatch(argsBuffer,
                              argsOffset,
                              drawCount,
                              strideBytes,
                              20u)) {
    gpu_renderValidationError(
      pass,
      "GPUMultiDrawIndexedIndirect skipped: invalid indirect/index batch"
    );
    return;
  }
  if (!(api = gpu_renderPassApi(pass))) {
    return;
  }
  device = gpu_renderPassDevice(pass);
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW) &&
      api->rce.multiDrawIndexedPrimsIndirect &&
      api->rce.multiDrawIndexedPrimsIndirect(pass,
                                             argsBuffer,
                                             argsOffset,
                                             drawCount,
                                             strideBytes)) {
    gpuDeviceRecordDraws(device, drawCount);
    return;
  }
  if (!api->rce.drawIndexedPrimsIndirect) {
    return;
  }

  for (uint32_t i = 0; i < drawCount; i++) {
    api->rce.drawIndexedPrimsIndirect(
      pass,
      argsBuffer,
      argsOffset + (uint64_t)i * strideBytes
    );
  }
  gpuDeviceRecordDraws(device, drawCount);
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
