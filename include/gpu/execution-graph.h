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

#ifndef gpu_execution_graph_h
#define gpu_execution_graph_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "buffer.h"
#include "library.h"
#include "pipeline.h"

typedef struct GPUComputePassEncoder        GPUComputePassEncoder;
typedef struct GPUExecutionGraphEXT         GPUExecutionGraphEXT;
typedef struct GPUExecutionGraphInstanceEXT GPUExecutionGraphInstanceEXT;

typedef struct GPUExecutionGraphCreateInfoEXT {
  GPUChainedStruct   chain;
  const char        *label;
  GPUShaderLibrary  *library;
  GPUPipelineLayout *layout;
  GPUPipelineCache  *cache;
  const char        *graphName;
} GPUExecutionGraphCreateInfoEXT;

typedef struct GPUExecutionGraphMemoryRequirementsEXT {
  uint64_t minSizeBytes;
  uint64_t maxSizeBytes;
  uint64_t sizeGranularityBytes;
} GPUExecutionGraphMemoryRequirementsEXT;

typedef struct GPUExecutionGraphInstanceCreateInfoEXT {
  GPUChainedStruct      chain;
  const char           *label;
  GPUExecutionGraphEXT *graph;
  uint64_t              memorySizeBytes;
} GPUExecutionGraphInstanceCreateInfoEXT;

typedef struct GPUExecutionGraphEntryEXT {
  uint32_t index;
  uint32_t recordSizeBytes;
  uint32_t recordAlignmentBytes;
} GPUExecutionGraphEntryEXT;

typedef struct GPUExecutionGraphInputEXT {
  const void               *pRecords;
  uint64_t                  recordStrideBytes;
  GPUExecutionGraphEntryEXT entry;
  uint32_t                  recordCount;
} GPUExecutionGraphInputEXT;

typedef struct GPUExecutionGraphBufferInputEXT {
  GPUBuffer                *records;
  uint64_t                  recordOffset;
  uint64_t                  recordStrideBytes;
  GPUExecutionGraphEntryEXT entry;
  uint32_t                  recordCount;
} GPUExecutionGraphBufferInputEXT;

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphEXT(GPUDevice                              *device,
                           const GPUExecutionGraphCreateInfoEXT   *info,
                           GPUExecutionGraphEXT                  **outGraph);

GPU_EXPORT
void
GPUDestroyExecutionGraphEXT(GPUExecutionGraphEXT *graph);

GPU_EXPORT
GPUResult
GPUGetExecutionGraphMemoryRequirementsEXT(const GPUExecutionGraphEXT             *graph,
                                          GPUExecutionGraphMemoryRequirementsEXT *outRequirements);

GPU_EXPORT
GPUResult
GPUCreateExecutionGraphInstanceEXT(GPUDevice                                    *device,
                                   const GPUExecutionGraphInstanceCreateInfoEXT *info,
                                   GPUExecutionGraphInstanceEXT                **outInstance);

GPU_EXPORT
void
GPUDestroyExecutionGraphInstanceEXT(GPUExecutionGraphInstanceEXT *instance);

GPU_EXPORT
GPUResult
GPUGetExecutionGraphEntryEXT(const GPUExecutionGraphEXT *graph,
                             const char                 *entryName,
                             GPUExecutionGraphEntryEXT  *outEntry);

GPU_EXPORT
void
GPUBindExecutionGraphEXT(GPUComputePassEncoder *pass,
                         GPUExecutionGraphEXT  *graph);

GPU_EXPORT
void
GPUDispatchExecutionGraphEXT(GPUComputePassEncoder             *pass,
                             GPUExecutionGraphInstanceEXT      *instance,
                             uint32_t                           inputCount,
                             const GPUExecutionGraphInputEXT   *pInputs);

GPU_EXPORT
void
GPUDispatchExecutionGraphBufferEXT(GPUComputePassEncoder                 *pass,
                                   GPUExecutionGraphInstanceEXT          *instance,
                                   uint32_t                               inputCount,
                                   const GPUExecutionGraphBufferInputEXT *pInputs);

#ifdef __cplusplus
}
#endif
#endif /* gpu_execution_graph_h */
