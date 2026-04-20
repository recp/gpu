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
  return [(id<MTLCommandBuffer>)cmdb->_priv renderCommandEncoderWithDescriptor: pass->_priv];
}

GPU_HIDE
void
mt_frontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  [(id<MTLRenderCommandEncoder>)rce setFrontFacingWinding:(MTLWinding)winding];
}

GPU_HIDE
void
mt_cullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  [(id<MTLRenderCommandEncoder>)rce setCullMode:(MTLCullMode)mode];
}

GPU_HIDE
void
mt_setRenderPipelineState(GPURenderCommandEncoder *rce, GPURenderPipelineState *piplineState) {
  [(id<MTLRenderCommandEncoder>)rce setRenderPipelineState:(id<MTLRenderPipelineState>)piplineState->_priv];
}

GPU_HIDE
void
mt_setDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds) {
  [(id<MTLRenderCommandEncoder>)rce setDepthStencilState:(id<MTLDepthStencilState>)ds->_priv];
}

GPU_HIDE
void
mt_viewport(GPURenderCommandEncoder *enc, GPUViewport *viewport) {
  MTLViewport vp;
  
  vp.originX = viewport->originX;
  vp.originY = viewport->originY;
  vp.width   = viewport->width;
  vp.height  = viewport->height;
  vp.znear   = viewport->znear;
  vp.zfar    = viewport->zfar;

  [(id<MTLRenderCommandEncoder>)enc setViewport:vp];
}

GPU_HIDE
void
mt_vertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex) {
  [(id<MTLRenderCommandEncoder>)enc setVertexBytes:bytes length:legth atIndex:atIndex];
}

GPU_HIDE
void
mt_vertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  [(id<MTLRenderCommandEncoder>)rce setVertexBuffer:(id<MTLBuffer>)buf offset:off atIndex:index];
}

GPU_HIDE
void
mt_fragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  [(id<MTLRenderCommandEncoder>)rce setFragmentBuffer:(id<MTLBuffer>)buf offset:off atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentTexture(GPURenderCommandEncoder *rce,
                         GPUTexture               *tex,
                         uint32_t                 index) {
  [(id<MTLRenderCommandEncoder>)rce setFragmentTexture:(id<MTLTexture>)tex atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentSampler(GPURenderCommandEncoder *rce,
                         GPUSampler              *sampler,
                         uint32_t                 index) {
  [(id<MTLRenderCommandEncoder>)rce setFragmentSamplerState:(id<MTLSamplerState>)sampler
                                                    atIndex:index];
}

GPU_HIDE
void
mt_drawPrimitives(GPURenderCommandEncoder *rce,
                  GPUPrimitiveType         type,
                  size_t                   start,
                  size_t                   count) {
  [(id<MTLRenderCommandEncoder>)rce drawPrimitives:(MTLPrimitiveType)type
                                       vertexStart:start
                                       vertexCount:count];
}

GPU_HIDE
void
mt_drawIndexedPrims(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType         type,
                    uint32_t                 indexCount,
                    GPUIndexType             indexType,
                    GPUBuffer               *indexBuffer,
                    uint32_t                 indexBufferOffset) {
  [(id<MTLRenderCommandEncoder>)rce drawIndexedPrimitives:(MTLPrimitiveType)type
                                               indexCount:indexCount
                                                indexType:(MTLIndexType)indexType
                                              indexBuffer:(id<MTLBuffer>)indexBuffer
                                        indexBufferOffset:indexBufferOffset];
}

GPU_HIDE
void
mt_endEncoding(GPURenderCommandEncoder *rce) {
  [(id<MTLRenderCommandEncoder>)rce endEncoding];
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
  api->setFragmentSampler     = mt_rceSetFragmentSampler;
  api->drawPrimitives         = mt_drawPrimitives;
  api->drawIndexedPrims       = mt_drawIndexedPrims;
  api->endEncoding            = mt_endEncoding;
}
