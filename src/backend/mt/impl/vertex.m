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

#include "../common.h"
#include "../../../api/vertex_internal.h"
#include "../../../api/render/pipeline_internal.h"

GPU_HIDE
GPUVertexDescriptor*
mt_newVertexDesc(void) {
  GPUVertexDescriptor *vdec;
  MTLVertexDescriptor *mtvdesc;

  mtvdesc    = [MTLVertexDescriptor new];
  vdec       = calloc(1, sizeof(*vdec));
  vdec->_priv = mtvdesc;

  return vdec;
}

GPU_HIDE
void
mt_destroyVertexDesc(GPUVertexDescriptor *vert) {
  if (!vert) {
    return;
  }

  if (vert->_priv) {
    [(id)vert->_priv release];
  }
  free(vert);
}

static MTLVertexFormat
mt_vertexFormat(GPUVertexFormat format) {
  static const MTLVertexFormat formats[GPU_VERTEX_FORMAT_COUNT] = {
    [GPU_VERTEX_FORMAT_UNDEFINED]       = MTLVertexFormatInvalid,
    [GPU_VERTEX_FORMAT_UINT8]           = MTLVertexFormatUChar,
    [GPU_VERTEX_FORMAT_UINT8X2]         = MTLVertexFormatUChar2,
    [GPU_VERTEX_FORMAT_UINT8X4]         = MTLVertexFormatUChar4,
    [GPU_VERTEX_FORMAT_SINT8]           = MTLVertexFormatChar,
    [GPU_VERTEX_FORMAT_SINT8X2]         = MTLVertexFormatChar2,
    [GPU_VERTEX_FORMAT_SINT8X4]         = MTLVertexFormatChar4,
    [GPU_VERTEX_FORMAT_UNORM8]          = MTLVertexFormatUCharNormalized,
    [GPU_VERTEX_FORMAT_UNORM8X2]        = MTLVertexFormatUChar2Normalized,
    [GPU_VERTEX_FORMAT_UNORM8X4]        = MTLVertexFormatUChar4Normalized,
    [GPU_VERTEX_FORMAT_SNORM8]          = MTLVertexFormatCharNormalized,
    [GPU_VERTEX_FORMAT_SNORM8X2]        = MTLVertexFormatChar2Normalized,
    [GPU_VERTEX_FORMAT_SNORM8X4]        = MTLVertexFormatChar4Normalized,
    [GPU_VERTEX_FORMAT_UINT16]          = MTLVertexFormatUShort,
    [GPU_VERTEX_FORMAT_UINT16X2]        = MTLVertexFormatUShort2,
    [GPU_VERTEX_FORMAT_UINT16X4]        = MTLVertexFormatUShort4,
    [GPU_VERTEX_FORMAT_SINT16]          = MTLVertexFormatShort,
    [GPU_VERTEX_FORMAT_SINT16X2]        = MTLVertexFormatShort2,
    [GPU_VERTEX_FORMAT_SINT16X4]        = MTLVertexFormatShort4,
    [GPU_VERTEX_FORMAT_UNORM16]         = MTLVertexFormatUShortNormalized,
    [GPU_VERTEX_FORMAT_UNORM16X2]       = MTLVertexFormatUShort2Normalized,
    [GPU_VERTEX_FORMAT_UNORM16X4]       = MTLVertexFormatUShort4Normalized,
    [GPU_VERTEX_FORMAT_SNORM16]         = MTLVertexFormatShortNormalized,
    [GPU_VERTEX_FORMAT_SNORM16X2]       = MTLVertexFormatShort2Normalized,
    [GPU_VERTEX_FORMAT_SNORM16X4]       = MTLVertexFormatShort4Normalized,
    [GPU_VERTEX_FORMAT_FLOAT16]         = MTLVertexFormatHalf,
    [GPU_VERTEX_FORMAT_FLOAT16X2]       = MTLVertexFormatHalf2,
    [GPU_VERTEX_FORMAT_FLOAT16X4]       = MTLVertexFormatHalf4,
    [GPU_VERTEX_FORMAT_FLOAT32]         = MTLVertexFormatFloat,
    [GPU_VERTEX_FORMAT_FLOAT32X2]       = MTLVertexFormatFloat2,
    [GPU_VERTEX_FORMAT_FLOAT32X3]       = MTLVertexFormatFloat3,
    [GPU_VERTEX_FORMAT_FLOAT32X4]       = MTLVertexFormatFloat4,
    [GPU_VERTEX_FORMAT_SINT32]          = MTLVertexFormatInt,
    [GPU_VERTEX_FORMAT_SINT32X2]        = MTLVertexFormatInt2,
    [GPU_VERTEX_FORMAT_SINT32X3]        = MTLVertexFormatInt3,
    [GPU_VERTEX_FORMAT_SINT32X4]        = MTLVertexFormatInt4,
    [GPU_VERTEX_FORMAT_UINT32]          = MTLVertexFormatUInt,
    [GPU_VERTEX_FORMAT_UINT32X2]        = MTLVertexFormatUInt2,
    [GPU_VERTEX_FORMAT_UINT32X3]        = MTLVertexFormatUInt3,
    [GPU_VERTEX_FORMAT_UINT32X4]        = MTLVertexFormatUInt4,
    [GPU_VERTEX_FORMAT_UNORM10_10_10_2] =
      MTLVertexFormatUInt1010102Normalized,
    [GPU_VERTEX_FORMAT_UNORM8X4_BGRA]   =
      MTLVertexFormatUChar4Normalized_BGRA
  };

  return (uint32_t)format < GPU_ARRAY_LEN(formats)
           ? formats[format]
           : MTLVertexFormatInvalid;
}

