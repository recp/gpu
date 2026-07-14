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

static bool
vk__bufferUsage(GPUBufferUsageFlags usage, VkBufferUsageFlags *outUsage) {
  const GPUBufferUsageFlags known = GPU_BUFFER_USAGE_VERTEX |
                                    GPU_BUFFER_USAGE_INDEX |
                                    GPU_BUFFER_USAGE_UNIFORM |
                                    GPU_BUFFER_USAGE_STORAGE |
                                    GPU_BUFFER_USAGE_COPY_SRC |
                                    GPU_BUFFER_USAGE_COPY_DST |
                                    GPU_BUFFER_USAGE_INDIRECT;
  VkBufferUsageFlags result;

  if (!outUsage || usage == 0u || (usage & ~known) != 0u) {
    return false;
  }

  result = 0u;
  if ((usage & GPU_BUFFER_USAGE_VERTEX) != 0u) {
    result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_INDEX) != 0u) {
    result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_UNIFORM) != 0u) {
    result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_STORAGE) != 0u) {
    result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_COPY_SRC) != 0u) {
    result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_COPY_DST) != 0u) {
    result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  if ((usage & GPU_BUFFER_USAGE_INDIRECT) != 0u) {
    result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  }

  *outUsage = result;
  return true;
}

GPU_HIDE
bool
vk_findMemoryType(GPUDevice             *device,
                  uint32_t               typeBits,
                  VkMemoryPropertyFlags  required,
                  VkMemoryPropertyFlags  preferred,
                  uint32_t              *outIndex,
                  VkMemoryPropertyFlags *outFlags) {
  GPUAdapterVk                     *adapterVk;
  VkPhysicalDeviceMemoryProperties properties;

  adapterVk = device && device->adapter ? device->adapter->_priv : NULL;
  if (!adapterVk || !outIndex || !outFlags) {
    return false;
  }

  vkGetPhysicalDeviceMemoryProperties(adapterVk->physicalDevice, &properties);
  for (uint32_t i = 0u; i < properties.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags;

    flags = properties.memoryTypes[i].propertyFlags;
    if ((typeBits & (1u << i)) != 0u &&
        (flags & required) == required &&
        (flags & preferred) == preferred) {
      *outIndex = i;
      *outFlags = flags;
      return true;
    }
  }

  for (uint32_t i = 0u; i < properties.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags;

    flags = properties.memoryTypes[i].propertyFlags;
    if ((typeBits & (1u << i)) != 0u &&
        (flags & required) == required) {
      *outIndex = i;
      *outFlags = flags;
      return true;
    }
  }

  return false;
}

static void
vk__destroyBufferState(GPUBufferVk *native) {
  if (!native || !native->device) {
    return;
  }

  if (native->mapped && native->memory) {
    vkUnmapMemory(native->device, native->memory);
  }
  if (native->buffer) {
    vkDestroyBuffer(native->device, native->buffer, NULL);
  }
  if (native->memory) {
    vkFreeMemory(native->device, native->memory, NULL);
  }
}

