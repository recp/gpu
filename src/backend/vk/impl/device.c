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

static bool
vk_subgroupStageFromGPU(GPUShaderStageFlags stage,
                        VkShaderStageFlags  *outStage) {
  if (!outStage) {
    return false;
  }

  switch (stage) {
    case GPU_SHADER_STAGE_VERTEX_BIT:
      *outStage = VK_SHADER_STAGE_VERTEX_BIT;
      return true;
    case GPU_SHADER_STAGE_FRAGMENT_BIT:
      *outStage = VK_SHADER_STAGE_FRAGMENT_BIT;
      return true;
    case GPU_SHADER_STAGE_COMPUTE_BIT:
      *outStage = VK_SHADER_STAGE_COMPUTE_BIT;
      return true;
#ifdef VK_EXT_mesh_shader
    case GPU_SHADER_STAGE_TASK_BIT:
      *outStage = VK_SHADER_STAGE_TASK_BIT_EXT;
      return true;
    case GPU_SHADER_STAGE_MESH_BIT:
      *outStage = VK_SHADER_STAGE_MESH_BIT_EXT;
      return true;
#endif
    default:
      return false;
  }
}

static bool
vk_subgroupOperationsFromGPU(
  GPUBackendSubgroupOperationFlags operations,
  VkSubgroupFeatureFlags          *outOperations) {
  const GPUBackendSubgroupOperationFlags knownOperations =
    GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;
  VkSubgroupFeatureFlags native;

  if (!outOperations || (operations & ~knownOperations) != 0u) {
    return false;
  }

  native = 0u;
  if ((operations & GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT) != 0u) {
    native |= VK_SUBGROUP_FEATURE_BASIC_BIT;
  }
  if ((operations & GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT) != 0u) {
    native |= VK_SUBGROUP_FEATURE_SHUFFLE_BIT;
  }
  if ((operations &
       GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT) != 0u) {
    native |= VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
  }
  *outOperations = native;
  return true;
}

static bool
vk_supportsSubgroupOperations(
  const GPUAdapter                 * __restrict adapter,
  GPUShaderStageFlags                           stage,
  GPUBackendSubgroupOperationFlags              operations) {
  GPUAdapterVk          *adapterVk;
  VkShaderStageFlags     nativeStage;
  VkSubgroupFeatureFlags nativeOperations;

  adapterVk = adapter ? adapter->_priv : NULL;
  return adapterVk && adapterVk->subgroupSize > 0u &&
         vk_subgroupStageFromGPU(stage, &nativeStage) &&
         vk_subgroupOperationsFromGPU(operations, &nativeOperations) &&
         (adapterVk->subgroupStages & nativeStage) == nativeStage &&
         (adapterVk->subgroupOperations & nativeOperations) ==
           nativeOperations;
}

