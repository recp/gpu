/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef gpu_vertex_desc_h
#define gpu_vertex_desc_h

#include "common.h"

typedef enum GPUVertexFormat {
  GPUVertexFormatUnknown = 0,
  
  GPUVertexFormatFloat   = 1,
  GPUVertexFormatFloat2  = 2,
  GPUVertexFormatFloat3  = 3,
  GPUVertexFormatFloat4  = 4,
  
  GPUVertexFormatInt     = 5,
  GPUVertexFormatInt2    = 6,
  GPUVertexFormatInt3    = 7,
  GPUVertexFormatInt4    = 8,
  
  GPUVertexFormatUInt    = 9,
  GPUVertexFormatUInt2   = 10,
  GPUVertexFormatUInt3   = 11,
  GPUVertexFormatUInt4   = 12
};

typedef enum GPUVertexStepFunction {
  GPUVertexStepFunctionConstant             = 0,
  GPUVertexStepFunctionPerVertex            = 1,
  GPUVertexStepFunctionPerInstance          = 2,
  GPUVertexStepFunctionPerPatch             = 3,
  GPUVertexStepFunctionPerPatchControlPoint = 4,
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
  GPUVertexAttribute    *attributes;
  GPUVertexBufferLayout *layouts;
} GPUVertexDescriptor;

#endif /* gpu_vertex_desc_h */