static GPUResult
vk__createBuffer(GPUDevice                 * __restrict device,
                 const GPUBufferCreateInfo * __restrict info,
                 bool                                   hostVisible,
                 GPUBuffer                ** __restrict outBuffer) {
  GPUDeviceVk             *deviceVk;
  GPUBuffer               *buffer;
  GPUBufferVk             *native;
  GPUBufferVk              state = {0};
  VkBufferCreateInfo       bufferInfo = {0};
  VkMemoryAllocateInfo     allocationInfo = {0};
  VkMemoryRequirements     requirements;
  VkMemoryPropertyFlags    memoryFlags;
  VkMemoryPropertyFlags    preferredFlags;
  VkMemoryPropertyFlags    requiredFlags;
  uint32_t                 memoryTypeIndex;

  if (!device || !device->_priv || !info || !outBuffer ||
      info->sizeBytes == 0u ||
      !vk__bufferUsage(info->usage, &bufferInfo.usage)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBuffer                = NULL;
  deviceVk                  = device->_priv;
  state.device              = deviceVk->device;
  bufferInfo.sType          = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size           = info->sizeBytes;
  bufferInfo.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(state.device,
                     &bufferInfo,
                     NULL,
                     &state.buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(state.device, state.buffer, &requirements);
  requiredFlags  = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                               : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  preferredFlags = hostVisible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0u;
  if (!vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         requiredFlags,
                         preferredFlags,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_UNSUPPORTED;
  }

  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(state.device,
                       &allocationInfo,
                       NULL,
                       &state.memory) != VK_SUCCESS ||
      vkBindBufferMemory(state.device,
                         state.buffer,
                         state.memory,
                         0u) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (hostVisible &&
      vkMapMemory(state.device,
                  state.memory,
                  0u,
                  VK_WHOLE_SIZE,
                  0u,
                  &state.mapped) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state.allocationSize = requirements.size;
  state.coherent       = hostVisible &&
                         (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;

  buffer = calloc(1, sizeof(*buffer) + sizeof(*native));
  if (!buffer) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native            = (GPUBufferVk *)(buffer + 1);
  *native           = state;
  buffer->_priv     = native;
  buffer->device    = device;
  buffer->sizeBytes = info->sizeBytes;
  buffer->usage     = info->usage;
  *outBuffer        = buffer;
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_createBuffer(GPUDevice                 * __restrict device,
                const GPUBufferCreateInfo * __restrict info,
                GPUBuffer                ** __restrict outBuffer) {
  const GPUBufferUsageFlags deviceLocal = GPU_BUFFER_USAGE_STORAGE |
                                          GPU_BUFFER_USAGE_COPY_DST;
  GPUResult result;
  bool hostVisible;

  hostVisible = info && (info->usage & deviceLocal) == 0u;
  result      = vk__createBuffer(device, info, hostVisible, outBuffer);
  if (result == GPU_ERROR_UNSUPPORTED && !hostVisible) {
    result = vk__createBuffer(device, info, true, outBuffer);
  }
  return result;
}

GPU_HIDE
GPUResult
vk_createHostBuffer(GPUDevice                 * __restrict device,
                    const GPUBufferCreateInfo * __restrict info,
                    GPUBuffer                ** __restrict outBuffer) {
  return vk__createBuffer(device, info, true, outBuffer);
}

GPU_HIDE
void
vk_destroyBuffer(GPUBuffer * __restrict buffer) {
  if (!buffer) {
    return;
  }

  vk__destroyBufferState(buffer->_priv);
  free(buffer);
}

static void
vk__bufferBarrier(VkCommandBuffer       command,
                  VkBuffer              buffer,
                  VkDeviceSize          offset,
                  VkDeviceSize          size,
                  VkPipelineStageFlags  srcStages,
                  VkPipelineStageFlags  dstStages,
                  VkAccessFlags         srcAccess,
                  VkAccessFlags         dstAccess) {
  VkBufferMemoryBarrier barrier = {0};

  barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask       = srcAccess;
  barrier.dstAccessMask       = dstAccess;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer              = buffer;
  barrier.offset              = offset;
  barrier.size                = size;
  vkCmdPipelineBarrier(command,
                       srcStages,
                       dstStages,
                       0u,
                       0u,
                       NULL,
                       1u,
                       &barrier,
                       0u,
                       NULL);
}

GPU_HIDE
GPUResult
vk_writeBuffer(GPUQueue * __restrict queue,
               GPUBuffer       * __restrict buffer,
               uint64_t                     dstOffset,
               const void      * __restrict data,
               uint64_t                     sizeBytes) {
  VkCommandBuffer       command;
  GPUBuffer            *staging;
  GPUBufferVk          *stagingVk;
  GPUBufferVk          *native;
  VkBufferCopy          copy = {0};
  VkMappedMemoryRange   range = {0};
  uint64_t              stagingOffset;
  GPUResult             result;

  native = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !native || !native->buffer || !data || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, dstOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (native->mapped) {
    memcpy((uint8_t *)native->mapped + dstOffset, data, (size_t)sizeBytes);
    if (!native->coherent) {
      range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.memory = native->memory;
      range.size   = VK_WHOLE_SIZE;
      if (vkFlushMappedMemoryRanges(native->device, 1u, &range) != VK_SUCCESS) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    return GPU_OK;
  }

  result = vk_beginTransfer(queue,
                            true,
                            sizeBytes,
                            GPU_VK_BUFFER_TRANSFER_CAPACITY,
                            &command,
                            &staging,
                            &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }

  stagingVk = staging ? staging->_priv : NULL;
  if (!stagingVk || !stagingVk->mapped || !stagingVk->buffer) {
    vk_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  memcpy((uint8_t *)stagingVk->mapped + stagingOffset,
         data,
         (size_t)sizeBytes);
  if (!stagingVk->coherent) {
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = stagingVk->memory;
    range.size   = VK_WHOLE_SIZE;
    if (vkFlushMappedMemoryRanges(stagingVk->device,
                                  1u,
                                  &range) != VK_SUCCESS) {
      vk_abortTransfer(queue);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk__bufferBarrier(command,
                    native->buffer,
                    dstOffset,
                    sizeBytes,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT);

  copy.srcOffset = stagingOffset;
  copy.dstOffset = dstOffset;
  copy.size      = sizeBytes;
  vkCmdCopyBuffer(command, stagingVk->buffer, native->buffer, 1u, &copy);

  vk__bufferBarrier(command,
                    native->buffer,
                    dstOffset,
                    sizeBytes,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  return vk_submitTransfer(queue, false);
}

GPU_HIDE
GPUResult
vk_readBuffer(GPUQueue * __restrict queue,
              GPUBuffer       * __restrict buffer,
              uint64_t                     srcOffset,
              void           * __restrict outData,
              uint64_t                     sizeBytes) {
  VkCommandBuffer       command;
  GPUBuffer            *staging;
  GPUBufferVk          *stagingVk;
  GPUBufferVk          *native;
  VkBufferCopy          copy = {0};
  VkMappedMemoryRange   range = {0};
  uint64_t              stagingOffset;
  GPUResult             result;

  native = buffer ? buffer->_priv : NULL;
  if (!queue || !buffer || queue->_device != buffer->device ||
      !native || !native->buffer || !outData || sizeBytes > SIZE_MAX ||
      !gpuBufferRangeValid(buffer, srcOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (native->mapped) {
    if (!native->coherent) {
      range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.memory = native->memory;
      range.size   = VK_WHOLE_SIZE;
      if (vkInvalidateMappedMemoryRanges(native->device,
                                         1u,
                                         &range) != VK_SUCCESS) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }
    memcpy(outData,
           (const uint8_t *)native->mapped + srcOffset,
           (size_t)sizeBytes);
    return GPU_OK;
  }

  result = vk_beginTransfer(queue,
                            false,
                            sizeBytes,
                            GPU_VK_BUFFER_TRANSFER_CAPACITY,
                            &command,
                            &staging,
                            &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }

  stagingVk = staging ? staging->_priv : NULL;
  if (!stagingVk || !stagingVk->mapped || !stagingVk->buffer) {
    vk_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk__bufferBarrier(command,
                    native->buffer,
                    srcOffset,
                    sizeBytes,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);

  copy.srcOffset = srcOffset;
  copy.dstOffset = stagingOffset;
  copy.size      = sizeBytes;
  vkCmdCopyBuffer(command, native->buffer, stagingVk->buffer, 1u, &copy);

  vk__bufferBarrier(command,
                    stagingVk->buffer,
                    stagingOffset,
                    sizeBytes,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_HOST_READ_BIT);

  result = vk_submitTransfer(queue, true);
  if (result != GPU_OK) {
    return result;
  }
  if (!stagingVk->coherent) {
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = stagingVk->memory;
    range.size   = VK_WHOLE_SIZE;
    if (vkInvalidateMappedMemoryRanges(stagingVk->device,
                                       1u,
                                       &range) != VK_SUCCESS) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  memcpy(outData,
         (const uint8_t *)stagingVk->mapped + stagingOffset,
         (size_t)sizeBytes);
  return GPU_OK;
}

GPU_HIDE
void*
vk_bufferContents(GPUBuffer * __restrict buffer) {
  GPUBufferVk *native;

  native = buffer ? buffer->_priv : NULL;
  return native && native->coherent ? native->mapped : NULL;
}

GPU_HIDE
void
vk_initBuff(GPUApiBuffer *api) {
  api->create   = vk_createBuffer;
  api->destroy  = vk_destroyBuffer;
  api->write    = vk_writeBuffer;
  api->read     = vk_readBuffer;
  api->contents = vk_bufferContents;
}