static bool
vk_hasSubgroupCapability(const GPUAdapterVk *adapter) {
  return adapter && adapter->subgroupSize > 0u &&
         (adapter->subgroupStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
         (adapter->subgroupOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0u;
}

#ifdef VK_KHR_cooperative_matrix
static bool
vk_subgroupMatrixComponent(VkComponentTypeKHR                  native,
                           GPUSubgroupMatrixComponentTypeEXT *outType) {
  if (!outType) {
    return false;
  }

  switch (native) {
    case VK_COMPONENT_TYPE_FLOAT16_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
      return true;
    case VK_COMPONENT_TYPE_FLOAT32_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
      return true;
    case VK_COMPONENT_TYPE_FLOAT64_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_F64_EXT;
      return true;
    case VK_COMPONENT_TYPE_SINT8_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I8_EXT;
      return true;
    case VK_COMPONENT_TYPE_SINT16_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I16_EXT;
      return true;
    case VK_COMPONENT_TYPE_SINT32_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I32_EXT;
      return true;
    case VK_COMPONENT_TYPE_SINT64_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_I64_EXT;
      return true;
    case VK_COMPONENT_TYPE_UINT8_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U8_EXT;
      return true;
    case VK_COMPONENT_TYPE_UINT16_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U16_EXT;
      return true;
    case VK_COMPONENT_TYPE_UINT32_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U32_EXT;
      return true;
    case VK_COMPONENT_TYPE_UINT64_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_U64_EXT;
      return true;
    case VK_COMPONENT_TYPE_BFLOAT16_KHR:
      *outType = GPU_SUBGROUP_MATRIX_COMPONENT_BF16_EXT;
      return true;
    default:
      return false;
  }
}

static GPUShaderStageFlags
vk_subgroupMatrixStages(VkShaderStageFlags native) {
  GPUShaderStageFlags stages;

  stages = 0u;
  if (native & VK_SHADER_STAGE_VERTEX_BIT) {
    stages |= GPU_SHADER_STAGE_VERTEX_BIT;
  }
  if (native & VK_SHADER_STAGE_FRAGMENT_BIT) {
    stages |= GPU_SHADER_STAGE_FRAGMENT_BIT;
  }
  if (native & VK_SHADER_STAGE_COMPUTE_BIT) {
    stages |= GPU_SHADER_STAGE_COMPUTE_BIT;
  }
#ifdef VK_EXT_mesh_shader
  if (native & VK_SHADER_STAGE_TASK_BIT_EXT) {
    stages |= GPU_SHADER_STAGE_TASK_BIT;
  }
  if (native & VK_SHADER_STAGE_MESH_BIT_EXT) {
    stages |= GPU_SHADER_STAGE_MESH_BIT;
  }
#endif
  return stages;
}

static GPUResult
vk_getSubgroupMatrixProperties(
  const GPUAdapter               * __restrict adapter,
  uint32_t                       * __restrict inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT * __restrict outProperties) {
  GPUAdapterVk                  *adapterVk;
  VkCooperativeMatrixPropertiesKHR *native;
  VkResult                       result;
  GPUShaderStageFlags            stages;
  uint32_t                       capacity;
  uint32_t                       nativeCount;
  uint32_t                       count;
  uint32_t                       written;

  adapterVk = adapter ? adapter->_priv : NULL;
  if (!adapterVk || !inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!adapterVk->subgroupMatrix ||
      !adapterVk->getCooperativeMatrixProperties) {
    *inoutPropertyCount = 0u;
    return GPU_ERROR_UNSUPPORTED;
  }

  nativeCount = 0u;
  result = adapterVk->getCooperativeMatrixProperties(
    adapterVk->physicalDevice,
    &nativeCount,
    NULL
  );
  if (result != VK_SUCCESS || nativeCount == 0u) {
    *inoutPropertyCount = 0u;
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = calloc(nativeCount, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0u; i < nativeCount; i++) {
    native[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
  }
  result = adapterVk->getCooperativeMatrixProperties(
    adapterVk->physicalDevice,
    &nativeCount,
    native
  );
  if (result != VK_SUCCESS) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  capacity = *inoutPropertyCount;
  stages   = vk_subgroupMatrixStages(adapterVk->subgroupMatrixStages);
  count    = 0u;
  written  = 0u;
  for (uint32_t i = 0u; i < nativeCount; i++) {
    GPUSubgroupMatrixPropertiesEXT property;

    memset(&property, 0, sizeof(property));
    if (native[i].scope != VK_SCOPE_SUBGROUP_KHR || stages == 0u ||
        !vk_subgroupMatrixComponent(native[i].AType, &property.aType) ||
        !vk_subgroupMatrixComponent(native[i].BType, &property.bType) ||
        !vk_subgroupMatrixComponent(native[i].CType, &property.cType) ||
        !vk_subgroupMatrixComponent(native[i].ResultType,
                                    &property.resultType)) {
      continue;
    }

    property.m                      = native[i].MSize;
    property.n                      = native[i].NSize;
    property.k                      = native[i].KSize;
    property.stages                 = stages;
    property.scope                  = GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
    property.saturatingAccumulation = native[i].saturatingAccumulation != 0u;
    if (outProperties && written < capacity) {
      outProperties[written++] = property;
    }
    count++;
  }
  free(native);

  *inoutPropertyCount = count;
  if (count == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return outProperties && capacity < count
           ? GPU_ERROR_INSUFFICIENT_CAPACITY
           : GPU_OK;
}

static bool
vk_hasSubgroupMatrixProperty(GPUAdapter *adapter) {
  GPUAdapterVk *adapterVk;
  GPUResult     result;
  uint32_t      count;

  adapterVk = adapter ? adapter->_priv : NULL;
  if (!adapterVk || !vk_hasSubgroupCapability(adapterVk)) {
    return false;
  }

  adapterVk->subgroupMatrix = true;
  count = 0u;
  result = vk_getSubgroupMatrixProperties(adapter, &count, NULL);
  adapterVk->subgroupMatrix = result == GPU_OK && count > 0u;
  return adapterVk->subgroupMatrix;
}
#endif

static bool
vk_addDeviceExtension(GPUAdapterVk *adapter, const char *name) {
  if (!adapter || !name) {
    return false;
  }
  for (uint32_t i = 0u; i < adapter->nEnabledExtensions; i++) {
    if (strcmp(adapter->extensionNames[i], name) == 0) {
      return true;
    }
  }
  if (adapter->nEnabledExtensions >= GPU_ARRAY_LEN(adapter->extensionNames)) {
    return false;
  }

  adapter->extensionNames[adapter->nEnabledExtensions++] = (char *)name;
  return true;
}

static bool
vk_featureEnabled(uint64_t enabledFeatureMask, GPUFeature feature) {
  return (enabledFeatureMask & (1ull << feature)) != 0u;
}

static bool
vk_extensionEnabled(const GPUAdapterVk *adapter,
                    const char         *name,
                    uint64_t            enabledFeatureMask) {
  bool descriptorIndexing;
  bool meshShader;
  bool rayQuery;
  bool rayTracingPipeline;

  descriptorIndexing =
    vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_DESCRIPTOR_INDEXING) ||
    vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_BINDLESS);
  meshShader = vk_featureEnabled(enabledFeatureMask,
                                 GPU_FEATURE_MESH_SHADER);
  rayQuery = vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_RAY_QUERY);
  rayTracingPipeline = vk_featureEnabled(
    enabledFeatureMask,
    GPU_FEATURE_RAY_TRACING_PIPELINE
  );

  if (strcmp(name, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_SHADER_F16);
  }
  if (strcmp(name, VK_KHR_16BIT_STORAGE_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_SHADER_F16);
  }
  if (strcmp(name, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask,
                             GPU_FEATURE_SUBGROUP_MATRIX);
  }
  if (strcmp(name, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_ATOMIC64);
  }
  if (strcmp(name, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
    return descriptorIndexing || rayQuery;
  }
#ifdef VK_EXT_descriptor_buffer
  if (strcmp(name, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0) {
    return adapter && adapter->descriptorBuffer;
  }
#endif
#ifdef VK_KHR_buffer_device_address
  if (strcmp(name, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
    return (adapter && adapter->descriptorBuffer) || rayQuery ||
           rayTracingPipeline;
  }
#endif
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  if (strcmp(name, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0 ||
      strcmp(name, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0 ||
      strcmp(name, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) {
    return rayQuery || rayTracingPipeline;
  }
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  if (strcmp(name, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0) {
    return rayTracingPipeline;
  }
#endif
#ifdef VK_EXT_mesh_shader
  if (strcmp(name, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0) {
    return meshShader;
  }
#endif
#ifdef VK_KHR_fragment_shading_rate
  if (strcmp(name, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask,
                             GPU_FEATURE_VARIABLE_RATE_SHADING);
  }
#endif
#ifdef VK_KHR_cooperative_matrix
  if (strcmp(name, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME) == 0) {
    return vk_featureEnabled(enabledFeatureMask,
                             GPU_FEATURE_SUBGROUP_MATRIX);
  }
#endif
  if (strcmp(name, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) == 0 ||
      strcmp(name, VK_KHR_SPIRV_1_4_EXTENSION_NAME) == 0) {
    return rayQuery || rayTracingPipeline || meshShader;
  }

  return true;
}

static uint32_t
vk_collectDeviceExtensions(const GPUAdapterVk *adapter,
                           uint64_t            enabledFeatureMask,
                           const char         **extensions,
                           uint32_t             capacity) {
  uint32_t count;

  count = 0u;
  for (uint32_t i = 0u; i < adapter->nEnabledExtensions; i++) {
    if (!vk_extensionEnabled(adapter,
                             adapter->extensionNames[i],
                             enabledFeatureMask)) {
      continue;
    }
    if (count >= capacity) {
      return 0u;
    }
    extensions[count++] = adapter->extensionNames[i];
  }
  return count;
}

#ifdef VK_EXT_descriptor_buffer
static bool
vk_hasHostVisibleDescriptorBufferMemory(VkPhysicalDevice physicalDevice,
                                        VkDevice         device) {
  VkPhysicalDeviceMemoryProperties memoryProperties;
  VkBufferCreateInfo                info = {0};
  VkMemoryRequirements              requirements;
  VkBuffer                          buffer;
  bool                              supported;

  if (!physicalDevice || !device) {
    return false;
  }
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size  = 256u;
  info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
               VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &info, NULL, &buffer) != VK_SUCCESS) {
    return false;
  }
  vkGetBufferMemoryRequirements(device, buffer, &requirements);
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  supported = false;
  for (uint32_t i = 0u; i < memoryProperties.memoryTypeCount; i++) {
    if ((requirements.memoryTypeBits & (1u << i)) != 0u &&
        (memoryProperties.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u) {
      supported = true;
      break;
    }
  }
  vkDestroyBuffer(device, buffer, NULL);
  return supported;
}
#endif

static void
vk_querySubgroupCapabilities(GPUInstanceVk *instance,
                             GPUAdapterVk  *adapter,
                             bool           sizeControl) {
  PFN_vkGetPhysicalDeviceProperties2 getProperties2;
  VkPhysicalDeviceSubgroupSizeControlProperties sizeProperties = {0};
  VkPhysicalDeviceSubgroupProperties            subgroup = {0};
  VkPhysicalDeviceProperties2                   properties = {0};

  if (!instance || !adapter ||
      instance->apiVersion < VK_API_VERSION_1_1 ||
      adapter->props.apiVersion < VK_API_VERSION_1_1) {
    return;
  }

  getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
    vkGetInstanceProcAddr(instance->inst, "vkGetPhysicalDeviceProperties2");
  if (!getProperties2) {
    return;
  }

  subgroup.sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &subgroup;
  if (sizeControl) {
    sizeProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
    subgroup.pNext = &sizeProperties;
  }
  getProperties2(adapter->physicalDevice, &properties);

  adapter->subgroupOperations = subgroup.supportedOperations;
  adapter->subgroupStages     = subgroup.supportedStages;
  adapter->subgroupSize       = subgroup.subgroupSize;
  adapter->minSubgroupSize    = subgroup.subgroupSize;
  adapter->maxSubgroupSize    = subgroup.subgroupSize;
  if (sizeControl && sizeProperties.minSubgroupSize > 0u &&
      sizeProperties.maxSubgroupSize >= sizeProperties.minSubgroupSize) {
    adapter->minSubgroupSize = sizeProperties.minSubgroupSize;
    adapter->maxSubgroupSize = sizeProperties.maxSubgroupSize;
  }
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
  VkPhysicalDeviceShaderFloat16Int8Features float16Features = {0};
  VkPhysicalDeviceFeatures2KHR              float16Features2 = {0};
  VkPhysicalDevice16BitStorageFeatures      storage16Features = {0};
  VkPhysicalDeviceFeatures2                 storage16Features2 = {0};
  VkPhysicalDeviceVulkanMemoryModelFeatures memoryModelFeatures = {0};
  VkPhysicalDeviceFeatures2                 memoryModelFeatures2 = {0};
  VkPhysicalDeviceShaderAtomicInt64Features atomic64Features = {0};
  VkPhysicalDeviceFeatures2                 atomic64Features2 = {0};
  VkPhysicalDeviceDescriptorIndexingFeatures descriptorFeatures = {0};
  VkPhysicalDeviceFeatures2                  descriptorFeatures2 = {0};
  VkPhysicalDeviceTimelineSemaphoreFeatures  timelineFeatures = {0};
  VkPhysicalDeviceFeatures2                  timelineFeatures2 = {0};
  VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {0};
  VkPhysicalDeviceFeatures2                  sync2Features2 = {0};
  VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddressFeatures = {0};
  VkPhysicalDeviceFeatures2                  bufferAddressFeatures2 = {0};
#ifdef VK_EXT_descriptor_buffer
  VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {0};
  VkPhysicalDeviceFeatures2                    descriptorBufferFeatures2 = {0};
  VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties = {0};
  VkPhysicalDeviceProperties2                  descriptorBufferProperties2 = {0};
#endif
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = {0};
  VkPhysicalDeviceRayQueryFeaturesKHR          rayQueryFeatures = {0};
  VkPhysicalDeviceFeatures2                    rayQueryFeatures2 = {0};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties = {0};
  VkPhysicalDeviceProperties2                  rayQueryProperties2 = {0};
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayPipelineFeatures = {0};
  VkPhysicalDeviceFeatures2                     rayPipelineFeatures2 = {0};
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayPipelineProperties = {0};
  VkPhysicalDeviceProperties2                   rayPipelineProperties2 = {0};
#endif
#ifdef VK_KHR_fragment_shading_rate
  VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrsFeatures = {0};
  VkPhysicalDeviceFeatures2                  vrsFeatures2 = {0};
  VkPhysicalDeviceFragmentShadingRatePropertiesKHR vrsProps = {0};
  VkPhysicalDeviceProperties2                vrsProps2 = {0};
#endif
#ifdef VK_EXT_mesh_shader
  VkPhysicalDeviceMeshShaderFeaturesEXT      meshFeatures = {0};
  VkPhysicalDeviceFeatures2                  meshFeatures2 = {0};
#endif
#ifdef VK_KHR_cooperative_matrix
  VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperativeFeatures = {0};
  VkPhysicalDeviceFeatures2                  cooperativeFeatures2 = {0};
  VkPhysicalDeviceCooperativeMatrixPropertiesKHR cooperativeProperties = {0};
  VkPhysicalDeviceProperties2                cooperativeProperties2 = {0};
#endif
  VkResult                                  err;
  uint32_t                                  i, nExtensions;
  bool                                      incrementalPresentEnabled;
  bool                                      displayTimingEnabled;
  bool                                      dynamicExtension;
  bool                                      dynamicCore;
  bool                                      descriptorExtension;
  bool                                      subgroupSizeControl;
  bool                                      float16Extension;
  bool                                      float16Core;
  bool                                      storage16Extension;
  bool                                      storage16Core;
  bool                                      memoryModelExtension;
  bool                                      memoryModelCore;
  bool                                      atomic64Extension;
  bool                                      atomic64Core;
  bool                                      descriptorCore;
  bool                                      bufferAddressExtension;
  bool                                      bufferAddressCore;
#ifdef VK_EXT_descriptor_buffer
  bool                                      descriptorBufferExtension;
#endif
  bool                                      timelineCore;
  bool                                      sync2Extension;
  bool                                      sync2Core;
  bool                                      maintenance1Extension;
  bool                                      maintenance1Core;
  bool                                      spirv14Extension;
  bool                                      shaderFloatControlsExtension;
  bool                                      spirv14Core;
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  bool                                      accelerationExtension;
  bool                                      rayQueryExtension;
  bool                                      deferredHostExtension;
  bool                                      rayQueryDependencies;
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  bool                                      rayPipelineExtension;
#endif
#ifdef VK_KHR_fragment_shading_rate
  bool                                      vrsExtension;
#endif
#ifdef VK_EXT_mesh_shader
  bool                                      meshExtension;
  bool                                      spirv14ExtensionUsable;
#endif
#ifdef VK_KHR_cooperative_matrix
  bool                                      cooperativeExtension;
#endif

  extensions                = NULL;
  nExtensions               = 0;
  incrementalPresentEnabled = true;
  displayTimingEnabled      = true;
  dynamicExtension          = false;
  dynamicCore               = false;
  descriptorExtension       = false;
  subgroupSizeControl       = false;
  float16Extension          = false;
  float16Core               = false;
  storage16Extension        = false;
  storage16Core             = false;
  memoryModelExtension      = false;
  memoryModelCore           = false;
  atomic64Extension         = false;
  atomic64Core              = false;
  descriptorCore            = false;
  bufferAddressExtension    = false;
  bufferAddressCore         = false;
#ifdef VK_EXT_descriptor_buffer
  descriptorBufferExtension = false;
#endif
  timelineCore              = false;
  sync2Extension            = false;
  sync2Core                 = false;
  maintenance1Extension     = false;
  maintenance1Core          = false;
  spirv14Extension          = false;
  shaderFloatControlsExtension = false;
  spirv14Core               = false;
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  accelerationExtension     = false;
  rayQueryExtension         = false;
  deferredHostExtension     = false;
  rayQueryDependencies      = false;
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  rayPipelineExtension      = false;
#endif
#ifdef VK_KHR_fragment_shading_rate
  vrsExtension              = false;
#endif
#ifdef VK_EXT_mesh_shader
  meshExtension                 = false;
  spirv14ExtensionUsable       = false;
#endif
#ifdef VK_KHR_cooperative_matrix
  cooperativeExtension         = false;
#endif

  adapter                   = calloc(1, sizeof(*adapter));
  adapterVk                 = calloc(1, sizeof(*adapterVk));
  if (!adapter || !adapterVk) {
    goto fail;
  }
  adapterVk->physicalDevice = raw;
  adapter->_priv            = adapterVk;
  adapter->inst             = inst;
  instanceVk                = inst ? inst->_priv : NULL;

  vkGetPhysicalDeviceProperties(raw, &adapterVk->props);

  /* Call with NULL data to get count */
  vkGetPhysicalDeviceQueueFamilyProperties(adapterVk->physicalDevice,
                                           &adapterVk->nQueFamilies,
                                           NULL);
  if (adapterVk->nQueFamilies == 0u) {
    goto fail;
  }

  adapterVk->queueFamilyProps = malloc(adapterVk->nQueFamilies *
                                       sizeof(*adapterVk->queueFamilyProps));
  if (!adapterVk->queueFamilyProps) {
    goto fail;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(adapterVk->physicalDevice,
                                           &adapterVk->nQueFamilies,
                                           adapterVk->queueFamilyProps);
  vkGetPhysicalDeviceFeatures(adapterVk->physicalDevice, &adapterVk->features);

  /* Look for device extensions */
  err = vkEnumerateDeviceExtensionProperties(adapterVk->physicalDevice, NULL,
                                             &nExtensions, NULL);
  if (err != VK_SUCCESS) {
    goto fail;
  }

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  err = vkGetPhysicalDeviceDisplayPropertiesKHR(adapterVk->physicalDevice,
                                                &adapterVk->nDisplayProperties,
                                                NULL);
  if (err != VK_SUCCESS) {
    adapterVk->nDisplayProperties = 0u;
  }
#endif

#define VK__ADD_EXT_IF(X, R)                                                  \
    if (!strcmp(X, extensions[i].extensionName)) {                            \
      adapterVk->extensionNames[adapterVk->nEnabledExtensions++] = X;         \
      R;                                                                      \
    }                                                                         \
    assert(adapterVk->nEnabledExtensions < 64);

  if (nExtensions > 0) {
    extensions = malloc(sizeof(*extensions) * nExtensions);
    if (!extensions) {
      goto fail;
    }
    err = vkEnumerateDeviceExtensionProperties(adapterVk->physicalDevice,
                                               NULL,
                                               &nExtensions,
                                               extensions);
    if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
      goto fail;
    }

    for (i = 0; i < nExtensions; i++) {
      VK__ADD_EXT_IF(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                     adapter->supportsSwapchain = true);

      VK__ADD_EXT_IF("VK_KHR_portability_subset", (void)NULL);

      VK__ADD_EXT_IF(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
                     float16Extension = true);

      if (!strcmp(VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        storage16Extension = true;
      }
      if (!strcmp(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        memoryModelExtension = true;
      }

      if (!strcmp(VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        atomic64Extension = true;
      }

      VK__ADD_EXT_IF(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                     descriptorExtension = true);

#ifdef VK_EXT_descriptor_buffer
      if (!strcmp(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        descriptorBufferExtension = true;
      }
#endif
#ifdef VK_KHR_buffer_device_address
      if (!strcmp(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        bufferAddressExtension = true;
      }
#endif

      if (!strcmp(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        dynamicExtension = true;
      }
      if (!strcmp(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        sync2Extension = true;
      }
      if (!strcmp(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        subgroupSizeControl = true;
      }
      if (!strcmp(VK_KHR_MAINTENANCE1_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        maintenance1Extension = true;
      }
      if (!strcmp(VK_KHR_SPIRV_1_4_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        spirv14Extension = true;
      }
      if (!strcmp(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        shaderFloatControlsExtension = true;
      }
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
      if (!strcmp(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        accelerationExtension = true;
      }
      if (!strcmp(VK_KHR_RAY_QUERY_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        rayQueryExtension = true;
      }
      if (!strcmp(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        deferredHostExtension = true;
      }
#endif
#ifdef VK_KHR_ray_tracing_pipeline
      if (!strcmp(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        rayPipelineExtension = true;
      }
#endif
#ifdef VK_EXT_mesh_shader
      if (!strcmp(VK_EXT_MESH_SHADER_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        meshExtension = true;
      }
#endif
#ifdef VK_KHR_fragment_shading_rate
      if (!strcmp(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        vrsExtension = true;
      }
#endif
#ifdef VK_KHR_cooperative_matrix
      if (!strcmp(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME,
                  extensions[i].extensionName)) {
        cooperativeExtension = true;
      }
#endif

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

  if (instanceVk && instanceVk->apiVersion >= VK_API_VERSION_1_3 &&
      adapterVk->props.apiVersion >= VK_API_VERSION_1_3) {
    subgroupSizeControl = true;
  }
  maintenance1Core = instanceVk &&
                     instanceVk->apiVersion >= VK_API_VERSION_1_1 &&
                     adapterVk->props.apiVersion >= VK_API_VERSION_1_1;
  if (!maintenance1Core && maintenance1Extension) {
    adapterVk->extensionNames[adapterVk->nEnabledExtensions++] =
      VK_KHR_MAINTENANCE1_EXTENSION_NAME;
    assert(adapterVk->nEnabledExtensions < 64);
  }
  adapterVk->negativeViewport = maintenance1Core || maintenance1Extension;
  vk_querySubgroupCapabilities(instanceVk,
                               adapterVk,
                               subgroupSizeControl);

  dynamicCore = instanceVk && instanceVk->apiVersion >= VK_API_VERSION_1_3 &&
                adapterVk->props.apiVersion >= VK_API_VERSION_1_3;
  sync2Core = dynamicCore;
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
  float16Core = instanceVk &&
                instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
                adapterVk->props.apiVersion >= VK_API_VERSION_1_2;
  storage16Core = instanceVk &&
                  instanceVk->apiVersion >= VK_API_VERSION_1_1 &&
                  adapterVk->props.apiVersion >= VK_API_VERSION_1_1;
  memoryModelCore = instanceVk &&
                    instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
                    adapterVk->props.apiVersion >= VK_API_VERSION_1_2;
  atomic64Core = instanceVk &&
                 instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
                 adapterVk->props.apiVersion >= VK_API_VERSION_1_2;
  descriptorCore = instanceVk &&
                   instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
                   adapterVk->props.apiVersion >= VK_API_VERSION_1_2;
  bufferAddressCore = descriptorCore;
  timelineCore = instanceVk &&
                 instanceVk->apiVersion >= VK_API_VERSION_1_2 &&
                 adapterVk->props.apiVersion >= VK_API_VERSION_1_2;
  spirv14Core = timelineCore;
#ifdef VK_EXT_mesh_shader
  spirv14ExtensionUsable = instanceVk &&
                           instanceVk->apiVersion >= VK_API_VERSION_1_1 &&
                           adapterVk->props.apiVersion >= VK_API_VERSION_1_1 &&
                           spirv14Extension &&
                           shaderFloatControlsExtension;
#endif
  if (getFeatures2 && (float16Core || float16Extension)) {
    float16Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
    float16Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    float16Features2.pNext = &float16Features;
    getFeatures2(raw, &float16Features2);
    adapterVk->shaderFloat16 = float16Features.shaderFloat16;
  }
  if (getFeatures2 && (storage16Core || storage16Extension)) {
    storage16Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
    storage16Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    storage16Features2.pNext = &storage16Features;
    getFeatures2(raw, &storage16Features2);
    adapterVk->storageBuffer16BitAccess =
      storage16Features.storageBuffer16BitAccess;
    if (adapterVk->storageBuffer16BitAccess && !storage16Core &&
        !vk_addDeviceExtension(adapterVk,
                               VK_KHR_16BIT_STORAGE_EXTENSION_NAME)) {
      goto fail;
    }
  }
  if (getFeatures2 && (memoryModelCore || memoryModelExtension)) {
    memoryModelFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    memoryModelFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    memoryModelFeatures2.pNext = &memoryModelFeatures;
    getFeatures2(raw, &memoryModelFeatures2);
    adapterVk->vulkanMemoryModel = memoryModelFeatures.vulkanMemoryModel;
    if (adapterVk->vulkanMemoryModel && !memoryModelCore &&
        !vk_addDeviceExtension(adapterVk,
                               VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME)) {
      goto fail;
    }
  }
  if (getFeatures2 && (atomic64Core || atomic64Extension)) {
    atomic64Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    atomic64Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    atomic64Features2.pNext = &atomic64Features;
    getFeatures2(raw, &atomic64Features2);
    if (adapterVk->features.shaderInt64 &&
        atomic64Features.shaderBufferInt64Atomics) {
      if (!atomic64Core &&
          !vk_addDeviceExtension(adapterVk,
                                 VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME)) {
        goto fail;
      }
      adapterVk->atomic64 = true;
    }
  }
  if (getFeatures2 && (bufferAddressCore || bufferAddressExtension)) {
    bufferAddressFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferAddressFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    bufferAddressFeatures2.pNext = &bufferAddressFeatures;
    getFeatures2(raw, &bufferAddressFeatures2);
    adapterVk->bufferDeviceAddress =
      bufferAddressFeatures.bufferDeviceAddress == VK_TRUE;
  }
#ifdef VK_EXT_descriptor_buffer
  if (getFeatures2 && descriptorBufferExtension &&
      adapterVk->bufferDeviceAddress) {
    PFN_vkGetPhysicalDeviceProperties2 getProperties2;

    descriptorBufferFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBufferFeatures2.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    descriptorBufferFeatures2.pNext = &descriptorBufferFeatures;
    getFeatures2(raw, &descriptorBufferFeatures2);

    getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
      vkGetInstanceProcAddr(instanceVk->inst,
                            "vkGetPhysicalDeviceProperties2");
    if (!getProperties2) {
      getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceProperties2KHR");
    }
    if (descriptorBufferFeatures.descriptorBuffer && getProperties2) {
      descriptorBufferProperties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
      descriptorBufferProperties2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      descriptorBufferProperties2.pNext = &descriptorBufferProperties;
      getProperties2(raw, &descriptorBufferProperties2);
      if (descriptorBufferProperties.descriptorBufferOffsetAlignment > 0u &&
          descriptorBufferProperties.maxDescriptorBufferBindings >=
            GPU_ENCODER_MAX_BIND_GROUPS &&
          descriptorBufferProperties.maxResourceDescriptorBufferBindings >=
            GPU_ENCODER_MAX_BIND_GROUPS &&
          descriptorBufferProperties.maxSamplerDescriptorBufferBindings >=
            GPU_ENCODER_MAX_BIND_GROUPS &&
          descriptorBufferProperties.maxResourceDescriptorBufferRange > 0u &&
          descriptorBufferProperties.maxSamplerDescriptorBufferRange > 0u &&
          vk_addDeviceExtension(adapterVk,
                                VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) &&
          (bufferAddressCore ||
           vk_addDeviceExtension(adapterVk,
                                 VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))) {
        adapterVk->descriptorBufferProperties = descriptorBufferProperties;
        adapterVk->descriptorBuffer           = true;
      }
    }
  }
#endif
  if (getFeatures2 && (descriptorCore || descriptorExtension)) {
    descriptorFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    descriptorFeatures2.pNext = &descriptorFeatures;
    getFeatures2(raw, &descriptorFeatures2);
    adapterVk->descriptorIndexing =
      descriptorFeatures.shaderUniformBufferArrayNonUniformIndexing &&
      descriptorFeatures.shaderSampledImageArrayNonUniformIndexing &&
      descriptorFeatures.shaderStorageBufferArrayNonUniformIndexing &&
      descriptorFeatures.shaderStorageImageArrayNonUniformIndexing;
    adapterVk->bindless = adapterVk->descriptorIndexing &&
                          descriptorFeatures.descriptorBindingPartiallyBound;
  }
#ifdef VK_KHR_cooperative_matrix
  if (getFeatures2 && cooperativeExtension) {
    PFN_vkGetPhysicalDeviceProperties2 getProperties2;

    cooperativeFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
    cooperativeFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    cooperativeFeatures2.pNext = &cooperativeFeatures;
    getFeatures2(raw, &cooperativeFeatures2);

    getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
      vkGetInstanceProcAddr(instanceVk->inst,
                            "vkGetPhysicalDeviceProperties2");
    if (!getProperties2) {
      getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceProperties2KHR");
    }
    adapterVk->getCooperativeMatrixProperties =
      (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)
        vkGetInstanceProcAddr(
          instanceVk->inst,
          "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR"
        );
    if (cooperativeFeatures.cooperativeMatrix &&
        adapterVk->vulkanMemoryModel && getProperties2 &&
        adapterVk->getCooperativeMatrixProperties) {
      cooperativeProperties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
      cooperativeProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      cooperativeProperties2.pNext = &cooperativeProperties;
      getProperties2(raw, &cooperativeProperties2);
      adapterVk->subgroupMatrixStages =
        cooperativeProperties.cooperativeMatrixSupportedStages;
      if ((adapterVk->subgroupMatrixStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
          vk_hasSubgroupMatrixProperty(adapter) &&
          !vk_addDeviceExtension(adapterVk,
                                 VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)) {
        goto fail;
      }
    }
  }
#endif
  if (getFeatures2 && timelineCore) {
    timelineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    timelineFeatures2.pNext = &timelineFeatures;
    getFeatures2(raw, &timelineFeatures2);
    adapterVk->timelineSemaphore = timelineFeatures.timelineSemaphore;
  }
  if (getFeatures2 && (sync2Core || sync2Extension)) {
    sync2Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    sync2Features2.pNext = &sync2Features;
    getFeatures2(raw, &sync2Features2);
    if (sync2Features.synchronization2) {
      if (!sync2Core) {
        adapterVk->extensionNames[adapterVk->nEnabledExtensions++] =
          VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
        assert(adapterVk->nEnabledExtensions < 64);
      }
      adapterVk->synchronization2 = true;
    }
  }
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  rayQueryDependencies = instanceVk &&
                         instanceVk->apiVersion >= VK_API_VERSION_1_1 &&
                         adapterVk->props.apiVersion >= VK_API_VERSION_1_1 &&
                         accelerationExtension && rayQueryExtension &&
                         deferredHostExtension &&
                         (timelineCore ||
                          (descriptorExtension && bufferAddressExtension &&
                           spirv14Extension &&
                           shaderFloatControlsExtension));
  if (getFeatures2 && rayQueryDependencies) {
    bufferAddressFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    accelerationFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    rayQueryFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    rayQueryFeatures.pNext       = &accelerationFeatures;
    accelerationFeatures.pNext   = &bufferAddressFeatures;
    rayQueryFeatures2.pNext      = &rayQueryFeatures;
    getFeatures2(raw, &rayQueryFeatures2);
    if (rayQueryFeatures.rayQuery &&
        accelerationFeatures.accelerationStructure &&
        bufferAddressFeatures.bufferDeviceAddress) {
      PFN_vkGetPhysicalDeviceProperties2 getProperties2;

      if (!vk_addDeviceExtension(
            adapterVk,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) ||
          !vk_addDeviceExtension(adapterVk,
                                 VK_KHR_RAY_QUERY_EXTENSION_NAME) ||
          !vk_addDeviceExtension(
            adapterVk,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) ||
          (!timelineCore &&
           (!vk_addDeviceExtension(
              adapterVk,
              VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) ||
            !vk_addDeviceExtension(adapterVk,
                                   VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) ||
            !vk_addDeviceExtension(adapterVk,
                                   VK_KHR_SPIRV_1_4_EXTENSION_NAME)))) {
        goto fail;
      }

      getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceProperties2");
      if (!getProperties2) {
        getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
          vkGetInstanceProcAddr(instanceVk->inst,
                                "vkGetPhysicalDeviceProperties2KHR");
      }
      if (getProperties2) {
        accelerationProperties.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        rayQueryProperties2.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        rayQueryProperties2.pNext = &accelerationProperties;
        getProperties2(raw, &rayQueryProperties2);
        adapterVk->accelerationStructureScratchAlignment =
          accelerationProperties.minAccelerationStructureScratchOffsetAlignment;
        adapterVk->rayQuery =
          adapterVk->accelerationStructureScratchAlignment > 0u;
      }
    }
  }
#ifdef VK_KHR_ray_tracing_pipeline
  if (getFeatures2 && adapterVk->rayQuery && rayPipelineExtension) {
    PFN_vkGetPhysicalDeviceProperties2 getProperties2;

    rayPipelineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayPipelineFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    rayPipelineFeatures2.pNext = &rayPipelineFeatures;
    getFeatures2(raw, &rayPipelineFeatures2);
    if (rayPipelineFeatures.rayTracingPipeline &&
        vk_addDeviceExtension(
          adapterVk,
          VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
      getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceProperties2");
      if (!getProperties2) {
        getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
          vkGetInstanceProcAddr(instanceVk->inst,
                                "vkGetPhysicalDeviceProperties2KHR");
      }
      if (getProperties2) {
        rayPipelineProperties.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        rayPipelineProperties2.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        rayPipelineProperties2.pNext = &rayPipelineProperties;
        getProperties2(raw, &rayPipelineProperties2);
        adapterVk->rayTracingShaderGroupHandleSize =
          rayPipelineProperties.shaderGroupHandleSize;
        adapterVk->rayTracingShaderGroupHandleAlignment =
          rayPipelineProperties.shaderGroupHandleAlignment;
        adapterVk->rayTracingShaderGroupBaseAlignment =
          rayPipelineProperties.shaderGroupBaseAlignment;
        adapterVk->rayTracingMaxRecursionDepth =
          rayPipelineProperties.maxRayRecursionDepth;
        adapterVk->rayTracingPipeline =
          adapterVk->rayTracingShaderGroupHandleSize > 0u &&
          adapterVk->rayTracingShaderGroupHandleAlignment > 0u &&
          adapterVk->rayTracingShaderGroupBaseAlignment > 0u &&
          adapterVk->rayTracingMaxRecursionDepth > 0u;
      }
    }
  }
#endif
#endif
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
#ifdef VK_KHR_fragment_shading_rate
  if (getFeatures2 && vrsExtension) {
    PFN_vkGetPhysicalDeviceProperties2 getProperties2;
    PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR getRates;
    VkPhysicalDeviceFragmentShadingRateKHR *rates;
    VkFormatProperties formatProps;
    uint32_t rateCount;

    vrsFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
    vrsFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vrsFeatures2.pNext = &vrsFeatures;
    getFeatures2(raw, &vrsFeatures2);
    adapterVk->vrsDrawRate = vrsFeatures.pipelineFragmentShadingRate;
    adapterVk->vrsAttachment =
      vrsFeatures.attachmentFragmentShadingRate &&
      adapterVk->dynamicRendering;
    if (adapterVk->vrsDrawRate || adapterVk->vrsAttachment) {
      adapterVk->vrsCombiners =
        GPU_SHADING_RATE_COMBINER_KEEP_BIT_EXT |
        GPU_SHADING_RATE_COMBINER_REPLACE_BIT_EXT;

      getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceProperties2");
      if (!getProperties2) {
        getProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
          vkGetInstanceProcAddr(instanceVk->inst,
                                "vkGetPhysicalDeviceProperties2KHR");
      }
      if (getProperties2) {
        vrsProps.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
        vrsProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vrsProps2.pNext = &vrsProps;
        getProperties2(raw, &vrsProps2);
        adapterVk->minVRSTexelSize =
          vrsProps.minFragmentShadingRateAttachmentTexelSize;
        adapterVk->maxVRSTexelSize =
          vrsProps.maxFragmentShadingRateAttachmentTexelSize;
        adapterVk->maxVRSTexelAspectRatio =
          vrsProps.maxFragmentShadingRateAttachmentTexelSizeAspectRatio;
        if (vrsProps.fragmentShadingRateNonTrivialCombinerOps) {
          adapterVk->vrsCombiners |=
            GPU_SHADING_RATE_COMBINER_MIN_BIT_EXT |
            GPU_SHADING_RATE_COMBINER_MAX_BIT_EXT;
        }
      } else {
        adapterVk->vrsAttachment = false;
      }
      if (adapterVk->vrsAttachment) {
        memset(&formatProps, 0, sizeof(formatProps));
        vkGetPhysicalDeviceFormatProperties(raw,
                                            VK_FORMAT_R8_UINT,
                                            &formatProps);
        if (adapterVk->minVRSTexelSize.width == 0u ||
            adapterVk->minVRSTexelSize.height == 0u ||
            adapterVk->maxVRSTexelSize.width <
              adapterVk->minVRSTexelSize.width ||
            adapterVk->maxVRSTexelSize.height <
              adapterVk->minVRSTexelSize.height ||
            (formatProps.optimalTilingFeatures &
             VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) ==
              0u) {
          adapterVk->vrsAttachment = false;
        }
      }

      getRates = (PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)
        vkGetInstanceProcAddr(instanceVk->inst,
                              "vkGetPhysicalDeviceFragmentShadingRatesKHR");
      rateCount = 0u;
      rates     = NULL;
      if (getRates &&
          getRates(raw, &rateCount, NULL) == VK_SUCCESS && rateCount > 0u) {
        rates = calloc(rateCount, sizeof(*rates));
      }
      if (rates) {
        for (i = 0u; i < rateCount; i++) {
          rates[i].sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
        }
        if (getRates(raw, &rateCount, rates) == VK_SUCCESS) {
          for (i = 0u; i < rateCount; i++) {
            const VkExtent2D size = rates[i].fragmentSize;

            if (size.width == 1u && size.height == 1u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_1X1_BIT_EXT;
            else if (size.width == 1u && size.height == 2u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_1X2_BIT_EXT;
            else if (size.width == 2u && size.height == 1u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_2X1_BIT_EXT;
            else if (size.width == 2u && size.height == 2u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_2X2_BIT_EXT;
            else if (size.width == 2u && size.height == 4u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_2X4_BIT_EXT;
            else if (size.width == 4u && size.height == 2u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_4X2_BIT_EXT;
            else if (size.width == 4u && size.height == 4u)
              adapterVk->vrsRates |= GPU_SHADING_RATE_4X4_BIT_EXT;
          }
        }
        free(rates);
      }
      if ((adapterVk->vrsRates & GPU_SHADING_RATE_1X1_BIT_EXT) == 0u) {
        adapterVk->vrsDrawRate = false;
        adapterVk->vrsAttachment = false;
      }
      if (adapterVk->vrsDrawRate || adapterVk->vrsAttachment) {
        adapterVk->extensionNames[adapterVk->nEnabledExtensions++] =
          VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
        assert(adapterVk->nEnabledExtensions < 64);
      }
    }
  }
#endif
#ifdef VK_EXT_mesh_shader
  if (getFeatures2 && meshExtension &&
      (spirv14Core || spirv14ExtensionUsable)) {
    meshFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    meshFeatures2.pNext = &meshFeatures;
    getFeatures2(raw, &meshFeatures2);
    if (meshFeatures.meshShader) {
      if (!spirv14Core) {
        if (!vk_addDeviceExtension(
              adapterVk,
              VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) ||
            !vk_addDeviceExtension(adapterVk,
                                   VK_KHR_SPIRV_1_4_EXTENSION_NAME)) {
          goto fail;
        }
      }
      adapterVk->extensionNames[adapterVk->nEnabledExtensions++] =
        VK_EXT_MESH_SHADER_EXTENSION_NAME;
      assert(adapterVk->nEnabledExtensions < 64);
      adapterVk->meshShader = true;
      adapterVk->taskShader = meshFeatures.taskShader;
    }
  }
#endif

#undef VK__ADD_EXT_IF

  return adapter;

fail:
  free(extensions);
  if (adapterVk) {
    free(adapterVk->queueFamilyProps);
  }
  free(adapterVk);
  free(adapter);
  return NULL;
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
    case GPU_FEATURE_SUBGROUPS:
      return vk_hasSubgroupCapability(adapterVk);
    case GPU_FEATURE_SHADER_F16:
      return adapterVk->shaderFloat16;
    case GPU_FEATURE_DESCRIPTOR_INDEXING:
      return adapterVk->descriptorIndexing;
    case GPU_FEATURE_BINDLESS:
      return adapterVk->bindless;
    case GPU_FEATURE_MESH_SHADER:
      return adapterVk->meshShader;
    case GPU_FEATURE_VARIABLE_RATE_SHADING:
      return adapterVk->vrsDrawRate || adapterVk->vrsAttachment;
    case GPU_FEATURE_RAY_QUERY:
      return adapterVk->rayQuery;
    case GPU_FEATURE_RAY_TRACING_PIPELINE:
      return adapterVk->rayTracingPipeline;
    case GPU_FEATURE_SUBGROUP_MATRIX:
      return adapterVk->subgroupMatrix;
    case GPU_FEATURE_ATOMIC64:
      return adapterVk->atomic64;
    case GPU_FEATURE_PLACED_RESOURCES:
      return true;
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
  if (vk_hasSubgroupCapability(adapterVk)) {
    outLimits->minSubgroupSize = adapterVk->minSubgroupSize;
    outLimits->maxSubgroupSize = adapterVk->maxSubgroupSize;
  }
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
  if (err != VK_SUCCESS || gpuCount == 0u) {
    return NULL;
  }

  physicalDevices = malloc(sizeof(VkPhysicalDevice) * gpuCount);
  if (!physicalDevices) {
    return NULL;
  }
  err             = vkEnumeratePhysicalDevices(instRaw,
                                               &gpuCount,
                                               physicalDevices);
  if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
    free(physicalDevices);
    return NULL;
  }

  for (i = 0; i < gpuCount && i < maxNumberOfItems; i++) {
    adapter = vk_newAdapter(inst, physicalDevices[i]);
    if (!adapter) {
      continue;
    }

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
vk_createDevice(GPUAdapter              * __restrict adapter,
                const GPUQueueCreateInfo queCI[],
                uint32_t                 nQueCI,
                uint64_t                 enabledFeatureMask) {
  GPUDevice               *device;
  GPUDeviceVk             *deviceVk;
  GPUAdapterVk            *adapterVk;
  GPUQueuePlanVk          *plans;
  GPUQueuePlanVk          *plan;
  VkDeviceQueueCreateInfo *queues;
  const char              *deviceExtensions[64];
  float                   *queuePriorities;
  VkPhysicalDeviceFeatures coreFeatures = {0};
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicFeatures = {0};
  VkPhysicalDeviceShaderFloat16Int8Features float16Features = {0};
  VkPhysicalDevice16BitStorageFeatures storage16Features = {0};
  VkPhysicalDeviceVulkanMemoryModelFeatures memoryModelFeatures = {0};
  VkPhysicalDeviceShaderAtomicInt64Features atomic64Features = {0};
  VkPhysicalDeviceDescriptorIndexingFeatures descriptorFeatures = {0};
  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {0};
  VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {0};
  VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddressFeatures = {0};
#ifdef VK_EXT_descriptor_buffer
  VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {0};
#endif
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = {0};
  VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {0};
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayPipelineFeatures = {0};
#endif
#ifdef VK_EXT_mesh_shader
  VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures = {0};
#endif
#ifdef VK_KHR_fragment_shading_rate
  VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrsFeatures = {0};
#endif
#ifdef VK_KHR_cooperative_matrix
  VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperativeFeatures = {0};
#endif
  VkDeviceCreateInfo       deviceCI = {0};
  VkResult                 result;
  uint32_t                 familyIndex;
  uint32_t                 deviceExtensionCount;
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

  adapterVk = adapter->_priv;
  if (!adapterVk->negativeViewport) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SHADER_F16)) != 0u &&
      !adapterVk->shaderFloat16) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_ATOMIC64)) != 0u &&
      !adapterVk->atomic64) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING)) != 0u &&
      !adapterVk->descriptorIndexing) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_BINDLESS)) != 0u &&
      !adapterVk->bindless) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) != 0u &&
      !adapterVk->meshShader) {
    goto err;
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) != 0u &&
      !adapterVk->vrsDrawRate && !adapterVk->vrsAttachment) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u &&
      !adapterVk->rayQuery) {
    goto err;
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) != 0u &&
      !adapterVk->rayTracingPipeline) {
    goto err;
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_SUBGROUP_MATRIX)) != 0u &&
      !adapterVk->subgroupMatrix) {
    goto err;
  }
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

  deviceExtensionCount = vk_collectDeviceExtensions(
    adapterVk,
    enabledFeatureMask,
    deviceExtensions,
    (uint32_t)GPU_ARRAY_LEN(deviceExtensions)
  );

  if (vk_featureEnabled(enabledFeatureMask,
                        GPU_FEATURE_PIPELINE_STATISTICS)) {
    coreFeatures.pipelineStatisticsQuery =
      adapterVk->features.pipelineStatisticsQuery;
  }
  if (vk_featureEnabled(enabledFeatureMask,
                        GPU_FEATURE_DESCRIPTOR_INDEXING) ||
      vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_BINDLESS)) {
    coreFeatures.shaderUniformBufferArrayDynamicIndexing =
      adapterVk->features.shaderUniformBufferArrayDynamicIndexing;
    coreFeatures.shaderSampledImageArrayDynamicIndexing =
      adapterVk->features.shaderSampledImageArrayDynamicIndexing;
    coreFeatures.shaderStorageBufferArrayDynamicIndexing =
      adapterVk->features.shaderStorageBufferArrayDynamicIndexing;
    coreFeatures.shaderStorageImageArrayDynamicIndexing =
      adapterVk->features.shaderStorageImageArrayDynamicIndexing;
  }
  if (vk_featureEnabled(enabledFeatureMask, GPU_FEATURE_MULTI_DRAW)) {
    coreFeatures.multiDrawIndirect = adapterVk->features.multiDrawIndirect;
  }
  coreFeatures.independentBlend   = adapterVk->features.independentBlend;
  coreFeatures.imageCubeArray     = adapterVk->features.imageCubeArray;
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_ATOMIC64)) != 0u) {
    coreFeatures.shaderInt64 = VK_TRUE;
  }

  deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.pEnabledFeatures        = &coreFeatures;
  deviceCI.queueCreateInfoCount    = planCount;
  deviceCI.pQueueCreateInfos       = queues;
  deviceCI.enabledExtensionCount   = deviceExtensionCount;
  deviceCI.ppEnabledExtensionNames = deviceExtensions;
  if (adapterVk->dynamicRendering) {
    dynamicFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamicFeatures.dynamicRendering = VK_TRUE;
    deviceCI.pNext = &dynamicFeatures;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SHADER_F16)) != 0u) {
    float16Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
    float16Features.pNext         = (void *)deviceCI.pNext;
    float16Features.shaderFloat16 = VK_TRUE;
    deviceCI.pNext                = &float16Features;
    if (adapterVk->storageBuffer16BitAccess) {
      storage16Features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
      storage16Features.pNext = (void *)deviceCI.pNext;
      storage16Features.storageBuffer16BitAccess = VK_TRUE;
      deviceCI.pNext = &storage16Features;
    }
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_ATOMIC64)) != 0u) {
    atomic64Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    atomic64Features.pNext = (void *)deviceCI.pNext;
    atomic64Features.shaderBufferInt64Atomics = VK_TRUE;
    deviceCI.pNext = &atomic64Features;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING)) != 0u ||
      (enabledFeatureMask & (1ull << GPU_FEATURE_BINDLESS)) != 0u) {
    descriptorFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorFeatures.pNext = (void *)deviceCI.pNext;
    descriptorFeatures.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    descriptorFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descriptorFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    descriptorFeatures.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    if ((enabledFeatureMask & (1ull << GPU_FEATURE_BINDLESS)) != 0u) {
      descriptorFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    }
    deviceCI.pNext = &descriptorFeatures;
  }
  if (adapterVk->timelineSemaphore) {
    timelineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeatures.pNext             = (void *)deviceCI.pNext;
    timelineFeatures.timelineSemaphore = VK_TRUE;
    deviceCI.pNext                      = &timelineFeatures;
  }
  if (adapterVk->synchronization2) {
    sync2Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync2Features.pNext            = (void *)deviceCI.pNext;
    sync2Features.synchronization2 = VK_TRUE;
    deviceCI.pNext                 = &sync2Features;
  }
  if (adapterVk->descriptorBuffer ||
      (enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u) {
    bufferAddressFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferAddressFeatures.pNext = (void *)deviceCI.pNext;
    bufferAddressFeatures.bufferDeviceAddress = VK_TRUE;
    deviceCI.pNext = &bufferAddressFeatures;
  }
#ifdef VK_EXT_descriptor_buffer
  if (adapterVk->descriptorBuffer) {
    descriptorBufferFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBufferFeatures.pNext = (void *)deviceCI.pNext;
    descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
    deviceCI.pNext = &descriptorBufferFeatures;
  }
#endif
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u) {
    accelerationFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    rayQueryFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    accelerationFeatures.pNext = (void *)deviceCI.pNext;
    accelerationFeatures.accelerationStructure = VK_TRUE;
    rayQueryFeatures.pNext   = &accelerationFeatures;
    rayQueryFeatures.rayQuery = VK_TRUE;
    deviceCI.pNext            = &rayQueryFeatures;
  }
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) != 0u) {
    rayPipelineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayPipelineFeatures.pNext = (void *)deviceCI.pNext;
    rayPipelineFeatures.rayTracingPipeline = VK_TRUE;
    deviceCI.pNext = &rayPipelineFeatures;
  }
#endif
#ifdef VK_EXT_mesh_shader
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) != 0u) {
    meshFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshFeatures.pNext      = (void *)deviceCI.pNext;
    meshFeatures.meshShader = VK_TRUE;
    meshFeatures.taskShader = adapterVk->taskShader;
    deviceCI.pNext           = &meshFeatures;
  }
#endif
#ifdef VK_KHR_fragment_shading_rate
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) != 0u) {
    vrsFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
    vrsFeatures.pNext = (void *)deviceCI.pNext;
    vrsFeatures.pipelineFragmentShadingRate = adapterVk->vrsDrawRate;
    vrsFeatures.attachmentFragmentShadingRate = adapterVk->vrsAttachment;
    deviceCI.pNext = &vrsFeatures;
  }
#endif
#ifdef VK_KHR_cooperative_matrix
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_SUBGROUP_MATRIX)) != 0u) {
    memoryModelFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    memoryModelFeatures.pNext = (void *)deviceCI.pNext;
    memoryModelFeatures.vulkanMemoryModel = VK_TRUE;
    deviceCI.pNext = &memoryModelFeatures;
    cooperativeFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
    cooperativeFeatures.pNext = (void *)deviceCI.pNext;
    cooperativeFeatures.cooperativeMatrix = VK_TRUE;
    deviceCI.pNext = &cooperativeFeatures;
  }
