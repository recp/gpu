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

static id<MTLRenderCommandEncoder>
mt_nativeRCE(GPURenderCommandEncoder *rce) {
  return rce ? (__bridge id<MTLRenderCommandEncoder>)rce->_priv : nil;
}

GPU_HIDE
GPURenderCommandEncoder*
mt_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  id<MTLRenderCommandEncoder> native;
  GPURenderCommandEncoder *enc;

  native = [(id<MTLCommandBuffer>)cmdb->_priv renderCommandEncoderWithDescriptor: pass->_priv];
  if (!native)
    return NULL;

  enc = calloc(1, sizeof(*enc));
  if (!enc) {
    [native endEncoding];
    return NULL;
  }

  enc->_priv = (__bridge void *)native;
  enc->_primitiveType = GPUPrimitiveTypeTriangle;
  return enc;
}

GPU_HIDE
void
mt_frontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  MTLWinding mtWinding;

  mtWinding = winding == GPU_FRONT_FACE_CW ?
    MTLWindingClockwise : MTLWindingCounterClockwise;
  [mt_nativeRCE(rce) setFrontFacingWinding:mtWinding];
}

GPU_HIDE
void
mt_cullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  [mt_nativeRCE(rce) setCullMode:(MTLCullMode)mode];
}

GPU_HIDE
void
mt_setRenderPipelineState(GPURenderCommandEncoder *rce, GPURenderPipelineState *piplineState) {
  [mt_nativeRCE(rce) setRenderPipelineState:(id<MTLRenderPipelineState>)piplineState->_priv];
}

GPU_HIDE
void
mt_setDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilPipelineState *ds) {
  [mt_nativeRCE(rce) setDepthStencilState:(id<MTLDepthStencilState>)ds->_priv];
}

GPU_HIDE
void
mt_viewport(GPURenderCommandEncoder *enc, const GPUViewport *viewport) {
  MTLViewport vp;
  
  vp.originX = viewport->originX;
  vp.originY = viewport->originY;
  vp.width   = viewport->width;
  vp.height  = viewport->height;
  vp.znear   = viewport->znear;
  vp.zfar    = viewport->zfar;

  [mt_nativeRCE(enc) setViewport:vp];
}

GPU_HIDE
void
mt_scissor(GPURenderCommandEncoder *enc, const GPUScissorRect *scissor) {
  MTLScissorRect rect;

  rect.x      = scissor->x;
  rect.y      = scissor->y;
  rect.width  = scissor->width;
  rect.height = scissor->height;

  [mt_nativeRCE(enc) setScissorRect:rect];
}

GPU_HIDE
void
mt_blendConstant(GPURenderCommandEncoder *enc, const float rgba[4]) {
  [mt_nativeRCE(enc) setBlendColorRed:rgba[0]
                                green:rgba[1]
                                 blue:rgba[2]
                                alpha:rgba[3]];
}

GPU_HIDE
void
mt_stencilReference(GPURenderCommandEncoder *enc, uint32_t reference) {
  [mt_nativeRCE(enc) setStencilReferenceValue:reference];
}

GPU_HIDE
void
mt_vertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex) {
  [mt_nativeRCE(enc) setVertexBytes:bytes length:legth atIndex:atIndex];
}

GPU_HIDE
void
mt_vertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  [mt_nativeRCE(rce) setVertexBuffer:(id<MTLBuffer>)buf offset:off atIndex:index];
}

GPU_HIDE
void
mt_rceSetVertexTexture(GPURenderCommandEncoder *rce,
                       GPUTextureView          *view,
                       uint32_t                 index) {
  [mt_nativeRCE(rce) setVertexTexture:view ? (id<MTLTexture>)view->_priv : nil
                              atIndex:index];
}

GPU_HIDE
void
mt_rceSetVertexSampler(GPURenderCommandEncoder *rce,
                       GPUSampler              *sampler,
                       uint32_t                 index) {
  [mt_nativeRCE(rce) setVertexSamplerState:sampler ? (id<MTLSamplerState>)sampler->_priv : nil
                                   atIndex:index];
}

GPU_HIDE
void
mt_fragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  [mt_nativeRCE(rce) setFragmentBuffer:(id<MTLBuffer>)buf offset:off atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentTexture(GPURenderCommandEncoder *rce,
                         GPUTextureView           *view,
                         uint32_t                 index) {
  [mt_nativeRCE(rce) setFragmentTexture:view ? (id<MTLTexture>)view->_priv : nil
                                atIndex:index];
}

GPU_HIDE
void
mt_rceSetFragmentSampler(GPURenderCommandEncoder *rce,
                         GPUSampler              *sampler,
                         uint32_t                 index) {
  [mt_nativeRCE(rce) setFragmentSamplerState:sampler ? (id<MTLSamplerState>)sampler->_priv : nil
                                     atIndex:index];
}

GPU_HIDE
void
mt_drawPrimitives(GPURenderCommandEncoder *rce,
                  GPUPrimitiveType         type,
                  size_t                   start,
                  size_t                   count,
                  uint32_t                 instanceCount,
                  uint32_t                 firstInstance) {
  [mt_nativeRCE(rce) drawPrimitives:(MTLPrimitiveType)type
                        vertexStart:start
                        vertexCount:count
                      instanceCount:instanceCount
                       baseInstance:firstInstance];
}

GPU_HIDE
void
mt_drawIndexedPrims(GPURenderCommandEncoder *rce,
                    uint32_t                 indexCount,
                    uint32_t                 instanceCount,
                    uint32_t                 firstIndex,
                    int32_t                  vertexOffset,
                    uint32_t                 firstInstance) {
  uint64_t indexSize;
  uint64_t indexBufferOffset;

  indexSize = rce->_indexType == GPUIndexTypeUInt32 ? 4 : 2;
  indexBufferOffset = rce->_indexBufferOffset + (uint64_t)firstIndex * indexSize;

  [mt_nativeRCE(rce) drawIndexedPrimitives:(MTLPrimitiveType)rce->_primitiveType
                                indexCount:indexCount
                                 indexType:(MTLIndexType)rce->_indexType
                               indexBuffer:(id<MTLBuffer>)rce->_indexBuffer
                         indexBufferOffset:(NSUInteger)indexBufferOffset
                             instanceCount:instanceCount
                                baseVertex:vertexOffset
                              baseInstance:firstInstance];
}

GPU_HIDE
void
mt_endEncoding(GPURenderCommandEncoder *rce) {
  [mt_nativeRCE(rce) endEncoding];
  free(rce);
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
  api->scissor                = mt_scissor;
  api->blendConstant          = mt_blendConstant;
  api->stencilReference       = mt_stencilReference;
  api->vertexBytes            = mt_vertexBytes;
  api->vertexBuffer           = mt_vertexBuffer;
  api->setVertexTexture       = mt_rceSetVertexTexture;
  api->setVertexSampler       = mt_rceSetVertexSampler;
  api->fragmentBuffer         = mt_fragmentBuffer;
  api->setFragmentTexture     = mt_rceSetFragmentTexture;
  api->setFragmentSampler     = mt_rceSetFragmentSampler;
  api->drawPrimitives         = mt_drawPrimitives;
  api->drawIndexedPrims       = mt_drawIndexedPrims;
  api->endEncoding            = mt_endEncoding;
}
