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
vk_hasQueueCapability(const GPUAdapterVk *adapter,
                      VkQueueFlags        capability) {
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
vk_hasTimestampCapability(const GPUAdapterVk *adapter) {
  return adapter && adapter->props.limits.timestampComputeAndGraphics;
}

GPU_HIDE
GPUAdapter *
vk_newAdapter(GPUInstance * __restrict inst, VkPhysicalDevice raw) {
  GPUAdapter                              *adapter;
  GPUAdapterVk                            *adapterVk;
  GPUInstanceVk                           *instanceVk;
  VkExtensionProperties                   *extensions;
  PFN_vkGetPhysicalDeviceFeatures2KHR       getFeatures2;
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicFeatures = {0};
  VkPhysicalDeviceFeatures2KHR              features2 = {0};
  VkResult                                  err;
  uint32_t                                  i, nExtensions;
  bool                                      incrementalPresentEnabled;
  bool                                      displayTimingEnabled;
  bool                                      dynamicExtension;
  bool                                      dynamicCore;

  nExtensions               = 0;
  incrementalPresentEnabled = true;
  displayTimingEnabled      = true;
  dynamicExtension          = false;
  dynamicCore               = false;

  adapter                   = calloc(1, sizeof(*adapter));
  adapterVk                 = calloc(1, sizeof(*adapterVk));
  adapterVk->physicalDevice = raw;
  adapter->_priv            = adapterVk;
  adapter->inst             = inst;
  instanceVk                = inst ? inst->_priv : NULL;

  vkGetPhysicalDeviceProperties(raw, &adapterVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(adapterVk->physicalDevice,
                                           &adapterVk->nQueFamilies,
                                           NULL);
  assert(adapterVk->nQueFamilies >= 1);

  adapterVk->queueFamilyProps = malloc(adapterVk->nQueFamilies *
                                       sizeof(*adapterVk->queueFamilyProps));
  vkGetPhysicalDeviceQueueFamilyProperties(adapterVk->physicalDevice,
                                           &adapterVk->nQueFamilies,
                                           adapterVk->queueFamilyProps);
  vkGetPhysicalDeviceFeatures(adapterVk->physicalDevice, &adapterVk->features);

  /* Look for device extensions */
  err = vkEnumerateDeviceExtensionProperties(adapterVk->physicalDevice, NULL,
                                             &nExtensions, NULL);
  assert(!err);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  err = vkGetPhysicalDeviceDisplayPropertiesKHR(adapterVk->physicalDevice,
                                                &adapterVk->nDisplayProperties,
                                                NULL);
#endif

#define VK__ADD_EXT_IF(X, R)                                                  \
    if (!strcmp(X, extensions[i].extensionName)) {                            \
      adapterVk->extensionNames[adapterVk->nEnabledExtensions++] = X;         \
      R;                                                                      \
    }                                                                         \
    assert(adapterVk->nEnabledExtensions < 64);

  if (nExtensions > 0) {
    extensions = malloc(sizeof(*extensions) * nExtensions);
    err = vkEnumerateDeviceExtensionProperties(adapterVk->physicalDevice,
                                               NULL,
                                               &nExtensions,
                                               extensions);
    assert(!err);

    for (i = 0; i < nExtensions; i++) {
      VK__ADD_EXT_IF(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                     adapter->supportsSwapchain = true);

      VK__ADD_EXT_IF("VK_KHR_portability_subset", (void)NULL);

      if (!strcmp(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        dynamicExtension = true;
      }

      if (incrementalPresentEnabled) {
        VK__ADD_EXT_IF(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
                       adapter->supportsIncrementalPresent = true);
      }

      if (displayTimingEnabled) {
        VK__ADD_EXT_IF(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                       adapter->supportsDisplayTiming = true);
      }
    }
    free(extensions);
  }

  dynamicCore = instanceVk && instanceVk->apiVersion >= VK_API_VERSION_1_3 &&
                adapterVk->props.apiVersion >= VK_API_VERSION_1_3;
  getFeatures2 = instanceVk
                   ? (PFN_vkGetPhysicalDeviceFeatures2KHR)
                       vkGetInstanceProcAddr(instanceVk->inst,
                                             "vkGetPhysicalDeviceFeatures2")
                   : NULL;
  if (!getFeatures2 && instanceVk) {
    getFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2KHR)
      vkGetInstanceProcAddr(instanceVk->inst,
                            "vkGetPhysicalDeviceFeatures2KHR");
  }
  if (getFeatures2 &&
      (dynamicCore ||
       (dynamicExtension && instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
        adapterVk->props.apiVersion >= VK_API_VERSION_1_2))) {
    dynamicFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
    features2.pNext = &dynamicFeatures;
    getFeatures2(raw, &features2);
    if (dynamicFeatures.dynamicRendering) {
      if (!dynamicCore) {
        adapterVk->extensionNames[adapterVk->nEnabledExtensions++] =
          VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
        assert(adapterVk->nEnabledExtensions < 64);
      }
      adapterVk->dynamicRendering = true;
    }
  }

#undef VK__ADD_EXT_IF

  return adapter;
}

GPU_HIDE
GPUResult
vk_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                        GPUAdapterProperties * __restrict outProps) {
  GPUAdapterVk *adapterVk;

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
  GPUAdapterVk *adapterVk;

  if (!adapter || !(adapterVk = adapter->_priv)) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      return vk_hasQueueCapability(adapterVk, VK_QUEUE_COMPUTE_BIT);
    case GPU_FEATURE_TIMESTAMPS:
      return vk_hasTimestampCapability(adapterVk);
    case GPU_FEATURE_PIPELINE_STATISTICS:
      return adapterVk->features.pipelineStatisticsQuery &&
             (vk_hasQueueCapability(adapterVk, VK_QUEUE_GRAPHICS_BIT) ||
              vk_hasQueueCapability(adapterVk, VK_QUEUE_COMPUTE_BIT));
    case GPU_FEATURE_INDIRECT_DRAW:
      return vk_hasQueueCapability(adapterVk, VK_QUEUE_GRAPHICS_BIT);
    case GPU_FEATURE_MULTI_DRAW:
      return adapterVk->features.multiDrawIndirect &&
             vk_hasQueueCapability(adapterVk, VK_QUEUE_GRAPHICS_BIT);
    default:
      return false;
  }
}

