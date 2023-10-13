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
GPUPhysicalDevice*
vk_autoSelectPhysicalDevice(GPUInstance * __restrict inst) {
  return NULL;
}

GPU_HIDE
static
uint32_t
vk__findQueueFamily(GPUPhysicalDevice *phyDevice, GPUQueueFlagBits flags) {
  GPUPhysicalDeviceVk     *phyDeviceVk;
  VkQueueFamilyProperties *queFamilies;
  VkQueueFlagBits          vkFlags;
  uint32_t                 nQueFamilies, i, index;

  index        = UINT32_MAX;
  vkFlags      = 0;
  nQueFamilies = 0;
  phyDeviceVk  = phyDevice->priv;

  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice,
                                           &nQueFamilies,
                                           NULL);

  if (nQueFamilies == 0
      || !(queFamilies = malloc(nQueFamilies * sizeof(queFamilies)))) {
    return UINT32_MAX;
  }

  vkGetPhysicalDeviceQueueFamilyProperties(phyDeviceVk->phyDevice, 
                                           &nQueFamilies,
                                           queFamilies);

  if (flags & GPU_QUEUE_GRAPHICS_BIT)         vkFlags |= VK_QUEUE_GRAPHICS_BIT;
  if (flags & GPU_QUEUE_COMPUTE_BIT)          vkFlags |= VK_QUEUE_COMPUTE_BIT;
  if (flags & GPU_QUEUE_TRANSFER_BIT)         vkFlags |= VK_QUEUE_TRANSFER_BIT;
  if (flags & GPU_QUEUE_SPARSE_BINDING_BIT)   vkFlags |= VK_QUEUE_SPARSE_BINDING_BIT;
  if (flags & GPU_QUEUE_PROTECTED_BIT)        vkFlags |= VK_QUEUE_PROTECTED_BIT;
  if (flags & GPU_QUEUE_VIDEO_DECODE_BIT_KHR) vkFlags |= VK_QUEUE_VIDEO_DECODE_BIT_KHR;
  if (flags & GPU_QUEUE_VIDEO_ENCODE_BIT_KHR) vkFlags |= VK_QUEUE_VIDEO_ENCODE_BIT_KHR;
  if (flags & GPU_QUEUE_OPTICAL_FLOW_BIT_NV)  vkFlags |= VK_QUEUE_OPTICAL_FLOW_BIT_NV;

  for (i = 0; i < nQueFamilies; i++) {
    if ((queFamilies[i].queueFlags & vkFlags) == vkFlags) {
      index = i;
      break;
    }
  }

  free(queFamilies);

  return index;
}

GPU_HIDE
static GPUCommandQueue*
vk__createCmdQueue(GPUDeviceVk             * __restrict deviceVk,
                   VkDeviceQueueCreateInfo * __restrict ci) {
  GPUCommandQueue   *que;
  GPUCommandQueueVk *queVk;

  queVk           = calloc(1, sizeof(*que));
  que             = calloc(1, sizeof(*que));
  que->priv       = queVk;
  queVk->createCI = ci;

  vkGetDeviceQueue(deviceVk->device, ci->queueFamilyIndex, 0, &queVk->queRaw);

  return que;
}

GPU_HIDE
GPUDevice*
vk_createDevice(GPUPhysicalDevice * __restrict phyDevice,
                GPUCommandQueueCreateInfo      queCI[],
                uint32_t                       nQueCI) {
  GPUInstance            *inst;
  GPUDevice              *device;
  GPUDeviceVk            *deviceVk;
  GPUPhysicalDeviceVk    *phyDeviceVk;
  GPUInstanceVk          *instVk;
  GPUCommandQueue        **createdQueues;
  float                  *queuePriorities;
  VkResult U_ASSERT_ONLY  err;
  VkDeviceQueueCreateInfo queues[nQueCI];
  VkDeviceCreateInfo      deviceCI = {0};
  GPUQueueFlagBits        queueFamilies;
  uint32_t                i, queueFamilyIndex, nQueues, maxQueCount;

  queueFamilyIndex = UINT32_MAX;
  nQueues          = 0;
  maxQueCount      = 0;
  queueFamilies    = 0;
  inst             = phyDevice->inst;
  phyDeviceVk      = phyDevice->priv;
  instVk           = inst->_priv;
  deviceVk         = calloc(1, sizeof(*deviceVk));
  device           = calloc(1, sizeof(*device));

  for (i = 0; i < nQueCI; i++) {
    if (queCI[i].count > maxQueCount) {
      maxQueCount = queCI[i].count;
    }
  }

  queuePriorities = alloca(maxQueCount);
  for (i = 0; i < maxQueCount; i++) {
    queuePriorities[i] = 1.0f; /* default queue priority */
  }

  for (i = 0; i < nQueCI; i++) {
    queueFamilyIndex = vk__findQueueFamily(phyDevice, queCI[i].flags);
    if(queueFamilyIndex == UINT32_MAX) {
      /* handle the error: The requested queue capabilities
         are not supported by this physical device. */
      continue;
    }

    queueFamilies                 |= queCI[i].flags;

    queues[nQueues].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[nQueues].pNext            = NULL;
    queues[nQueues].flags            = 0;
    queues[nQueues].queueFamilyIndex = queueFamilyIndex;
    queues[nQueues].queueCount       = queCI[i].count;
    queues[nQueues].pQueuePriorities = queuePriorities;

    nQueues++;
  }

  createdQueues = calloc(nQueues, sizeof(*createdQueues));
  for (i = 0; i < nQueues; i++) {
    createdQueues[i] = vk__createCmdQueue(deviceVk, &queues[i]);
  }

  /* If specific features are required, pass them in here: pEnabledFeatures */
  deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.queueCreateInfoCount    = nQueues;
  deviceCI.pQueueCreateInfos       = queues;
  deviceCI.enabledExtensionCount   = instVk->nEnabledExtensions;
  deviceCI.ppEnabledExtensionNames = (void *)instVk->extensionNames;

  err = vkCreateDevice(phyDeviceVk->phyDevice, &deviceCI, NULL, &deviceVk->device);
  if(!err) {
#if DEBUG
    fprintf(stderr, "vkCreateDevice failed: %d\n", err);
#endif
    goto err;
  }

  device->priv             = deviceVk;
  device->inst             = inst;
  device->phyDevice        = phyDevice;
  device->queueFamilies    = queueFamilies;

  deviceVk->createCI       = queCI;
  deviceVk->nCreateCI      = nQueCI;
  deviceVk->nCreatedQueues = nQueues;
  deviceVk->createdQueues  = createdQueues;

  /* set device->availableQueues by created available queues */

  return device;
err:

  if (deviceVk) { free(deviceVk); }
  if (device)   { free(device); }

  return NULL;
}

GPU_HIDE
GPUDevice*
vk_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;

  phyDevice = GPUGetFirstPhysicalDevice(inst);

  /* TODO: */
  return vk_createDevice(phyDevice, (GPUCommandQueueCreateInfo[]){
    [0] = {
      .count = 1,
      .flags = GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT,
    },
    [1] = {
      .count = 1,
      .flags = GPU_QUEUE_GRAPHICS_BIT
    },
    [2] = {
      .count = 1,
      .flags = GPU_QUEUE_COMPUTE_BIT
    }
  }, 3);
}

GPU_HIDE
void
vk_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = vk_getAvailablePhysicalDevicesBy;
  apiDevice->createDevice                  = vk_createDevice;
  apiDevice->createSystemDefaultDevice     = vk_createSystemDefaultDevice;
}
