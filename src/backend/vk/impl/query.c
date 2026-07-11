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
#include "../../../api/buffer_internal.h"
#include "../../../api/query_internal.h"

typedef struct GPUQuerySetVk {
  VkDevice    device;
  VkQueryPool pool;
} GPUQuerySetVk;

#define VK__ASSERT_PIPESTAT(GPU_BIT, VK_BIT)                                  \
  _Static_assert((uint32_t)(GPU_BIT) == (uint32_t)(VK_BIT),                   \
                 #GPU_BIT " must match Vulkan")

VK__ASSERT_PIPESTAT(GPU_PIPESTAT_INPUT_ASSEMBLY_VERTICES,
                    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_INPUT_ASSEMBLY_PRIMITIVES,
                    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_VERTEX_SHADER_INVOCATIONS,
                    VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_GEOMETRY_SHADER_INVOCATIONS,
                    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_GEOMETRY_SHADER_PRIMITIVES,
                    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_CLIPPING_INVOCATIONS,
                    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_CLIPPING_PRIMITIVES,
                    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_FRAGMENT_SHADER_INVOCATIONS,
                    VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT);
VK__ASSERT_PIPESTAT(
  GPU_PIPESTAT_TESS_CONTROL_SHADER_PATCHES,
  VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT
);
VK__ASSERT_PIPESTAT(
  GPU_PIPESTAT_TESS_EVALUATION_SHADER_INVOCATIONS,
  VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT
);
VK__ASSERT_PIPESTAT(GPU_PIPESTAT_COMPUTE_SHADER_INVOCATIONS,
                    VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT);

#undef VK__ASSERT_PIPESTAT

static VkQueryPipelineStatisticFlags
vk_pipelineStatisticFlags(void) {
  return (VkQueryPipelineStatisticFlags)GPU_PIPESTAT_ALL;
}

GPU_HIDE
GPUResult
vk_createQuerySet(GPUDevice                  *device,
                  const GPUQuerySetCreateInfo *info,
                  GPUQuerySet                *set) {
  GPUDeviceVk          *deviceVk;
  GPUQuerySetVk        *native;
  VkQueryPoolCreateInfo queryInfo = {0};

  deviceVk = device ? device->_priv : NULL;
  if (!deviceVk || !deviceVk->device || !info || !set ||
      (info->type != GPU_QUERY_TIMESTAMP &&
       info->type != GPU_QUERY_PIPELINE_STATISTICS)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryInfo.queryCount = info->count;
  if (info->type == GPU_QUERY_PIPELINE_STATISTICS) {
    queryInfo.queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    queryInfo.pipelineStatistics = vk_pipelineStatisticFlags();
  } else {
    queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  }
  if (vkCreateQueryPool(deviceVk->device,
                        &queryInfo,
                        NULL,
                        &native->pool) != VK_SUCCESS) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->device = deviceVk->device;
  set->_priv      = native;
  return GPU_OK;
}

GPU_HIDE
void
vk_beginPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                                GPUQuerySet      *set,
                                uint32_t          queryIndex) {
  GPUCommandBufferVk *command;
  GPUQuerySetVk      *native;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!command || !command->command || !native || !native->pool) {
    return;
  }

  vkCmdResetQueryPool(command->command, native->pool, queryIndex, 1u);
  vkCmdBeginQuery(command->command, native->pool, queryIndex, 0u);
}

GPU_HIDE
void
vk_endPipelineStatisticsQuery(GPUCommandBuffer *cmdb,
                              GPUQuerySet      *set,
                              uint32_t          queryIndex) {
  GPUCommandBufferVk *command;
  GPUQuerySetVk      *native;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!command || !command->command || !native || !native->pool) {
    return;
  }

  vkCmdEndQuery(command->command, native->pool, queryIndex);
}

GPU_HIDE
void
vk_destroyQuerySet(GPUQuerySet *set) {
  GPUQuerySetVk *native;

  native = set ? set->_priv : NULL;
  if (!native) {
    return;
  }

  if (native->device && native->pool) {
    vkDestroyQueryPool(native->device, native->pool, NULL);
  }
  free(native);
  set->_priv = NULL;
}

GPU_HIDE
void
vk_writeTimestamp(GPUCommandBuffer *cmdb,
                  GPUQuerySet      *set,
                  uint32_t          queryIndex) {
  GPUCommandBufferVk *command;
  GPUQuerySetVk      *native;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!command || !command->command || !command->owner ||
      command->owner->timestampValidBits == 0u ||
      !native || !native->pool) {
    return;
  }

  vkCmdResetQueryPool(command->command, native->pool, queryIndex, 1u);
  vkCmdWriteTimestamp(command->command,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      native->pool,
                      queryIndex);
}

GPU_HIDE
void
vk_resolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet      *set,
                   uint32_t          firstQuery,
                   uint32_t          queryCount,
                   GPUBuffer        *dstBuffer,
                   uint64_t          dstOffset) {
  GPUCommandBufferVk *command;
  GPUQuerySetVk      *native;
  GPUBufferVk        *buffer;
  VkDeviceSize        resultStride;

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  buffer  = dstBuffer ? dstBuffer->_priv : NULL;
  if (!command || !command->command || !command->owner ||
      !native || !native->pool ||
      !buffer || !buffer->buffer) {
    return;
  }
  if (set->type == GPU_QUERY_TIMESTAMP) {
    if (command->owner->timestampValidBits == 0u) {
      return;
    }
    resultStride = sizeof(uint64_t);
  } else if (set->type == GPU_QUERY_PIPELINE_STATISTICS) {
    resultStride = sizeof(GPUPipelineStatisticsResult);
  } else {
    return;
  }

  vkCmdCopyQueryPoolResults(command->command,
                            native->pool,
                            firstQuery,
                            queryCount,
                            buffer->buffer,
                            dstOffset,
                            resultStride,
                            VK_QUERY_RESULT_64_BIT |
                            VK_QUERY_RESULT_WAIT_BIT);
}

GPU_HIDE
void
vk_initQuery(GPUApiCommandBuffer *api) {
  api->createQuerySet                  = vk_createQuerySet;
  api->destroyQuerySet                 = vk_destroyQuerySet;
  api->writeTimestamp                  = vk_writeTimestamp;
  api->beginPipelineStatisticsQuery    = vk_beginPipelineStatisticsQuery;
  api->endPipelineStatisticsQuery      = vk_endPipelineStatisticsQuery;
  api->resolveQuerySet                 = vk_resolveQuerySet;
}