static uint32_t
vk_limitU32(uint32_t implementationLimit, uint32_t nativeLimit) {
  return implementationLimit < nativeLimit ? implementationLimit : nativeLimit;
}

static void
vk_getLimits(const GPUAdapter * __restrict adapter,
             GPULimits       * __restrict outLimits) {
  GPUAdapterVk                 *adapterVk;
  const VkPhysicalDeviceLimits *native;

  adapterVk = adapter ? adapter->_priv : NULL;
  if (!adapterVk || !outLimits) {
    return;
  }

  native = &adapterVk->props.limits;
  outLimits->maxBindGroups = vk_limitU32(
    outLimits->maxBindGroups,
    native->maxBoundDescriptorSets
  );
  outLimits->maxBindingsPerGroup = vk_limitU32(
    outLimits->maxBindingsPerGroup,
    native->maxPerStageResources
  );
  outLimits->maxDynamicUniformBuffers = vk_limitU32(
    outLimits->maxDynamicUniformBuffers,
    native->maxDescriptorSetUniformBuffersDynamic
  );
  outLimits->maxDynamicStorageBuffers = vk_limitU32(
    outLimits->maxDynamicStorageBuffers,
    native->maxDescriptorSetStorageBuffersDynamic
  );
  outLimits->minUniformBufferOffsetAlignment =
    native->minUniformBufferOffsetAlignment;
  outLimits->minStorageBufferOffsetAlignment =
    native->minStorageBufferOffsetAlignment;
  outLimits->maxColorAttachments = vk_limitU32(
    outLimits->maxColorAttachments,
    native->maxColorAttachments
  );
  outLimits->maxComputeWorkgroupSizeX = native->maxComputeWorkGroupSize[0];
  outLimits->maxComputeWorkgroupSizeY = native->maxComputeWorkGroupSize[1];
  outLimits->maxComputeWorkgroupSizeZ = native->maxComputeWorkGroupSize[2];
}

