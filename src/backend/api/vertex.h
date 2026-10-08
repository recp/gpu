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

#ifndef gpu_gpudef_vertex_h
#define gpu_gpudef_vertex_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>

typedef struct VertexDescriptor    VertexDescriptor;

typedef struct ApiVertex {
  VertexDescriptor* (*newVertexDesc)(void);

  void (*destroyVertexDesc)(VertexDescriptor    *vert);

  void
  (*attrib)(VertexDescriptor    *__restrict vert,
            uint32_t                        attribIndex,
            GPUVertexFormat                 format,
            uint32_t                        offset,
            uint32_t                        bufferIndex);

  void
  (*layout)(VertexDescriptor    *__restrict vert,
            uint32_t                        layoutIndex,
            uint32_t                        stride,
            GPUVertexStepMode               stepMode);

  void (*vertexDesc)(GPURenderPipeline *__restrict pipeline, VertexDescriptor    *__restrict vert);
} ApiVertex;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_vertex_h */
