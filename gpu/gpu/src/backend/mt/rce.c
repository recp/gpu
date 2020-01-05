/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/cmdqueue.h"
#include "../../../include/gpu/cmd-enc.h"
#include "../../../include/gpu/buffer.h"
#include "../../../include/gpu/stage-io.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPURenderCommandEncoder*
gpuRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  return mtRenderCommandEncoder(cmdb, pass);
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
gpuSetRenderPipeline(GPURenderCommandEncoder *rce, GPUPipeline *pipline) {
  mtSetRenderState(rce, pipline);
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

GPU_EXPORT
void
gpuDraw(GPURenderCommandEncoder *rce, GPUDrawArgs *args) {
 
}

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
