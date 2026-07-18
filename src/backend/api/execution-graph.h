/*
 * Copyright (C) 2026 Recep Aslantas
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

#ifndef gpu_api_execution_graph_h
#define gpu_api_execution_graph_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/execution-graph.h>

typedef struct GPUApiExecutionGraph {
  GPUResult
  (*create)(GPUDevice                            *device,
            const GPUExecutionGraphCreateInfoEXT *info,
            GPUExecutionGraphEXT                 *graph);

  void
  (*destroy)(GPUExecutionGraphEXT *graph);

  GPUResult
  (*createInstance)(GPUDevice                                    *device,
                    const GPUExecutionGraphInstanceCreateInfoEXT *info,
                    GPUExecutionGraphInstanceEXT                 *instance);

  void
  (*destroyInstance)(GPUExecutionGraphInstanceEXT *instance);

  GPUResult
  (*getEntry)(const GPUExecutionGraphEXT *graph,
              const char                 *entryName,
              GPUExecutionGraphEntryEXT  *outEntry);

  void
  (*bind)(GPUComputePassEncoder *pass, GPUExecutionGraphEXT *graph);

  void
  (*dispatch)(GPUComputePassEncoder           *pass,
              GPUExecutionGraphInstanceEXT    *instance,
              uint32_t                         inputCount,
              const GPUExecutionGraphInputEXT *inputs);

  void
  (*dispatchBuffer)(GPUComputePassEncoder                 *pass,
                    GPUExecutionGraphInstanceEXT          *instance,
                    uint32_t                               inputCount,
                    const GPUExecutionGraphBufferInputEXT *inputs);
} GPUApiExecutionGraph;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_execution_graph_h */
