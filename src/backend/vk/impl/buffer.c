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
#include "../impl.h"
#include "../../../api/buffer_internal.h"
#include "../../../api/suballoc_internal.h"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static void
vk__destroyBufferState(GPUBufferVk *native);

static bool
vk__bufferUsage(GPUBufferUsageFlags usage, VkBufferUsageFlags *outUsage) {
  const GPUBufferUsageFlags known = GPU_BUFFER_USAGE_VERTEX |
                                    GPU_BUFFER_USAGE_INDEX |
                                    GPU_BUFFER_USAGE_UNIFORM |
                                    GPU_BUFFER_USAGE_STORAGE |
                                    GPU_BUFFER_USAGE_COPY_SRC |
                                    GPU_BUFFER_USAGE_COPY_DST |
                                    GPU_BUFFER_USAGE_INDIRECT |
                                    GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT |
                                    GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT |
                                    GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT |
                                    GPU_BUFFER_USAGE_HOST_READ |
                                    GPU_BUFFER_USAGE_TEXEL;
  VkBufferUsageFlags        result;

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

  if ((usage & GPU_BUFFER_USAGE_TEXEL) != 0u) {
    result |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
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

  if ((usage & GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT) != 0u) {
    result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  if ((usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT) != 0u) {
    result |=
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }

  if ((usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT) != 0u) {
    result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }
#else
  if ((usage &
       (GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT |
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT)) != 0u) {
    return false;
  }
#endif

  *outUsage = result;

  return true;
}

static void
vk__destroyBufferState(GPUBufferVk *native) {
  if (!native || !native->device) {
    return;
  }

  if (native->ownsMemory && native->mapped && native->memory && !native->imported) {
    vkUnmapMemory(native->device, native->memory);
  }

  if (native->buffer) {
    vkDestroyBuffer(native->device, native->buffer, NULL);
  }

  vk_poolRelease(&native->poolRange);

  if (native->ownsMemory && native->memory) {
    vkFreeMemory(native->device, native->memory, NULL);
  }
}

/* the memory type of a buffer living in 'memory' (VKBufferMemory), its
 * memoryTypeBits given; shared with the pool's warm-up so both pick alike */
GPU_HIDE
bool
vk_bufferMemoryType(GPUDevice             *device,
                    uint32_t               typeBits,
                    VKBufferMemory         memory,
                    uint32_t              *outIndex,
                    VkMemoryPropertyFlags *outFlags) {
  bool hostVisible;

  hostVisible = memory != VK_BUFFER_DEVICE;

  /* read through the mapping with no invalidate: coherent always (every
   * device has such a type), cached where one is also coherent */
  if (memory == VK_BUFFER_HOST_READBACK)
    return vk_findMemoryType(device,
                             typeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                             outIndex,
                             outFlags);

  if (memory == VK_BUFFER_HOST_READ
      && vk_findMemoryType(device,
                           typeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           outIndex,
                           outFlags))
    return true;

  /* an integrated GPU's memory is the host's: a device buffer there where the host sees it too, as every buffer is
   * on Metal, so a write into a new one is a copy into its mapping, not a staging buffer, a copy command and a
   * submit (vk_writeBuffer keeps the queue's order for one in use) */
  {
    GPUAdapterVk *adapterVk = device && device->adapter ? device->adapter->_priv : NULL;
    bool          unified   = !hostVisible && adapterVk
                              && adapterVk->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    return vk_findMemoryType(device,
                             typeBits,
                             hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             hostVisible ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                             : unified   ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                         : 0u,
                             outIndex,
                             outFlags);
  }
}

static GPUResult
vk__createBuffer(GPUDevice                 *__restrict device,
                 const GPUBufferCreateInfo *__restrict info,
                 VKBufferMemory                        memory,
                 GPUBuffer                **__restrict outBuffer) {
  GPUBufferVk               state           = {0};
  VkBufferCreateInfo        bufferInfo      = {0};
  VkMemoryAllocateInfo      allocationInfo  = {0};
  VkMemoryAllocateFlagsInfo allocationFlags = {0};
  VkMemoryRequirements      requirements;
  GPUDeviceVk              *deviceVk;
  VkMemoryPropertyFlags     memoryFlags;
  uint32_t                  memoryTypeIndex;
  bool                      hostVisible, mapped;

  if (!device || !device->_priv || !info || !outBuffer
      || info->sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBuffer = NULL;
  deviceVk   = device->_priv;

  if (vk_bufferCreateInfo(device, info, &bufferInfo) != GPU_OK) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  state.device = deviceVk->device;

  if (vkCreateBuffer(state.device,
                     &bufferInfo,
                     NULL,
                     &state.buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(state.device, state.buffer, &requirements);
  hostVisible = memory != VK_BUFFER_DEVICE;

  if (!vk_bufferMemoryType(device, requirements.memoryTypeBits, memory, &memoryTypeIndex, &memoryFlags)) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_UNSUPPORTED;
  }

  mapped             = hostVisible || (memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u;
  state.deviceMapped = !hostVisible && mapped;
  state.stagedReads  = state.deviceMapped && (memoryFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == 0u;

  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;

  if ((bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u) {
    if (!deviceVk->bufferDeviceAddress
        || !deviceVk->getBufferDeviceAddress) {
      vk__destroyBufferState(&state);
      return GPU_ERROR_UNSUPPORTED;
    }

    allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocationInfo.pNext  = &allocationFlags;
  }

  if (vk_poolBindBuffer(deviceVk, state.buffer, memoryTypeIndex, memoryFlags,
                        (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u, mapped,
                        &state.memory, &state.mapped, &state.allocationSize, &state.poolRange)) {
    state.coherent = mapped && (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
    return vk_wrapBuffer(device, info, &bufferInfo, &state, outBuffer);
  }

  if (vkAllocateMemory(state.device,
                       &allocationInfo,
                       NULL,
                       &state.memory) != VK_SUCCESS
      || vkBindBufferMemory(state.device,
                            state.buffer,
                            state.memory,
                            0u) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (mapped
      && vkMapMemory(state.device,
                     state.memory,
                     0u,
                     VK_WHOLE_SIZE,
                     0u,
                     &state.mapped) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state.allocationSize = requirements.size;
  state.coherent       = mapped
                         && (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
  state.ownsMemory     = true;

  return vk_wrapBuffer(device, info, &bufferInfo, &state, outBuffer);
}

static void
vk__bufferBarrier(GPUDeviceVk         *device,
                  VkCommandBuffer      command,
                  VkBuffer             buffer,
                  VkDeviceSize         offset,
                  VkDeviceSize         size,
                  VkPipelineStageFlags srcStages,
                  VkPipelineStageFlags dstStages,
                  VkAccessFlags        srcAccess,
                  VkAccessFlags        dstAccess) {
  VkBufferMemoryBarrier barrier = {0};

  barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask       = srcAccess;
  barrier.dstAccessMask       = dstAccess;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer              = buffer;
  barrier.offset              = offset;
  barrier.size                = size;
  vk_pipelineBarrier(device,
                     command,
                     srcStages,
                     dstStages,
                     1u,
                     &barrier,
                     0u,
                     NULL);
}

GPU_HIDE
bool
vk_findMemoryType(GPUDevice             *device,
                  uint32_t               typeBits,
                  VkMemoryPropertyFlags  required,
                  VkMemoryPropertyFlags  preferred,
                  uint32_t              *outIndex,
                  VkMemoryPropertyFlags *outFlags) {
  VkPhysicalDeviceMemoryProperties properties;
  GPUAdapterVk                    *adapterVk;
  uint32_t                         i;
  VkMemoryPropertyFlags            flags;
  uint32_t                         j;
  VkMemoryPropertyFlags            fallbackFlags;

  adapterVk = device && device->adapter ? device->adapter->_priv : NULL;

  if (!adapterVk || !outIndex || !outFlags) {
    return false;
  }

  vkGetPhysicalDeviceMemoryProperties(adapterVk->physicalDevice, &properties);

  for (i = 0u; i < properties.memoryTypeCount; i++) {
    flags = properties.memoryTypes[i].propertyFlags;

    if ((typeBits & (1u << i)) != 0u
        && (flags & required) == required
        && (flags & preferred) == preferred) {
      *outIndex = i;
      *outFlags = flags;

      return true;
    }
  }

  for (j = 0u; j < properties.memoryTypeCount; j++) {
    fallbackFlags = properties.memoryTypes[j].propertyFlags;

    if ((typeBits & (1u << j)) != 0u
        && (fallbackFlags & required) == required) {
      *outIndex = j;
      *outFlags = fallbackFlags;

      return true;
    }
  }

  return false;
}

GPU_HIDE
GPUResult
vk_bufferCreateInfo(GPUDevice                 *device,
                    const GPUBufferCreateInfo *info,
                    VkBufferCreateInfo        *outInfo) {
  GPUDeviceVk *deviceVk;

  if (!device || !(deviceVk = device->_priv) || !info || !outInfo
      || !vk__bufferUsage(info->usage, &outInfo->usage)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (deviceVk->descriptorBuffer
      && (info->usage & (GPU_BUFFER_USAGE_UNIFORM |
                      GPU_BUFFER_USAGE_STORAGE)) != 0u) {
    outInfo->usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  }

  /* texel reads view a read-only storage buffer as texels */
  if (device->uslTexelReads && (info->usage & GPU_BUFFER_USAGE_STORAGE) != 0u) {
    outInfo->usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
  }

  /* whole 32-bit words: bindings round their ranges up to one (descriptor.c) */
  outInfo->sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  outInfo->size        = (info->sizeBytes + 3u) & ~(VkDeviceSize)3u;
  outInfo->sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_getBufferMemoryRequirements(GPUDevice                 *device,
                               const GPUBufferCreateInfo *info,
                               GPUMemoryRequirements     *outRequirements) {
  VkBufferCreateInfo   bufferInfo = {0};
  VkMemoryRequirements requirements;
  GPUDeviceVk         *deviceVk;
  VkBuffer             buffer;
  uint32_t             memoryTypes;
  GPUResult            result;

  if (!device || !(deviceVk = device->_priv) || !info || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vk_bufferCreateInfo(device, info, &bufferInfo);

  if (result != GPU_OK) {
    return result;
  }

  if (vkCreateBuffer(deviceVk->device,
                     &bufferInfo,
                     NULL,
                     &buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(deviceVk->device, buffer, &requirements);
  vkDestroyBuffer(deviceVk->device, buffer, NULL);

  memoryTypes = vk_filterMemoryTypes(device, requirements.memoryTypeBits);

  if (memoryTypes == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->sizeBytes         = requirements.size;
  outRequirements->alignmentBytes    = requirements.alignment;
  outRequirements->compatibilityMask = memoryTypes;

  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_createPlacedBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      uint64_t                   heapOffset,
                      GPUBuffer                **outBuffer) {
  VkBufferCreateInfo        bufferInfo  = {0};
  VkBufferDeviceAddressInfo addressInfo = {0};
  VkMemoryRequirements      requirements;
  GPUDeviceVk              *deviceVk;
  GPUHeapVk                *heapVk;
  GPUBuffer                *buffer;
  GPUBufferVk              *native;
  GPUResult                 result;

  if (!device || !(deviceVk = device->_priv) || !info || !heap
      || !(heapVk = heap->_priv) || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vk_bufferCreateInfo(device, info, &bufferInfo);

  if (result != GPU_OK) {
    return result;
  }

  if (!(buffer = calloc(1, sizeof(*buffer) + sizeof(*native)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUBufferVk *)(buffer + 1);
  native->device = deviceVk->device;

  if (vkCreateBuffer(native->device,
                     &bufferInfo,
                     NULL,
                     &native->buffer) != VK_SUCCESS) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(native->device,
                                native->buffer,
                                &requirements);

  if ((requirements.memoryTypeBits &
       (1u << heapVk->memoryTypeIndex)) == 0u
      || vkBindBufferMemory(native->device,
                            native->buffer,
                            heapVk->memory,
                            heapOffset) != VK_SUCCESS) {
    vkDestroyBuffer(native->device, native->buffer, NULL);
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->memory         = heapVk->memory;
  native->allocationSize = requirements.size;
  native->mapped         = heapVk->mapped
                             ? (uint8_t *)heapVk->mapped + heapOffset
                             : NULL;
  native->coherent       = heapVk->coherent;
  native->ownsMemory     = false;
  buffer->_priv          = native;
  buffer->device         = device;
  buffer->sizeBytes      = info->sizeBytes;
  buffer->usage          = info->usage;

  if ((bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u) {
    if (!deviceVk->bufferDeviceAddress
        || !deviceVk->getBufferDeviceAddress) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_UNSUPPORTED;
    }

    addressInfo.sType   = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer  = native->buffer;
    buffer->_gpuAddress = deviceVk->getBufferDeviceAddress(deviceVk->device,
                                                           &addressInfo);

    if (buffer->_gpuAddress == 0u) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk_setDebugName(device,
                  VK_OBJECT_TYPE_BUFFER,
                  (uint64_t)native->buffer,
                  info->label);

  *outBuffer = buffer;

  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_getSparseBufferRequirements(GPUDevice                   *device,
                               const GPUBufferCreateInfo   *info,
                               GPUSparseBufferRequirements *outRequirements) {
  VkBufferCreateInfo   bufferInfo = {0};
  VkMemoryRequirements requirements;
  GPUDeviceVk         *deviceVk;
  VkBuffer             buffer;
  uint32_t             memoryTypes;
  GPUResult            result;

  if (!device || !(deviceVk = device->_priv) || !info || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vk_bufferCreateInfo(device, info, &bufferInfo);

  if (result != GPU_OK) {
    return result;
  }

  bufferInfo.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                     VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;

  if (vkCreateBuffer(deviceVk->device,
                     &bufferInfo,
                     NULL,
                     &buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(deviceVk->device, buffer, &requirements);
  vkDestroyBuffer(deviceVk->device, buffer, NULL);

  memoryTypes = vk_filterMemoryTypes(device, requirements.memoryTypeBits);

  if (memoryTypes == 0u || requirements.alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->compatibilityMask = memoryTypes;
  outRequirements->pageSizeBytes     = requirements.alignment;
  outRequirements->tileCount         = requirements.size / requirements.alignment +
    (requirements.size % requirements.alignment != 0u);

  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_createSparseBuffer(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      GPUHeap                   *heap,
                      GPUBuffer                **outBuffer) {
  VkBufferCreateInfo          bufferInfo  = {0};
  VkBufferDeviceAddressInfo   addressInfo = {0};
  VkMemoryRequirements        requirements;
  GPUSparseBufferRequirements sparseRequirements;
  GPUDeviceVk                *deviceVk;
  GPUHeapVk                  *heapVk;
  GPUBuffer                  *buffer;
  GPUBufferVk                *native;
  GPUResult                   result;

  if (!device || !(deviceVk = device->_priv) || !info || !heap
      || !(heapVk = heap->_priv) || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = vk_bufferCreateInfo(device, info, &bufferInfo);

  if (result != GPU_OK) {
    return result;
  }

  result = vk_getSparseBufferRequirements(device,
                                          info,
                                          &sparseRequirements);

  if (result != GPU_OK
      || sparseRequirements.tileCount >
        UINT64_MAX / sparseRequirements.pageSizeBytes) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }

  bufferInfo.size  = sparseRequirements.tileCount *
                    sparseRequirements.pageSizeBytes;
  bufferInfo.flags = VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                     VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;

  if (!(buffer = calloc(1, sizeof(*buffer) + sizeof(*native)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUBufferVk *)(buffer + 1);
  native->device = deviceVk->device;

  if (vkCreateBuffer(native->device,
                     &bufferInfo,
                     NULL,
                     &native->buffer) != VK_SUCCESS) {
    free(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(native->device,
                                native->buffer,
                                &requirements);

  if ((requirements.memoryTypeBits &
       (1u << heapVk->memoryTypeIndex)) == 0u
      || requirements.alignment != sparseRequirements.pageSizeBytes) {
    vk__destroyBufferState(native);
    free(buffer);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  native->allocationSize = requirements.size;
  native->ownsMemory     = false;
  native->sparse         = true;
  buffer->_priv          = native;
  buffer->device         = device;
  buffer->sizeBytes      = info->sizeBytes;
  buffer->usage          = info->usage;

  if ((bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u) {
    if (!deviceVk->bufferDeviceAddress
        || !deviceVk->getBufferDeviceAddress) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_UNSUPPORTED;
    }

    addressInfo.sType   = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer  = native->buffer;
    buffer->_gpuAddress = deviceVk->getBufferDeviceAddress(deviceVk->device,
                                                           &addressInfo);

    if (buffer->_gpuAddress == 0u) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk_setDebugName(device,
                  VK_OBJECT_TYPE_BUFFER,
                  (uint64_t)native->buffer,
                  info->label);

  *outBuffer = buffer;

  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_wrapBuffer(GPUDevice                 *device,
              const GPUBufferCreateInfo *info,
              const VkBufferCreateInfo  *bufferInfo,
              GPUBufferVk               *state,
              GPUBuffer                **outBuffer) {
  VkBufferDeviceAddressInfo addressInfo = {0};
  GPUDeviceVk              *deviceVk;
  GPUBuffer                *buffer;
  GPUBufferVk              *native;

  deviceVk = device ? device->_priv : NULL;

  if (!deviceVk || !info || !bufferInfo || !state || !state->buffer
      || !state->memory || !outBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outBuffer = NULL;

  if (!(buffer = calloc(1, sizeof(*buffer) + sizeof(*native)))) {
    vk__destroyBufferState(state);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native            = (GPUBufferVk *)(buffer + 1);
  *native           = *state;
  buffer->_priv     = native;
  buffer->device    = device;
  buffer->sizeBytes = info->sizeBytes;
  buffer->usage     = info->usage;

  if ((bufferInfo->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u) {
    if (!deviceVk->bufferDeviceAddress
        || !deviceVk->getBufferDeviceAddress) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_UNSUPPORTED;
    }

    addressInfo.sType   = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer  = state->buffer;
    buffer->_gpuAddress = deviceVk->getBufferDeviceAddress(deviceVk->device,
                                                           &addressInfo);

    if (buffer->_gpuAddress == 0u) {
      vk__destroyBufferState(native);
      free(buffer);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk_setDebugName(device,
                  VK_OBJECT_TYPE_BUFFER,
                  (uint64_t)native->buffer,
                  info->label);

  *outBuffer = buffer;

  return GPU_OK;
}

#ifdef VK_EXT_external_memory_host
static size_t
vk__hostPageSize(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;

  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  long size = sysconf(_SC_PAGESIZE);

  return size > 0 ? (size_t)size : 0u;
#endif
}

/* the caller's pages as a buffer (GPUBufferHostMemoryEXT, VK_EXT_external_memory_host): the GPU reads and writes
 * them in place, nothing copied; imported into a host-coherent type the pointer allows, so what the GPU wrote is
 * read there once its work is waited for (none such: unsupported, the caller copies). The pages stay the caller's,
 * freed by them after the buffer is destroyed */
static GPUResult
vk__importBuffer(GPUDevice                    *__restrict device,
                 const GPUBufferCreateInfo    *__restrict info,
                 const GPUBufferHostMemoryEXT *__restrict host,
                 GPUBuffer                   **__restrict outBuffer) {
  GPUBufferVk                       state          = {0};
  VkBufferCreateInfo                bufferInfo     = {0};
  VkExternalMemoryBufferCreateInfo  external       = {0};
  VkImportMemoryHostPointerInfoEXT  import         = {0};
  VkMemoryHostPointerPropertiesEXT  pointer        = {0};
  VkMemoryAllocateFlagsInfo         allocationFlags = {0};
  VkMemoryAllocateInfo              allocationInfo = {0};
  VkMemoryRequirements              requirements;
  VkMemoryPropertyFlags             memoryFlags;
  GPUDeviceVk                      *deviceVk;
  VkDeviceSize                      align;
  uint32_t                          memoryTypeIndex;
  size_t                            page;
  GPUResult                         result;

  deviceVk = device->_priv;
  align    = deviceVk->hostPointerAlignment;
  page     = vk__hostPageSize();

  /* whole pages, as on Metal; a driver that imports only coarser ranges can't take these (the caller copies) */
  if (page == 0u || (uintptr_t)host->pData % page != 0u || host->allocationSize % page != 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!deviceVk->getMemoryHostPointerProperties || align == 0u
      || ((uintptr_t)host->pData & (uintptr_t)(align - 1u)) != 0u
      || (host->allocationSize & (align - 1u)) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (vk_bufferCreateInfo(device, info, &bufferInfo) != GPU_OK) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  external.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
  bufferInfo.pNext     = &external;
  state.device         = deviceVk->device;

  if (vkCreateBuffer(state.device, &bufferInfo, NULL, &state.buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(state.device, state.buffer, &requirements);
  pointer.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;

  if (deviceVk->getMemoryHostPointerProperties(state.device,
                                               VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                               host->pData,
                                               &pointer) != VK_SUCCESS
      || requirements.size > host->allocationSize
      || !vk_findMemoryType(device,
                            requirements.memoryTypeBits & pointer.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                            &memoryTypeIndex,
                            &memoryFlags)) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_UNSUPPORTED;
  }

  import.sType                   = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
  import.handleType              = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
  import.pHostPointer            = host->pData;
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext           = &import;
  allocationInfo.allocationSize  = host->allocationSize;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;

  if ((bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u) {
    allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    import.pNext          = &allocationFlags;
  }

  if (vkAllocateMemory(state.device, &allocationInfo, NULL, &state.memory) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state.ownsMemory = true;

  if (vkBindBufferMemory(state.device, state.buffer, state.memory, 0u) != VK_SUCCESS) {
    vk__destroyBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  state.mapped         = host->pData;
  state.imported       = true;
  state.coherent       = true;
  state.allocationSize = host->allocationSize;

  if ((result = vk_wrapBuffer(device, info, &bufferInfo, &state, outBuffer)) == GPU_OK) {
    (*outBuffer)->_hostMemory     = host->pData;
    (*outBuffer)->_hostImported   = true;
    (*outBuffer)->_allocationSize = host->allocationSize;
  }

  return result;
}
#endif

GPU_HIDE
GPUResult
vk_createBuffer(GPUDevice                 *__restrict device,
                const GPUBufferCreateInfo *__restrict info,
                GPUBuffer                **__restrict outBuffer) {
  const GPUBufferUsageFlags deviceLocal = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_TEXEL |
                                          GPU_BUFFER_USAGE_COPY_DST |
                                          GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT;
  GPUResult                 result;
  bool                      hostVisible;

#ifdef VK_EXT_external_memory_host
  if (info && info->chain.pNext) {
    return device ? vk__importBuffer(device, info, info->chain.pNext, outBuffer) : GPU_ERROR_INVALID_ARGUMENT;
  }
#endif

  if (info && (info->usage & GPU_BUFFER_USAGE_HOST_READ) != 0u) {
    return vk__createBuffer(device, info, VK_BUFFER_HOST_READBACK, outBuffer);
  }

  hostVisible = info && (info->usage & deviceLocal) == 0u;
  result      = vk__createBuffer(device, info, hostVisible ? VK_BUFFER_HOST_WRITE : VK_BUFFER_DEVICE, outBuffer);

  if (result == GPU_ERROR_UNSUPPORTED && !hostVisible) {
    result = vk__createBuffer(device, info, VK_BUFFER_HOST_WRITE, outBuffer);
  }

  return result;
}

GPU_HIDE
GPUResult
vk_createHostBuffer(GPUDevice                 *__restrict device,
                    const GPUBufferCreateInfo *__restrict info,
                    GPUBuffer                **__restrict outBuffer) {
  return vk__createBuffer(device, info, VK_BUFFER_HOST_WRITE, outBuffer);
}

GPU_HIDE
GPUResult
vk_createReadbackBuffer(GPUDevice                 *__restrict device,
                        const GPUBufferCreateInfo *__restrict info,
                        GPUBuffer                **__restrict outBuffer) {
  return vk__createBuffer(device, info, VK_BUFFER_HOST_READ, outBuffer);
}

GPU_HIDE
void
vk_releaseBuffer(GPUBuffer *__restrict buffer) {
  if (!buffer) {
    return;
  }

  vk__destroyBufferState(buffer->_priv);
  free(buffer);
}

static void
vk__releaseRetiredBuffer(void *deviceVk, void *owner) {
  (void)deviceVk;
  vk_releaseBuffer(owner);
}

GPU_HIDE
void
vk_destroyBuffer(GPUBuffer *__restrict buffer) {
  GPUBufferVk *native;

  if (!buffer) {
    return;
  }

  native = buffer->_priv;

  if (!native || !buffer->device) {
    vk_releaseBuffer(buffer);
    return;
  }

  vk_retire(buffer->device->_priv, &native->retired, buffer, vk__releaseRetiredBuffer);
}

/* [offset, offset + size) of a mapped buffer as a range of its memory for
 * flushes and invalidations: rounded out to nonCoherentAtomSize, never the
 * whole memory (a pooled buffer's memory is a block of many) */

static void
vk__mappedRange(GPUDevice           *device,
                const GPUBufferVk   *native,
                uint64_t             offset,
                uint64_t             size,
                VkMappedMemoryRange *range) {
  GPUAdapterVk *adapterVk;
  uint64_t      atom, start, end, limit;

  adapterVk = device && device->adapter ? device->adapter->_priv : NULL;
  atom      = adapterVk && adapterVk->props.limits.nonCoherentAtomSize ? adapterVk->props.limits.nonCoherentAtomSize
                                                                         : 1u;
  start     = (native->poolRange.block ? native->poolRange.offset : 0u) + offset;
  end       = start + size;
  limit     = native->poolRange.block ? native->poolRange.block->size : native->allocationSize;
  start    -= start % atom;
  end       = end % atom ? end + (atom - end % atom) : end;

  memset(range, 0, sizeof(*range));
  range->sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range->memory = native->memory;
  range->offset = start;
  range->size   = end > limit ? VK_WHOLE_SIZE : end - start;
}

GPU_HIDE
GPUResult
vk_writeBuffer(GPUQueue   *__restrict queue,
               GPUBuffer  *__restrict buffer,
               uint64_t               dstOffset,
               const void            *data,
               uint64_t               sizeBytes) {
  VkCommandBuffer     command;
  GPUBuffer          *staging;
  GPUBufferVk        *stagingVk;
  GPUBufferVk        *native;
  VkBufferCopy        copy  = {0};
  VkMappedMemoryRange range = {0};
  uint64_t            stagingOffset;
  GPUResult           result;

  native = buffer ? buffer->_priv : NULL;

  if (!queue || !buffer || queue->_device != buffer->device
      || !native || !native->buffer || !data || sizeBytes > SIZE_MAX
      || !bufferRangeValid(buffer, dstOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  /* host memory is written now (its users order their own writes); mapped
   * device-local memory too while nothing the queue was given still runs or
   * waits, which keeps the queue's order without a staging copy */
  if (native->mapped && (!native->deviceMapped || vk_queueIdle(queue))) {
    memmove((uint8_t *)native->mapped + dstOffset, data, (size_t)sizeBytes);

    if (!native->coherent) {
      vk__mappedRange(queue->_device, native, dstOffset, sizeBytes, &range);

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
    vk__mappedRange(queue->_device, stagingVk, stagingOffset, sizeBytes, &range);

    if (vkFlushMappedMemoryRanges(stagingVk->device,
                                  1u,
                                  &range) != VK_SUCCESS) {
      vk_abortTransfer(queue);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  vk__bufferBarrier(queue->_device->_priv,
                    command,
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

  vk__bufferBarrier(queue->_device->_priv,
                    command,
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
vk_readBuffer(GPUQueue  *__restrict queue,
              GPUBuffer *__restrict buffer,
              uint64_t              srcOffset,
              void                 *outData,
              uint64_t              sizeBytes) {
  VkCommandBuffer     command;
  GPUBuffer          *staging;
  GPUBufferVk        *stagingVk;
  GPUBufferVk        *native;
  VkBufferCopy        copy  = {0};
  VkMappedMemoryRange range = {0};
  uint64_t            stagingOffset;
  GPUResult           result;

  native = buffer ? buffer->_priv : NULL;

  if (!queue || !buffer || queue->_device != buffer->device
      || !native || !native->buffer || !outData || sizeBytes > SIZE_MAX
      || !bufferRangeValid(buffer, srcOffset, sizeBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  /* device-local memory mapped uncached reads slower than a copy into
   * cached staging */
  if (native->mapped && !native->stagedReads) {
    result = vk_waitCommandQueueIdle(queue);

    if (result != GPU_OK) {
      return result;
    }

    if (!native->coherent) {
      vk__mappedRange(queue->_device, native, srcOffset, sizeBytes, &range);

      if (vkInvalidateMappedMemoryRanges(native->device,
                                         1u,
                                         &range) != VK_SUCCESS) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    }

    memmove(outData,
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

  vk__bufferBarrier(queue->_device->_priv,
                    command,
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

  vk__bufferBarrier(queue->_device->_priv,
                    command,
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
    vk__mappedRange(queue->_device, stagingVk, stagingOffset, sizeBytes, &range);

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
vk_bufferContents(GPUBuffer *__restrict buffer) {
  GPUBufferVk *native;

  native = buffer ? buffer->_priv : NULL;

  return native && native->coherent ? native->mapped : NULL;
}

GPU_HIDE
void
vk_initBuff(GPUBufferApi *api) {
  api->create   = vk_createBuffer;
  api->destroy  = vk_destroyBuffer;
  api->write    = vk_writeBuffer;
  api->read     = vk_readBuffer;
  api->contents = vk_bufferContents;
}
