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

#include "../common.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "query_internal.h"

static bool
gpu_validQueryType(GPUQueryType type) {
  return type == GPU_QUERY_TIMESTAMP ||
         type == GPU_QUERY_OCCLUSION ||
         type == GPU_QUERY_PIPELINE_STATISTICS;
}

static bool
gpu_validPipelineStatsMask(uint32_t mask) {
  const uint32_t knownMask = GPU_PIPESTAT_INPUT_ASSEMBLY_VERTICES |
                             GPU_PIPESTAT_INPUT_ASSEMBLY_PRIMITIVES |
                             GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS |
                             GPU_PIPESTAT_FRAGMENT_SHADER_INVOCATIONS |
                             GPU_PIPESTAT_COMPUTE_SHADER_INVOCATIONS;

  return mask != 0u && (mask & ~knownMask) == 0u;
}

static bool
gpu_validQueryCreateInfo(const GPUQuerySetCreateInfo *info) {
  if (!info) {
    return false;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO) {
    return false;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return false;
  }
  if (!gpu_validQueryType(info->type) || info->count == 0u) {
    return false;
  }
  if (info->type == GPU_QUERY_PIPELINE_STATISTICS) {
    return gpu_validPipelineStatsMask(info->pipelineStatsMask);
  }
  return info->pipelineStatsMask == 0u;
}

GPU_EXPORT
GPUResult
GPUCreateQuerySet(GPUDevice                *device,
                  const GPUQuerySetCreateInfo *info,
                  GPUQuerySet             **outSet) {
  GPUApi *api;
  GPUQuerySet *set;
  GPUResult result;

  if (!outSet) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSet = NULL;

  if (!device || !gpu_validQueryCreateInfo(info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->type == GPU_QUERY_OCCLUSION) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->type == GPU_QUERY_TIMESTAMP &&
      !GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->type == GPU_QUERY_PIPELINE_STATISTICS &&
      !GPUIsFeatureEnabled(device, GPU_FEATURE_PIPELINE_STATISTICS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!(api = gpuActiveGPUApi()) || !api->cmdbuf.createQuerySet) {
    return GPU_ERROR_UNSUPPORTED;
  }

  set = calloc(1, sizeof(*set));
  if (!set) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  set->device = device;
  set->type = info->type;
  set->count = info->count;
  set->pipelineStatsMask = info->pipelineStatsMask;

  result = api->cmdbuf.createQuerySet(device, info, set);
  if (result != GPU_OK) {
    free(set);
    return result;
  }

  *outSet = set;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyQuerySet(GPUQuerySet *set) {
  GPUApi *api;

  if (!set) {
    return;
  }

  if ((api = gpuActiveGPUApi()) && api->cmdbuf.destroyQuerySet) {
    api->cmdbuf.destroyQuerySet(set);
  }
  free(set);
}

GPU_EXPORT
void
GPUWriteTimestamp(GPUCommandBuffer *cmdb,
                  GPUQuerySet      *set,
                  uint32_t          queryIndex) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !set || set->type != GPU_QUERY_TIMESTAMP || queryIndex >= set->count) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->cmdbuf.writeTimestamp) {
    return;
  }

  api->cmdbuf.writeTimestamp(cmdb, set, queryIndex);
}

GPU_EXPORT
void
GPUBeginOcclusionQuery(GPURenderPassEncoder *pass,
                       GPUQuerySet          *set,
                       uint32_t              queryIndex) {
  GPU__UNUSED(pass);
  GPU__UNUSED(set);
  GPU__UNUSED(queryIndex);
}

GPU_EXPORT
void
GPUEndOcclusionQuery(GPURenderPassEncoder *pass) {
  GPU__UNUSED(pass);
}

GPU_EXPORT
void
GPUBeginPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                GPUQuerySet      *set,
                                uint32_t          queryIndex) {
  GPU__UNUSED(cmdb);
  GPU__UNUSED(set);
  GPU__UNUSED(queryIndex);
}

GPU_EXPORT
void
GPUEndPipelineStatisticsQuery(GPUCommandBuffer *cmdb, GPUQuerySet *set) {
  GPU__UNUSED(cmdb);
  GPU__UNUSED(set);
}

GPU_EXPORT
void
GPUResolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet      *set,
                   uint32_t          firstQuery,
                   uint32_t          queryCount,
                   GPUBuffer        *dstBuffer,
                   uint64_t          dstOffset) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !set || !dstBuffer || set->type != GPU_QUERY_TIMESTAMP ||
      !gpuBufferHasUsage(dstBuffer, GPU_BUFFER_USAGE_COPY_DST) ||
      !gpuBufferRangeValid(dstBuffer,
                           dstOffset,
                           (uint64_t)queryCount * sizeof(uint64_t)) ||
      queryCount == 0u || firstQuery > set->count ||
      queryCount > set->count - firstQuery) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->cmdbuf.resolveQuerySet) {
    return;
  }

  api->cmdbuf.resolveQuerySet(cmdb,
                              set,
                              firstQuery,
                              queryCount,
                              dstBuffer,
                              dstOffset);
}
