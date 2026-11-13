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

/* small buffers and optimal-tiling images bound to ranges of shared
 * VkDeviceMemory blocks (api/suballoc): vkAllocateMemory once a block, not
 * once a resource, and far under maxMemoryAllocationCount. buffers and
 * images never share a block, so bufferImageGranularity needs no padding;
 * lazily allocated memory and resources the driver wants dedicated keep
 * their own allocation. GPU_RESOURCE_POOL=0 turns the pool off. */

#include "../common.h"
#include "../impl.h"
#include "../../../api/suballoc_internal.h"

#include <stdlib.h>
#include <string.h>

enum {
  VK_POOL_TYPE_MASK = 0xffu,
  VK_POOL_IMAGE     = 1u << 8,
  VK_POOL_ADDRESS   = 1u << 9,
  VK_POOL_MAPPED    = 1u << 10
};

#define VK_POOL_FIRST_BLOCK   ((uint64_t)8u << 20)
#define VK_POOL_LARGEST_BLOCK ((uint64_t)64u << 20)
#define VK_POOL_MAX_RESOURCE  ((uint64_t)4u << 20)

static bool
vk__poolCreateBlock(void *user, uint32_t key, uint64_t size, uint64_t *outHandle, void **outMapped) {
  VkMemoryAllocateInfo      allocationInfo  = {0};
  VkMemoryAllocateFlagsInfo allocationFlags = {0};
  GPUDeviceVk              *deviceVk;
  VkDeviceMemory            memory;
  void                     *mapped;

  deviceVk                       = user;
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = size;
  allocationInfo.memoryTypeIndex = key & VK_POOL_TYPE_MASK;

  if (key & VK_POOL_ADDRESS) {
    allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocationInfo.pNext  = &allocationFlags;
  }

  memory = VK_NULL_HANDLE;
  mapped = NULL;

  if (vkAllocateMemory(deviceVk->device, &allocationInfo, NULL, &memory) != VK_SUCCESS)
    return false;

  if ((key & VK_POOL_MAPPED)
      && vkMapMemory(deviceVk->device, memory, 0u, VK_WHOLE_SIZE, 0u, &mapped) != VK_SUCCESS) {
    vkFreeMemory(deviceVk->device, memory, NULL);
    return false;
  }

  *outHandle = 0u;
  memcpy(outHandle, &memory, sizeof(memory));
  *outMapped = mapped;

  return true;
}

static void
vk__poolDestroyBlock(void *user, uint32_t key, uint64_t handle, void *mapped) {
  GPUDeviceVk   *deviceVk;
  VkDeviceMemory memory;

  deviceVk = user;
  memcpy(&memory, &handle, sizeof(memory));
  GPU__UNUSED(key);

  if (mapped)
    vkUnmapMemory(deviceVk->device, memory);

  vkFreeMemory(deviceVk->device, memory, NULL);
}

GPU_HIDE
void
vk_initResourcePool(GPUDeviceVk *deviceVk) {
  GPUSubAllocatorInfo info = {0};
  GPUSubAllocator    *pool;
  const char         *env;

  if (!deviceVk || !deviceVk->device
      || ((env = getenv("GPU_RESOURCE_POOL")) && strcmp(env, "0") == 0))
    return;

  if (!(pool = malloc(sizeof(*pool))))
    return;

  info.createBlock      = vk__poolCreateBlock;
  info.destroyBlock     = vk__poolDestroyBlock;
  info.user             = deviceVk;
  info.firstBlockSize   = VK_POOL_FIRST_BLOCK;
  info.largestBlockSize = VK_POOL_LARGEST_BLOCK;
  info.maxResourceSize  = VK_POOL_MAX_RESOURCE;

  if (!gpuSubInit(pool, &info)) {
    free(pool);
    return;
  }

  deviceVk->resourcePool = pool;
}

/* after every resource placed in it is gone, before the device */

GPU_HIDE
void
vk_destroyResourcePool(GPUDeviceVk *deviceVk) {
  if (!deviceVk || !deviceVk->resourcePool)
    return;

  gpuSubDestroy(deviceVk->resourcePool);
  free(deviceVk->resourcePool);
  deviceVk->resourcePool = NULL;
}

GPU_HIDE
void
vk_trimResourcePool(GPUDeviceVk *deviceVk) {
  if (deviceVk && deviceVk->resourcePool)
    gpuSubTrim(deviceVk->resourcePool);
}

