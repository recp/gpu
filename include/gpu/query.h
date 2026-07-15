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

#ifndef gpu_query_h
#define gpu_query_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "buffer.h"
#include "cmdqueue.h"
#include "cmd-enc.h"

struct GPUDevice;
typedef struct GPUQuerySet GPUQuerySet;

typedef enum GPUQueryType {
  GPU_QUERY_TIMESTAMP           = 0,
  GPU_QUERY_OCCLUSION           = 1,
  GPU_QUERY_PIPELINE_STATISTICS = 2
} GPUQueryType;

typedef enum GPUPipelineStatisticBits {
  GPU_PIPESTAT_INPUT_ASSEMBLY_VERTICES            = 1u << 0,
  GPU_PIPESTAT_INPUT_ASSEMBLY_PRIMITIVES          = 1u << 1,
  GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS          = 1u << 2,
  GPU_PIPESTAT_GEOMETRY_SHADER_INVOCATIONS        = 1u << 3,
  GPU_PIPESTAT_GEOMETRY_SHADER_PRIMITIVES         = 1u << 4,
  GPU_PIPESTAT_CLIPPING_INVOCATIONS               = 1u << 5,
  GPU_PIPESTAT_CLIPPING_PRIMITIVES                = 1u << 6,
  GPU_PIPESTAT_FRAGMENT_SHADER_INVOCATIONS        = 1u << 7,
  GPU_PIPESTAT_TESS_CONTROL_SHADER_PATCHES        = 1u << 8,
  GPU_PIPESTAT_TESS_EVALUATION_SHADER_INVOCATIONS = 1u << 9,
  GPU_PIPESTAT_COMPUTE_SHADER_INVOCATIONS         = 1u << 10,
  GPU_PIPESTAT_ALL                                = (1u << 11) - 1u
} GPUPipelineStatisticBits;

/* Fields outside the query set mask are undefined. */
typedef struct GPUPipelineStatisticsResult {
  uint64_t inputAssemblyVertices;
  uint64_t inputAssemblyPrimitives;
  uint64_t vertexShaderInvocations;
  uint64_t geometryShaderInvocations;
  uint64_t geometryShaderPrimitives;
  uint64_t clippingInvocations;
  uint64_t clippingPrimitives;
  uint64_t fragmentShaderInvocations;
  uint64_t tessControlShaderPatches;
  uint64_t tessEvaluationShaderInvocations;
  uint64_t computeShaderInvocations;
} GPUPipelineStatisticsResult;

typedef struct GPUQuerySetCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  GPUQueryType     type;
  uint32_t         count;
  uint32_t         pipelineStatsMask;
} GPUQuerySetCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateQuerySet(struct GPUDevice          *device,
                  const GPUQuerySetCreateInfo *info,
                  GPUQuerySet              **outSet);

GPU_EXPORT
void
GPUDestroyQuerySet(GPUQuerySet *set);

/* nanoseconds represented by one raw timestamp tick. */
GPU_EXPORT
GPUResult
GPUGetTimestampPeriod(GPUQueue *queue,
                      double   *outNanosecondsPerTick);

GPU_EXPORT
void
GPUWriteTimestamp(GPUCommandBuffer *cmdb,
                  GPUQuerySet      *set,
                  uint32_t          queryIndex);

GPU_EXPORT
void
GPUBeginOcclusionQuery(GPURenderPassEncoder *pass,
                       GPUQuerySet          *set,
                       uint32_t              queryIndex);

GPU_EXPORT
void
GPUEndOcclusionQuery(GPURenderPassEncoder *pass);

GPU_EXPORT
void
GPUBeginPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                GPUQuerySet      *set,
                                uint32_t          queryIndex);

GPU_EXPORT
void
GPUEndPipelineStatisticsQuery(GPUCommandBuffer *cmdb, GPUQuerySet *set);

GPU_EXPORT
void
GPUResolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet      *set,
                   uint32_t          firstQuery,
                   uint32_t          queryCount,
                   GPUBuffer        *dstBuffer,
                   uint64_t          dstOffset);

#ifdef __cplusplus
}
#endif
#endif /* gpu_query_h */
