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
#include "render/pipeline_internal.h"
#include "vertex_internal.h"

GPU_HIDE
VertexDescriptor*
createVertexDesc(Api    *api) {
  if (!api || !api->vertex.newVertexDesc)
    return NULL;

  return api->vertex.newVertexDesc();
}

GPU_HIDE
void
gpuDestroyVertexDesc(Api    *api, VertexDescriptor    *vert) {
  if (!vert)
    return;

  if (api && api->vertex.destroyVertexDesc) {
    api->vertex.destroyVertexDesc(vert);
    return;
  }

  free(vert);
}

GPU_HIDE
void
vertexDescAttrib(Api                 *__restrict api,
                 VertexDescriptor    *__restrict vert,
                 uint32_t                        attribIndex,
                 GPUVertexFormat                 format,
                 uint32_t                        offset,
                 uint32_t                        bufferIndex) {
  if (!api || !vert || !api->vertex.attrib)
    return;

  api->vertex.attrib(vert, attribIndex, format, offset, bufferIndex);
}

GPU_HIDE
void
vertexDescLayout(Api                 *__restrict api,
                 VertexDescriptor    *__restrict vert,
                 uint32_t                        layoutIndex,
                 uint32_t                        stride,
                 GPUVertexStepMode               stepMode) {
  if (!api || !vert || !api->vertex.layout)
    return;

  api->vertex.layout(vert, layoutIndex, stride, stepMode);
}

GPU_HIDE
void
pipelineSetVertexDesc(GPURenderPipeline   *__restrict pipeline,
                      VertexDescriptor    *__restrict vert) {
  Api    *api;

  if (!pipeline || !vert || !(api = pipeline->_api)
      || !api->vertex.vertexDesc)
    return;

  api->vertex.vertexDesc(pipeline, vert);
}
