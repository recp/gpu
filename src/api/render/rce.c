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

GPU_EXPORT
GPURenderCommandEncoder*
gpuRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;
  
  return api->rce.renderCommandEncoder(cmdb, pass);
}

GPU_EXPORT
void
gpuFrontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.frontFace(rce, winding);
}

GPU_EXPORT
void
gpuCullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.cullMode(rce, mode);
}

GPU_EXPORT
void
gpuSetRenderPipelineState(GPURenderCommandEncoder *rce, GPURenderPipelineState *piplineState) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.setRenderPipelineState(rce, piplineState);
}

GPU_EXPORT
void
gpuSetDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.setDepthStencil(rce, ds);
}

GPU_EXPORT
void
gpuViewport(GPURenderCommandEncoder *enc, GPUViewport *viewport) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.viewport(enc, viewport);
}

GPU_EXPORT
void
gpuVertexBytes(GPURenderCommandEncoder *enc,
               void                    *bytes,
               size_t                   legth,
               uint32_t                 atIndex) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.vertexBytes(enc, bytes, legth, atIndex);
}

GPU_EXPORT
void
gpuVertexBuffer(GPURenderCommandEncoder *rce,
                GPUBuffer               *buf,
                size_t                   off,
                uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.vertexBuffer(rce, buf, off, index);
}

GPU_EXPORT
void
gpuFragmentBuffer(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.fragmentBuffer(rce, buf, off, index);
}

GPU_EXPORT
void
gpuRCESetTexture(GPURenderCommandEncoder *rce,
                 GPUTexture               *tex,
                 uint32_t                 index) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.setFragmentTexture(rce, tex, index);
}

GPU_EXPORT
void
gpuDrawIndexedPrims(GPURenderCommandEncoder *rce,
                    GPUPrimitiveType         type,
                    uint32_t                 indexCount,
                    GPUIndexType             indexType,
                    GPUBuffer               *indexBuffer,
                    uint32_t                 indexBufferOffset) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.drawIndexedPrims(rce,
                            type,
                            indexCount,
                            indexType,
                            indexBuffer,
                            indexBufferOffset);
}

GPU_EXPORT
void
gpuEndEncoding(GPURenderCommandEncoder *rce) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->rce.endEncoding(rce);
}