#endif

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
  deviceVk->multiDrawIndirect = coreFeatures.multiDrawIndirect;
  deviceVk->independentBlend  = coreFeatures.independentBlend;
  deviceVk->timelineSemaphore = adapterVk->timelineSemaphore;
  if (adapterVk->bufferDeviceAddress &&
      (adapterVk->descriptorBuffer ||
       (enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u)) {
    deviceVk->getBufferDeviceAddress =
      (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetBufferDeviceAddress"
      );
    if (!deviceVk->getBufferDeviceAddress) {
      deviceVk->getBufferDeviceAddress =
        (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(
          deviceVk->device,
          "vkGetBufferDeviceAddressKHR"
        );
    }
    if (!deviceVk->getBufferDeviceAddress) {
      goto err;
    }
    deviceVk->bufferDeviceAddress = true;
  }
#ifdef VK_EXT_descriptor_buffer
  if (adapterVk->descriptorBuffer) {
    deviceVk->getDescriptorSetLayoutSize =
      (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetDescriptorSetLayoutSizeEXT"
      );
    deviceVk->getDescriptorSetLayoutBindingOffset =
      (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetDescriptorSetLayoutBindingOffsetEXT"
      );
    deviceVk->getDescriptor =
      (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetDescriptorEXT"
      );
    deviceVk->bindDescriptorBuffers =
      (PFN_vkCmdBindDescriptorBuffersEXT)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCmdBindDescriptorBuffersEXT"
      );
    deviceVk->setDescriptorBufferOffsets =
      (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCmdSetDescriptorBufferOffsetsEXT"
      );
    if (deviceVk->getDescriptorSetLayoutSize &&
        deviceVk->getDescriptorSetLayoutBindingOffset &&
        deviceVk->getDescriptor && deviceVk->bindDescriptorBuffers &&
        deviceVk->setDescriptorBufferOffsets &&
        vk_hasHostVisibleDescriptorBufferMemory(adapterVk->physicalDevice,
                                                deviceVk->device)) {
      deviceVk->descriptorBufferProperties =
        adapterVk->descriptorBufferProperties;
      deviceVk->descriptorBuffer = true;
    }
  }
#endif
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u) {
    deviceVk->createAccelerationStructure =
      (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCreateAccelerationStructureKHR"
      );
    deviceVk->destroyAccelerationStructure =
      (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkDestroyAccelerationStructureKHR"
      );
    deviceVk->getAccelerationStructureBuildSizes =
      (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetAccelerationStructureBuildSizesKHR"
      );
    deviceVk->buildAccelerationStructures =
      (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCmdBuildAccelerationStructuresKHR"
      );
    deviceVk->getAccelerationStructureAddress =
      (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetAccelerationStructureDeviceAddressKHR"
      );
    if (!deviceVk->createAccelerationStructure ||
        !deviceVk->destroyAccelerationStructure ||
        !deviceVk->getAccelerationStructureBuildSizes ||
        !deviceVk->buildAccelerationStructures ||
        !deviceVk->getAccelerationStructureAddress) {
      goto err;
    }
    deviceVk->accelerationStructureScratchAlignment =
      adapterVk->accelerationStructureScratchAlignment;
    deviceVk->rayQuery = true;
  }
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) != 0u) {
    deviceVk->createRayTracingPipelines =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCreateRayTracingPipelinesKHR"
      );
    deviceVk->getRayTracingShaderGroupHandles =
      (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkGetRayTracingShaderGroupHandlesKHR"
      );
    deviceVk->traceRays =
      (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(
        deviceVk->device,
        "vkCmdTraceRaysKHR"
      );
    if (!deviceVk->createRayTracingPipelines ||
        !deviceVk->getRayTracingShaderGroupHandles ||
        !deviceVk->traceRays) {
      goto err;
    }
    deviceVk->rayTracingShaderGroupHandleSize =
      adapterVk->rayTracingShaderGroupHandleSize;
    deviceVk->rayTracingShaderGroupHandleAlignment =
      adapterVk->rayTracingShaderGroupHandleAlignment;
    deviceVk->rayTracingShaderGroupBaseAlignment =
      adapterVk->rayTracingShaderGroupBaseAlignment;
    deviceVk->rayTracingMaxRecursionDepth =
      adapterVk->rayTracingMaxRecursionDepth;
    deviceVk->rayTracingPipeline = true;
  }
