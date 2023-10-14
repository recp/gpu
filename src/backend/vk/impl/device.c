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
vk__newPhyDeviceFrom(GPUInstance * __restrict inst, VkPhysicalDevice raw) {
  GPUPhysicalDevice     *item;
  GPUPhysicalDeviceVk   *itemVk;
  VkExtensionProperties *extensions;
  GPUFeatures            gpuFeatures;
  VkResult               err;
  uint32_t               i, nExtensions;
  VkBool32               swapchainExtFound;
  bool                   incrementalPresentEnabled, displayTimingEnabled;

  gpuFeatures               = inst->initParams->optionalFeatures | inst->initParams->requiredFeatures;
  nExtensions               = swapchainExtFound = 0;

  incrementalPresentEnabled = (gpuFeatures & GPU_FEATURE_INCREMENTAL_PRESENT);
  displayTimingEnabled      = (gpuFeatures & GPU_FEATURE_DISPLAY_TIMING);

  item                      = calloc(1, sizeof(*item));
  itemVk                    = calloc(1, sizeof(*itemVk));
  itemVk->phyDevice         = raw;
  item->_priv               = itemVk;
  item->inst                = inst;

  vkGetPhysicalDeviceProperties(raw, &itemVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(itemVk->phyDevice, &itemVk->nQueFamilies, NULL);
  assert(itemVk->nQueFamilies >= 1);

  itemVk->queueFamilyProps = malloc(itemVk->nQueFamilies * sizeof(*itemVk->queueFamilyProps));
  vkGetPhysicalDeviceQueueFamilyProperties(itemVk->phyDevice,
                                           &itemVk->nQueFamilies,
                                           itemVk->queueFamilyProps);
  vkGetPhysicalDeviceFeatures(itemVk->phyDevice, &itemVk->features);

  /* Look for device extensions */
  err = vkEnumerateDeviceExtensionProperties(itemVk->phyDevice, NULL, 
                                             &nExtensions, NULL);
  assert(!err);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  err = vkGetPhysicalDeviceDisplayPropertiesKHR(itemVk->phyDevice,
                                                &itemVk->nDisplayProperties,
                                                NULL);
#endif

#define VK__ADD_EXT_IF(X, R)                                                  \
    if (!strcmp(X, extensions[i].extensionName)) {                            \
      itemVk->extensionNames[itemVk->nEnabledExtensions++] = X;               \
      R;                                                                      \
    }                                                                         \
    assert(itemVk->nEnabledExtensions < 64);

  if (nExtensions > 0) {
    extensions = malloc(sizeof(*extensions) * nExtensions);
    err        = vkEnumerateDeviceExtensionProperties(itemVk->phyDevice, NULL,
                                                      &nExtensions, extensions);
    assert(!err);

    for (i = 0; i < nExtensions; i++) {
      VK__ADD_EXT_IF(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                     item->supportsSwapchain = true);

      VK__ADD_EXT_IF("VK_KHR_portability_subset", (void)NULL);

      if (incrementalPresentEnabled) {
        VK__ADD_EXT_IF(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
                       item->supportsIncrementalPresent = true);
      }

      if (displayTimingEnabled) {
        VK__ADD_EXT_IF(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                       item->supportsDisplayTiming = true);
      }
    }
    free(extensions);
  }

#undef VK__ADD_EXT_IF

  return item;
}

GPU_HIDE
GPUPhysicalDevice*
vk_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                 GPUInstance * __restrict inst,
                                 uint32_t                 maxNumberOfItems) {
  GPUInstanceVk             *instVk;
  GPUPhysicalDevice         *firstDevice, *lastDevice, *item;
  VkPhysicalDevice          *phyDevices;
  VkInstance                 instRaw;
  VkResult                   err;
  uint32_t                   i, gpuCount;

  firstDevice = lastDevice = NULL;
  instVk      = inst->_priv;
  instRaw     = instVk->inst;

  gpuCount    = 0;
  err         = vkEnumeratePhysicalDevices(instRaw, &gpuCount, NULL);
  assert(!err);

  if (gpuCount <= 0) {
    ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n",
             "vkEnumeratePhysicalDevices Failure");
  }

  phyDevices = malloc(sizeof(VkPhysicalDevice) * gpuCount);
  err        = vkEnumeratePhysicalDevices(instRaw, &gpuCount, phyDevices);
  assert(!err);

  for (i = 0; i < gpuCount && i < maxNumberOfItems; i++) {
    item = vk__newPhyDeviceFrom(inst, phyDevices[i]);

    /* add to linked list of devices */
    if (lastDevice) { lastDevice->next = item; }
    else            { firstDevice      = item; }
    lastDevice = item;
  }

  return firstDevice;
}

