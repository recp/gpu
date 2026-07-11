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
#include "../impl.h"

#ifdef DEBUG
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
#endif

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

static GPUAdapterType
vk_adapterType(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return GPU_ADAPTER_TYPE_INTEGRATED;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return GPU_ADAPTER_TYPE_DISCRETE;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return GPU_ADAPTER_TYPE_SOFTWARE;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
      return GPU_ADAPTER_TYPE_UNKNOWN;
  }
}

static bool
vk_hasQueueCapability(const GPUPhysicalDeviceVk *adapter,
                      VkQueueFlags               capability) {
  if (!adapter) {
    return false;
  }

  for (uint32_t i = 0; i < adapter->nQueFamilies; i++) {
    if (adapter->queueFamilyProps[i].queueFlags & capability) {
      return true;
    }
  }

  return false;
}

static bool
vk_hasTimestampCapability(const GPUPhysicalDeviceVk *adapter) {
  return adapter && adapter->props.limits.timestampComputeAndGraphics;
}

GPU_HIDE
GPUPhysicalDevice*
vk__newPhyDeviceFrom(GPUInstance * __restrict inst, VkPhysicalDevice raw) {
  GPUPhysicalDevice     *item;
  GPUPhysicalDeviceVk   *itemVk;
  VkExtensionProperties *extensions;
  VkResult               err;
  uint32_t               i, nExtensions;
  bool                   incrementalPresentEnabled, displayTimingEnabled;

  nExtensions               = 0;
  incrementalPresentEnabled = true;
  displayTimingEnabled      = true;

  item                      = calloc(1, sizeof(*item));
  itemVk                    = calloc(1, sizeof(*itemVk));
  itemVk->phyDevice         = raw;
  item->_priv               = itemVk;
  item->inst                = inst;

  vkGetPhysicalDeviceProperties(raw, &itemVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(itemVk->phyDevice,
                                           &itemVk->nQueFamilies,
                                           NULL);
  assert(itemVk->nQueFamilies >= 1);

  itemVk->queueFamilyProps = malloc(itemVk->nQueFamilies *
                                    sizeof(*itemVk->queueFamilyProps));
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
GPUResult
vk_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                        GPUAdapterProperties * __restrict outProps) {
  GPUPhysicalDeviceVk *adapterVk;

  if (!adapter || !outProps || !adapter->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  adapterVk = adapter->_priv;
  memset(outProps, 0, sizeof(*outProps));
  outProps->backend = GPU_BACKEND_VULKAN;
  outProps->name = adapterVk->props.deviceName;
  outProps->type = vk_adapterType(adapterVk->props.deviceType);

  return GPU_OK;
}

GPU_HIDE
bool
vk_supportsFeature(const GPUAdapter * __restrict adapter, GPUFeature feature) {
  GPUPhysicalDeviceVk *adapterVk;

  if (!adapter || !(adapterVk = adapter->_priv)) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      return vk_hasQueueCapability(adapterVk, VK_QUEUE_COMPUTE_BIT);
    case GPU_FEATURE_TIMESTAMPS:
      return vk_hasTimestampCapability(adapterVk);
    case GPU_FEATURE_INDIRECT_DRAW:
      return vk_hasQueueCapability(adapterVk, VK_QUEUE_GRAPHICS_BIT);
    case GPU_FEATURE_MULTI_DRAW:
      return adapterVk->features.multiDrawIndirect &&
             vk_hasQueueCapability(adapterVk, VK_QUEUE_GRAPHICS_BIT);
    default:
      return false;
  }
}

GPU_HIDE
GPUPhysicalDevice*
vk_getAvailablePhysicalDevicesBy(GPUInstance * __restrict inst,
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

#ifdef DEBUG
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

  gpuIndex    = -1;
  instVk      = inst->_priv;
  instRaw     = instVk->inst;
  phyDevice   = NULL;
  phyDeviceVk = NULL;

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

#ifdef DEBUG
  fprintf(stderr, "Selected GPU %d: %s, type: %s\n",
          gpuIndex,
          phyDeviceProps.deviceName,
          vk__devicetype_string(phyDeviceVk->props.deviceType));
#endif

  free(phyDevices);

  return phyDevice;
}

typedef struct GPUQueuePlanVk {
  GPUQueueFlagBits bits;
  uint32_t         familyIndex;
  uint32_t         count;
} GPUQueuePlanVk;

static bool
vk__queueFlags(GPUQueueFlagBits bits, VkQueueFlags *outFlags) {
  VkQueueFlags flags;
  uint32_t     mappedBits;

  flags      = 0u;
  mappedBits = GPU_QUEUE_GRAPHICS_BIT |
               GPU_QUEUE_COMPUTE_BIT |
               GPU_QUEUE_TRANSFER_BIT |
               GPU_QUEUE_SPARSE_BINDING_BIT |
               GPU_QUEUE_PROTECTED_BIT;

  if (bits & GPU_QUEUE_GRAPHICS_BIT)       flags |= VK_QUEUE_GRAPHICS_BIT;
  if (bits & GPU_QUEUE_COMPUTE_BIT)        flags |= VK_QUEUE_COMPUTE_BIT;
  if (bits & GPU_QUEUE_TRANSFER_BIT)       flags |= VK_QUEUE_TRANSFER_BIT;
  if (bits & GPU_QUEUE_SPARSE_BINDING_BIT) flags |= VK_QUEUE_SPARSE_BINDING_BIT;
  if (bits & GPU_QUEUE_PROTECTED_BIT)      flags |= VK_QUEUE_PROTECTED_BIT;

  /* Header guards protect old SDKs; queue-family flags decide runtime support. */
#ifdef VK_QUEUE_VIDEO_DECODE_BIT_KHR
  mappedBits |= GPU_QUEUE_VIDEO_DECODE_BIT_KHR;
  if (bits & GPU_QUEUE_VIDEO_DECODE_BIT_KHR) flags |= VK_QUEUE_VIDEO_DECODE_BIT_KHR;
#endif
#ifdef VK_QUEUE_VIDEO_ENCODE_BIT_KHR
  mappedBits |= GPU_QUEUE_VIDEO_ENCODE_BIT_KHR;
  if (bits & GPU_QUEUE_VIDEO_ENCODE_BIT_KHR) flags |= VK_QUEUE_VIDEO_ENCODE_BIT_KHR;
#endif
#ifdef VK_QUEUE_OPTICAL_FLOW_BIT_NV
  mappedBits |= GPU_QUEUE_OPTICAL_FLOW_BIT_NV;
  if (bits & GPU_QUEUE_OPTICAL_FLOW_BIT_NV) flags |= VK_QUEUE_OPTICAL_FLOW_BIT_NV;
#endif

  *outFlags = flags;
  return ((uint32_t)bits & ~mappedBits) == 0u;
}

static uint32_t
vk__flagCount(VkQueueFlags flags) {
  uint32_t count;

  count = 0u;
  while (flags) {
    flags &= flags - 1u;
    count++;
  }
  return count;
}

static uint32_t
vk__findQueueFamily(const GPUPhysicalDeviceVk *phyDeviceVk,
                    GPUQueueFlagBits           requiredBits,
                    GPUQueueFlagBits           optionalBits,
                    uint32_t                   count) {
  VkQueueFlags requiredFlags;
  VkQueueFlags optionalFlags;
  VkQueueFlags commonFlags;
  uint32_t     bestIndex;
  uint32_t     bestScore;

  requiredFlags = 0u;
  optionalFlags = 0u;
  if (!vk__queueFlags(requiredBits, &requiredFlags)) {
    return UINT32_MAX;
  }
  (void)vk__queueFlags(optionalBits, &optionalFlags);
  commonFlags   = VK_QUEUE_GRAPHICS_BIT |
                  VK_QUEUE_COMPUTE_BIT |
                  VK_QUEUE_TRANSFER_BIT;
  bestIndex     = UINT32_MAX;
  bestScore     = UINT32_MAX;

  for (uint32_t i = 0; i < phyDeviceVk->nQueFamilies; i++) {
    const VkQueueFamilyProperties *family;
    uint32_t missingOptional;
    uint32_t extraCapabilities;
    uint32_t score;

    family = &phyDeviceVk->queueFamilyProps[i];
    if (family->queueCount < count ||
        (family->queueFlags & requiredFlags) != requiredFlags) {
      continue;
    }

    missingOptional   = vk__flagCount(optionalFlags & ~family->queueFlags);
    extraCapabilities = vk__flagCount((family->queueFlags & commonFlags) &
                                      ~requiredFlags);
    score              = missingOptional * 16u + extraCapabilities;
    if (score < bestScore) {
      bestScore = score;
      bestIndex = i;
    }
  }

  return bestIndex;
}

static GPUQueuePlanVk*
vk__findQueuePlan(GPUQueuePlanVk *plans,
                  uint32_t        planCount,
                  uint32_t        familyIndex) {
  for (uint32_t i = 0; i < planCount; i++) {
    if (plans[i].familyIndex == familyIndex) {
      return &plans[i];
    }
  }
  return NULL;
}

GPU_HIDE
GPUDevice*
vk_createDevice(GPUPhysicalDevice          * __restrict phyDevice,
                GPUCommandQueueCreateInfo      queCI[],
                uint32_t                       nQueCI) {
  GPUDevice               *device;
  GPUDeviceVk             *deviceVk;
  GPUPhysicalDeviceVk     *phyDeviceVk;
  GPUQueuePlanVk          *plans;
  GPUQueuePlanVk          *plan;
  VkDeviceQueueCreateInfo *queues;
  float                   *queuePriorities;
  VkPhysicalDeviceFeatures enabledFeatures = {0};
  VkDeviceCreateInfo       deviceCI = {0};
  VkResult                 result;
  uint32_t                 familyIndex;
  uint32_t                 maxQueueCount;
  uint32_t                 planCount;
  uint32_t                 totalQueueCount;

  device          = NULL;
  deviceVk        = NULL;
  plans           = NULL;
  queues          = NULL;
  queuePriorities = NULL;

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI);

  if (!phyDevice || !phyDevice->_priv || !queCI || nQueCI == 0u) {
    return NULL;
  }

  device   = calloc(1, sizeof(*device));
  deviceVk = calloc(1, sizeof(*deviceVk));
  plans    = calloc(nQueCI, sizeof(*plans));
  queues   = calloc(nQueCI, sizeof(*queues));
  if (!device || !deviceVk || !plans || !queues) {
    goto err;
  }

  phyDeviceVk     = phyDevice->_priv;
  planCount       = 0u;
  maxQueueCount   = 0u;
  totalQueueCount = 0u;

  for (uint32_t i = 0; i < nQueCI; i++) {
    familyIndex = vk__findQueueFamily(phyDeviceVk,
                                      queCI[i].flags,
                                      queCI[i].optionalFlags,
                                      queCI[i].count);
    if (familyIndex == UINT32_MAX) {
      goto err;
    }

    plan = vk__findQueuePlan(plans, planCount, familyIndex);
    if (!plan) {
      plan              = &plans[planCount++];
      plan->familyIndex = familyIndex;
    }
    plan->bits |= queCI[i].flags;
    if (queCI[i].count > plan->count) {
      plan->count = queCI[i].count;
    }
  }

  for (uint32_t i = 0; i < planCount; i++) {
    if (plans[i].count > maxQueueCount) {
      maxQueueCount = plans[i].count;
    }
    totalQueueCount += plans[i].count;
  }

  queuePriorities = calloc(maxQueueCount, sizeof(*queuePriorities));
  if (!queuePriorities || totalQueueCount == 0u) {
    goto err;
  }
  for (uint32_t i = 0; i < maxQueueCount; i++) {
    queuePriorities[i] = 1.0f;
  }

  for (uint32_t i = 0; i < planCount; i++) {
    queues[i].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[i].queueFamilyIndex = plans[i].familyIndex;
    queues[i].queueCount       = plans[i].count;
    queues[i].pQueuePriorities = queuePriorities;
  }

  enabledFeatures.multiDrawIndirect = phyDeviceVk->features.multiDrawIndirect;

  deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.pEnabledFeatures        = &enabledFeatures;
  deviceCI.queueCreateInfoCount    = planCount;
  deviceCI.pQueueCreateInfos       = queues;
  deviceCI.enabledExtensionCount   = phyDeviceVk->nEnabledExtensions;
  deviceCI.ppEnabledExtensionNames = (void *)phyDeviceVk->extensionNames;

  result = vkCreateDevice(phyDeviceVk->phyDevice,
                          &deviceCI,
                          NULL,
                          &deviceVk->device);
  if (result != VK_SUCCESS) {
#ifdef DEBUG
    fprintf(stderr, "vkCreateDevice failed: %d\n", result);
#endif
    goto err;
  }

  deviceVk->maxDrawIndirectCount =
    phyDeviceVk->props.limits.maxDrawIndirectCount;
  deviceVk->multiDrawIndirect = enabledFeatures.multiDrawIndirect;

  device->_priv            = deviceVk;
  device->inst             = phyDevice->inst;
  device->phyDevice        = phyDevice;

  deviceVk->createdQueues = calloc(totalQueueCount,
                                   sizeof(*deviceVk->createdQueues));
  if (!deviceVk->createdQueues) {
    goto err;
  }

  for (uint32_t i = 0; i < planCount; i++) {
    for (uint32_t queueIndex = 0; queueIndex < plans[i].count; queueIndex++) {
      GPUCommandQueue *queue;

      queue = vk_createCommandQueue(device,
                                    plans[i].familyIndex,
                                    queueIndex,
                                    plans[i].bits);
      if (!queue) {
        goto err;
      }
      deviceVk->createdQueues[deviceVk->nCreatedQueues++] = queue;
      device->queueFamilies |= queue->bits;
    }
  }

  free(queuePriorities);
  free(queues);
  free(plans);

  return device;
err:
  free(queuePriorities);
  free(queues);
  free(plans);
  if (deviceVk) {
    if (deviceVk->device) {
      vkDeviceWaitIdle(deviceVk->device);
    }
    if (deviceVk->createdQueues) {
      for (uint32_t i = 0; i < deviceVk->nCreatedQueues; i++) {
        vk_destroyCommandQueue(deviceVk->createdQueues[i]);
      }
    }
    free(deviceVk->createdQueues);
    if (deviceVk->device) {
      vkDestroyDevice(deviceVk->device, NULL);
    }
    free(deviceVk);
  }
  free(device);

  return NULL;
}

