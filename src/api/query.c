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
#include "device_internal.h"
#include "query_internal.h"

static bool
gpu_validQueryType(GPUQueryType type) {
  return type == GPU_QUERY_TIMESTAMP ||
         type == GPU_QUERY_OCCLUSION ||
         type == GPU_QUERY_PIPELINE_STATISTICS;
}

static bool
gpu_validPipelineStatsMask(uint32_t mask) {
  return mask != 0u && (mask & ~GPU_PIPESTAT_ALL) == 0u;
}

static uint64_t
gpu_queryResultStride(const GPUQuerySet *set) {
  if (!set) {
    return 0u;
  }

  switch (set->type) {
    case GPU_QUERY_TIMESTAMP:
    case GPU_QUERY_OCCLUSION:
      return sizeof(uint64_t);
    case GPU_QUERY_PIPELINE_STATISTICS:
      return sizeof(GPUPipelineStatisticsResult);
    default:
      return 0u;
  }
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
  GPUApi      *api;
  GPUQuerySet *set;
  GPUResult    result;

  if (!outSet) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outSet = NULL;

  if (!device || !gpu_validQueryCreateInfo(info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->type == GPU_QUERY_TIMESTAMP &&
      !GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (info->type == GPU_QUERY_PIPELINE_STATISTICS &&
      !GPUIsFeatureEnabled(device, GPU_FEATURE_PIPELINE_STATISTICS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdbuf.createQuerySet) {
    return GPU_ERROR_UNSUPPORTED;
  }

  set = calloc(1, sizeof(*set));
  if (!set) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  set->device            = device;
  set->type              = info->type;
  set->count             = info->count;
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

  if ((api = gpuDeviceApi(set->device)) && api->cmdbuf.destroyQuerySet) {
    api->cmdbuf.destroyQuerySet(set);
  }
  free(set);
}

GPU_EXPORT
GPUResult
GPUGetTimestampPeriod(GPUCommandQueue *queue,
                      double          *outNanosecondsPerTick) {
  GPUDevice *device;
  GPUResult  result;
  GPUApi    *api;

  if (!outNanosecondsPerTick) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outNanosecondsPerTick = 0.0;

  device = queue ? queue->_device : NULL;
  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TIMESTAMPS)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdque.getTimestampPeriod) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->cmdque.getTimestampPeriod(queue, outNanosecondsPerTick);
  if (result != GPU_OK) {
    *outNanosecondsPerTick = 0.0;
    return result;
  }
  if (!(*outNanosecondsPerTick > 0.0)) {
    *outNanosecondsPerTick = 0.0;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return GPU_OK;
}

GPU_EXPORT
void
GPUWriteTimestamp(GPUCommandBuffer *cmdb,
                  GPUQuerySet      *set,
                  uint32_t          queryIndex) {
  GPUDevice *device;
  GPUApi    *api;

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !set || set->device != device || set->type != GPU_QUERY_TIMESTAMP ||
      queryIndex >= set->count) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdbuf.writeTimestamp) {
    return;
  }

  api->cmdbuf.writeTimestamp(cmdb, set, queryIndex);
}

GPU_EXPORT
void
GPUBeginOcclusionQuery(GPURenderPassEncoder *pass,
                       GPUQuerySet          *set,
                       uint32_t              queryIndex) {
  GPUDevice *device;
  GPUApi    *api;

  device = pass && pass->_cmdb && pass->_cmdb->_queue
             ? pass->_cmdb->_queue->_device
             : NULL;
  if (!pass || pass->_ended || pass->_occlusionQueryActive ||
      !set || pass->_occlusionQuerySet != set || set->device != device ||
      set->type != GPU_QUERY_OCCLUSION || queryIndex >= set->count) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdbuf.beginOcclusionQuery) {
    return;
  }

  pass->_occlusionQueryIndex  = queryIndex;
  pass->_occlusionQueryActive = true;
  api->cmdbuf.beginOcclusionQuery(pass, set, queryIndex);
}

