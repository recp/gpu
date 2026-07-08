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

#ifndef gpu_cmd_enc_h
#define gpu_cmd_enc_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "pass.h"
#include "pipeline.h"
#include "depthstencil.h"
#include "buffer.h"
#include "stage-io.h"
#include "texture.h"
#include "sampler.h"

#ifndef GPU_RENDER_ENCODER_TYPES_DEFINED
#define GPU_RENDER_ENCODER_TYPES_DEFINED
typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;
typedef GPURenderCommandEncoder GPURenderPassEncoder;
#endif

typedef enum GPUPrimitiveType {
  GPUPrimitiveTypePoint         = 0,
  GPUPrimitiveTypeLine          = 1,
  GPUPrimitiveTypeLineStrip     = 2,
  GPUPrimitiveTypeTriangle      = 3,
  GPUPrimitiveTypeTriangleStrip = 4
} GPUPrimitiveType;

typedef enum GPUVisibilityResultMode {
  GPUVisibilityResultModeDisabled = 0,
  GPUVisibilityResultModeBoolean  = 1,
  GPUVisibilityResultModeCounting = 2
} GPUVisibilityResultMode;

typedef struct GPUScissorRect {
  uint32_t x, y, width, height;
} GPUScissorRect;

typedef struct GPUViewport {
  double originX, originY, width, height, znear, zfar;
} GPUViewport;

typedef enum GPUDepthClipMode {
  GPUDepthClipModeClip  = 0,
  GPUDepthClipModeClamp = 1
} GPUDepthClipMode;

typedef enum GPUTriangleFillMode {
  GPUTriangleFillModeFill  = 0,
  GPUTriangleFillModeLines = 1
} GPUTriangleFillMode;

typedef enum GPURenderStages {
  GPURenderStageVertex   = 0,
  GPURenderStageFragment = 1
} GPURenderStages;

typedef struct GPUBufferBinding {
  GPUBuffer *buffer;
  size_t     offset;
} GPUBufferBinding;

GPU_EXPORT
void
GPUBindRenderPipeline(GPURenderPassEncoder *pass, GPURenderPipeline *pipeline);

GPU_EXPORT
void
GPUSetVertexBuffer(GPURenderCommandEncoder *rce,
                   GPUBuffer               *buf,
                   size_t                   off,
                   uint32_t                 index);

GPU_EXPORT
void
GPUBindVertexBuffers(GPURenderPassEncoder     *pass,
                     uint32_t                  firstSlot,
                     uint32_t                  count,
                     const GPUBufferBinding   *bindings);

GPU_EXPORT
void
GPUBindIndexBuffer(GPURenderPassEncoder *pass,
                   GPUBuffer            *indexBuffer,
                   uint64_t              offset,
                   GPUIndexType          indexType);

GPU_EXPORT
void
GPUSetViewport(GPURenderPassEncoder *pass, const GPUViewport *viewport);

GPU_EXPORT
void
GPUSetScissor(GPURenderPassEncoder *pass, const GPUScissorRect *scissor);

GPU_EXPORT
void
GPUSetBlendConstant(GPURenderPassEncoder *pass, const float rgba[4]);

GPU_EXPORT
void
GPUSetStencilReference(GPURenderPassEncoder *pass, uint32_t reference);

GPU_EXPORT
void
GPUSetVertexTexture(GPURenderCommandEncoder *rce,
                    GPUTextureView          *view,
                    uint32_t                 index);

GPU_EXPORT
void
GPUSetVertexSampler(GPURenderCommandEncoder *rce,
                    GPUSampler              *sampler,
                    uint32_t                 index);

GPU_EXPORT
void
GPUSetFragmentBuffer(GPURenderCommandEncoder *rce,
                     GPUBuffer               *buf,
                     size_t                   off,
                     uint32_t                 index);

GPU_EXPORT
void
GPUSetFragmentTexture(GPURenderCommandEncoder *rce,
                      GPUTextureView           *view,
                      uint32_t                 index);

GPU_EXPORT
void
GPUSetFragmentSampler(GPURenderCommandEncoder *rce,
                      GPUSampler              *sampler,
                      uint32_t                 index);

GPU_EXPORT
void
GPUDraw(GPURenderPassEncoder *pass,
        uint32_t              vertexCount,
        uint32_t              instanceCount,
        uint32_t              firstVertex,
        uint32_t              firstInstance);

GPU_EXPORT
void
GPUDrawIndexed(GPURenderPassEncoder *pass,
               uint32_t              indexCount,
               uint32_t              instanceCount,
               uint32_t              firstIndex,
               int32_t               vertexOffset,
               uint32_t              firstInstance);

typedef uint64_t GPUDynamicStateMask;
enum {
  GPU_DYNAMIC_STATE_VIEWPORT_BIT          = 1ull << 0,
  GPU_DYNAMIC_STATE_SCISSOR_BIT           = 1ull << 1,
  GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT    = 1ull << 2,
  GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT = 1ull << 3
};

typedef struct GPUDynamicStateApplyInfo {
  GPUChainedStruct    chain;
  GPUDynamicStateMask mask;
  GPUViewport         viewport;
  GPUScissorRect      scissor;
  float               blendConstant[4];
  uint32_t            stencilReference;
} GPUDynamicStateApplyInfo;

GPU_EXPORT
void
GPUApplyDynamicState(GPURenderPassEncoder *pass,
                     const GPUDynamicStateApplyInfo *info);

#ifdef __cplusplus
}
#endif
#endif /* gpu_cmd_enc_h */
