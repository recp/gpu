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

GPU_EXPORT
GPUVertexDescriptor*
mt_newVertexDesc(void) {
  GPUVertexDescriptor *vdec;
  MtVertexDescriptor  *mtvdesc;

  mtvdesc    = mtVertexDescNew();
  vdec       = calloc(1, sizeof(*vdec));
  vdec->priv = mtvdesc;

  return vdec;
}

GPU_EXPORT
void
mt_attrib(GPUVertexDescriptor * __restrict vert,
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
mt_layout(GPUVertexDescriptor * __restrict vert,
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
mt_vertexDesc(GPURenderPipeline         * __restrict pipeline,
              GPUVertexDescriptor * __restrict vert) {
  mtSetVertexDesc(pipeline->priv, vert->priv);
}

GPU_HIDE
void
mt_initVertex(GPUApiVertex *api) {
  api->newVertexDesc = mt_newVertexDesc;
  api->attrib        = mt_attrib;
  api->layout        = mt_layout;
  api->vertexDesc    = mt_vertexDesc;
}
