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

static uint32_t
vk_sparseMipExtent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel < 32u ? mipLevel : 31u;
  return extent ? extent : 1u;
}

static GPUResult
vk_submitSparse(GPUQueue                       *queueHandle,
                const GPUQueueSparseSubmitInfo *info) {
  GPUQueueVk                       *queue;
  GPUDeviceVk                      *device;
  VkSparseMemoryBind               *bufferBinds;
  VkSparseBufferMemoryBindInfo     *bufferInfos;
  VkSparseImageMemoryBind          *imageBinds;
  VkSparseImageMemoryBindInfo      *imageInfos;
  VkSparseMemoryBind               *opaqueBinds;
  VkSparseImageOpaqueMemoryBindInfo *opaqueInfos;
  VkSemaphore                      *waits;
  VkSemaphore                      *signals;
  uint64_t                         *waitValues;
  uint64_t                         *signalValues;
  VkTimelineSemaphoreSubmitInfo     timelineInfo = {0};
  VkBindSparseInfo                  bindInfo = {0};
  uint8_t                          *storage;
  size_t                            storageSize;
  uint32_t                          imageBindCount;
  uint32_t                          opaqueBindCount;
  VkResult                          result;

  queue  = queueHandle ? queueHandle->_priv : NULL;
  device = queueHandle && queueHandle->_device
             ? queueHandle->_device->_priv
             : NULL;
  if (!queue || !device || !info ||
      (queueHandle->bits & (GPU_QUEUE_GRAPHICS_BIT |
                            GPU_QUEUE_COMPUTE_BIT |
                            GPU_QUEUE_TRANSFER_BIT)) == 0u ||
      !queue->queRaw) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!queueHandle->_device->adapter ||
      queue->familyIndex >=
        ((GPUAdapterVk *)queueHandle->_device->adapter->_priv)->nQueFamilies ||
      ((((GPUAdapterVk *)queueHandle->_device->adapter->_priv)
          ->queueFamilyProps[queue->familyIndex].queueFlags &
         VK_QUEUE_SPARSE_BINDING_BIT) == 0u)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if ((info->waitCount > 0u || info->signalCount > 0u) &&
      !device->timelineSemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (vk_flushTransfers(queueHandle) != GPU_OK) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  storageSize = (size_t)info->bufferMappingCount *
                  (sizeof(*bufferBinds) + sizeof(*bufferInfos)) +
                (size_t)info->textureMappingCount *
                  (sizeof(*imageBinds) + sizeof(*imageInfos) +
                   sizeof(*opaqueBinds) + sizeof(*opaqueInfos)) +
                (size_t)info->waitCount *
                  (sizeof(*waits) + sizeof(*waitValues)) +
                (size_t)info->signalCount *
                  (sizeof(*signals) + sizeof(*signalValues));
  storage = calloc(1u, storageSize);
  if (!storage) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  bufferBinds = (VkSparseMemoryBind *)storage;
  bufferInfos = (VkSparseBufferMemoryBindInfo *)(
    bufferBinds + info->bufferMappingCount
  );
  imageBinds = (VkSparseImageMemoryBind *)(
    bufferInfos + info->bufferMappingCount
  );
  imageInfos = (VkSparseImageMemoryBindInfo *)(
    imageBinds + info->textureMappingCount
  );
  opaqueBinds = (VkSparseMemoryBind *)(
    imageInfos + info->textureMappingCount
  );
  opaqueInfos = (VkSparseImageOpaqueMemoryBindInfo *)(
    opaqueBinds + info->textureMappingCount
  );
  waits = (VkSemaphore *)(opaqueInfos + info->textureMappingCount);
  waitValues = (uint64_t *)(waits + info->waitCount);
  signals = (VkSemaphore *)(waitValues + info->waitCount);
  signalValues = (uint64_t *)(signals + info->signalCount);

  imageBindCount  = 0u;
  opaqueBindCount = 0u;
  for (uint32_t i = 0u; i < info->bufferMappingCount; i++) {
    const GPUSparseBufferMapping *mapping;
    GPUBufferVk                  *buffer;
    GPUHeapVk                    *heap;
    VkSparseMemoryBind           *bind;
    VkSparseBufferMemoryBindInfo *bufferInfo;

    mapping    = &info->pBufferMappings[i];
    buffer     = mapping->buffer->_priv;
    heap       = mapping->heap->_priv;
    bind       = &bufferBinds[i];
    bufferInfo = &bufferInfos[i];
    if (!buffer || !buffer->sparse || !buffer->buffer ||
        !heap || !heap->memory) {
      free(storage);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    bind->resourceOffset = mapping->bufferTileOffset *
                           mapping->buffer->_sparseRequirements.pageSizeBytes;
    bind->size           = mapping->tileCount *
                           mapping->buffer->_sparseRequirements.pageSizeBytes;
    bind->memory         = mapping->mode == GPU_SPARSE_MAPPING_MAP
                             ? heap->memory
                             : VK_NULL_HANDLE;
    bind->memoryOffset   = mapping->mode == GPU_SPARSE_MAPPING_MAP
                             ? mapping->heapTileOffset *
                                 mapping->heap->pageSizeBytes
                             : 0u;
    bufferInfo->buffer    = buffer->buffer;
    bufferInfo->bindCount = 1u;
    bufferInfo->pBinds    = bind;
  }
  for (uint32_t i = 0u; i < info->textureMappingCount; i++) {
    const GPUSparseTextureMapping *mapping;
    GPUTextureVk                  *texture;
    GPUHeapVk                     *heap;
    VkDeviceSize                   memoryOffset;

    mapping = &info->pTextureMappings[i];
    texture = mapping->texture->_priv;
    heap    = mapping->heap->_priv;
    if (!texture || !texture->sparse || !heap || !heap->memory) {
      free(storage);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    memoryOffset = mapping->mode == GPU_SPARSE_MAPPING_MAP
                     ? mapping->heapTileOffset *
                         mapping->heap->pageSizeBytes
                     : 0u;
    if (mapping->mipLevel ==
        mapping->texture->_sparseRequirements.firstMipInTail) {
      VkSparseMemoryBind               *bind;
      VkSparseImageOpaqueMemoryBindInfo *bindInfo;

      bind     = &opaqueBinds[opaqueBindCount];
      bindInfo = &opaqueInfos[opaqueBindCount++];
      bind->resourceOffset =
        texture->sparseRequirements.imageMipTailOffset +
        (texture->sparseRequirements.formatProperties.flags &
         VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT
           ? 0u
           : (VkDeviceSize)mapping->arrayLayer *
               texture->sparseRequirements.imageMipTailStride);
      bind->size         = texture->sparseRequirements.imageMipTailSize;
      bind->memory       = mapping->mode == GPU_SPARSE_MAPPING_MAP
                             ? heap->memory
                             : VK_NULL_HANDLE;
      bind->memoryOffset = memoryOffset;
      bindInfo->image    = texture->image;
      bindInfo->bindCount = 1u;
      bindInfo->pBinds    = bind;
    } else {
      VkSparseImageMemoryBind     *bind;
      VkSparseImageMemoryBindInfo *bindInfo;
      VkExtent3D                   granularity;
      uint32_t                     mipWidth;
      uint32_t                     mipHeight;
      uint32_t                     mipDepth;
      uint32_t                     offsetX;
      uint32_t                     offsetY;
      uint32_t                     offsetZ;

      bind        = &imageBinds[imageBindCount];
      bindInfo    = &imageInfos[imageBindCount++];
      granularity = texture->sparseRequirements.formatProperties
                       .imageGranularity;
      mipWidth  = vk_sparseMipExtent(mapping->texture->width,
                                     mapping->mipLevel);
      mipHeight = vk_sparseMipExtent(mapping->texture->height,
                                     mapping->mipLevel);
      mipDepth  = mapping->texture->dimension == GPU_TEXTURE_DIMENSION_3D
                    ? vk_sparseMipExtent(mapping->texture->depthOrLayers,
                                         mapping->mipLevel)
                    : 1u;
      offsetX = mapping->tileX * granularity.width;
      offsetY = mapping->tileY * granularity.height;
      offsetZ = mapping->tileZ * granularity.depth;
      bind->subresource.aspectMask =
        texture->sparseRequirements.formatProperties.aspectMask;
      bind->subresource.mipLevel   = mapping->mipLevel;
      bind->subresource.arrayLayer = mapping->arrayLayer;
      bind->offset.x = (int32_t)offsetX;
      bind->offset.y = (int32_t)offsetY;
      bind->offset.z = (int32_t)offsetZ;
      bind->extent.width =
        mapping->tileWidth * granularity.width < mipWidth - offsetX
          ? mapping->tileWidth * granularity.width
          : mipWidth - offsetX;
      bind->extent.height =
        mapping->tileHeight * granularity.height < mipHeight - offsetY
          ? mapping->tileHeight * granularity.height
          : mipHeight - offsetY;
      bind->extent.depth =
        mapping->tileDepth * granularity.depth < mipDepth - offsetZ
          ? mapping->tileDepth * granularity.depth
          : mipDepth - offsetZ;
      bind->memory       = mapping->mode == GPU_SPARSE_MAPPING_MAP
                             ? heap->memory
                             : VK_NULL_HANDLE;
      bind->memoryOffset = memoryOffset;
      bindInfo->image     = texture->image;
      bindInfo->bindCount = 1u;
      bindInfo->pBinds    = bind;
    }
  }

  for (uint32_t i = 0u; i < info->waitCount; i++) {
    GPUSemaphoreVk *semaphore;

    semaphore = info->pWaits[i].semaphore->_priv;
    if (!semaphore || semaphore->device != device->device ||
        !semaphore->semaphore) {
      free(storage);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    waits[i]      = semaphore->semaphore;
    waitValues[i] = info->pWaits[i].value;
  }
  for (uint32_t i = 0u; i < info->signalCount; i++) {
    GPUSemaphoreVk *semaphore;

    semaphore = info->pSignals[i].semaphore->_priv;
    if (!semaphore || semaphore->device != device->device ||
        !semaphore->semaphore) {
      free(storage);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    signals[i]      = semaphore->semaphore;
    signalValues[i] = info->pSignals[i].value;
  }

  timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timelineInfo.waitSemaphoreValueCount   = info->waitCount;
  timelineInfo.pWaitSemaphoreValues      = waitValues;
  timelineInfo.signalSemaphoreValueCount = info->signalCount;
  timelineInfo.pSignalSemaphoreValues    = signalValues;
  bindInfo.sType                = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  bindInfo.pNext                = info->waitCount > 0u ||
                                  info->signalCount > 0u
                                    ? &timelineInfo
                                    : NULL;
  bindInfo.waitSemaphoreCount   = info->waitCount;
  bindInfo.pWaitSemaphores      = waits;
  bindInfo.bufferBindCount      = info->bufferMappingCount;
  bindInfo.pBufferBinds         = bufferInfos;
  bindInfo.imageOpaqueBindCount = opaqueBindCount;
  bindInfo.pImageOpaqueBinds    = opaqueInfos;
  bindInfo.imageBindCount       = imageBindCount;
  bindInfo.pImageBinds          = imageInfos;
  bindInfo.signalSemaphoreCount = info->signalCount;
  bindInfo.pSignalSemaphores    = signals;
  result = vkQueueBindSparse(queue->queRaw,
                             1u,
                             &bindInfo,
                             VK_NULL_HANDLE);
  free(storage);
  return result == VK_SUCCESS ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
void
vk_initMemory(GPUApiMemory *api) {
  api->getBufferRequirements        = vk_getBufferMemoryRequirements;
  api->getTextureRequirements       = vk_getTextureMemoryRequirements;
  api->getSparseBufferRequirements  = vk_getSparseBufferRequirements;
  api->getSparseTextureRequirements = vk_getSparseTextureRequirements;
  api->createHeap                   = vk_createHeap;
  api->destroyHeap                  = vk_destroyHeap;
  api->createPlacedBuffer           = vk_createPlacedBuffer;
  api->createPlacedTexture          = vk_createPlacedTexture;
  api->createSparseBuffer           = vk_createSparseBuffer;
  api->createSparseTexture          = vk_createSparseTexture;
  api->submitSparse                 = vk_submitSparse;
}
