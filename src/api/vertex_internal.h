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

#ifndef gpu_vertex_internal_h
#define gpu_vertex_internal_h

#include "../common.h"

struct VertexDescriptor {
  void *_priv;
};

GPU_HIDE
VertexDescriptor*
createVertexDesc(Api    *api);

GPU_HIDE
void
gpuDestroyVertexDesc(Api    *api, VertexDescriptor    *vert);

GPU_HIDE
void
vertexDescAttrib(Api                 *__restrict api,
                 VertexDescriptor    *__restrict vertex,
                 uint32_t                        attribIndex,
                 GPUVertexFormat                 format,
                 uint32_t                        offset,
                 uint32_t                        bufferIndex);

GPU_HIDE
void
vertexDescLayout(Api                 *__restrict api,
                 VertexDescriptor    *__restrict vertex,
                 uint32_t                        layoutIndex,
                 uint32_t                        stride,
                 GPUVertexStepMode               stepMode);

GPU_HIDE
void
pipelineSetVertexDesc(GPURenderPipeline   *__restrict pipeline,
                      VertexDescriptor    *__restrict vert);

#endif /* gpu_vertex_internal_h */
