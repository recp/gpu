/*
 * Copyright (C) 2020 Recep Aslantas
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

GPU_HIDE
static char const *
vk__devicetype_string(const VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
      return "Other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "IntegratedGpu";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "DiscreteGpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "VirtualGpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "Cpu";
    default:
      return "Unknown";
  }
}

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
GPU_HIDE
static int
vk__find_display_gpu(int               gpu_number,
                     uint32_t          gpu_count,
                     VkPhysicalDevice *physical_devices) {
  uint32_t               i, display_count;
  VkResult U_ASSERT_ONLY result;
  int                    gpu_return;

  display_count = 0;
  gpu_return    = gpu_number;

  if (gpu_number >= 0) {
    result = vkGetPhysicalDeviceDisplayPropertiesKHR(physical_devices[gpu_number],
                                                     &display_count,
                                                     NULL);
    assert(!result);
  } else {
    for (i = 0; i < gpu_count; i++) {
      result = vkGetPhysicalDeviceDisplayPropertiesKHR(physical_devices[i], 
                                                       &display_count,
                                                       NULL);
      assert(!result);

      if (display_count) {
        gpu_return = i;
        break;
      }
    }
  }

  return display_count > 0 ? gpu_return : -1;
}
#endif

GPU_HIDE
GPUDevice*
vk_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUDevice     *device;

  device       = calloc(1, sizeof(*device));
  device->priv = NULL;

  /* TODO: select-phy device auto */

  return device;
}

GPU_HIDE
GPUPhysicalDevice*
vk_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                 GPUInstance * __restrict inst) {
  GPUInstanceVk             *instVk;
  GPUPhysicalDevice         *phyDevice;
  GPUPhysicalDeviceVk       *phyDeviceVk;
  VkPhysicalDevice          *phyDevices;
  VkPhysicalDeviceProperties phyDeviceProps;
  VkInstance                 instRaw;
  VkResult                   err;
  uint32_t                   gpuCount;
  int                        gpu_number;

  gpu_number      = -1;
  instVk          = inst->_priv;
  instRaw         = instVk->inst;
  phyDevice       = calloc(1, sizeof(*phyDevice));
  phyDeviceVk     = calloc(1, sizeof(*phyDeviceVk));
  phyDevice->priv = phyDeviceVk;

  gpuCount = 0;
  err      = vkEnumeratePhysicalDevices(instRaw, &gpuCount, NULL);
  assert(!err);

  if (gpuCount <= 0) {
    ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n",
             "vkEnumeratePhysicalDevices Failure");
  }

  phyDevices = malloc(sizeof(VkPhysicalDevice) * gpuCount);
  err        = vkEnumeratePhysicalDevices(instRaw, &gpuCount, phyDevices);
  assert(!err);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  gpu_number = vk__find_display_gpu(gpu_number, gpu_count, physical_devices);
  if (gpu_number < 0) {
    ERR_EXIT("Cannot find any display!\n", "vk__find_display_gpu failure");
  }
#else
  /* Try to auto select most suitable device */
  if (gpu_number == -1) {
    uint32_t             countDeviceType[VK_PHYSICAL_DEVICE_TYPE_CPU + 1], i;
    VkPhysicalDeviceType searchForDeviceType;

    memset(countDeviceType, 0, sizeof(countDeviceType));
    searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

    for (i = 0; i < gpuCount; i++) {
      vkGetPhysicalDeviceProperties(phyDevices[i], &phyDeviceProps);
      assert(phyDeviceProps.deviceType <= VK_PHYSICAL_DEVICE_TYPE_CPU);
      countDeviceType[phyDeviceProps.deviceType]++;
    }

    if (countDeviceType[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]) {
      searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    } else if (countDeviceType[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU]) {
      searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    } else if (countDeviceType[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]) {
      searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    } else if (countDeviceType[VK_PHYSICAL_DEVICE_TYPE_CPU]) {
      searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    } else if (countDeviceType[VK_PHYSICAL_DEVICE_TYPE_OTHER]) {
      searchForDeviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    }

    for (i = 0; i < gpuCount; i++) {
      vkGetPhysicalDeviceProperties(phyDevices[i], &phyDeviceProps);
      if (phyDeviceProps.deviceType == searchForDeviceType) {
        gpu_number = i;
        break;
      }
    }
  }
#endif

  phyDeviceVk->phyDevice = phyDevices[gpu_number];
#if DEBUG
  {
    vkGetPhysicalDeviceProperties(phyDeviceVk->phyDevice, &phyDeviceProps);
    fprintf(stderr, "Selected GPU %d: %s, type: %s\n",
            gpu_number,
            phyDeviceProps.deviceName,
            vk__devicetype_string(phyDeviceProps.deviceType));
  }
#endif
  free(phyDevices);

  vkGetPhysicalDeviceProperties(phyDeviceVk->phyDevice, &phyDeviceVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice, 
                                           &phyDeviceVk->queueFamilyCount,
                                           NULL);
  assert(phyDeviceVk->queueFamilyCount >= 1);

  phyDeviceVk->queueFamilyProps = malloc(phyDeviceVk->queueFamilyCount * sizeof(*phyDeviceVk->queueFamilyProps));
  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice, 
                                           &phyDeviceVk->queueFamilyCount,
                                           phyDeviceVk->queueFamilyProps);

  // Query fine-grained feature support for this device.
  //  If app has specific feature requirements it should check supported
  //  features based on this query
  vkGetPhysicalDeviceFeatures(phyDeviceVk->phyDevice, &phyDeviceVk->physDevFeatures);

  return phyDevice;
}

GPU_HIDE
void
vk_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->createSystemDefaultDevice     = vk_createSystemDefaultDevice;
  apiDevice->getAvailablePhysicalDevicesBy = vk_getAvailablePhysicalDevicesBy;
}
