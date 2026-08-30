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
#include "../../../api/multigpu_internal.h"

#if defined(__linux__) || defined(__ANDROID__)
#  include <unistd.h>
#endif

enum {
  VK_SHARED_BARRIER_CHUNK_SIZE = 16u
};

static void
vk_destroySharedBufferState(GPUBufferVk *state);

static void
vk_destroySharedTextureState(GPUTextureVk *state);

typedef struct GPUDeviceInteropVk {
  VkExternalMemoryHandleTypeFlagBits    memoryHandleType;
  VkExternalSemaphoreHandleTypeFlagBits semaphoreHandleType;
} GPUDeviceInteropVk;

static bool
vk_samePhysicalDevice(const GPUDevice *first, const GPUDevice *second) {
  bool sameDevice;

  sameDevice = false;
  return first && second && first->adapter && second->adapter &&
         GPUAdaptersSharePhysicalDevice(first->adapter,
                                        second->adapter,
                                        &sameDevice) == GPU_OK &&
         sameDevice;
}

static bool
vk_externalHandleTypes(VkExternalMemoryHandleTypeFlagBits    *memory,
                       VkExternalSemaphoreHandleTypeFlagBits *semaphore) {
  if (!memory || !semaphore) {
    return false;
  }

#if defined(_WIN32) || defined(WIN32)
  *memory    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  *semaphore = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  return true;
#elif defined(__linux__) || defined(__ANDROID__)
  *memory    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  *semaphore = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  return true;
#else
  *memory    = 0;
  *semaphore = 0;
  return false;
#endif
}

