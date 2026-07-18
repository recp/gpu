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

#ifndef gpu_execution_graph_internal_h
#define gpu_execution_graph_internal_h

#include "../common.h"

struct GPUExecutionGraphEXT {
  void                                   *_priv;
  GPUApi                                 *_api;
  GPUDevice                              *device;
  GPUShaderLibrary                       *library;
  GPUPipelineLayout                      *layout;
  GPUExecutionGraphMemoryRequirementsEXT  memoryRequirements;
  uint32_t                                requiredBindGroupMask;
  uint32_t                                pushConstantSizeBytes;
  GPUShaderStageFlags                     pushConstantStages;
  uint32_t                                refCount;
};

struct GPUExecutionGraphInstanceEXT {
  void                     *_priv;
  GPUApi                   *_api;
  GPUDevice                *device;
  GPUExecutionGraphEXT     *graph;
  uint64_t                  memorySizeBytes;
};

GPU_HIDE
void
gpuRetainExecutionGraph(GPUExecutionGraphEXT *graph);

#endif /* gpu_execution_graph_internal_h */
