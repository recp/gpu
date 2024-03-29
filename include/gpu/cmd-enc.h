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

typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;

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

typedef enum GPUCullMode {
  GPUCullModeNone  = 0,
  GPUCullModeFront = 1,
  GPUCullModeBack  = 2
} GPUCullMode;

typedef enum GPUWinding {
  GPUWindingClockwise        = 0,
  GPUWindingCounterClockwise = 1
} GPUWinding;

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

GPU_EXPORT
GPURenderCommandEncoder*
GPUNewRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass);

GPU_EXPORT
void
GPUSetFrontFace(GPURenderCommandEncoder *rce, GPUWinding winding);

GPU_EXPORT
void
GPUSetCullMode(GPURenderCommandEncoder *rce, GPUCullMode mode);

GPU_EXPORT
void
GPUSetRenderState(GPURenderCommandEncoder *rce,
                  GPURenderPipelineState  *piplineState);

GPU_EXPORT
void
GPUSetDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds);

GPU_EXPORT
void
gpuViewport(GPURenderCommandEncoder *enc, GPUViewport *viewport);

GPU_EXPORT
void
gpuVertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex);

GPU_EXPORT
void
GPUSetVertexBuffer(GPURenderCommandEncoder *rce,
                   GPUBuffer               *buf,
                   size_t                   off,
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
                      GPUTexture               *tex,
                      uint32_t                 index);

GPU_EXPORT
void
gpuDrawPrimitives(GPURenderCommandEncoder *rce,
                  GPUPrimitiveType         type,
                  size_t                   start,
                  size_t                   count);

GPU_EXPORT
void
GPUDrawIndexed(GPURenderCommandEncoder *rce,
               GPUPrimitiveType         type,
               uint32_t                 indexCount,
               GPUIndexType             indexType,
               GPUBuffer               *indexBuffer,
               uint32_t                 indexBufferOffset);

GPU_EXPORT
void
GPUEndEncoding(GPURenderCommandEncoder *rce);

#ifdef __cplusplus
}
#endif
#endif /* gpu_cmd_enc_h */
