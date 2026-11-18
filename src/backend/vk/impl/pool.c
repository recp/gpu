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

/* Android: small blocks, none made ahead, so less memory is held for the same first frames. GPU_POOL_FIRST_KB,
 * GPU_POOL_LARGEST_KB and GPU_POOL_WARM override both (measuring) */
#if defined(__ANDROID__)
#  define VK_POOL_FIRST_BLOCK   ((uint64_t)2u << 20)
#  define VK_POOL_LARGEST_BLOCK ((uint64_t)8u << 20)
#  define VK_POOL_WARM          false
#else
#  define VK_POOL_FIRST_BLOCK   ((uint64_t)8u << 20)
#  define VK_POOL_LARGEST_BLOCK ((uint64_t)64u << 20)
#  define VK_POOL_WARM          true
#endif
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

/* a block size from the environment in KB (GPU_POOL_FIRST_KB, GPU_POOL_LARGEST_KB: measuring a device's memory
 * against its first frames), else the default; a power of two from 64 KB */
static uint64_t
vk__poolSize(const char *name, uint64_t fallback) {
  const char *env;
  uint64_t    kb, size;

  if (!(env = getenv(name)) || !(kb = strtoull(env, NULL, 10)) || kb < 64u || kb > ((uint64_t)1u << 22))
    return fallback;
  for (size = (uint64_t)64u << 10; size < (kb << 10); size <<= 1u) {}
  return size;
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
  info.firstBlockSize   = vk__poolSize("GPU_POOL_FIRST_KB", VK_POOL_FIRST_BLOCK);
  info.largestBlockSize = vk__poolSize("GPU_POOL_LARGEST_KB", VK_POOL_LARGEST_BLOCK);
  if (info.largestBlockSize < info.firstBlockSize)
    info.largestBlockSize = info.firstBlockSize;
  info.maxResourceSize  = VK_POOL_MAX_RESOURCE;

  if (!gpuSubInit(pool, &info)) {
    free(pool);
    return;
  }

  deviceVk->resourcePool = pool;
}

/* a pooled buffer's block: by memory type, device addresses and mapping */
static uint32_t
vk__poolBufferKey(uint32_t memoryTypeIndex, bool deviceAddress, bool mapped) {
  return memoryTypeIndex | (deviceAddress ? VK_POOL_ADDRESS : 0u) | (mapped ? VK_POOL_MAPPED : 0u);
}

typedef struct VKPoolWarm {
  GPUDevice *device;
#if defined(_WIN32) || defined(WIN32)
  HANDLE     thread;
#else
  pthread_t  thread;
#endif
} VKPoolWarm;

/* making queued first blocks until none is left: one of the warm-up's threads */
#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
vk__warmMake(LPVOID context) {
  while (gpuSubMakeQueued(context) != 0) {}
  return 0;
}
#else
static void*
vk__warmMake(void *context) {
  while (gpuSubMakeQueued(context) != 0) {}
  return NULL;
}
#endif

/* the first blocks a device's work uses, made as the device starts:
 * allocating and mapping a block can take milliseconds, which the first
 * allocations would otherwise pay. The keys are those
 * vk__createBuffer and vk_createTexture reach for device and host-written
 * buffers and optimal-tiling color images; each block is made on a thread
 * of its own. Nothing waits for a block not yet started: an allocation
 * that comes first makes it itself (gpuSubAllocate) */