GPU_EXPORT
GPUPhysicalDevice*
vk_autoSelectPhysicalDeviceIn(GPUInstance       * __restrict inst,
                              GPUPhysicalDevice * __restrict deviceList) {
  GPUPhysicalDevice   *item;
  GPUPhysicalDeviceVk *itemVk;
  GPUPhysicalDevice   *devicesByType[VK_PHYSICAL_DEVICE_TYPE_CPU + 1] = {0};
  uint32_t             i;

  item   = deviceList;
  itemVk = NULL;

  while (item) {
    itemVk = item->_priv;
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (itemVk->nDisplayProperties) { goto ok; }
#else
    if (!devicesByType[itemVk->props.deviceType]) {
      devicesByType[itemVk->props.deviceType] = item;
    }
#endif
    item = item->next;
  }

#ifndef VK_USE_PLATFORM_DISPLAY_KHR
  GPUPhysicalDevice *priorityList[] = {
    devicesByType[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU],
    devicesByType[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU],
    devicesByType[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU],
    devicesByType[VK_PHYSICAL_DEVICE_TYPE_CPU],
    devicesByType[VK_PHYSICAL_DEVICE_TYPE_OTHER]
  };

  for (i = 0; i < GPU_ARRAY_LEN(priorityList) && !(item = priorityList[i]); ++i);
#endif

ok:
  if (!item) { goto err; }

#if DEBUG
  fprintf(stderr, "Selected GPU: %s, type: %s\n",
          itemVk->props.deviceName,
          vk__devicetype_string(itemVk->props.deviceType));
#endif

  return item;

err:
  return NULL;
}

GPU_HIDE
GPUPhysicalDevice*
vk_getAutoSelectedPhysicalDevice(GPUInstance * __restrict inst) {
  GPUInstanceVk             *instVk;
  GPUPhysicalDevice         *phyDevice;
  GPUPhysicalDeviceVk       *phyDeviceVk;
  VkPhysicalDevice          *phyDevices;
  VkPhysicalDeviceProperties phyDeviceProps;
  VkInstance                 instRaw;
  VkResult                   err;
  uint32_t                   gpuCount;
  int                        gpuIndex;

  gpuIndex         = -1;
  instVk           = inst->_priv;
  instRaw          = instVk->inst;
  phyDevice        = calloc(1, sizeof(*phyDevice));
  phyDeviceVk      = calloc(1, sizeof(*phyDeviceVk));
  phyDevice->_priv = phyDeviceVk;

  gpuCount         = 0;
  err              = vkEnumeratePhysicalDevices(instRaw, &gpuCount, NULL);
  assert(!err);

  if (gpuCount <= 0) {
    ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n",
             "vkEnumeratePhysicalDevices Failure");
  }

  phyDevices = malloc(sizeof(VkPhysicalDevice) * gpuCount);
  err        = vkEnumeratePhysicalDevices(instRaw, &gpuCount, phyDevices);
  assert(!err);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  gpuIndex = vk__find_display_gpu(gpu_number, gpu_count, physical_devices);
  if (gpuIndex < 0) {
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

  phyDevice   = vk__newPhyDeviceFrom(inst, phyDevices[gpuIndex]);
  phyDeviceVk = phyDevice->_priv;

#if DEBUG
  fprintf(stderr, "Selected GPU %d: %s, type: %s\n",
          gpuIndex,
          phyDeviceProps.deviceName,
          vk__devicetype_string(phyDeviceVk->props.deviceType));
#endif

  free(phyDevices);

  return phyDevice;
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
  phyDeviceVk  = phyDevice->_priv;

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
  que->_priv      = queVk;
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
  phyDeviceVk      = phyDevice->_priv;
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

  device->_priv             = deviceVk;
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

  phyDevice = vk_getAutoSelectedPhysicalDevice(inst);

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
  apiDevice->getAutoSelectedPhysicalDevice = vk_getAutoSelectedPhysicalDevice;
  apiDevice->autoSelectPhysicalDeviceIn    = vk_autoSelectPhysicalDeviceIn;
  apiDevice->createDevice                  = vk_createDevice;
  apiDevice->createSystemDefaultDevice      = vk_createSystemDefaultDevice;
}