#endif
#ifdef VK_KHR_fragment_shading_rate
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) != 0u) {
    if (adapterVk->vrsDrawRate) {
      deviceVk->setFragmentShadingRate =
        (PFN_vkCmdSetFragmentShadingRateKHR)vkGetDeviceProcAddr(
          deviceVk->device,
          "vkCmdSetFragmentShadingRateKHR"
        );
      if (!deviceVk->setFragmentShadingRate) {
        goto err;
      }
    }
    deviceVk->vrsRates               = adapterVk->vrsRates;
    deviceVk->vrsCombiners           = adapterVk->vrsCombiners;
    deviceVk->minVRSTexelSize        = adapterVk->minVRSTexelSize;
    deviceVk->maxVRSTexelSize        = adapterVk->maxVRSTexelSize;
    deviceVk->maxVRSTexelAspectRatio = adapterVk->maxVRSTexelAspectRatio;
    deviceVk->vrsDrawRate            = adapterVk->vrsDrawRate;
    deviceVk->vrsAttachment          = adapterVk->vrsAttachment;
  }
#endif
#ifdef VK_EXT_mesh_shader
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) != 0u) {
    deviceVk->drawMeshTasks  = (PFN_vkCmdDrawMeshTasksEXT)
      vkGetDeviceProcAddr(deviceVk->device, "vkCmdDrawMeshTasksEXT");
    if (!deviceVk->drawMeshTasks) {
      goto err;
    }
    deviceVk->meshShader     = true;
    deviceVk->taskShader     = adapterVk->taskShader;
  }
