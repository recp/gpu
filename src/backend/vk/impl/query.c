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
      info->type != GPU_QUERY_TIMESTAMP) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
  queryInfo.queryCount = info->count;
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

  command = cmdb ? cmdb->_priv : NULL;
  native  = set ? set->_priv : NULL;
  buffer  = dstBuffer ? dstBuffer->_priv : NULL;
  if (!command || !command->command || !command->owner ||
      command->owner->timestampValidBits == 0u ||
      !native || !native->pool ||
      !buffer || !buffer->buffer) {
    return;
  }

  vkCmdCopyQueryPoolResults(command->command,
                            native->pool,
                            firstQuery,
                            queryCount,
                            buffer->buffer,
                            dstOffset,
                            sizeof(uint64_t),
                            VK_QUERY_RESULT_64_BIT |
                            VK_QUERY_RESULT_WAIT_BIT);
}

GPU_HIDE
void
vk_initQuery(GPUApiCommandBuffer *api) {
  api->createQuerySet  = vk_createQuerySet;
  api->destroyQuerySet = vk_destroyQuerySet;
  api->writeTimestamp  = vk_writeTimestamp;
  api->resolveQuerySet = vk_resolveQuerySet;
}
