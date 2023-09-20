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

GPU_HIDE
GPURenderCommandEncoder*
mt_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  return [(id<MTLCommandBuffer>)cmdb->priv renderCommandEncoderWithDescriptor: pass->_priv];
}

GPU_HIDE
void
mt_frontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  mtFrontFace(rce, (MtWinding)winding);
}

GPU_HIDE
void
mt_cullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  mtCullMode(rce, (MtCullMode)mode);
}

GPU_HIDE
void
mt_setRenderPipelineState(GPURenderCommandEncoder *rce, GPURenderPipelineState *piplineState) {
  mtSetRenderState(rce, piplineState->priv);
}

GPU_HIDE
void
mt_setDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds) {
  mtSetDepthStencil(rce, ds->priv);
}

GPU_HIDE
void
mt_viewport(GPURenderCommandEncoder *enc, GPUViewport *viewport) {
  MtViewport vp;
  
  vp.originX = viewport->originX;
  vp.originY = viewport->originY;
  vp.width   = viewport->width;
  vp.height  = viewport->height;
  vp.znear   = viewport->znear;
  vp.zfar    = viewport->zfar;

  mtViewport(enc, &vp);
}

GPU_HIDE
void
mt_vertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex) {
  mtVertexBytes(enc, bytes, legth, atIndex);
}

GPU_HIDE
void
mt_vertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  mtVertexBuffer(rce, buf, off, index);
}

GPU_HIDE
void
mt_fragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  mtFragmentBuffer(rce, buf, off, index);
}

GPU_HIDE
void
mt_rceSetFragmentTexture(GPURenderCommandEncoder *rce,
                         GPUTexture               *tex,
                         uint32_t                 index) {
  mtRenderCommandEncoderSetFragmentTextureAtIndex(rce, tex, index);
}

GPU_HIDE
void
mt_drawIndexedPrims(GPURenderCommandEncoder *rce,
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

GPU_HIDE
void
mt_endEncoding(GPURenderCommandEncoder *rce) {
  mtCommandEncoderEndEncoding(rce);
}

GPU_HIDE
void
mt_initRCE(GPUApiRCE *api) {
  api->renderCommandEncoder   = mt_renderCommandEncoder;
  api->frontFace              = mt_frontFace;
  api->cullMode               = mt_cullMode;
  api->setRenderPipelineState = mt_setRenderPipelineState;
  api->setDepthStencil        = mt_setDepthStencil;
  api->viewport               = mt_viewport;
  api->vertexBytes            = mt_vertexBytes;
  api->vertexBuffer           = mt_vertexBuffer;
  api->fragmentBuffer         = mt_fragmentBuffer;
  api->setFragmentTexture     = mt_rceSetFragmentTexture;
  api->drawIndexedPrims       = mt_drawIndexedPrims;
  api->endEncoding            = mt_endEncoding;
}