static MTLVertexStepFunction
mt_vertexStepFunction(GPUVertexStepMode mode) {
  static const MTLVertexStepFunction functions[] = {
    [GPU_VERTEX_STEP_MODE_VERTEX]   = MTLVertexStepFunctionPerVertex,
    [GPU_VERTEX_STEP_MODE_INSTANCE] = MTLVertexStepFunctionPerInstance
  };

  return (uint32_t)mode < GPU_ARRAY_LEN(functions)
           ? functions[mode]
           : MTLVertexStepFunctionPerVertex;
}

GPU_HIDE
void
mt_attrib(GPUVertexDescriptor * __restrict vert,
          uint32_t                         attribIndex,
          GPUVertexFormat                  format,
          uint32_t                         offset,
          uint32_t                         bufferIndex) {
  MTLVertexDescriptor *desc;
  uint32_t              nativeIndex;

  nativeIndex = mt_vertexBufferIndex(bufferIndex);
  if (nativeIndex == UINT32_MAX) {
    return;
  }
  desc = vert->_priv;
  desc.attributes[attribIndex].format      = mt_vertexFormat(format);
  desc.attributes[attribIndex].offset      = offset;
  desc.attributes[attribIndex].bufferIndex = nativeIndex;
}

GPU_HIDE
void
mt_layout(GPUVertexDescriptor * __restrict vert,
          uint32_t                         layoutIndex,
          uint32_t                         stride,
          GPUVertexStepMode                stepMode) {
  MTLVertexDescriptor *desc;
  uint32_t              nativeIndex;

  nativeIndex = mt_vertexBufferIndex(layoutIndex);
  if (nativeIndex == UINT32_MAX) {
    return;
  }
  desc = vert->_priv;
  desc.layouts[nativeIndex].stride       = stride;
  desc.layouts[nativeIndex].stepRate     = 1u;
  desc.layouts[nativeIndex].stepFunction = mt_vertexStepFunction(stepMode);
}

GPU_HIDE
void
mt_vertexDesc(GPURenderPipeline         * __restrict pipeline,
              GPUVertexDescriptor * __restrict vert) {
  MTRenderPipelineDesc *native;

  native = pipeline->_priv;
  ((MTLRenderPipelineDescriptor *)native->classic).vertexDescriptor = vert->_priv;
}

GPU_HIDE
void
mt_initVertex(GPUApiVertex *api) {
  api->newVertexDesc     = mt_newVertexDesc;
  api->destroyVertexDesc = mt_destroyVertexDesc;
  api->attrib            = mt_attrib;
  api->layout            = mt_layout;
  api->vertexDesc        = mt_vertexDesc;
}
