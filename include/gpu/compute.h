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
#include "pass.h"

typedef struct GPUComputePipeline    GPUComputePipeline;
typedef struct GPUComputePassEncoder GPUComputePassEncoder;

typedef struct GPUComputePipelineCreateInfo {
  GPUChainedStruct   chain;
  const char        *label;
  GPUPipelineLayout *layout;
  GPUPipelineCache  *cache;
  GPUShaderLibrary  *library;
  const char        *entryPoint;
} GPUComputePipelineCreateInfo;

typedef struct GPUComputePassCreateInfo {
  GPUChainedStruct              chain;
  const char                   *label;
  const GPUPassTimestampWrites *timestampWrites;
} GPUComputePassCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateComputePipeline(GPUDevice                          *__restrict device,
                         const GPUComputePipelineCreateInfo *__restrict info,
                         GPUComputePipeline                **__restrict outPipeline);

GPU_EXPORT
void
GPUDestroyComputePipeline(GPUComputePipeline *pipeline);

/* compute pipelines compiled on threadCount background threads (at most
 * 16; 0, the default, compiles on the creating thread): GPUCreateComputePipeline
 * checks its info and returns at once, and binding the pipeline waits for
 * its compile, running it there if no thread started it yet. destroying the
 * pipeline, or the shader library or pipeline layout it was made from, waits
 * too. a compile that fails shows at GPUWaitComputePipeline, and a bind of
 * it is skipped. */
GPU_EXPORT
GPUResult
GPUSetPipelineCompileThreads(GPUDevice *device, uint32_t threadCount);

/* GPU_OK once the pipeline's compile is done and it can be bound */
GPU_EXPORT
GPUResult
GPUWaitComputePipeline(GPUComputePipeline *pipeline);

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePass(GPUCommandBuffer *cmdb, const char *label);

GPU_EXPORT
GPUComputePassEncoder*
GPUBeginComputePassWithInfo(GPUCommandBuffer               *cmdb,
                            const GPUComputePassCreateInfo *info);

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
                    GPUBuffer             *argsBuffer,
                    uint64_t               argsOffset);

GPU_EXPORT
void
GPUMultiDispatchIndirect(GPUComputePassEncoder *pass,
                         GPUBuffer             *argsBuffer,
                         uint64_t               argsOffset,
                         uint32_t               dispatchCount,
                         uint32_t               strideBytes);

GPU_EXPORT
void
GPUEndComputePass(GPUComputePassEncoder *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_compute_h */
