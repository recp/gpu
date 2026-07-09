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
#include "vertex_internal.h"

GPU_HIDE
GPUVertexDescriptor*
gpuCreateVertexDesc(void) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->vertex.newVertexDesc();
}

GPU_HIDE
void
gpuDestroyVertexDesc(GPUVertexDescriptor *vert) {
  GPUApi *api;

  if (!vert)
    return;

  if ((api = gpuActiveGPUApi()) && api->vertex.destroyVertexDesc) {
    api->vertex.destroyVertexDesc(vert);
    return;
  }

  free(vert);
}

GPU_HIDE
void
gpuVertexDescAttrib(GPUVertexDescriptor * __restrict vert,
                    uint32_t                         attribIndex,
                    GPUVertexFormat                  format,
                    uint32_t                         offset,
                    uint32_t                         bufferIndex) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.attrib(vert, attribIndex, format, offset, bufferIndex);
}

GPU_HIDE
void
gpuVertexDescLayout(GPUVertexDescriptor * __restrict vert,
                    uint32_t                         layoutIndex,
                    uint32_t                         stride,
                    uint32_t                         stepRate,
                    GPUVertexStepFunction            stepFunction) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.layout(vert, layoutIndex, stride, stepRate, stepFunction);
}

GPU_HIDE
void
gpuPipelineSetVertexDesc(GPURenderPipeline   * __restrict pipeline,
                         GPUVertexDescriptor * __restrict vert) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.vertexDesc(pipeline, vert);
}
