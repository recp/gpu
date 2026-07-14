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
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct GPURenderPipeline GPURenderPipeline;

/* Portable Metal, Vulkan, and Direct3D 12 vertex input formats. */
typedef enum GPUVertexFormat {
  GPU_VERTEX_FORMAT_UNDEFINED = 0,
  GPU_VERTEX_FORMAT_UINT8,
  GPU_VERTEX_FORMAT_UINT8X2,
  GPU_VERTEX_FORMAT_UINT8X4,
  GPU_VERTEX_FORMAT_SINT8,
  GPU_VERTEX_FORMAT_SINT8X2,
  GPU_VERTEX_FORMAT_SINT8X4,
  GPU_VERTEX_FORMAT_UNORM8,
  GPU_VERTEX_FORMAT_UNORM8X2,
  GPU_VERTEX_FORMAT_UNORM8X4,
  GPU_VERTEX_FORMAT_SNORM8,
  GPU_VERTEX_FORMAT_SNORM8X2,
  GPU_VERTEX_FORMAT_SNORM8X4,
  GPU_VERTEX_FORMAT_UINT16,
  GPU_VERTEX_FORMAT_UINT16X2,
  GPU_VERTEX_FORMAT_UINT16X4,
  GPU_VERTEX_FORMAT_SINT16,
  GPU_VERTEX_FORMAT_SINT16X2,
  GPU_VERTEX_FORMAT_SINT16X4,
  GPU_VERTEX_FORMAT_UNORM16,
  GPU_VERTEX_FORMAT_UNORM16X2,
  GPU_VERTEX_FORMAT_UNORM16X4,
  GPU_VERTEX_FORMAT_SNORM16,
  GPU_VERTEX_FORMAT_SNORM16X2,
  GPU_VERTEX_FORMAT_SNORM16X4,
  GPU_VERTEX_FORMAT_FLOAT16,
  GPU_VERTEX_FORMAT_FLOAT16X2,
  GPU_VERTEX_FORMAT_FLOAT16X4,
  GPU_VERTEX_FORMAT_FLOAT32,
  GPU_VERTEX_FORMAT_FLOAT32X2,
  GPU_VERTEX_FORMAT_FLOAT32X3,
  GPU_VERTEX_FORMAT_FLOAT32X4,
  GPU_VERTEX_FORMAT_SINT32,
  GPU_VERTEX_FORMAT_SINT32X2,
  GPU_VERTEX_FORMAT_SINT32X3,
  GPU_VERTEX_FORMAT_SINT32X4,
  GPU_VERTEX_FORMAT_UINT32,
  GPU_VERTEX_FORMAT_UINT32X2,
  GPU_VERTEX_FORMAT_UINT32X3,
  GPU_VERTEX_FORMAT_UINT32X4,
  GPU_VERTEX_FORMAT_UNORM10_10_10_2,
  GPU_VERTEX_FORMAT_UNORM8X4_BGRA,
  GPU_VERTEX_FORMAT_COUNT
} GPUVertexFormat;

typedef enum GPUVertexStepMode {
  GPU_VERTEX_STEP_MODE_VERTEX   = 0,
  GPU_VERTEX_STEP_MODE_INSTANCE = 1
} GPUVertexStepMode;

typedef enum GPUVertexStepFunction {
  GPUConstant             = 0,
  GPUPerVertex            = 1,
  GPUPerInstance          = 2,
  GPUPerPatch             = 3,
  GPUPerPatchControlPoint = 4,
} GPUVertexStepFunction;

typedef struct GPUVertexAttribute {
  uint32_t        shaderLocation;
  GPUVertexFormat format;
  uint32_t        offset;
} GPUVertexAttribute;

typedef struct GPUVertexBufferLayout {
  uint32_t                  strideBytes;
  GPUVertexStepMode         stepMode;
  uint32_t                  attributeCount;
  const GPUVertexAttribute *pAttributes;
} GPUVertexBufferLayout;

typedef struct GPUVertexState {
  uint32_t                     bufferLayoutCount;
  const GPUVertexBufferLayout *pBufferLayouts;
} GPUVertexState;

#ifdef __cplusplus
}
#endif
#endif /* gpu_vertex_desc_h */