GPU_EXPORT
void
GPUEndOcclusionQuery(GPURenderPassEncoder *pass) {
  GPUQuerySet *set;
  GPUDevice   *device;
  GPUApi      *api;
  uint32_t     queryIndex;

  set    = pass ? pass->_occlusionQuerySet : NULL;
  device = pass && pass->_cmdb && pass->_cmdb->_queue
             ? pass->_cmdb->_queue->_device
             : NULL;
  if (!pass || pass->_ended || !pass->_occlusionQueryActive ||
      !set || set->device != device || set->type != GPU_QUERY_OCCLUSION) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdbuf.endOcclusionQuery) {
    return;
  }

  queryIndex = pass->_occlusionQueryIndex;
  api->cmdbuf.endOcclusionQuery(pass, set, queryIndex);
  pass->_occlusionQueryIndex  = 0u;
  pass->_occlusionQueryActive = false;
}

GPU_EXPORT
void
GPUBeginPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                GPUQuerySet      *set,
                                uint32_t          queryIndex) {
  GPUDevice *device;
  GPUApi    *api;

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      cmdb->_pipelineStatsQuery || !set || set->device != device ||
      set->type != GPU_QUERY_PIPELINE_STATISTICS || queryIndex >= set->count ||
      !(cmdb->_queue->bits & (GPU_QUEUE_GRAPHICS | GPU_QUEUE_COMPUTE))) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) ||
      !api->cmdbuf.beginPipelineStatisticsQuery) {
    return;
  }

  cmdb->_pipelineStatsQuery      = set;
  cmdb->_pipelineStatsQueryIndex = queryIndex;
  api->cmdbuf.beginPipelineStatisticsQuery(cmdb, set, queryIndex);
}

GPU_EXPORT
void
GPUEndPipelineStatisticsQuery(GPUCommandBuffer *cmdb, GPUQuerySet *set) {
  GPUDevice *device;
  GPUApi    *api;
  uint32_t   queryIndex;

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      !set || cmdb->_pipelineStatsQuery != set || set->device != device ||
      set->type != GPU_QUERY_PIPELINE_STATISTICS) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) ||
      !api->cmdbuf.endPipelineStatisticsQuery) {
    return;
  }

  queryIndex = cmdb->_pipelineStatsQueryIndex;
  api->cmdbuf.endPipelineStatisticsQuery(cmdb, set, queryIndex);
  cmdb->_pipelineStatsQuery      = NULL;
  cmdb->_pipelineStatsQueryIndex = 0u;
}

GPU_EXPORT
void
GPUResolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet      *set,
                   uint32_t          firstQuery,
                   uint32_t          queryCount,
                   GPUBuffer        *dstBuffer,
                   uint64_t          dstOffset) {
  GPUDevice *device;
  GPUApi    *api;
  uint64_t   resultBytes;
  uint64_t   resultStride;

  device = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder ||
      cmdb->_pipelineStatsQuery || !set || !dstBuffer ||
      (set->type != GPU_QUERY_TIMESTAMP &&
       set->type != GPU_QUERY_OCCLUSION &&
       set->type != GPU_QUERY_PIPELINE_STATISTICS) ||
      set->device != device || dstBuffer->device != device ||
      !gpuBufferHasUsage(dstBuffer, GPU_BUFFER_USAGE_COPY_DST) ||
      (dstOffset & 7u) != 0u ||
      queryCount == 0u || firstQuery > set->count ||
      queryCount > set->count - firstQuery) {
    return;
  }
  resultStride = gpu_queryResultStride(set);
  resultBytes  = (uint64_t)queryCount * resultStride;
  if (resultStride == 0u ||
      !gpuBufferRangeValid(dstBuffer, dstOffset, resultBytes)) {
    return;
  }
  if (!(api = gpuDeviceApi(device)) || !api->cmdbuf.resolveQuerySet) {
    return;
  }

  api->cmdbuf.resolveQuerySet(cmdb,
                              set,
                              firstQuery,
                              queryCount,
                              dstBuffer,
                              dstOffset);
}
