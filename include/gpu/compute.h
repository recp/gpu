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

#ifndef gpu_compute_h
#define gpu_compute_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"
#include "pipeline.h"
#include "bindgroup.h"
#include "buffer.h"

typedef struct GPUComputePipeline GPUComputePipeline;

typedef struct GPUComputePassEncoder GPUComputePassEncoder;

typedef struct GPUComputePipelineCreateInfo {
  GPUChainedStruct  chain;
  const char       *label;
  GPUPipelineLayout *layout;
  GPUPipelineCache *cache;
  GPUShaderLibrary *library;
  const char       *entryPoint;
} GPUComputePipelineCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateComputePipeline(GPUDevice                          * __restrict device,
                         const GPUComputePipelineCreateInfo * __restrict info,
                         GPUComputePipeline                ** __restrict outPipeline);

GPU_EXPORT
void
GPUDestroyComputePipeline(GPUComputePipeline *pipeline);

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePass(GPUCommandBuffer *cmdb, const char *label);

GPU_EXPORT
void
GPUBindComputePipeline(GPUComputePassEncoder *pass,
                       GPUComputePipeline    *pipeline);

GPU_EXPORT
void
GPUBindComputeGroup(GPUComputePassEncoder *pass,
                    uint32_t               groupIndex,
                    GPUBindGroup          *bindGroup,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *pDynamicOffsets);

GPU_EXPORT
void
GPUSetComputePushConstants(GPUComputePassEncoder *pass,
                           uint32_t               offset,
                           uint32_t               sizeBytes,
                           const void            *data);

GPU_EXPORT
void
GPUDispatch(GPUComputePassEncoder *pass,
            uint32_t               x,
            uint32_t               y,
            uint32_t               z);

GPU_EXPORT
void
GPUDispatchIndirect(GPUComputePassEncoder *pass,
                    GPUBuffer            *argsBuffer,
                    uint64_t              argsOffset);

GPU_EXPORT
void
GPUMultiDispatchIndirect(GPUComputePassEncoder *pass,
                         GPUBuffer            *argsBuffer,
                         uint64_t              argsOffset,
                         uint32_t              dispatchCount,
                         uint32_t              strideBytes);

GPU_EXPORT
void
GPUEndComputePass(GPUComputePassEncoder *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_compute_h */