#endif
  if (adapterVk->synchronization2) {
    deviceVk->pipelineBarrier2 = (PFN_vkCmdPipelineBarrier2KHR)
      vkGetDeviceProcAddr(deviceVk->device, "vkCmdPipelineBarrier2");
    if (!deviceVk->pipelineBarrier2) {
      deviceVk->pipelineBarrier2 = (PFN_vkCmdPipelineBarrier2KHR)
        vkGetDeviceProcAddr(deviceVk->device, "vkCmdPipelineBarrier2KHR");
    }
    deviceVk->synchronization2 = deviceVk->pipelineBarrier2 != NULL;
  }
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
  apiDevice->getAvailableAdapters        = vk_getAvailableAdapters;
  apiDevice->selectAdapter               = vk_selectAdapter;
  apiDevice->destroyAdapter              = vk_destroyAdapter;
  apiDevice->getAdapterProperties        = vk_getAdapterProperties;
  apiDevice->supportsFeature             = vk_supportsFeature;
  apiDevice->supportsSubgroupOperations  = vk_supportsSubgroupOperations;
  apiDevice->getLimits                   = vk_getLimits;
  apiDevice->getFormatCapabilities       = vk_getFormatCapabilities;
#ifdef VK_KHR_cooperative_matrix
  apiDevice->getSubgroupMatrixProperties = vk_getSubgroupMatrixProperties;
#endif
  apiDevice->createDevice                = vk_createDevice;
  apiDevice->waitIdle                    = vk_waitDeviceIdle;
  apiDevice->destroyDevice               = vk_destroyDevice;
}