static void
vk__warmPool(GPUDevice *device) {
  static const GPUBufferUsageFlags usages[2] = {
    GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST,
    GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_INDEX
  };
  static const VKBufferMemory memories[2] = {VK_BUFFER_DEVICE, VK_BUFFER_HOST_WRITE};
  GPUBufferCreateInfo   info      = {0};
  VkBufferCreateInfo    bufferInfo;
  VkImageCreateInfo     imageInfo = {0};
  VkMemoryRequirements  requirements;
  VkMemoryPropertyFlags flags;
  GPUDeviceVk          *deviceVk;
  VkBuffer              buffer;
  VkImage               image;
  uint32_t              keys[3], count, index, i;
  bool                  mapped;
#if defined(_WIN32) || defined(WIN32)
  HANDLE                helpers[2];
#else
  pthread_t             helpers[2];
#endif
  uint32_t              helperCount;

  deviceVk = device->_priv;
  count    = 0u;

  for (i = 0u; i < 2u; i++) {
    memset(&bufferInfo, 0, sizeof(bufferInfo));
    info.sizeBytes = 256u;
    info.usage     = usages[i];
    buffer         = VK_NULL_HANDLE;
    if (vk_bufferCreateInfo(device, &info, &bufferInfo) != GPU_OK
        || vkCreateBuffer(deviceVk->device, &bufferInfo, NULL, &buffer) != VK_SUCCESS)
      continue;

    vkGetBufferMemoryRequirements(deviceVk->device, buffer, &requirements);
    vkDestroyBuffer(deviceVk->device, buffer, NULL);

    if (!vk_bufferMemoryType(device, requirements.memoryTypeBits, memories[i], &index, &flags)
        || index > VK_POOL_TYPE_MASK || (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))
      continue;

    mapped          = memories[i] != VK_BUFFER_DEVICE || (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u;
    keys[count++]   = vk__poolBufferKey(index, (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u,
                                        mapped);
  }

  imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType     = VK_IMAGE_TYPE_2D;
  imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent.width  = 64u;
  imageInfo.extent.height = 64u;
  imageInfo.extent.depth  = 1u;
  imageInfo.mipLevels     = 1u;
  imageInfo.arrayLayers   = 1u;
  imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image                   = VK_NULL_HANDLE;

  if (vkCreateImage(deviceVk->device, &imageInfo, NULL, &image) == VK_SUCCESS) {
    vkGetImageMemoryRequirements(deviceVk->device, image, &requirements);
    vkDestroyImage(deviceVk->device, image, NULL);

    if (vk_findMemoryType(device, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0u, &index,
                          &flags)
        && index <= VK_POOL_TYPE_MASK && !(flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))
      keys[count++] = index | VK_POOL_IMAGE;
  }

  gpuSubQueue(deviceVk->resourcePool, keys, count);

  /* the blocks' allocations don't wait for each other */
  for (helperCount = 0u; count > 1u && helperCount < count - 1u; helperCount++) {
#if defined(_WIN32) || defined(WIN32)
    if (!(helpers[helperCount] = CreateThread(NULL, 0, vk__warmMake, deviceVk->resourcePool, 0, NULL)))
      break;
#else
    if (pthread_create(&helpers[helperCount], NULL, vk__warmMake, deviceVk->resourcePool) != 0)
      break;
#endif
  }

  vk__warmMake(deviceVk->resourcePool);

  for (i = 0u; i < helperCount; i++) {
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(helpers[i], INFINITE);
    CloseHandle(helpers[i]);
#else
    pthread_join(helpers[i], NULL);
#endif
  }
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
vk__warmPoolMain(LPVOID context) {
  vk__warmPool(((VKPoolWarm *)context)->device);
  return 0;
}
#else
static void*
vk__warmPoolMain(void *context) {
  vk__warmPool(((VKPoolWarm *)context)->device);
  return NULL;
}
#endif

/* after vk_initResourcePool, with the device's _priv and adapter set */
GPU_HIDE
void
vk_warmResourcePool(GPUDevice *device) {
  GPUDeviceVk *deviceVk;
  VKPoolWarm  *warm;

  const char  *env;

  /* GPU_POOL_WARM=0|1 (measuring): first blocks ahead of the first resources or not, else VK_POOL_WARM */
  if (!device || !(deviceVk = device->_priv) || !deviceVk->resourcePool
      || !((env = getenv("GPU_POOL_WARM")) ? strcmp(env, "0") != 0 : VK_POOL_WARM)
      || !(warm = calloc(1, sizeof(*warm))))
    return;

  warm->device = device;
#if defined(_WIN32) || defined(WIN32)
  if (!(warm->thread = CreateThread(NULL, 0, vk__warmPoolMain, warm, 0, NULL))) {
    free(warm);
    return;
  }
#else
  if (pthread_create(&warm->thread, NULL, vk__warmPoolMain, warm) != 0) {
    free(warm);
    return;
  }
#endif
  deviceVk->resourcePoolWarm = warm;
}

static void
vk__joinWarm(GPUDeviceVk *deviceVk) {
  VKPoolWarm *warm;

  if (!(warm = deviceVk->resourcePoolWarm))
    return;

#if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(warm->thread, INFINITE);
  CloseHandle(warm->thread);
#else
  pthread_join(warm->thread, NULL);
#endif
  free(warm);
  deviceVk->resourcePoolWarm = NULL;
}

/* after every resource placed in it is gone, before the device */

GPU_HIDE
void
vk_destroyResourcePool(GPUDeviceVk *deviceVk) {
  if (!deviceVk || !deviceVk->resourcePool)
    return;

  vk__joinWarm(deviceVk);
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

  key = vk__poolBufferKey(memoryTypeIndex, deviceAddress, mapped);

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
