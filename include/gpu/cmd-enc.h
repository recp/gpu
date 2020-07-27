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
gpuRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass);

GPU_EXPORT
void
gpuFrontFace(GPURenderCommandEncoder *rce, GPUWinding winding);

GPU_EXPORT
void
gpuCullMode(GPURenderCommandEncoder *rce, GPUCullMode mode);

GPU_EXPORT
void
gpuSetRenderPipelineState(GPURenderCommandEncoder *rce,
                          GPURenderPipelineState  *piplineState);

GPU_EXPORT
void
gpuSetDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds);

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
gpuVertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index);

GPU_EXPORT
void
gpuFragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index);

GPU_EXPORT
void
gpuRCESetTexture(GPURenderCommandEncoder *rce,
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
gpuDrawIndexedPrims(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType         type,
                    uint32_t                 indexCount,
                    GPUIndexType             indexType,
                    GPUBuffer               *indexBuffer,
                    uint32_t                 indexBufferOffset);

GPU_EXPORT
void
gpuEndEncoding(GPURenderCommandEncoder *rce);

#endif /* gpu_cmd_enc_h */