static void
vk_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps) {
  GPUAdapterVk        *adapterVk;
  VkFormatProperties   properties;
  VkFormat             nativeFormat;
  VkFormatFeatureFlags features;

  adapterVk = adapter ? adapter->_priv : NULL;
  if (!adapterVk || !outCaps ||
      !vk_formatFromGPU(format, &nativeFormat)) {
    if (outCaps) {
      memset(outCaps, 0, sizeof(*outCaps));
    }
    return;
  }

  vkGetPhysicalDeviceFormatProperties(adapterVk->physicalDevice,
                                      nativeFormat,
                                      &properties);
  features = properties.optimalTilingFeatures;
  memset(outCaps, 0, sizeof(*outCaps));
  outCaps->sampled =
    (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0u;
  outCaps->filterable =
    (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0u;
  outCaps->storage =
    (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0u;
  outCaps->colorAttachment =
    (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0u;
  outCaps->blendable =
    (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0u;
  outCaps->depthStencil =
    (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u;
}

GPU_HIDE
GPUAdapter *
vk_getAvailableAdapters(GPUInstance * __restrict inst,
                        uint32_t                 maxNumberOfItems) {
  GPUInstanceVk    *instVk;
  GPUAdapter       *firstAdapter, *lastAdapter, *adapter;
  VkPhysicalDevice *physicalDevices;
  VkInstance        instRaw;
  VkResult          err;
  uint32_t          i, gpuCount;

  firstAdapter = lastAdapter = NULL;
  instVk       = inst->_priv;
  instRaw      = instVk->inst;

  gpuCount    = 0;
  err         = vkEnumeratePhysicalDevices(instRaw, &gpuCount, NULL);
  assert(!err);

  if (gpuCount <= 0) {
    ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n",
             "vkEnumeratePhysicalDevices Failure");
  }

  physicalDevices = malloc(sizeof(VkPhysicalDevice) * gpuCount);
  err             = vkEnumeratePhysicalDevices(instRaw,
                                               &gpuCount,
                                               physicalDevices);
  assert(!err);

  for (i = 0; i < gpuCount && i < maxNumberOfItems; i++) {
    adapter = vk_newAdapter(inst, physicalDevices[i]);

    if (lastAdapter) { lastAdapter->next = adapter; }
    else             { firstAdapter      = adapter; }
    lastAdapter = adapter;
  }

  free(physicalDevices);

  return firstAdapter;
}

GPU_HIDE
GPUAdapter *
vk_selectAdapter(GPUInstance * __restrict inst,
                 GPUAdapter  * __restrict adapters) {
  GPUAdapter   *adapter;
  GPUAdapterVk *adapterVk;
  GPUAdapter   *adaptersByType[VK_PHYSICAL_DEVICE_TYPE_CPU + 1] = {0};
#ifndef VK_USE_PLATFORM_DISPLAY_KHR
  GPUAdapter   *priorityList[VK_PHYSICAL_DEVICE_TYPE_CPU + 1];
#endif
  uint32_t      i;

  GPU__UNUSED(inst);
  adapter   = adapters;
  adapterVk = NULL;

  while (adapter) {
    adapterVk = adapter->_priv;
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (adapterVk->nDisplayProperties) { goto ok; }
#else
    if (!adaptersByType[adapterVk->props.deviceType]) {
      adaptersByType[adapterVk->props.deviceType] = adapter;
    }
#endif
    adapter = adapter->next;
  }

#ifndef VK_USE_PLATFORM_DISPLAY_KHR
  priorityList[0] = adaptersByType[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU];
  priorityList[1] = adaptersByType[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU];
  priorityList[2] = adaptersByType[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU];
  priorityList[3] = adaptersByType[VK_PHYSICAL_DEVICE_TYPE_CPU];
  priorityList[4] = adaptersByType[VK_PHYSICAL_DEVICE_TYPE_OTHER];

  for (i = 0;
       i < GPU_ARRAY_LEN(priorityList) && !(adapter = priorityList[i]);
       i++);
#endif

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
ok:
#endif
  if (!adapter) { goto err; }
  adapterVk = adapter->_priv;

#ifdef DEBUG
  fprintf(stderr, "Selected GPU: %s, type: %s\n",
          adapterVk->props.deviceName,
          vk__devicetype_string(adapterVk->props.deviceType));
#endif

  return adapter;

err:
  return NULL;
}

GPU_HIDE
void
vk_destroyAdapter(GPUAdapter * __restrict adapter) {
  GPUAdapterVk *adapterVk;

  if (!adapter) {
    return;
  }

  adapterVk = adapter->_priv;
  if (adapterVk) {
    free(adapterVk->queueFamilyProps);
    free(adapterVk);
  }
  free(adapter);
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
               GPU_QUEUE_TRANSFER_BIT;

  if (bits & GPU_QUEUE_GRAPHICS_BIT) flags |= VK_QUEUE_GRAPHICS_BIT;
  if (bits & GPU_QUEUE_COMPUTE_BIT)  flags |= VK_QUEUE_COMPUTE_BIT;
  if (bits & GPU_QUEUE_TRANSFER_BIT) flags |= VK_QUEUE_TRANSFER_BIT;

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
vk__findQueueFamily(const GPUAdapterVk *adapterVk,
                    GPUQueueFlagBits    requiredBits,
                    GPUQueueFlagBits    optionalBits,
                    uint32_t            count) {
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

  for (uint32_t i = 0; i < adapterVk->nQueFamilies; i++) {
    const VkQueueFamilyProperties *family;
    uint32_t missingOptional;
    uint32_t extraCapabilities;
    uint32_t score;

    family = &adapterVk->queueFamilyProps[i];
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
GPUDevice *
vk_createDevice(GPUAdapter        * __restrict adapter,
                GPUQueueCreateInfo queCI[],
                uint32_t           nQueCI) {
  GPUDevice               *device;
  GPUDeviceVk             *deviceVk;
  GPUAdapterVk            *adapterVk;
  GPUQueuePlanVk          *plans;
  GPUQueuePlanVk          *plan;
  VkDeviceQueueCreateInfo *queues;
  float                   *queuePriorities;
  VkPhysicalDeviceFeatures enabledFeatures = {0};
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicFeatures = {0};
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

  if (!adapter || !adapter->_priv || !queCI || nQueCI == 0u) {
    return NULL;
  }

  device   = calloc(1, sizeof(*device));
  deviceVk = calloc(1, sizeof(*deviceVk));
  plans    = calloc(nQueCI, sizeof(*plans));
  queues   = calloc(nQueCI, sizeof(*queues));
  if (!device || !deviceVk || !plans || !queues) {
    goto err;
  }

  adapterVk       = adapter->_priv;
  planCount       = 0u;
  maxQueueCount   = 0u;
  totalQueueCount = 0u;

  for (uint32_t i = 0; i < nQueCI; i++) {
    familyIndex = vk__findQueueFamily(adapterVk,
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

  enabledFeatures.pipelineStatisticsQuery =
    adapterVk->features.pipelineStatisticsQuery;
  enabledFeatures.multiDrawIndirect = adapterVk->features.multiDrawIndirect;
  enabledFeatures.independentBlend   = adapterVk->features.independentBlend;
  enabledFeatures.imageCubeArray     = adapterVk->features.imageCubeArray;

  deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.pEnabledFeatures        = &enabledFeatures;
  deviceCI.queueCreateInfoCount    = planCount;
  deviceCI.pQueueCreateInfos       = queues;
  deviceCI.enabledExtensionCount   = adapterVk->nEnabledExtensions;
  deviceCI.ppEnabledExtensionNames = (void *)adapterVk->extensionNames;
  if (adapterVk->dynamicRendering) {
    dynamicFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamicFeatures.dynamicRendering = VK_TRUE;
    deviceCI.pNext = &dynamicFeatures;
  }

  result = vkCreateDevice(adapterVk->physicalDevice,
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
    adapterVk->props.limits.maxDrawIndirectCount;
  deviceVk->colorSampleCounts =
    adapterVk->props.limits.framebufferColorSampleCounts;
  deviceVk->depthSampleCounts =
    adapterVk->props.limits.framebufferDepthSampleCounts;
  deviceVk->multiDrawIndirect = enabledFeatures.multiDrawIndirect;
  deviceVk->independentBlend  = enabledFeatures.independentBlend;
  if (adapterVk->dynamicRendering) {
    deviceVk->beginRendering = (PFN_vkCmdBeginRenderingKHR)
      vkGetDeviceProcAddr(deviceVk->device, "vkCmdBeginRendering");
    deviceVk->endRendering = (PFN_vkCmdEndRenderingKHR)
      vkGetDeviceProcAddr(deviceVk->device, "vkCmdEndRendering");
    if (!deviceVk->beginRendering || !deviceVk->endRendering) {
      deviceVk->beginRendering = (PFN_vkCmdBeginRenderingKHR)
        vkGetDeviceProcAddr(deviceVk->device, "vkCmdBeginRenderingKHR");
      deviceVk->endRendering = (PFN_vkCmdEndRenderingKHR)
        vkGetDeviceProcAddr(deviceVk->device, "vkCmdEndRenderingKHR");
    }
    if (!deviceVk->beginRendering || !deviceVk->endRendering) {
      goto err;
    }
    deviceVk->dynamicRendering = true;
  }

  device->_priv            = deviceVk;
  device->inst             = adapter->inst;
  device->adapter          = adapter;

  deviceVk->createdQueues = calloc(totalQueueCount,
                                   sizeof(*deviceVk->createdQueues));
  if (!deviceVk->createdQueues) {
    goto err;
  }

  for (uint32_t i = 0; i < planCount; i++) {
    for (uint32_t queueIndex = 0; queueIndex < plans[i].count; queueIndex++) {
      GPUQueue        *queue;

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
void
vk_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailableAdapters      = vk_getAvailableAdapters;
  apiDevice->selectAdapter             = vk_selectAdapter;
  apiDevice->destroyAdapter            = vk_destroyAdapter;
  apiDevice->getAdapterProperties      = vk_getAdapterProperties;
  apiDevice->supportsFeature           = vk_supportsFeature;
  apiDevice->getLimits                 = vk_getLimits;
  apiDevice->getFormatCapabilities     = vk_getFormatCapabilities;
  apiDevice->createDevice              = vk_createDevice;
  apiDevice->waitIdle                  = vk_waitDeviceIdle;
  apiDevice->destroyDevice             = vk_destroyDevice;
}
