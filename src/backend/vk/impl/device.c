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
GPUPhysicalDevice*
vk_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                 GPUInstance * __restrict inst,
                                 uint32_t                 maxNumberOfItems) {
  GPUInstanceVk             *instVk;
  GPUPhysicalDevice         *phyDevice;
  GPUPhysicalDeviceVk       *phyDeviceVk;
  VkPhysicalDevice          *phyDevices;
  VkPhysicalDeviceProperties phyDeviceProps;
  VkInstance                 instRaw;
  GPUFeatures                gpuFeatures;
  VkResult                   err;
  uint32_t                   i, gpuCount, nPhyDeviceExtensions;
  VkBool32                   swapchainExtFound;
  int                        gpuIndex;
  bool                       incrementalPresentEnabled, displayTimingEnabled;

  gpuIndex                  = -1;
  nPhyDeviceExtensions      = 0;
  swapchainExtFound         = 0;
  instVk                    = inst->_priv;
  instRaw                   = instVk->inst;
  phyDevice                 = calloc(1, sizeof(*phyDevice));
  phyDeviceVk               = calloc(1, sizeof(*phyDeviceVk));
  phyDevice->priv           = phyDeviceVk;

  gpuFeatures               = inst->initParams->optionalFeatures | inst->initParams->requiredFeatures;
  incrementalPresentEnabled = (gpuFeatures & GPU_FEATURE_INCREMENTAL_PRESENT);
  displayTimingEnabled      = (gpuFeatures & GPU_FEATURE_DISPLAY_TIMING);

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
  if (gpuIndex == -1) {
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
        gpuIndex = i;
        break;
      }
    }
  }
#endif

  phyDeviceVk->phyDevice = phyDevices[gpuIndex];
#if DEBUG
  {
    vkGetPhysicalDeviceProperties(phyDeviceVk->phyDevice, &phyDeviceProps);
    fprintf(stderr, "Selected GPU %d: %s, type: %s\n",
            gpuIndex,
            phyDeviceProps.deviceName,
            vk__devicetype_string(phyDeviceProps.deviceType));
  }
#endif
  free(phyDevices);

  /* Look for device extensions */
  memset(instVk->extensionNames, 0, sizeof(*instVk->extensionNames));

  err = vkEnumerateDeviceExtensionProperties(phyDeviceVk->phyDevice, 
                                             NULL, 
                                             &nPhyDeviceExtensions,
                                             NULL);
  assert(!err);

  if (nPhyDeviceExtensions > 0) {
    VkExtensionProperties *device_extensions;
    device_extensions = malloc(sizeof(*device_extensions) * nPhyDeviceExtensions);
    err               = vkEnumerateDeviceExtensionProperties(phyDeviceVk->phyDevice,
                                                             NULL,
                                                             &nPhyDeviceExtensions,
                                                             device_extensions);
    assert(!err);

    for (i = 0; i < nPhyDeviceExtensions; i++) {
      if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, 
                  device_extensions[i].extensionName)) {
        swapchainExtFound = 1;
        instVk->extensionNames[instVk->nEnabledExtensions++] 
            = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      }

      if (!strcmp("VK_KHR_portability_subset", 
                  device_extensions[i].extensionName)) {
        instVk->extensionNames[instVk->nEnabledExtensions++] 
            = "VK_KHR_portability_subset";
      }
      assert(instVk->nEnabledExtensions < 64);
    }

    if (incrementalPresentEnabled) {
      // Even though the user "enabled" the extension via the command
      // line, we must make sure that it's enumerated for use with the
      // device.  Therefore, disable it here, and re-enable it again if
      // enumerated.
      incrementalPresentEnabled = false;
      for (i = 0; i < nPhyDeviceExtensions; i++) {
        if (!strcmp(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME, 
                    device_extensions[i].extensionName)) {
          instVk->extensionNames[instVk->nEnabledExtensions++] 
              = VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
          incrementalPresentEnabled = true;
        }
        assert(instVk->nEnabledExtensions < 64);
      }
    }

    if (displayTimingEnabled) {
      // Even though the user "enabled" the extension via the command
      // line, we must make sure that it's enumerated for use with the
      // device.  Therefore, disable it here, and re-enable it again if
      // enumerated.
      displayTimingEnabled = false;
      for (i = 0; i < nPhyDeviceExtensions; i++) {
        if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, 
                    device_extensions[i].extensionName)) {
          instVk->extensionNames[instVk->nEnabledExtensions++] 
              = VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME;
          displayTimingEnabled = true;
        }
        assert(instVk->nEnabledExtensions < 64);
      }
    }

    free(device_extensions);
  }

  vkGetPhysicalDeviceProperties(phyDeviceVk->phyDevice, &phyDeviceVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice, 
                                           &phyDeviceVk->queueFamilyCount,
                                           NULL);
  assert(phyDeviceVk->queueFamilyCount >= 1);

  phyDeviceVk->queueFamilyProps = malloc(phyDeviceVk->queueFamilyCount 
                                         * sizeof(*phyDeviceVk->queueFamilyProps));
  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice,
                                           &phyDeviceVk->queueFamilyCount,
                                           phyDeviceVk->queueFamilyProps);

  // Query fine-grained feature support for this device.
  //  If app has specific feature requirements it should check supported
  //  features based on this query
  vkGetPhysicalDeviceFeatures(phyDeviceVk->phyDevice, &phyDeviceVk->physDevFeatures);

  phyDevice->supportsSwapchain          = swapchainExtFound;
  phyDevice->supportsDisplayTiming      = displayTimingEnabled;
  phyDevice->supportsIncrementalPresent = incrementalPresentEnabled;

  return phyDevice;
}

