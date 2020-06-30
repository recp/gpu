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

#include "../../../include/gpu/cmdqueue.h"
#include "../../../include/gpu/cmd-enc.h"
#include "../../../include/gpu/buffer.h"
#include "../../../include/gpu/stage-io.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPURenderCommandEncoder*
gpuRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  return mtRenderCommandEncoder(cmdb->priv, pass);
}

GPU_EXPORT
void
gpuFrontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  mtFrontFace(rce, (MtWinding)winding);
}

GPU_EXPORT
void
gpuCullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  mtCullMode(rce, (MtCullMode)mode);
}

GPU_EXPORT
void
gpuSetRenderPipelineState(GPURenderCommandEncoder *rce, GPURenderPipelineState *piplineState) {
  mtSetRenderState(rce, piplineState->priv);
}

GPU_EXPORT
void
gpuSetDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencil *ds) {
  mtSetDepthStencil(rce, ds->priv);
}

GPU_EXPORT
void
gpuViewport(GPURenderCommandEncoder *enc, GPUViewport *viewport) {
  MtViewport vp;
  
  vp.originX = viewport->originX;
  vp.originY = viewport->originY;
  vp.width   = viewport->width;
  vp.height  = viewport->height;
  vp.znear   = viewport->znear;
  vp.zfar    = viewport->zfar;

  mtViewport(enc, &vp);
}

GPU_EXPORT
void
gpuVertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex) {
  mtVertexBytes(enc, bytes, legth, atIndex);
}

GPU_EXPORT
void
gpuVertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  mtVertexBuffer(rce, buf, off, index);
}

GPU_EXPORT
void
gpuFragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  mtFragmentBuffer(rce, buf, off, index);
}

//GPU_EXPORT
//void
//gpuDraw(GPURenderCommandEncoder *rce, GPUDrawArgs *args) {
// 
//}

GPU_EXPORT
void
gpuDrawIndexedPrims(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType         type,
                    uint32_t                 indexCount,
                    GPUIndexType             indexType,
                    GPUBuffer               *indexBuffer,
                    uint32_t                 indexBufferOffset) {
  mtDrawIndexedPrims(rce,
                     (MtPrimitiveType)type,
                     indexCount,
                     (MtIndexType)indexType,
                     indexBuffer,
                     indexBufferOffset);
}

GPU_EXPORT
void
gpuEndEncoding(GPURenderCommandEncoder *rce) {
  mtEndEncoding(rce);
}
