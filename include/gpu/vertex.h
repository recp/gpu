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

#ifndef gpu_vertex_desc_h
#define gpu_vertex_desc_h

#include "common.h"
#include "pipeline.h"

typedef enum GPUVertexFormat {
  GPUUnknown = 0,
  
  GPUUChar2 = 1,
  GPUUChar3 = 2,
  GPUUChar4 = 3,
  
  GPUChar2 = 4,
  GPUChar3 = 5,
  GPUChar4 = 6,
  
  GPUUChar2Normalized = 7,
  GPUUChar3Normalized = 8,
  GPUUChar4Normalized = 9,
  
  GPUChar2Normalized = 10,
  GPUChar3Normalized = 11,
  GPUChar4Normalized = 12,
  
  GPUUShort2 = 13,
  GPUUShort3 = 14,
  GPUUShort4 = 15,
  
  GPUShort2 = 16,
  GPUShort3 = 17,
  GPUShort4 = 18,
  
  GPUUShort2Normalized = 19,
  GPUUShort3Normalized = 20,
  GPUUShort4Normalized = 21,
  
  GPUShort2Normalized = 22,
  GPUShort3Normalized = 23,
  GPUShort4Normalized = 24,
  
  GPUHalf2 = 25,
  GPUHalf3 = 26,
  GPUHalf4 = 27,
  
  GPUFloat  = 28,
  GPUFloat2 = 29,
  GPUFloat3 = 30,
  GPUFloat4 = 31,
  
  GPUInt  = 32,
  GPUInt2 = 33,
  GPUInt3 = 34,
  GPUInt4 = 35,
  
  GPUUInt  = 36,
  GPUUInt2 = 37,
  GPUUInt3 = 38,
  GPUUInt4 = 39,
  
  GPUInt1010102Normalized  = 40,
  GPUUInt1010102Normalized = 41,
  
  GPUUChar4Normalized_BGRA = 42,

  GPUUChar           = 45,
  GPUChar            = 46,
  GPUUCharNormalized = 47,
  GPUCharNormalized  = 48,
  
  GPUUShort           = 49,
  GPUShort            = 50,
  GPUUShortNormalized = 51,
  GPUShortNormalized  = 52,
  
  GPUVertexFormatHalf = 53
} GPUVertexFormat;

typedef enum GPUVertexStepFunction {
  GPUConstant             = 0,
  GPUPerVertex            = 1,
  GPUPerInstance          = 2,
  GPUPerPatch             = 3,
  GPUPerPatchControlPoint = 4,
} GPUVertexStepFunction;

typedef struct GPUVertexAttribute {
  struct GPUVertexAttribute *next;
  GPUVertexFormat            format;
  uint32_t                   offset;
  uint32_t                   bufferIndex;
} GPUVertexAttribute;

typedef struct GPUVertexBufferLayout {
  struct GPUVertexBufferLayout *next;
  uint32_t                      stride;
  uint32_t                      stepRate;
  GPUVertexStepFunction         stepFunction;
} GPUVertexBufferLayout;

typedef struct GPUVertexDescriptor {
  void                  *priv;
  GPUVertexAttribute    *attributes;
  GPUVertexBufferLayout *layouts;
} GPUVertexDescriptor;

GPU_EXPORT
GPUVertexDescriptor*
gpuNewVertexDesc(void);

GPU_EXPORT
void
gpuAttrib(GPUVertexDescriptor * __restrict vertex,
          uint32_t                         attribIndex,
          GPUVertexFormat                  format,
          uint32_t                         offset,
          uint32_t                         bufferIndex);

GPU_EXPORT
void
gpuLayout(GPUVertexDescriptor * __restrict vertex,
          uint32_t                         layoutIndex,
          uint32_t                         stride,
          uint32_t                         stepRate,
          GPUVertexStepFunction            stepFunction);

GPU_EXPORT
void
gpuVertexDesc(GPURenderPipeline         * __restrict pipeline,
              GPUVertexDescriptor * __restrict vert);

#endif /* gpu_vertex_desc_h */