GPU_HIDE
GPUDevice*
vk_createDevice(GPUInstance       * __restrict inst,
                GPUPhysicalDevice * __restrict phyDevice,
                GPUCommandQueueCreateInfo      queueCI[],
                uint32_t                       nQueueCI) {
  GPUDevice              *device;
  GPUDeviceVk            *deviceVk;
  GPUPhysicalDeviceVk    *phyDeviceVk;
  GPUInstanceVk          *instVk;
  VkResult U_ASSERT_ONLY  err;
  float                   queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[nQueueCI];
  uint32_t                i;

//  vk_createDevice(api, inst, phyDevice, (GPUCommandQueueCreateInfo[]){
//    [0] = {
//      .flags = GPU_QUEUE_GRAPHICS_BIT,
//      .count = 0
//    }
//  }, 2);

  phyDeviceVk                = phyDevice->priv;
  instVk                     = inst->_priv;
  deviceVk                   = calloc(1, sizeof(*deviceVk));

  // Search for a graphics and a present queue in the array of queue
  // families, try to find one that supports both
  uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
  uint32_t presentQueueFamilyIndex  = UINT32_MAX;

  for (i = 0; i < phyDeviceVk->queueFamilyCount; i++) {
    if ((phyDeviceVk->queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      if (graphicsQueueFamilyIndex == UINT32_MAX) {
        graphicsQueueFamilyIndex = i;
      }

//      if (supportsPresent[i] == VK_TRUE) {
//        graphicsQueueFamilyIndex = i;
//        presentQueueFamilyIndex  = i;
//        break;
//      }
    }
  }

//  if (presentQueueFamilyIndex == UINT32_MAX) {
//    // If didn't find a queue that supports both graphics and present, then
//    // find a separate present queue.
//    for (uint32_t i = 0; i < phyDeviceVk->queueFamilyCount; ++i) {
//      if (supportsPresent[i] == VK_TRUE) {
//        presentQueueFamilyIndex = i;
//        break;
//      }
//    }
//  }

//  queues[0].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
//  queues[0].pNext            = NULL;
//  queues[0].queueFamilyIndex = demo->graphics_queue_family_index;
//  queues[0].queueCount       = 1;
//  queues[0].pQueuePriorities = queue_priorities;
//  queues[0].flags            = 0;
//
//  VkDeviceCreateInfo createInfoCI = {
//    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
//    .pNext                   = NULL,
//    .queueCreateInfoCount    = 1,
//    .pQueueCreateInfos       = queues,
//    .enabledLayerCount       = 0,
//    .ppEnabledLayerNames     = NULL,
//    .enabledExtensionCount   = instVk->nEnabledExtensions,
//    .ppEnabledExtensionNames = (const char *const *)instVk->extensionNames,
//    .pEnabledFeatures        = NULL,  // If specific features are required, pass them in here
//  };
//
//  if (demo->separate_present_queue) {
//    queues[1].sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
//    queues[1].pNext                   = NULL;
//    queues[1].queueFamilyIndex        = demo->present_queue_family_index;
//    queues[1].queueCount              = 1;
//    queues[1].pQueuePriorities        = queue_priorities;
//    queues[1].flags                   = 0;
//    createInfoCI.queueCreateInfoCount = 2;
//  }
//
//  err = vkCreateDevice(phyDeviceVk->phyDevice, &createInfoCI, NULL, &deviceVk->device);
//  assert(!err);
//
//  device       = calloc(1, sizeof(*device));
//  device->priv = NULL;
//
//  /* TODO: select-phy device auto */

  return device;
}

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
void
vk_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = vk_getAvailablePhysicalDevicesBy;
  apiDevice->createDevice                  = vk_createDevice;
  apiDevice->createSystemDefaultDevice     = vk_createSystemDefaultDevice;
}