/* a range of a block of memoryTypeIndex; false: the caller allocates */

static bool
vk__poolPlace(GPUDeviceVk                *deviceVk,
              uint32_t                    key,
              const VkMemoryRequirements *requirements,
              VkDeviceMemory             *outMemory,
              void                      **outMapped,
              VKPoolRange                *outRange) {
  GPUSubAllocation allocation;

  if (!gpuSubAllocate(deviceVk->resourcePool, key, requirements->size, requirements->alignment, &allocation))
    return false;

  memcpy(outMemory, &allocation.block->handle, sizeof(*outMemory));
  if (outMapped)
    *outMapped = allocation.block->mapped ? (uint8_t *)allocation.block->mapped + allocation.offset : NULL;

  outRange->pool   = deviceVk->resourcePool;
  outRange->block  = allocation.block;
  outRange->offset = allocation.offset;
  outRange->size   = allocation.size;

  return true;
}

GPU_HIDE
void
vk_poolRelease(VKPoolRange *range) {
  GPUSubAllocation allocation;

  if (!range || !range->pool || !range->block)
    return;

  allocation.block  = range->block;
  allocation.offset = range->offset;
  allocation.size   = range->size;
  gpuSubFree(range->pool, &allocation);
  memset(range, 0, sizeof(*range));
}

GPU_HIDE
bool
vk_poolBindBuffer(GPUDeviceVk          *deviceVk,
                  VkBuffer              buffer,
                  uint32_t              memoryTypeIndex,
                  VkMemoryPropertyFlags memoryFlags,
                  bool                  deviceAddress,
                  bool                  mapped,
                  VkDeviceMemory       *outMemory,
                  void                **outMapped,
                  VkDeviceSize         *outSize,
                  VKPoolRange          *outRange) {
  VkBufferMemoryRequirementsInfo2 info         = {0};
  VkMemoryDedicatedRequirements   dedicated    = {0};
  VkMemoryRequirements2           requirements = {0};
  uint32_t                        key;

  if (!deviceVk || !deviceVk->resourcePool || memoryTypeIndex > VK_POOL_TYPE_MASK
      || (memoryFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))
    return false;

  info.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
  info.buffer        = buffer;
  dedicated.sType    = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetBufferMemoryRequirements2(deviceVk->device, &info, &requirements);

  if (dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation
      || !(requirements.memoryRequirements.memoryTypeBits & (1u << memoryTypeIndex)))
    return false;

  key = memoryTypeIndex | (deviceAddress ? VK_POOL_ADDRESS : 0u) | (mapped ? VK_POOL_MAPPED : 0u);

  if (!vk__poolPlace(deviceVk, key, &requirements.memoryRequirements, outMemory, outMapped, outRange))
    return false;

  if (vkBindBufferMemory(deviceVk->device, buffer, *outMemory, outRange->offset) != VK_SUCCESS) {
    vk_poolRelease(outRange);
    return false;
  }

  *outSize = requirements.memoryRequirements.size;

  return true;
}

GPU_HIDE
bool
vk_poolBindImage(GPUDeviceVk          *deviceVk,
                 VkImage               image,
                 VkImageTiling         tiling,
                 uint32_t              memoryTypeIndex,
                 VkMemoryPropertyFlags memoryFlags,
                 VkDeviceMemory       *outMemory,
                 VKPoolRange          *outRange) {
  VkImageMemoryRequirementsInfo2 info         = {0};
  VkMemoryDedicatedRequirements  dedicated    = {0};
  VkMemoryRequirements2          requirements = {0};

  if (!deviceVk || !deviceVk->resourcePool || memoryTypeIndex > VK_POOL_TYPE_MASK
      || tiling != VK_IMAGE_TILING_OPTIMAL
      || (memoryFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))
    return false;

  info.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
  info.image         = image;
  dedicated.sType    = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetImageMemoryRequirements2(deviceVk->device, &info, &requirements);

  if (dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation
      || !(requirements.memoryRequirements.memoryTypeBits & (1u << memoryTypeIndex)))
    return false;

  if (!vk__poolPlace(deviceVk, memoryTypeIndex | VK_POOL_IMAGE, &requirements.memoryRequirements,
                     outMemory, NULL, outRange))
    return false;

  if (vkBindImageMemory(deviceVk->device, image, *outMemory, outRange->offset) != VK_SUCCESS) {
    vk_poolRelease(outRange);
    return false;
  }

  return true;
}
