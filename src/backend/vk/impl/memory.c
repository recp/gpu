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

GPU_HIDE
uint32_t
vk_filterMemoryTypes(GPUDevice *device, uint32_t typeBits) {
  GPUAdapterVk                    *adapter;
  VkPhysicalDeviceMemoryProperties properties;
  uint32_t                        result;

  adapter = device && device->adapter ? device->adapter->_priv : NULL;
  if (!adapter) {
    return 0u;
  }
  vkGetPhysicalDeviceMemoryProperties(adapter->physicalDevice, &properties);

  result = 0u;
  for (uint32_t i = 0u; i < properties.memoryTypeCount; i++) {
    if ((typeBits & (1u << i)) != 0u &&
        (properties.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u) {
      result |= 1u << i;
    }
  }
  return result;
}

static GPUResult
vk_createHeap(GPUDevice               *device,
              const GPUHeapCreateInfo *info,
              GPUHeap                **outHeap) {
  GPUDeviceVk                 *deviceVk;
  GPUHeap                     *heap;
  GPUHeapVk                   *native;
  VkMemoryAllocateInfo         allocationInfo = {0};
  VkMemoryAllocateFlagsInfo    allocationFlags = {0};
  VkMemoryPropertyFlags        memoryFlags;
  uint32_t                     memoryTypeIndex;

  if (!device || !(deviceVk = device->_priv) || !info || !outHeap ||
      info->compatibilityMask > UINT32_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!vk_findMemoryType(device,
                         (uint32_t)info->compatibilityMask,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  heap = calloc(1, sizeof(*heap) + sizeof(*native));
  if (!heap) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native                  = (GPUHeapVk *)(heap + 1);
  native->device          = deviceVk->device;
  native->sizeBytes       = info->sizeBytes;
  native->memoryFlags     = memoryFlags;
  native->memoryTypeIndex = memoryTypeIndex;
  native->coherent        =
    (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;

  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = info->sizeBytes;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (deviceVk->bufferDeviceAddress) {
    allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocationInfo.pNext  = &allocationFlags;
  }
  if (vkAllocateMemory(native->device,
                       &allocationInfo,
                       NULL,
                       &native->memory) != VK_SUCCESS) {
    free(heap);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if ((memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u &&
      vkMapMemory(native->device,
                  native->memory,
                  0u,
                  VK_WHOLE_SIZE,
                  0u,
                  &native->mapped) != VK_SUCCESS) {
    vkFreeMemory(native->device, native->memory, NULL);
    free(heap);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  heap->_priv             = native;
  heap->device            = device;
  heap->compatibilityMask = UINT64_C(1) << memoryTypeIndex;
  *outHeap                = heap;
  return GPU_OK;
}

static void
vk_destroyHeap(GPUHeap *heap) {
  GPUHeapVk *native;

  if (!heap) {
    return;
  }
  native = heap->_priv;
  if (native) {
    if (native->mapped) {
      vkUnmapMemory(native->device, native->memory);
    }
    if (native->memory) {
      vkFreeMemory(native->device, native->memory, NULL);
    }
  }
  free(heap);
}

GPU_HIDE
void
vk_initMemory(GPUApiMemory *api) {
  api->getBufferRequirements  = vk_getBufferMemoryRequirements;
  api->getTextureRequirements = vk_getTextureMemoryRequirements;
  api->createHeap             = vk_createHeap;
  api->destroyHeap            = vk_destroyHeap;
  api->createPlacedBuffer     = vk_createPlacedBuffer;
  api->createPlacedTexture    = vk_createPlacedTexture;
}