static GPUResult
vk_interopDevices(GPUDeviceInteropEXT *interop,
                  GPUDeviceVk        **outFirst,
                  GPUDeviceVk        **outSecond,
                  GPUDeviceInteropVk **outNative) {
  GPUDeviceVk        *first, *second;
  GPUDeviceInteropVk *native;

  if (!interop || !interop->firstDevice || !interop->secondDevice ||
      gpuDeviceApi(interop->firstDevice) !=
        gpuDeviceApi(interop->secondDevice) ||
      !outFirst || !outSecond || !outNative ||
      !vk_samePhysicalDevice(interop->firstDevice,
                             interop->secondDevice)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  first  = interop->firstDevice->_priv;
  second = interop->secondDevice->_priv;
  native = interop->_priv;
  if (!first || !second || !native || !first->device || !second->device ||
      !first->externalInterop || !second->externalInterop ||
      !first->timelineSemaphore || !second->timelineSemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outFirst  = first;
  *outSecond = second;
  *outNative = native;
  return GPU_OK;
}

static GPUResult
vk_createDeviceInterop(GPUDevice            *firstDevice,
                       GPUDevice            *secondDevice,
                       GPUDeviceInteropEXT  *interop) {
  GPUDeviceVk        *first, *second;
  GPUDeviceInteropVk *native;

  if (!firstDevice || !secondDevice || !interop ||
      gpuDeviceApi(firstDevice) != gpuDeviceApi(secondDevice)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  first  = firstDevice->_priv;
  second = secondDevice->_priv;
  if (!first || !second ||
      !vk_samePhysicalDevice(firstDevice, secondDevice) ||
      !first->externalInterop || !second->externalInterop ||
      !first->timelineSemaphore || !second->timelineSemaphore) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (!vk_externalHandleTypes(&native->memoryHandleType,
                              &native->semaphoreHandleType)) {
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  interop->_priv = native;
  return GPU_OK;
}

static void
vk_destroyDeviceInterop(GPUDeviceInteropEXT *interop) {
  if (!interop) {
    return;
  }
  free(interop->_priv);
  interop->_priv = NULL;
}

static bool
vk_externalMemoryUsable(VkExternalMemoryProperties       properties,
                        VkExternalMemoryHandleTypeFlagBits handleType,
                        VkExternalMemoryFeatureFlags       required,
                        bool                              *outDedicated) {
  if ((properties.externalMemoryFeatures & required) != required ||
      (properties.compatibleHandleTypes & handleType) == 0u) {
    return false;
  }
  if (outDedicated) {
    *outDedicated =
      (properties.externalMemoryFeatures &
       VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0u;
  }
  return true;
}

static void
vk_bufferMemoryRequirements(VkDevice              device,
                            VkBuffer              buffer,
                            VkMemoryRequirements *outRequirements,
                            bool                 *outDedicated) {
  VkBufferMemoryRequirementsInfo2 info = {0};
  VkMemoryDedicatedRequirements  dedicated = {0};
  VkMemoryRequirements2          requirements = {0};

  info.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
  info.buffer        = buffer;
  dedicated.sType    = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetBufferMemoryRequirements2(device, &info, &requirements);
  *outRequirements = requirements.memoryRequirements;
  if (outDedicated &&
      (dedicated.requiresDedicatedAllocation ||
       dedicated.prefersDedicatedAllocation)) {
    *outDedicated = true;
  }
}

static void
vk_imageMemoryRequirements(VkDevice              device,
                           VkImage               image,
                           VkMemoryRequirements *outRequirements,
                           bool                 *outDedicated) {
  VkImageMemoryRequirementsInfo2 info = {0};
  VkMemoryDedicatedRequirements dedicated = {0};
  VkMemoryRequirements2         requirements = {0};

  info.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
  info.image         = image;
  dedicated.sType    = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
  requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  requirements.pNext = &dedicated;
  vkGetImageMemoryRequirements2(device, &info, &requirements);
  *outRequirements = requirements.memoryRequirements;
  if (outDedicated &&
      (dedicated.requiresDedicatedAllocation ||
       dedicated.prefersDedicatedAllocation)) {
    *outDedicated = true;
  }
}

static GPUResult
vk_externalBufferPlan(GPUDevice                 *device,
                      const GPUBufferCreateInfo *info,
                      VkBufferCreateInfo        *outCreateInfo,
                      VkMemoryRequirements      *outRequirements,
                      VkExternalMemoryHandleTypeFlagBits *outHandleType,
                      bool                      *outDedicated) {
  GPUDeviceVk  *native;
  GPUAdapterVk *adapter;
  VkPhysicalDeviceExternalBufferInfo    externalInfo = {0};
  VkExternalBufferProperties            externalProperties = {0};
  VkExternalMemoryBufferCreateInfo      externalCreate = {0};
  VkExternalSemaphoreHandleTypeFlagBits semaphoreHandleType;
  VkBuffer                              buffer;
  GPUResult                             result;

  native  = device ? device->_priv : NULL;
  adapter = device && device->adapter ? device->adapter->_priv : NULL;
  if (!native || !native->device || !native->externalInterop || !adapter ||
      !info || !outCreateInfo || !outRequirements || !outHandleType ||
      !outDedicated ||
      !vk_externalHandleTypes(outHandleType, &semaphoreHandleType)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(semaphoreHandleType);

  result = vk_bufferCreateInfo(device, info, outCreateInfo);
  if (result != GPU_OK) {
    return result;
  }
  externalInfo.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
  externalInfo.flags      = outCreateInfo->flags;
  externalInfo.usage      = outCreateInfo->usage;
  externalInfo.handleType = *outHandleType;
  externalProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
  vkGetPhysicalDeviceExternalBufferProperties(adapter->physicalDevice,
                                               &externalInfo,
                                               &externalProperties);
  if (!vk_externalMemoryUsable(
        externalProperties.externalMemoryProperties,
        *outHandleType,
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT,
        outDedicated
      )) {
    return GPU_ERROR_UNSUPPORTED;
  }

  externalCreate.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  externalCreate.handleTypes = *outHandleType;
  outCreateInfo->pNext        = &externalCreate;
  buffer = VK_NULL_HANDLE;
  if (vkCreateBuffer(native->device,
                     outCreateInfo,
                     NULL,
                     &buffer) != VK_SUCCESS) {
    outCreateInfo->pNext = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  vk_bufferMemoryRequirements(native->device,
                              buffer,
                              outRequirements,
                              outDedicated);
  vkDestroyBuffer(native->device, buffer, NULL);
  outCreateInfo->pNext = NULL;
  if (vk_filterMemoryTypes(device, outRequirements->memoryTypeBits) == 0u ||
      outRequirements->size == 0u || outRequirements->alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return GPU_OK;
}

static GPUResult
vk_getExternalBufferRequirements(GPUDevice                 *device,
                                  const GPUBufferCreateInfo *info,
                                  GPUMemoryRequirements     *outRequirements) {
  VkBufferCreateInfo                 createInfo = {0};
  VkMemoryRequirements               requirements;
  VkExternalMemoryHandleTypeFlagBits handleType;
  bool                               dedicated;
  GPUResult                          result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = vk_externalBufferPlan(device,
                                 info,
                                 &createInfo,
                                 &requirements,
                                 &handleType,
                                 &dedicated);
  GPU__UNUSED(handleType);
  GPU__UNUSED(dedicated);
  if (result != GPU_OK) {
    return result;
  }
  outRequirements->sizeBytes         = requirements.size;
  outRequirements->alignmentBytes    = requirements.alignment;
  outRequirements->compatibilityMask =
    vk_filterMemoryTypes(device, requirements.memoryTypeBits);
  return GPU_OK;
}

static GPUResult
vk_createExternalBuffer(GPUDevice                  *device,
                         const GPUBufferCreateInfo  *info,
                         GPUBuffer                 **outBuffer,
                         GPUExternalMemoryExport    *outExport) {
  GPUDeviceVk                        *native;
  VkBufferCreateInfo                  createInfo = {0};
  VkExternalMemoryBufferCreateInfo    externalCreate = {0};
  VkExportMemoryAllocateInfo          exportInfo = {0};
  VkMemoryDedicatedAllocateInfo       dedicatedInfo = {0};
  VkMemoryAllocateFlagsInfo           flagsInfo = {0};
  VkMemoryAllocateInfo                allocationInfo = {0};
  VkMemoryRequirements                requirements;
  VkExternalMemoryHandleTypeFlagBits  handleType;
  VkMemoryPropertyFlags               memoryFlags;
  GPUBufferVk                         state = {0};
  uint32_t                            memoryTypeIndex;
  bool                                dedicated, deviceAddress;
  GPUResult                           result;

  native = device ? device->_priv : NULL;
  if (!native || !info || !outBuffer || !outExport) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outBuffer = NULL;
  memset(outExport, 0, sizeof(*outExport));
  result = vk_externalBufferPlan(device,
                                 info,
                                 &createInfo,
                                 &requirements,
                                 &handleType,
                                 &dedicated);
  if (result != GPU_OK ||
      !vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryFlags);

  externalCreate.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  externalCreate.handleTypes = handleType;
  createInfo.pNext            = &externalCreate;
  state.device                = native->device;
  if (vkCreateBuffer(native->device,
                     &createInfo,
                     NULL,
                     &state.buffer) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  deviceAddress = (createInfo.usage &
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u;
  if (dedicated) {
    dedicatedInfo.sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.buffer = state.buffer;
  }
  if (deviceAddress) {
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  }
  if (dedicated) {
    dedicatedInfo.pNext = deviceAddress ? &flagsInfo : NULL;
  }
  exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
  exportInfo.pNext       = dedicated
                             ? (const void *)&dedicatedInfo
                             : (deviceAddress
                                  ? (const void *)&flagsInfo
                                  : NULL);
  exportInfo.handleTypes = handleType;
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext           = &exportInfo;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(native->device,
                       &allocationInfo,
                       NULL,
                       &state.memory) != VK_SUCCESS ||
      vkBindBufferMemory(native->device,
                         state.buffer,
                         state.memory,
                         0u) != VK_SUCCESS) {
    vk_destroySharedBufferState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if defined(_WIN32) || defined(WIN32)
  {
    VkMemoryGetWin32HandleInfoKHR getInfo = {0};
    HANDLE handle;

    handle             = NULL;
    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    getInfo.memory     = state.memory;
    getInfo.handleType = handleType;
    if (native->getMemoryHandle(native->device, &getInfo, &handle) !=
          VK_SUCCESS ||
        !handle) {
      vk_destroySharedBufferState(&state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.win32 = handle;
    outExport->type         = GPU_EXTERNAL_MEMORY_OPAQUE_WIN32;
  }
#elif defined(__linux__) || defined(__ANDROID__)
  {
    VkMemoryGetFdInfoKHR getInfo = {0};
    int fd;

    fd                 = -1;
    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    getInfo.memory     = state.memory;
    getInfo.handleType = handleType;
    if (native->getMemoryHandle(native->device, &getInfo, &fd) != VK_SUCCESS ||
        fd < 0) {
      vk_destroySharedBufferState(&state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.fd = fd;
    outExport->type      = GPU_EXTERNAL_MEMORY_OPAQUE_FD;
  }
#else
  vk_destroySharedBufferState(&state);
  return GPU_ERROR_UNSUPPORTED;
#endif

  state.ownsMemory = true;
  result = vk_wrapBuffer(device, info, &createInfo, &state, outBuffer);
  if (result != GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
    CloseHandle((HANDLE)outExport->handle.win32);
#elif defined(__linux__) || defined(__ANDROID__)
    close(outExport->handle.fd);
#endif
    memset(outExport, 0, sizeof(*outExport));
    return result;
  }
  outExport->sizeBytes = requirements.size;
  outExport->dedicated = dedicated;
  return GPU_OK;
}

static GPUResult
vk_externalTexturePlan(
  GPUDevice                  *device,
  const GPUTextureCreateInfo *info,
  VkImageCreateInfo          *outCreateInfo,
  VkImageAspectFlags         *outAspect,
  VkMemoryRequirements       *outRequirements,
  VkExternalMemoryHandleTypeFlagBits *outHandleType,
  bool                       *outDedicated
) {
  GPUDeviceVk                       *native;
  GPUAdapterVk                      *adapter;
  VkPhysicalDeviceExternalImageFormatInfo externalInfo = {0};
  VkPhysicalDeviceImageFormatInfo2  formatInfo = {0};
  VkExternalImageFormatProperties   externalProperties = {0};
  VkImageFormatProperties2          properties = {0};
  VkExternalMemoryImageCreateInfo   externalCreate = {0};
  VkExternalSemaphoreHandleTypeFlagBits semaphoreHandleType;
  VkImage                            image;
  GPUResult                         result;

  native  = device ? device->_priv : NULL;
  adapter = device && device->adapter ? device->adapter->_priv : NULL;
  if (!native || !native->device || !native->externalInterop || !adapter ||
      !info || !outCreateInfo || !outAspect || !outRequirements ||
      !outHandleType || !outDedicated ||
      !vk_externalHandleTypes(outHandleType, &semaphoreHandleType)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(semaphoreHandleType);

  result = vk_textureCreateInfo(device, info, outCreateInfo, outAspect);
  if (result != GPU_OK) {
    return result;
  }
  externalInfo.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
  externalInfo.handleType = *outHandleType;
  formatInfo.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
  formatInfo.pNext  = &externalInfo;
  formatInfo.format = outCreateInfo->format;
  formatInfo.type   = outCreateInfo->imageType;
  formatInfo.tiling = outCreateInfo->tiling;
  formatInfo.usage  = outCreateInfo->usage;
  formatInfo.flags  = outCreateInfo->flags;
  externalProperties.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
  properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
  properties.pNext = &externalProperties;
  if (vkGetPhysicalDeviceImageFormatProperties2(adapter->physicalDevice,
                                                 &formatInfo,
                                                 &properties) != VK_SUCCESS ||
      !vk_externalMemoryUsable(
        externalProperties.externalMemoryProperties,
        *outHandleType,
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT,
        outDedicated
      )) {
    return GPU_ERROR_UNSUPPORTED;
  }

  externalCreate.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalCreate.handleTypes = *outHandleType;
  outCreateInfo->pNext        = &externalCreate;
  image = VK_NULL_HANDLE;
  if (vkCreateImage(native->device,
                    outCreateInfo,
                    NULL,
                    &image) != VK_SUCCESS) {
    outCreateInfo->pNext = NULL;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  vk_imageMemoryRequirements(native->device,
                             image,
                             outRequirements,
                             outDedicated);
  vkDestroyImage(native->device, image, NULL);
  outCreateInfo->pNext = NULL;
  if (vk_filterMemoryTypes(device, outRequirements->memoryTypeBits) == 0u ||
      outRequirements->size == 0u || outRequirements->alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return GPU_OK;
}

static GPUResult
vk_getExternalTextureRequirements(
  GPUDevice                  *device,
  const GPUTextureCreateInfo *info,
  GPUMemoryRequirements      *outRequirements
) {
  VkImageCreateInfo                 createInfo = {0};
  VkImageAspectFlags                aspect;
  VkMemoryRequirements              requirements;
  VkExternalMemoryHandleTypeFlagBits handleType;
  bool                              dedicated;
  GPUResult                         result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = vk_externalTexturePlan(device,
                                  info,
                                  &createInfo,
                                  &aspect,
                                  &requirements,
                                  &handleType,
                                  &dedicated);
  GPU__UNUSED(aspect);
  GPU__UNUSED(handleType);
  GPU__UNUSED(dedicated);
  if (result != GPU_OK) {
    return result;
  }
  outRequirements->sizeBytes         = requirements.size;
  outRequirements->alignmentBytes    = requirements.alignment;
  outRequirements->compatibilityMask =
    vk_filterMemoryTypes(device, requirements.memoryTypeBits);
  return GPU_OK;
}

static GPUResult
vk_createExternalTexture(GPUDevice                   *device,
                         const GPUTextureCreateInfo  *info,
                         GPUTexture                 **outTexture,
                         GPUExternalMemoryExport     *outExport) {
  GPUDeviceVk                       *native;
  VkImageCreateInfo                  createInfo = {0};
  VkExternalMemoryImageCreateInfo    externalCreate = {0};
  VkExportMemoryAllocateInfo         exportInfo = {0};
  VkMemoryDedicatedAllocateInfo      dedicatedInfo = {0};
  VkMemoryAllocateInfo               allocationInfo = {0};
  VkMemoryRequirements               requirements;
  VkExternalMemoryHandleTypeFlagBits handleType;
  VkMemoryPropertyFlags              memoryFlags;
  GPUTextureVk                       state = {0};
  uint32_t                           memoryTypeIndex;
  bool                               dedicated;
  GPUResult                          result;

  native = device ? device->_priv : NULL;
  if (!native || !native->device || !info || !outTexture || !outExport) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  memset(outExport, 0, sizeof(*outExport));
  result = vk_externalTexturePlan(device,
                                  info,
                                  &createInfo,
                                  &state.aspect,
                                  &requirements,
                                  &handleType,
                                  &dedicated);
  if (result != GPU_OK ||
      !vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryFlags);
  if (createInfo.arrayLayers == 0u ||
      createInfo.mipLevels > UINT32_MAX / createInfo.arrayLayers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  externalCreate.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalCreate.handleTypes = handleType;
  createInfo.pNext            = &externalCreate;
  state.gpuDevice             = native;
  state.device                = native->device;
  state.layout                = VK_IMAGE_LAYOUT_UNDEFINED;
  state.layoutUniform         = true;
  state.mipLevelCount         = createInfo.mipLevels;
  state.arrayLayerCount       = createInfo.arrayLayers;
  state.subresourceCount      = createInfo.mipLevels * createInfo.arrayLayers;
  if (vkCreateImage(native->device,
                    &createInfo,
                    NULL,
                    &state.image) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (dedicated) {
    dedicatedInfo.sType =
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.image = state.image;
  }
  exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
  exportInfo.pNext       = dedicated ? &dedicatedInfo : NULL;
  exportInfo.handleTypes = handleType;
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext           = &exportInfo;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(native->device,
                       &allocationInfo,
                       NULL,
                       &state.memory) != VK_SUCCESS ||
      vkBindImageMemory(native->device,
                        state.image,
                        state.memory,
                        0u) != VK_SUCCESS) {
    vk_destroySharedTextureState(&state);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#if defined(_WIN32) || defined(WIN32)
  {
    VkMemoryGetWin32HandleInfoKHR getInfo = {0};
    HANDLE handle;

    handle             = NULL;
    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    getInfo.memory     = state.memory;
    getInfo.handleType = handleType;
    if (native->getMemoryHandle(native->device, &getInfo, &handle) !=
          VK_SUCCESS ||
        !handle) {
      vk_destroySharedTextureState(&state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.win32 = handle;
    outExport->type         = GPU_EXTERNAL_MEMORY_OPAQUE_WIN32;
  }
#elif defined(__linux__) || defined(__ANDROID__)
  {
    VkMemoryGetFdInfoKHR getInfo = {0};
    int fd;

    fd                 = -1;
    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    getInfo.memory     = state.memory;
    getInfo.handleType = handleType;
    if (native->getMemoryHandle(native->device, &getInfo, &fd) != VK_SUCCESS ||
        fd < 0) {
      vk_destroySharedTextureState(&state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.fd = fd;
    outExport->type      = GPU_EXTERNAL_MEMORY_OPAQUE_FD;
  }
#else
  vk_destroySharedTextureState(&state);
  return GPU_ERROR_UNSUPPORTED;
#endif

  state.ownsMemory = true;
  result = vk_finishTexture(device,
                            info,
                            &createInfo,
                            &state,
                            outTexture);
  if (result != GPU_OK) {
#if defined(_WIN32) || defined(WIN32)
    CloseHandle((HANDLE)outExport->handle.win32);
#elif defined(__linux__) || defined(__ANDROID__)
    close(outExport->handle.fd);
#endif
    memset(outExport, 0, sizeof(*outExport));
    return result;
  }
  outExport->sizeBytes = requirements.size;
  outExport->dedicated = dedicated;
  return GPU_OK;
}

static GPUResult
vk_sharedBufferPlan(GPUDeviceInteropEXT       *interop,
                    const GPUBufferCreateInfo *firstInfo,
                    const GPUBufferCreateInfo *secondInfo,
                    VkBufferCreateInfo        *outFirstInfo,
                    VkBufferCreateInfo        *outSecondInfo,
                    VkMemoryRequirements      *outFirstRequirements,
                    VkMemoryRequirements      *outSecondRequirements,
                    uint32_t                  *outMemoryTypes,
                    bool                      *outDedicated) {
  GPUDeviceVk                      *first, *second;
  GPUDeviceInteropVk               *native;
  GPUAdapterVk                     *adapter;
  VkPhysicalDeviceExternalBufferInfo externalInfo = {0};
  VkExternalBufferProperties        externalProperties = {0};
  VkExternalMemoryBufferCreateInfo  firstExternal = {0};
  VkExternalMemoryBufferCreateInfo  secondExternal = {0};
  VkBuffer                          firstBuffer, secondBuffer;
  uint32_t                          memoryTypes;
  GPUResult                         result;

  if (!outFirstInfo || !outSecondInfo || !outFirstRequirements ||
      !outSecondRequirements || !outMemoryTypes || !outDedicated) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = vk_interopDevices(interop, &first, &second, &native);
  if (result != GPU_OK) {
    return result;
  }
  adapter = interop->firstDevice->adapter
              ? interop->firstDevice->adapter->_priv
              : NULL;
  if (!adapter ||
      vk_bufferCreateInfo(interop->firstDevice,
                          firstInfo,
                          outFirstInfo) != GPU_OK ||
      vk_bufferCreateInfo(interop->secondDevice,
                          secondInfo,
                          outSecondInfo) != GPU_OK) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outFirstInfo->usage  |= outSecondInfo->usage;
  outSecondInfo->usage  = outFirstInfo->usage;
  firstExternal.sType       =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  firstExternal.handleTypes = native->memoryHandleType;
  secondExternal            = firstExternal;
  outFirstInfo->pNext       = &firstExternal;
  outSecondInfo->pNext      = &secondExternal;

  externalInfo.sType      =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
  externalInfo.flags      = outFirstInfo->flags;
  externalInfo.usage      = outFirstInfo->usage;
  externalInfo.handleType = native->memoryHandleType;
  externalProperties.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
  vkGetPhysicalDeviceExternalBufferProperties(adapter->physicalDevice,
                                               &externalInfo,
                                               &externalProperties);
  if (!vk_externalMemoryUsable(
        externalProperties.externalMemoryProperties,
        native->memoryHandleType,
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
        outDedicated
      )) {
    return GPU_ERROR_UNSUPPORTED;
  }

  firstBuffer  = VK_NULL_HANDLE;
  secondBuffer = VK_NULL_HANDLE;
  if (vkCreateBuffer(first->device,
                     outFirstInfo,
                     NULL,
                     &firstBuffer) != VK_SUCCESS ||
      vkCreateBuffer(second->device,
                     outSecondInfo,
                     NULL,
                     &secondBuffer) != VK_SUCCESS) {
    if (secondBuffer) {
      vkDestroyBuffer(second->device, secondBuffer, NULL);
    }
    if (firstBuffer) {
      vkDestroyBuffer(first->device, firstBuffer, NULL);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk_bufferMemoryRequirements(first->device,
                              firstBuffer,
                              outFirstRequirements,
                              outDedicated);
  vk_bufferMemoryRequirements(second->device,
                              secondBuffer,
                              outSecondRequirements,
                              outDedicated);
  vkDestroyBuffer(second->device, secondBuffer, NULL);
  vkDestroyBuffer(first->device, firstBuffer, NULL);

  memoryTypes = outFirstRequirements->memoryTypeBits &
                outSecondRequirements->memoryTypeBits;
  memoryTypes = vk_filterMemoryTypes(interop->firstDevice, memoryTypes) &
                vk_filterMemoryTypes(interop->secondDevice, memoryTypes);
  if (outFirstRequirements->size != outSecondRequirements->size ||
      outFirstRequirements->alignment != outSecondRequirements->alignment ||
      memoryTypes == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outMemoryTypes = memoryTypes;
  outFirstInfo->pNext  = NULL;
  outSecondInfo->pNext = NULL;
  return GPU_OK;
}

static GPUResult
vk_getSharedBufferRequirements(GPUDeviceInteropEXT       *interop,
                               const GPUBufferCreateInfo *firstInfo,
                               const GPUBufferCreateInfo *secondInfo,
                               GPUMemoryRequirements     *outRequirements) {
  VkBufferCreateInfo  firstCreateInfo = {0}, secondCreateInfo = {0};
  VkMemoryRequirements firstRequirements, secondRequirements;
  uint32_t             memoryTypes;
  bool                 dedicated;
  GPUResult            result;

  result = vk_sharedBufferPlan(interop,
                               firstInfo,
                               secondInfo,
                               &firstCreateInfo,
                               &secondCreateInfo,
                               &firstRequirements,
                               &secondRequirements,
                               &memoryTypes,
                               &dedicated);
  GPU__UNUSED(secondRequirements);
  GPU__UNUSED(dedicated);
  if (result != GPU_OK) {
    return result;
  }

  outRequirements->sizeBytes         = firstRequirements.size;
  outRequirements->alignmentBytes    = firstRequirements.alignment;
  outRequirements->compatibilityMask = memoryTypes;
  return GPU_OK;
}

static void
vk_destroySharedBufferState(GPUBufferVk *state) {
  if (!state || !state->device) {
    return;
  }
  if (state->buffer) {
    vkDestroyBuffer(state->device, state->buffer, NULL);
  }
  if (state->memory) {
    vkFreeMemory(state->device, state->memory, NULL);
  }
  memset(state, 0, sizeof(*state));
}

static GPUResult
vk_allocateSharedMemory(GPUDeviceVk                  *first,
                        GPUDeviceVk                  *second,
                        GPUDeviceInteropVk           *interop,
                        const VkMemoryRequirements   *requirements,
                        uint32_t                      memoryTypeIndex,
                        VkBuffer                      firstBuffer,
                        VkBuffer                      secondBuffer,
                        VkImage                       firstImage,
                        VkImage                       secondImage,
                        bool                          deviceAddress,
                        bool                          dedicated,
                        VkDeviceMemory               *outFirstMemory,
                        VkDeviceMemory               *outSecondMemory) {
  VkMemoryAllocateInfo          firstInfo = {0}, secondInfo = {0};
  VkExportMemoryAllocateInfo    exportInfo = {0};
  VkMemoryAllocateFlagsInfo     firstFlags = {0}, secondFlags = {0};
  VkMemoryDedicatedAllocateInfo firstDedicated = {0}, secondDedicated = {0};
  const void                   *firstNext, *secondNext;
  VkResult                      result;

  if (!first || !second || !interop || !requirements ||
      !outFirstMemory || !outSecondMemory) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFirstMemory  = VK_NULL_HANDLE;
  *outSecondMemory = VK_NULL_HANDLE;

  firstNext  = NULL;
  secondNext = NULL;
  if (dedicated) {
    firstDedicated.sType  =
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    firstDedicated.buffer = firstBuffer;
    firstDedicated.image  = firstImage;
    secondDedicated.sType  = firstDedicated.sType;
    secondDedicated.buffer = secondBuffer;
    secondDedicated.image  = secondImage;
    firstNext              = &firstDedicated;
    secondNext             = &secondDedicated;
  }
  if (deviceAddress) {
    firstFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    firstFlags.pNext = firstNext;
    firstFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    secondFlags      = firstFlags;
    secondFlags.pNext = secondNext;
    firstNext         = &firstFlags;
    secondNext        = &secondFlags;
  }

  exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
  exportInfo.pNext       = firstNext;
  exportInfo.handleTypes = interop->memoryHandleType;
  firstInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  firstInfo.pNext           = &exportInfo;
  firstInfo.allocationSize  = requirements->size;
  firstInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(first->device,
                       &firstInfo,
                       NULL,
                       outFirstMemory) != VK_SUCCESS) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  secondInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  secondInfo.allocationSize  = requirements->size;
  secondInfo.memoryTypeIndex = memoryTypeIndex;
#if defined(_WIN32) || defined(WIN32)
  {
    VkMemoryGetWin32HandleInfoKHR getInfo = {0};
    VkImportMemoryWin32HandleInfoKHR importInfo = {0};
    HANDLE handle;

    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    getInfo.memory     = *outFirstMemory;
    getInfo.handleType = interop->memoryHandleType;
    if (first->getMemoryHandle(first->device, &getInfo, &handle) != VK_SUCCESS ||
        !handle) {
      vkFreeMemory(first->device, *outFirstMemory, NULL);
      *outFirstMemory = VK_NULL_HANDLE;
      return GPU_ERROR_BACKEND_FAILURE;
    }

    importInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    importInfo.pNext      = secondNext;
    importInfo.handleType = interop->memoryHandleType;
    importInfo.handle     = handle;
    secondInfo.pNext      = &importInfo;
    result = vkAllocateMemory(second->device,
                              &secondInfo,
                              NULL,
                              outSecondMemory);
    CloseHandle(handle);
  }
#elif defined(__linux__) || defined(__ANDROID__)
  {
    VkMemoryGetFdInfoKHR getInfo = {0};
    VkImportMemoryFdInfoKHR importInfo = {0};
    int fd;

    fd                 = -1;
    getInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    getInfo.memory     = *outFirstMemory;
    getInfo.handleType = interop->memoryHandleType;
    if (first->getMemoryHandle(first->device, &getInfo, &fd) != VK_SUCCESS ||
        fd < 0) {
      vkFreeMemory(first->device, *outFirstMemory, NULL);
      *outFirstMemory = VK_NULL_HANDLE;
      return GPU_ERROR_BACKEND_FAILURE;
    }

    importInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importInfo.pNext      = secondNext;
    importInfo.handleType = interop->memoryHandleType;
    importInfo.fd         = fd;
    secondInfo.pNext      = &importInfo;
    result = vkAllocateMemory(second->device,
                              &secondInfo,
                              NULL,
                              outSecondMemory);
    if (result != VK_SUCCESS) {
      close(fd);
    }
  }
#else
  result = VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
  if (result != VK_SUCCESS) {
    vkFreeMemory(first->device, *outFirstMemory, NULL);
    *outFirstMemory = VK_NULL_HANDLE;
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static GPUResult
vk_createSharedBuffer(GPUDeviceInteropEXT       *interop,
                      const GPUBufferCreateInfo *firstInfo,
                      const GPUBufferCreateInfo *secondInfo,
                      GPUBuffer                **outFirstBuffer,
                      GPUBuffer                **outSecondBuffer) {
  GPUDeviceVk          *first, *second;
  GPUDeviceInteropVk   *native;
  VkBufferCreateInfo    firstCreateInfo = {0}, secondCreateInfo = {0};
  VkExternalMemoryBufferCreateInfo firstExternal = {0}, secondExternal = {0};
  VkMemoryRequirements  firstRequirements, secondRequirements;
  VkMemoryPropertyFlags memoryFlags;
  GPUBufferVk           firstState = {0}, secondState = {0};
  uint32_t              memoryTypes, memoryTypeIndex;
  bool                  dedicated, deviceAddress;
  GPUResult             result;

  result = vk_interopDevices(interop, &first, &second, &native);
  if (result != GPU_OK) {
    return result;
  }
  result = vk_sharedBufferPlan(interop,
                               firstInfo,
                               secondInfo,
                               &firstCreateInfo,
                               &secondCreateInfo,
                               &firstRequirements,
                               &secondRequirements,
                               &memoryTypes,
                               &dedicated);
  if (result != GPU_OK ||
      !vk_findMemoryType(interop->firstDevice,
                         memoryTypes,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(secondRequirements);
  GPU__UNUSED(memoryFlags);

  firstExternal.sType       =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
  firstExternal.handleTypes = native->memoryHandleType;
  secondExternal            = firstExternal;
  firstCreateInfo.pNext     = &firstExternal;
  secondCreateInfo.pNext    = &secondExternal;
  firstState.device         = first->device;
  secondState.device        = second->device;
  if (vkCreateBuffer(first->device,
                     &firstCreateInfo,
                     NULL,
                     &firstState.buffer) != VK_SUCCESS ||
      vkCreateBuffer(second->device,
                     &secondCreateInfo,
                     NULL,
                     &secondState.buffer) != VK_SUCCESS) {
    vk_destroySharedBufferState(&secondState);
    vk_destroySharedBufferState(&firstState);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  deviceAddress = (firstCreateInfo.usage &
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u;
  result = vk_allocateSharedMemory(first,
                                   second,
                                   native,
                                   &firstRequirements,
                                   memoryTypeIndex,
                                   firstState.buffer,
                                   secondState.buffer,
                                   VK_NULL_HANDLE,
                                   VK_NULL_HANDLE,
                                   deviceAddress,
                                   dedicated,
                                   &firstState.memory,
                                   &secondState.memory);
  if (result != GPU_OK ||
      vkBindBufferMemory(first->device,
                         firstState.buffer,
                         firstState.memory,
                         0u) != VK_SUCCESS ||
      vkBindBufferMemory(second->device,
                         secondState.buffer,
                         secondState.memory,
                         0u) != VK_SUCCESS) {
    vk_destroySharedBufferState(&secondState);
    vk_destroySharedBufferState(&firstState);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  firstState.allocationSize  = firstRequirements.size;
  secondState.allocationSize = secondRequirements.size;
  firstState.ownsMemory      = true;
  secondState.ownsMemory     = true;
  result = vk_wrapBuffer(interop->firstDevice,
                         firstInfo,
                         &firstCreateInfo,
                         &firstState,
                         outFirstBuffer);
  if (result != GPU_OK) {
    vk_destroySharedBufferState(&secondState);
    return result;
  }
  return vk_wrapBuffer(interop->secondDevice,
                       secondInfo,
                       &secondCreateInfo,
                       &secondState,
                       outSecondBuffer);
}

static GPUResult
vk_sharedTexturePlan(GPUDeviceInteropEXT        *interop,
                     const GPUTextureCreateInfo *firstInfo,
                     const GPUTextureCreateInfo *secondInfo,
                     VkImageCreateInfo          *outFirstInfo,
                     VkImageCreateInfo          *outSecondInfo,
                     VkImageAspectFlags         *outAspect,
                     VkMemoryRequirements       *outFirstRequirements,
                     VkMemoryRequirements       *outSecondRequirements,
                     uint32_t                   *outMemoryTypes,
                     bool                       *outDedicated) {
  GPUDeviceVk                       *first, *second;
  GPUDeviceInteropVk                *native;
  GPUAdapterVk                      *adapter;
  VkPhysicalDeviceExternalImageFormatInfo externalInfo = {0};
  VkPhysicalDeviceImageFormatInfo2  formatInfo = {0};
  VkExternalImageFormatProperties   externalProperties = {0};
  VkImageFormatProperties2          properties = {0};
  VkExternalMemoryImageCreateInfo   firstExternal = {0}, secondExternal = {0};
  VkImageAspectFlags                secondAspect;
  VkImage                           firstImage, secondImage;
  uint32_t                          memoryTypes;
  GPUResult                         result;

  if (!outFirstInfo || !outSecondInfo || !outAspect ||
      !outFirstRequirements || !outSecondRequirements ||
      !outMemoryTypes || !outDedicated) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = vk_interopDevices(interop, &first, &second, &native);
  if (result != GPU_OK) {
    return result;
  }
  adapter = interop->firstDevice->adapter
              ? interop->firstDevice->adapter->_priv
              : NULL;
  if (!adapter ||
      vk_textureCreateInfo(interop->firstDevice,
                           firstInfo,
                           outFirstInfo,
                           outAspect) != GPU_OK ||
      vk_textureCreateInfo(interop->secondDevice,
                           secondInfo,
                           outSecondInfo,
                           &secondAspect) != GPU_OK ||
      secondAspect != *outAspect) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outFirstInfo->usage  |= outSecondInfo->usage;
  outSecondInfo->usage  = outFirstInfo->usage;
  outFirstInfo->flags  |= outSecondInfo->flags;
  outSecondInfo->flags  = outFirstInfo->flags;

  externalInfo.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
  externalInfo.handleType = native->memoryHandleType;
  formatInfo.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
  formatInfo.pNext  = &externalInfo;
  formatInfo.format = outFirstInfo->format;
  formatInfo.type   = outFirstInfo->imageType;
  formatInfo.tiling = outFirstInfo->tiling;
  formatInfo.usage  = outFirstInfo->usage;
  formatInfo.flags  = outFirstInfo->flags;
  externalProperties.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
  properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
  properties.pNext = &externalProperties;
  if (vkGetPhysicalDeviceImageFormatProperties2(adapter->physicalDevice,
                                                 &formatInfo,
                                                 &properties) != VK_SUCCESS ||
      !vk_externalMemoryUsable(
        externalProperties.externalMemoryProperties,
        native->memoryHandleType,
        VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
        outDedicated
      )) {
    return GPU_ERROR_UNSUPPORTED;
  }

  firstExternal.sType       =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  firstExternal.handleTypes = native->memoryHandleType;
  secondExternal            = firstExternal;
  outFirstInfo->pNext       = &firstExternal;
  outSecondInfo->pNext      = &secondExternal;
  firstImage                = VK_NULL_HANDLE;
  secondImage               = VK_NULL_HANDLE;
  if (vkCreateImage(first->device,
                    outFirstInfo,
                    NULL,
                    &firstImage) != VK_SUCCESS ||
      vkCreateImage(second->device,
                    outSecondInfo,
                    NULL,
                    &secondImage) != VK_SUCCESS) {
    if (secondImage) {
      vkDestroyImage(second->device, secondImage, NULL);
    }
    if (firstImage) {
      vkDestroyImage(first->device, firstImage, NULL);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vk_imageMemoryRequirements(first->device,
                             firstImage,
                             outFirstRequirements,
                             outDedicated);
  vk_imageMemoryRequirements(second->device,
                             secondImage,
                             outSecondRequirements,
                             outDedicated);
  vkDestroyImage(second->device, secondImage, NULL);
  vkDestroyImage(first->device, firstImage, NULL);

  memoryTypes = outFirstRequirements->memoryTypeBits &
                outSecondRequirements->memoryTypeBits;
  memoryTypes = vk_filterMemoryTypes(interop->firstDevice, memoryTypes) &
                vk_filterMemoryTypes(interop->secondDevice, memoryTypes);
  if (outFirstRequirements->size != outSecondRequirements->size ||
      outFirstRequirements->alignment != outSecondRequirements->alignment ||
      memoryTypes == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outMemoryTypes = memoryTypes;
  outFirstInfo->pNext  = NULL;
  outSecondInfo->pNext = NULL;
  return GPU_OK;
}

static GPUResult
vk_getSharedTextureRequirements(GPUDeviceInteropEXT        *interop,
                                const GPUTextureCreateInfo *firstInfo,
                                const GPUTextureCreateInfo *secondInfo,
                                GPUMemoryRequirements      *outRequirements) {
  VkImageCreateInfo    firstCreateInfo = {0}, secondCreateInfo = {0};
  VkImageAspectFlags   aspect;
  VkMemoryRequirements firstRequirements, secondRequirements;
  uint32_t             memoryTypes;
  bool                 dedicated;
  GPUResult            result;

  result = vk_sharedTexturePlan(interop,
                                firstInfo,
                                secondInfo,
                                &firstCreateInfo,
                                &secondCreateInfo,
                                &aspect,
                                &firstRequirements,
                                &secondRequirements,
                                &memoryTypes,
                                &dedicated);
  GPU__UNUSED(aspect);
  GPU__UNUSED(secondRequirements);
  GPU__UNUSED(dedicated);
  if (result != GPU_OK) {
    return result;
  }

  outRequirements->sizeBytes         = firstRequirements.size;
  outRequirements->alignmentBytes    = firstRequirements.alignment;
  outRequirements->compatibilityMask = memoryTypes;
  return GPU_OK;
}

static void
vk_destroySharedTextureState(GPUTextureVk *state) {
  if (!state || !state->device) {
    return;
  }
  if (state->image) {
    vkDestroyImage(state->device, state->image, NULL);
  }
  if (state->memory) {
    vkFreeMemory(state->device, state->memory, NULL);
  }
  memset(state, 0, sizeof(*state));
}

static GPUResult
vk_createSharedTexture(GPUDeviceInteropEXT        *interop,
                       const GPUTextureCreateInfo *firstInfo,
                       const GPUTextureCreateInfo *secondInfo,
                       GPUTexture                **outFirstTexture,
                       GPUTexture                **outSecondTexture) {
  GPUDeviceVk         *first, *second;
  GPUDeviceInteropVk  *native;
  VkImageCreateInfo    firstCreateInfo = {0}, secondCreateInfo = {0};
  VkExternalMemoryImageCreateInfo firstExternal = {0}, secondExternal = {0};
  VkMemoryRequirements firstRequirements, secondRequirements;
  VkMemoryPropertyFlags memoryFlags;
  GPUTextureVk          firstState = {0}, secondState = {0};
  uint32_t              memoryTypes, memoryTypeIndex;
  bool                  dedicated;
  GPUResult             result;

  result = vk_interopDevices(interop, &first, &second, &native);
  if (result != GPU_OK) {
    return result;
  }
  result = vk_sharedTexturePlan(interop,
                                firstInfo,
                                secondInfo,
                                &firstCreateInfo,
                                &secondCreateInfo,
                                &firstState.aspect,
                                &firstRequirements,
                                &secondRequirements,
                                &memoryTypes,
                                &dedicated);
  if (result != GPU_OK ||
      !vk_findMemoryType(interop->firstDevice,
                         memoryTypes,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         0u,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryFlags);

  firstExternal.sType       =
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  firstExternal.handleTypes = native->memoryHandleType;
  secondExternal            = firstExternal;
  firstCreateInfo.pNext     = &firstExternal;
  secondCreateInfo.pNext    = &secondExternal;
  if (firstCreateInfo.arrayLayers == 0u ||
      firstCreateInfo.mipLevels > UINT32_MAX / firstCreateInfo.arrayLayers) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  firstState.gpuDevice      = first;
  firstState.device         = first->device;
  firstState.layout         = VK_IMAGE_LAYOUT_UNDEFINED;
  firstState.layoutUniform  = true;
  firstState.mipLevelCount  = firstCreateInfo.mipLevels;
  firstState.arrayLayerCount = firstCreateInfo.arrayLayers;
  firstState.subresourceCount = firstCreateInfo.mipLevels *
                                firstCreateInfo.arrayLayers;
  secondState               = firstState;
  secondState.gpuDevice     = second;
  secondState.device        = second->device;
  secondState.aspect        = firstState.aspect;
  if (vkCreateImage(first->device,
                    &firstCreateInfo,
                    NULL,
                    &firstState.image) != VK_SUCCESS ||
      vkCreateImage(second->device,
                    &secondCreateInfo,
                    NULL,
                    &secondState.image) != VK_SUCCESS) {
    vk_destroySharedTextureState(&secondState);
    vk_destroySharedTextureState(&firstState);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = vk_allocateSharedMemory(first,
                                   second,
                                   native,
                                   &firstRequirements,
                                   memoryTypeIndex,
                                   VK_NULL_HANDLE,
                                   VK_NULL_HANDLE,
                                   firstState.image,
                                   secondState.image,
                                   false,
                                   dedicated,
                                   &firstState.memory,
                                   &secondState.memory);
  if (result != GPU_OK ||
      vkBindImageMemory(first->device,
                        firstState.image,
                        firstState.memory,
                        0u) != VK_SUCCESS ||
      vkBindImageMemory(second->device,
                        secondState.image,
                        secondState.memory,
                        0u) != VK_SUCCESS) {
    vk_destroySharedTextureState(&secondState);
    vk_destroySharedTextureState(&firstState);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  firstState.ownsMemory  = true;
  secondState.ownsMemory = true;

  result = vk_finishTexture(interop->firstDevice,
                            firstInfo,
                            &firstCreateInfo,
                            &firstState,
                            outFirstTexture);
  if (result != GPU_OK) {
    vk_destroySharedTextureState(&secondState);
    return result;
  }
  return vk_finishTexture(interop->secondDevice,
                          secondInfo,
                          &secondCreateInfo,
                          &secondState,
                          outSecondTexture);
}

static GPUResult
vk_createSharedSemaphore(GPUDeviceInteropEXT          *interop,
                         const GPUSemaphoreCreateInfo *info,
                         GPUSemaphore                 *firstSemaphore,
                         GPUSemaphore                 *secondSemaphore) {
  GPUDeviceVk         *first, *second;
  GPUDeviceInteropVk  *interopVk;
  GPUAdapterVk        *adapter;
  GPUSemaphoreVk      *firstState, *secondState;
  VkPhysicalDeviceExternalSemaphoreInfo externalInfo = {0};
  VkExternalSemaphoreProperties externalProperties = {0};
  VkExportSemaphoreCreateInfo exportInfo = {0};
  VkSemaphoreTypeCreateInfo   firstType = {0}, secondType = {0};
  VkSemaphoreCreateInfo       firstInfo = {0}, secondInfo = {0};
  VkResult                    result;
  GPUResult                   interopResult;

  interopResult = vk_interopDevices(interop,
                                    &first,
                                    &second,
                                    &interopVk);
  if (interopResult != GPU_OK || !firstSemaphore || !secondSemaphore) {
    return interopResult != GPU_OK ? interopResult
                                   : GPU_ERROR_INVALID_ARGUMENT;
  }
  adapter = interop->firstDevice->adapter
              ? interop->firstDevice->adapter->_priv
              : NULL;
  if (!adapter) {
    return GPU_ERROR_UNSUPPORTED;
  }

  externalInfo.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
  firstType.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  firstType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  firstType.initialValue  = info ? info->initialValue : 0u;
  externalInfo.pNext      = &firstType;
  externalInfo.handleType = interopVk->semaphoreHandleType;
  externalProperties.sType =
    VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
  vkGetPhysicalDeviceExternalSemaphoreProperties(adapter->physicalDevice,
                                                  &externalInfo,
                                                  &externalProperties);
  if ((externalProperties.externalSemaphoreFeatures &
       (VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
        VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) !=
      (VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
       VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) ||
      (externalProperties.compatibleHandleTypes &
       interopVk->semaphoreHandleType) == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  firstState  = calloc(1, sizeof(*firstState));
  secondState = calloc(1, sizeof(*secondState));
  if (!firstState || !secondState) {
    free(secondState);
    free(firstState);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  secondType              = firstType;
  exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
  exportInfo.pNext       = &firstType;
  exportInfo.handleTypes = interopVk->semaphoreHandleType;
  firstInfo.sType        = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  firstInfo.pNext        = &exportInfo;
  secondInfo.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  secondInfo.pNext       = &secondType;
  if (vkCreateSemaphore(first->device,
                        &firstInfo,
                        NULL,
                        &firstState->semaphore) != VK_SUCCESS ||
      vkCreateSemaphore(second->device,
                        &secondInfo,
                        NULL,
                        &secondState->semaphore) != VK_SUCCESS) {
    result = VK_ERROR_INITIALIZATION_FAILED;
    goto fail;
  }

#if defined(_WIN32) || defined(WIN32)
  {
    VkSemaphoreGetWin32HandleInfoKHR getInfo = {0};
    VkImportSemaphoreWin32HandleInfoKHR importInfo = {0};
    HANDLE handle;

    getInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    getInfo.semaphore  = firstState->semaphore;
    getInfo.handleType = interopVk->semaphoreHandleType;
    if (first->getSemaphoreHandle(first->device,
                                  &getInfo,
                                  &handle) != VK_SUCCESS ||
        !handle) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
    }
    importInfo.sType      =
      VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    importInfo.semaphore  = secondState->semaphore;
    importInfo.handleType = interopVk->semaphoreHandleType;
    importInfo.handle     = handle;
    result = second->importSemaphoreHandle(second->device, &importInfo);
    CloseHandle(handle);
  }
#elif defined(__linux__) || defined(__ANDROID__)
  {
    VkSemaphoreGetFdInfoKHR getInfo = {0};
    VkImportSemaphoreFdInfoKHR importInfo = {0};
    int fd;

    fd                 = -1;
    getInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    getInfo.semaphore  = firstState->semaphore;
    getInfo.handleType = interopVk->semaphoreHandleType;
    if (first->getSemaphoreHandle(first->device,
                                  &getInfo,
                                  &fd) != VK_SUCCESS ||
        fd < 0) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
    }
    importInfo.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
    importInfo.semaphore  = secondState->semaphore;
    importInfo.handleType = interopVk->semaphoreHandleType;
    importInfo.fd         = fd;
    result = second->importSemaphoreHandle(second->device, &importInfo);
    if (result != VK_SUCCESS) {
      close(fd);
    }
  }
#else
  result = VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
  if (result != VK_SUCCESS) {
    goto fail;
  }

  firstState->device     = first->device;
  secondState->device    = second->device;
  firstSemaphore->_priv  = firstState;
  secondSemaphore->_priv = secondState;
  vk_setDebugName(interop->firstDevice,
                  VK_OBJECT_TYPE_SEMAPHORE,
                  (uint64_t)(uintptr_t)firstState->semaphore,
                  info ? info->label : NULL);
  vk_setDebugName(interop->secondDevice,
                  VK_OBJECT_TYPE_SEMAPHORE,
                  (uint64_t)(uintptr_t)secondState->semaphore,
                  info ? info->label : NULL);
  return GPU_OK;

fail:
  if (secondState->semaphore) {
    vkDestroySemaphore(second->device, secondState->semaphore, NULL);
  }
  if (firstState->semaphore) {
    vkDestroySemaphore(first->device, firstState->semaphore, NULL);
  }
  free(secondState);
  free(firstState);
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
vk_createExternalSemaphore(GPUDevice                     *device,
                            const GPUSemaphoreCreateInfo  *info,
                            GPUSemaphore                  *semaphore,
                            GPUExternalSemaphoreExport    *outExport) {
  GPUDeviceVk                           *native;
  GPUAdapterVk                          *adapter;
  GPUSemaphoreVk                        *state;
  VkExternalMemoryHandleTypeFlagBits     memoryHandleType;
  VkExternalSemaphoreHandleTypeFlagBits  handleType;
  VkPhysicalDeviceExternalSemaphoreInfo  externalInfo = {0};
  VkExternalSemaphoreProperties          externalProperties = {0};
  VkSemaphoreTypeCreateInfo              typeInfo = {0};
  VkExportSemaphoreCreateInfo            exportInfo = {0};
  VkSemaphoreCreateInfo                  createInfo = {0};

  native  = device ? device->_priv : NULL;
  adapter = device && device->adapter ? device->adapter->_priv : NULL;
  if (!native || !native->device || !native->externalInterop ||
      !native->timelineSemaphore || !adapter || !semaphore || !outExport ||
      !vk_externalHandleTypes(&memoryHandleType, &handleType)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  GPU__UNUSED(memoryHandleType);
  memset(outExport, 0, sizeof(*outExport));

  typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  typeInfo.initialValue  = info ? info->initialValue : 0u;
  externalInfo.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
  externalInfo.pNext      = &typeInfo;
  externalInfo.handleType = handleType;
  externalProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
  vkGetPhysicalDeviceExternalSemaphoreProperties(adapter->physicalDevice,
                                                  &externalInfo,
                                                  &externalProperties);
  if ((externalProperties.externalSemaphoreFeatures &
       VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) == 0u ||
      (externalProperties.compatibleHandleTypes & handleType) == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  state = calloc(1, sizeof(*state));
  if (!state) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  exportInfo.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
  exportInfo.pNext       = &typeInfo;
  exportInfo.handleTypes = handleType;
  createInfo.sType       = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  createInfo.pNext       = &exportInfo;
  if (vkCreateSemaphore(native->device,
                        &createInfo,
                        NULL,
                        &state->semaphore) != VK_SUCCESS) {
    free(state);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  state->device = native->device;

#if defined(_WIN32) || defined(WIN32)
  {
    VkSemaphoreGetWin32HandleInfoKHR getInfo = {0};
    HANDLE handle;

    handle             = NULL;
    getInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    getInfo.semaphore  = state->semaphore;
    getInfo.handleType = handleType;
    if (native->getSemaphoreHandle(native->device, &getInfo, &handle) !=
          VK_SUCCESS ||
        !handle) {
      vkDestroySemaphore(native->device, state->semaphore, NULL);
      free(state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.win32 = handle;
    outExport->type         = GPU_EXTERNAL_SEMAPHORE_TIMELINE_WIN32;
  }
#elif defined(__linux__) || defined(__ANDROID__)
  {
    VkSemaphoreGetFdInfoKHR getInfo = {0};
    int fd;

    fd                 = -1;
    getInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    getInfo.semaphore  = state->semaphore;
    getInfo.handleType = handleType;
    if (native->getSemaphoreHandle(native->device, &getInfo, &fd) !=
          VK_SUCCESS ||
        fd < 0) {
      vkDestroySemaphore(native->device, state->semaphore, NULL);
      free(state);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outExport->handle.fd = fd;
    outExport->type      = GPU_EXTERNAL_SEMAPHORE_TIMELINE_FD;
  }
#else
  vkDestroySemaphore(native->device, state->semaphore, NULL);
  free(state);
  return GPU_ERROR_UNSUPPORTED;
#endif

  semaphore->_priv = state;
  vk_setDebugName(device,
                  VK_OBJECT_TYPE_SEMAPHORE,
                  (uint64_t)(uintptr_t)state->semaphore,
                  info ? gpuDeviceDebugLabel(device, info->label) : NULL);
  return GPU_OK;
}

static GPUResult
vk_encodeSharedBuffers(GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers,
                       bool                            acquire) {
  GPUCommandBufferVk  *command;
  GPUDeviceVk         *device;
  GPUQueueVk          *queue;
  VkPipelineStageFlags srcStages, dstStages;
  uint32_t             offset;

  command = cmdb ? cmdb->_priv : NULL;
  device  = cmdb && cmdb->_queue && cmdb->_queue->_device
              ? cmdb->_queue->_device->_priv
              : NULL;
  queue   = cmdb && cmdb->_queue ? cmdb->_queue->_priv : NULL;
  if (!command || !command->command || !device || !queue) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  srcStages = acquire
                ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                : vk_barrierStages(device, barriers->srcStages);
  dstStages = acquire
                ? vk_barrierStages(device, barriers->dstStages)
                : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  offset = 0u;
  while (offset < barriers->bufferBarrierCount) {
    VkBufferMemoryBarrier nativeBarriers[VK_SHARED_BARRIER_CHUNK_SIZE];
    uint32_t              count;

    count = barriers->bufferBarrierCount - offset;
    if (count > VK_SHARED_BARRIER_CHUNK_SIZE) {
      count = VK_SHARED_BARRIER_CHUNK_SIZE;
    }
    for (uint32_t i = 0u; i < count; i++) {
      const GPUSharedBufferBarrierEXT *shared;
      GPUBuffer                       *buffer;
      GPUBufferVk                     *bufferVk;
      VkBufferMemoryBarrier           *native;

      shared   = &barriers->pBufferBarriers[offset + i];
      buffer   = acquire ? shared->destinationBuffer : shared->sourceBuffer;
      bufferVk = buffer->_priv;
      native   = &nativeBarriers[i];
      memset(native, 0, sizeof(*native));
      native->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      native->srcAccessMask = acquire
                                ? 0u
                                : vk_bufferBarrierAccess(
                                    buffer,
                                    shared->srcAccess,
                                    barriers->srcStages
                                  );
      native->dstAccessMask = acquire
                                ? vk_bufferBarrierAccess(
                                    buffer,
                                    shared->dstAccess,
                                    barriers->dstStages
                                  )
                                : 0u;
      native->srcQueueFamilyIndex = acquire
                                      ? VK_QUEUE_FAMILY_EXTERNAL
                                      : queue->familyIndex;
      native->dstQueueFamilyIndex = acquire
                                      ? queue->familyIndex
                                      : VK_QUEUE_FAMILY_EXTERNAL;
      native->buffer = bufferVk->buffer;
      native->offset = shared->offset;
      native->size   = shared->sizeBytes;
    }
    vk_pipelineBarrier(device,
                       command->command,
                       srcStages,
                       dstStages,
                       count,
                       nativeBarriers,
                       0u,
                       NULL);
    offset += count;
  }
  return GPU_OK;
}

static GPUResult
vk_encodeSharedTextures(GPUCommandBuffer               *cmdb,
                        const GPUSharedBarrierBatchEXT *barriers,
                        bool                            acquire) {
  GPUCommandBufferVk  *command;
  GPUDeviceVk         *device;
  GPUQueueVk          *queue;
  VkPipelineStageFlags srcStages, dstStages;

  command = cmdb ? cmdb->_priv : NULL;
  device  = cmdb && cmdb->_queue && cmdb->_queue->_device
              ? cmdb->_queue->_device->_priv
              : NULL;
  queue   = cmdb && cmdb->_queue ? cmdb->_queue->_priv : NULL;
  if (!command || !command->command || !device || !queue) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  srcStages = vk_barrierStages(device, barriers->srcStages);
  dstStages = vk_barrierStages(device, barriers->dstStages);
  for (uint32_t i = 0u; i < barriers->textureBarrierCount; i++) {
    const GPUSharedTextureBarrierEXT *shared;
    GPUTexture                       *texture;
    GPUTextureVk                     *textureVk;
    VkImageMemoryBarrier              ownership = {0};
    VkImageLayout                     destinationLayout;

    shared    = &barriers->pTextureBarriers[i];
    texture   = acquire ? shared->destinationTexture : shared->sourceTexture;
    textureVk = texture->_priv;
    destinationLayout = vk_textureBarrierLayout(shared->destinationTexture,
                                                shared->dstAccess,
                                                false);
    if (!acquire) {
      if (!vk_transitionTextureBarrier(command->command,
                                       textureVk,
                                       shared->baseMip,
                                       shared->mipCount,
                                       shared->baseLayer,
                                       shared->layerCount,
                                       VK_IMAGE_LAYOUT_GENERAL,
                                       srcStages,
                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       vk_barrierAccess(shared->srcAccess),
                                       VK_ACCESS_MEMORY_READ_BIT |
                                         VK_ACCESS_MEMORY_WRITE_BIT)) {
        return GPU_ERROR_BACKEND_FAILURE;
      }
    } else {
      vk_setTextureLayout(textureVk,
                          shared->baseMip,
                          shared->mipCount,
                          shared->baseLayer,
                          shared->layerCount,
                          VK_IMAGE_LAYOUT_GENERAL);
    }

    ownership.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ownership.srcAccessMask       = acquire
                                      ? 0u
                                      : VK_ACCESS_MEMORY_READ_BIT |
                                        VK_ACCESS_MEMORY_WRITE_BIT;
    ownership.dstAccessMask       = acquire
                                      ? VK_ACCESS_MEMORY_READ_BIT |
                                        VK_ACCESS_MEMORY_WRITE_BIT
                                      : 0u;
    ownership.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ownership.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ownership.srcQueueFamilyIndex = acquire
                                      ? VK_QUEUE_FAMILY_EXTERNAL
                                      : queue->familyIndex;
    ownership.dstQueueFamilyIndex = acquire
                                      ? queue->familyIndex
                                      : VK_QUEUE_FAMILY_EXTERNAL;
    ownership.image                           = textureVk->image;
    ownership.subresourceRange.aspectMask     = textureVk->aspect;
    ownership.subresourceRange.baseMipLevel   = shared->baseMip;
    ownership.subresourceRange.levelCount     = shared->mipCount;
    ownership.subresourceRange.baseArrayLayer = shared->baseLayer;
    ownership.subresourceRange.layerCount     = shared->layerCount;
    vk_pipelineBarrier(device,
                       command->command,
                       acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       acquire ? dstStages
                               : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0u,
                       NULL,
                       1u,
                       &ownership);

    if (acquire && destinationLayout != VK_IMAGE_LAYOUT_GENERAL &&
        !vk_transitionTextureBarrier(command->command,
                                     textureVk,
                                     shared->baseMip,
                                     shared->mipCount,
                                     shared->baseLayer,
                                     shared->layerCount,
                                     destinationLayout,
                                     dstStages,
                                     dstStages,
                                     VK_ACCESS_MEMORY_READ_BIT |
                                       VK_ACCESS_MEMORY_WRITE_BIT,
                                     vk_barrierAccess(shared->dstAccess))) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
  return GPU_OK;
}

static GPUResult
vk_encodeSharedBarriers(GPUDeviceInteropEXT           *interop,
                        GPUCommandBuffer               *cmdb,
                        const GPUSharedBarrierBatchEXT *barriers,
                        bool                            acquire) {
  GPUDeviceVk        *first, *second;
  GPUDeviceInteropVk *native;
  GPUResult           result;

  result = vk_interopDevices(interop, &first, &second, &native);
  if (result != GPU_OK || !cmdb || !barriers) {
    return result != GPU_OK ? result : GPU_ERROR_INVALID_ARGUMENT;
  }
  GPU__UNUSED(first);
  GPU__UNUSED(second);
  GPU__UNUSED(native);

  result = vk_encodeSharedBuffers(cmdb, barriers, acquire);
  if (result != GPU_OK) {
    return result;
  }
  return vk_encodeSharedTextures(cmdb, barriers, acquire);
}

static GPUResult
vk_encodeSharedRelease(GPUDeviceInteropEXT           *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers) {
  return vk_encodeSharedBarriers(interop, cmdb, barriers, false);
}

static GPUResult
vk_encodeSharedAcquire(GPUDeviceInteropEXT           *interop,
                       GPUCommandBuffer               *cmdb,
                       const GPUSharedBarrierBatchEXT *barriers) {
  return vk_encodeSharedBarriers(interop, cmdb, barriers, true);
}

static GPUResult
vk_encodeExternalRelease(GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  GPUResult result;

  result = vk_encodeSharedBuffers(cmdb, barriers, false);
  return result == GPU_OK
           ? vk_encodeSharedTextures(cmdb, barriers, false)
           : result;
}

static GPUResult
vk_encodeExternalAcquire(GPUCommandBuffer               *cmdb,
                         const GPUSharedBarrierBatchEXT *barriers) {
  GPUResult result;

  result = vk_encodeSharedBuffers(cmdb, barriers, true);
  return result == GPU_OK
           ? vk_encodeSharedTextures(cmdb, barriers, true)
           : result;
}

GPU_HIDE
void
vk_initMultiGPU(GPUApiMultiGPU *api) {
  api->createInterop          = vk_createDeviceInterop;
  api->destroyInterop         = vk_destroyDeviceInterop;
  api->getBufferRequirements  = vk_getSharedBufferRequirements;
  api->createBuffer           = vk_createSharedBuffer;
  api->getTextureRequirements = vk_getSharedTextureRequirements;
  api->createTexture          = vk_createSharedTexture;
  api->createSemaphore        = vk_createSharedSemaphore;
  api->encodeRelease          = vk_encodeSharedRelease;
  api->encodeAcquire          = vk_encodeSharedAcquire;
  api->getExternalBufferRequirements = vk_getExternalBufferRequirements;
  api->createExternalBuffer    = vk_createExternalBuffer;
  api->getExternalTextureRequirements = vk_getExternalTextureRequirements;
  api->createExternalTexture   = vk_createExternalTexture;
  api->createExternalSemaphore = vk_createExternalSemaphore;
  api->encodeExternalRelease   = vk_encodeExternalRelease;
  api->encodeExternalAcquire   = vk_encodeExternalAcquire;
}
