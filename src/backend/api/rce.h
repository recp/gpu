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

#ifndef gpu_api_rce_h
#define gpu_api_rce_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>
#include "descriptor.h"

typedef struct GPURenderPipelineState GPURenderPipelineState;
typedef struct GPURenderPassDesc      GPURenderPassDesc;
typedef struct GPUPipelineLayout      GPUPipelineLayout;
typedef struct GPUBindGroupLayout     GPUBindGroupLayout;
typedef struct GPUBindGroup           GPUBindGroup;

typedef enum GPUPrimitiveType {
  GPUPrimitiveTypePoint         = 0,
  GPUPrimitiveTypeLine          = 1,
  GPUPrimitiveTypeLineStrip     = 2,
  GPUPrimitiveTypeTriangle      = 3,
  GPUPrimitiveTypeTriangleStrip = 4
} GPUPrimitiveType;

#define GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT 32u

enum {
  GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS = 8u
};

struct GPURenderCommandEncoder {
  void                   *_priv;
  void                   *_pipeline;
  struct GPUApi          *_api;
  GPUDevice              *_device;
  GPUCommandBuffer       *_cmdb;
  GPUFrameStats          *_stats;
  GPUQuerySet            *_occlusionQuerySet;
  GPUBuffer              *_indexBuffer;
  GPUPipelineLayout      *_pipelineLayout;
  GPUBindGroup           *_boundGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBindGroupLayout     *_boundGroupLayouts[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBuffer              *_vertexBuffers[GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT];
  uint64_t                _indexBufferOffset;
  uint64_t                _vertexBufferOffsets[GPU__RENDER_VERTEX_SHADOW_SLOT_COUNT];
  GPUDynamicOffsetShadow  _boundDynamicOffsets[GPU_ENCODER_MAX_BIND_GROUPS];
  uint32_t                _boundDynamicOffsetCounts[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUDynamicStateMask     _dynamicStateMask;
  GPUViewport             _viewport;
  GPUPrimitiveType        _primitiveType;
  GPUIndexType            _indexType;
  GPUScissorRect          _scissor;
  uint32_t                _occlusionQueryIndex;
  uint32_t                _requiredBindGroupMask;
  uint32_t                _colorAttachmentCount;
  GPUFormat               _colorAttachmentFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  uint32_t                _colorAttachmentSampleCounts[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUFormat               _depthStencilFormat;
  uint32_t                _depthStencilSampleCount;
  uint32_t                _pushConstantSizeBytes;
  GPUShaderStageFlags     _pushConstantStages;
  uint32_t                _vertexBufferMask;
  uint32_t                _stencilReference;
  float                   _blendConstant[4];
  bool                    _colorAttachmentHasResolve[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  bool                    _hasIndexBuffer;
  bool                    _hasPipeline;
  bool                    _pushConstantsEmitted;
  bool                    _occlusionQueryActive;
  bool                    _meshPipeline;
  bool                    _ended;
  uint8_t                 _pushConstants[4096];
};

typedef struct GPUApiRCE {
  GPURenderCommandEncoder*
  (*renderCommandEncoder)(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass);
  
  void
  (*frontFace)(GPURenderCommandEncoder *rce, GPUFrontFace frontFace);
  
  void
  (*cullMode)(GPURenderCommandEncoder *rce, GPUCullMode mode);
  
  void
  (*setRenderPipelineState)(GPURenderCommandEncoder *rce,
                            GPURenderPipelineState  *piplineState);
  
  void
  (*viewport)(GPURenderCommandEncoder *enc, const GPUViewport *viewport);

  void
  (*scissor)(GPURenderCommandEncoder *enc, const GPUScissorRect *scissor);

  void
  (*blendConstant)(GPURenderCommandEncoder *enc, const float rgba[4]);

  void
  (*stencilReference)(GPURenderCommandEncoder *enc, uint32_t reference);

  void
  (*pushConstants)(GPURenderCommandEncoder *enc,
                   GPUShaderStageFlags     stages,
                   const void             *data,
                   uint32_t                sizeBytes);
  
  void
  (*vertexBytes)(GPURenderCommandEncoder *enc,
                 void                    *bytes,
                 size_t                   legth,
                 uint32_t                 atIndex);
  
  void
  (*vertexBuffer)(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  uint64_t                 off,
                  uint32_t                 index);

  void
  (*vertexInputBuffer)(GPURenderCommandEncoder *rce,
                       GPUBuffer               *buf,
                       uint64_t                 off,
                       uint32_t                 index);

  void
  (*setVertexTexture)(GPURenderCommandEncoder *rce,
                      GPUTextureView          *view,
                      uint32_t                 index);

  void
  (*setVertexSampler)(GPURenderCommandEncoder *rce,
                      GPUSampler              *sampler,
                      uint32_t                 index);

  void
  (*setVertexAccelerationStructure)(
    GPURenderCommandEncoder      *rce,
    GPUAccelerationStructureEXT  *structure,
    uint32_t                      index);

  void
  (*taskBuffer)(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                uint64_t                 off,
                uint32_t                 index);

  void
  (*setTaskTexture)(GPURenderCommandEncoder *rce,
                    GPUTextureView          *view,
                    uint32_t                 index);

  void
  (*setTaskSampler)(GPURenderCommandEncoder *rce,
                    GPUSampler              *sampler,
                    uint32_t                 index);

  void
  (*meshBuffer)(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                uint64_t                 off,
                uint32_t                 index);

  void
  (*setMeshTexture)(GPURenderCommandEncoder *rce,
                    GPUTextureView          *view,
                    uint32_t                 index);

  void
  (*setMeshSampler)(GPURenderCommandEncoder *rce,
                    GPUSampler              *sampler,
                    uint32_t                 index);
  
  void
  (*fragmentBuffer)(GPURenderCommandEncoder *rce,
                    GPUBuffer               *buf,
                    uint64_t                 off,
                    uint32_t                 index);
  
  void
  (*setFragmentTexture)(GPURenderCommandEncoder *rce,
                        GPUTextureView           *view,
                        uint32_t                 index);

  void
  (*setFragmentSampler)(GPURenderCommandEncoder *rce,
                        GPUSampler              *sampler,
                        uint32_t                 index);

  void
  (*setFragmentAccelerationStructure)(
    GPURenderCommandEncoder      *rce,
    GPUAccelerationStructureEXT  *structure,
    uint32_t                      index);

  void
  (*drawPrimitives)(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType         type,
                    size_t                   start,
                    size_t                   count,
                    uint32_t                 instanceCount,
                    uint32_t                 firstInstance);

  void
  (*drawIndexedPrims)(GPURenderCommandEncoder *rce,
                      uint32_t                 indexCount,
                      uint32_t                 instanceCount,
                      uint32_t                 firstIndex,
                      int32_t                  vertexOffset,
                      uint32_t                 firstInstance);

  void
  (*drawMesh)(GPURenderCommandEncoder *rce,
              uint32_t                 groupCountX,
              uint32_t                 groupCountY,
              uint32_t                 groupCountZ,
              const uint32_t           taskWorkgroupSize[3],
              const uint32_t           meshWorkgroupSize[3]);

  void
  (*setFragmentShadingRate)(
    GPURenderCommandEncoder      *rce,
    GPUShadingRateEXT             rate,
    GPUShadingRateCombinerEXT     primitiveCombiner,
    GPUShadingRateCombinerEXT     attachmentCombiner
  );

  void
  (*drawPrimitivesIndirect)(GPURenderCommandEncoder *rce,
                            GPUPrimitiveType         type,
                            GPUBuffer               *argsBuffer,
                            uint64_t                 argsOffset);

  void
  (*drawIndexedPrimsIndirect)(GPURenderCommandEncoder *rce,
                              GPUBuffer               *argsBuffer,
                              uint64_t                 argsOffset);

  bool
  (*multiDrawPrimitivesIndirect)(GPURenderCommandEncoder *rce,
                                 GPUPrimitiveType         type,
                                 GPUBuffer               *argsBuffer,
                                 uint64_t                 argsOffset,
                                 uint32_t                 drawCount,
                                 uint32_t                 strideBytes);

  bool
  (*multiDrawIndexedPrimsIndirect)(GPURenderCommandEncoder *rce,
                                   GPUBuffer               *argsBuffer,
                                   uint64_t                 argsOffset,
                                   uint32_t                 drawCount,
                                   uint32_t                 strideBytes);

  void
  (*endEncoding)(GPURenderCommandEncoder *rce);
} GPUApiRCE;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_rce_h */
