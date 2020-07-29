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
gpuNewVertexDesc() {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->vertex.newVertexDesc();
}

GPU_EXPORT
void
gpuAttrib(GPUVertexDescriptor * __restrict vert,
          uint32_t                         attribIndex,
          GPUVertexFormat                  format,
          uint32_t                         offset,
          uint32_t                         bufferIndex) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.attrib(vert, attribIndex, format, offset, bufferIndex);
}

GPU_EXPORT
void
gpuLayout(GPUVertexDescriptor * __restrict vert,
          uint32_t                         layoutIndex,
          uint32_t                         stride,
          uint32_t                         stepRate,
          GPUVertexStepFunction            stepFunction) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.layout(vert, layoutIndex, stride, stepRate, stepFunction);
}

GPU_EXPORT
void
gpuVertexDesc(GPURenderPipeline   * __restrict pipeline,
              GPUVertexDescriptor * __restrict vert) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return;
  
  api->vertex.vertexDesc(pipeline, vert);
}
