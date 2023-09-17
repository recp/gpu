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

#include "../common.h"
#include "../gpu.h"

typedef struct GPUApiRCE {
  GPURenderCommandEncoder*
  (*renderCommandEncoder)(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass);
  
  void
  (*frontFace)(GPURenderCommandEncoder *rce, GPUWinding winding);
  
  void
  (*cullMode)(GPURenderCommandEncoder *rce, GPUCullMode mode);
  
  void
  (*setRenderPipelineState)(GPURenderCommandEncoder *rce,
                            GPURenderPipelineState  *piplineState);
  
  void
  (*setDepthStencil)(GPURenderCommandEncoder *rce, GPUDepthStencilState *ds);
  
  void
  (*viewport)(GPURenderCommandEncoder *enc, GPUViewport *viewport);
  
  void
  (*vertexBytes)(GPURenderCommandEncoder *enc,
                 void                    *bytes,
                 size_t                   legth,
                 uint32_t                 atIndex);
  
  void
  (*vertexBuffer)(GPURenderCommandEncoder *rce,
                  GPUBuffer               *buf,
                  size_t                   off,
                  uint32_t                 index);
  
  void
  (*fragmentBuffer)(GPURenderCommandEncoder *rce,
                    GPUBuffer               *buf,
                    size_t                   off,
                    uint32_t                 index);
  
  void
  (*setFragmentTexture)(GPURenderCommandEncoder *rce,
                   GPUTexture               *tex,
                   uint32_t                 index);
  void
  (*drawIndexedPrims)(GPURenderCommandEncoder *rce,
                      GPUPrimitiveType         type,
                      uint32_t                 indexCount,
                      GPUIndexType             indexType,
                      GPUBuffer               *indexBuffer,
                      uint32_t                 indexBufferOffset);
  void
  (*endEncoding)(GPURenderCommandEncoder *rce);
} GPUApiRCE;

#endif /* gpu_api_rce_h */