GPU_HIDE
void
vk_destroyDevice(GPUDevice * __restrict device) {
  GPUDeviceVk *deviceVk;

  if (!device) {
    return;
  }

  deviceVk = device->_priv;
  if (deviceVk) {
    if (deviceVk->device) {
      vkDeviceWaitIdle(deviceVk->device);
    }
    if (deviceVk->createdQueues) {
      for (uint32_t i = 0; i < deviceVk->nCreatedQueues; i++) {
        vk_destroyCommandQueue(deviceVk->createdQueues[i]);
      }
    }
    free(deviceVk->createdQueues);
    if (deviceVk->device) {
      vkDestroyDevice(deviceVk->device, NULL);
    }
    free(deviceVk);
  }
  free(device);
}

GPU_HIDE
GPUDevice*
vk_createSystemDefaultDevice(GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;

  phyDevice = vk_getAutoSelectedPhysicalDevice(inst);
  return phyDevice ? vk_createDevice(phyDevice, NULL, 0u) : NULL;
}

GPU_HIDE
void
vk_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailableAdapters      = vk_getAvailablePhysicalDevicesBy;
  apiDevice->getAdapterProperties      = vk_getAdapterProperties;
  apiDevice->supportsFeature           = vk_supportsFeature;
  apiDevice->createDevice              = vk_createDevice;
  apiDevice->createSystemDefaultDevice = vk_createSystemDefaultDevice;
  apiDevice->destroyDevice             = vk_destroyDevice;
}
