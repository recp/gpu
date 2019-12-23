/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/vertex.h"
#include "../../../include/gpu/pipeline.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPUVertexDescriptor*
gpu_vertex_new() {
  GPUVertexDescriptor *vdec;
  MtVertexDescriptor  *mtvdesc;

  mtvdesc    = mtVertexDescNew();
  vdec       = calloc(1, sizeof(*vdec));
  vdec->priv = mtvdesc;

  return vdec;
}

GPU_EXPORT
void
gpu_attrib(GPUVertexDescriptor * __restrict vert,
           uint32_t                         attribIndex,
           GPUVertexFormat                  format,
           uint32_t                         offset,
           uint32_t                         bufferIndex) {
  mtVertexAttrib(vert->priv,
                 attribIndex,
                 (MtVertexFormat)format,
                 offset,
                 bufferIndex);
}

GPU_EXPORT
void
gpu_layout(GPUVertexDescriptor * __restrict vert,
           uint32_t                         layoutIndex,
           uint32_t                         stride,
           uint32_t                         stepRate,
           GPUVertexStepFunction            stepFunction) {
  mtVertexLayout(vert->priv, layoutIndex,
                 stride,
                 stepRate,
                 (MtVertexStepFunction)stepFunction);
}

GPU_EXPORT
void
gpu_vertex(GPUPipeline         * __restrict pipeline,
           GPUVertexDescriptor * __restrict vert) {
  mtSetVertexDesc(pipeline->priv, vert->priv);
}
