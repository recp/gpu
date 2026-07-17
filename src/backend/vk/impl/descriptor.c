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
#include "../../../api/buffer_internal.h"
#include "../../../api/descr/descriptor_internal.h"
#include "../../../api/ray_internal.h"
#include "../../../api/sampler_internal.h"
#include "../../../api/texture_internal.h"

enum {
  GPU_VK_DESCRIPTOR_POOL_SET_COUNT        = 64u,
  GPU_VK_DESCRIPTOR_POOL_DESCRIPTOR_COUNT = 1024u,
  GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT     = 16u
};

typedef struct GPUDescriptorWriteVk {
  GPUDevice             *device;
  GPUBindGroupVk        *group;
  uint32_t                writeCount;
  bool                    valid;
  VkWriteDescriptorSet   writes[GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT];
  VkDescriptorBufferInfo bufferInfos[GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT];
  VkDescriptorImageInfo  imageInfos[GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT];
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  VkWriteDescriptorSetAccelerationStructureKHR
    accelerationInfos[GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT];
  VkAccelerationStructureKHR
    accelerationStructures[GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT];
#endif
} GPUDescriptorWriteVk;

#ifdef VK_EXT_descriptor_buffer
typedef struct GPUDescriptorBufferWriteVk {
  GPUDevice      *device;
  GPUBindGroupVk *group;
  VkDeviceSize    dirtyBegin;
  VkDeviceSize    dirtyEnd;
  bool            valid;
} GPUDescriptorBufferWriteVk;
#endif

static bool
vk__addPoolSize(VkDescriptorPoolSize *sizes,
                uint32_t             *count,
                VkDescriptorType      type,
                uint32_t              descriptorCount);

static bool
vk__descriptorType(GPUBindingType type,
                   bool dynamic,
                   VkDescriptorType *outType) {
  if (!outType) {
    return false;
  }

  switch (type) {
    case GPU_BINDING_UNIFORM_BUFFER:
      *outType = dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                         : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      return true;
    case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER:
      *outType = dynamic ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                         : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      return true;
    case GPU_BINDING_SAMPLED_TEXTURE:
      if (!dynamic) {
        *outType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        return true;
      }
      break;
    case GPU_BINDING_STORAGE_TEXTURE:
      if (!dynamic) {
        *outType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        return true;
      }
      break;
    case GPU_BINDING_SAMPLER:
      if (!dynamic) {
        *outType = VK_DESCRIPTOR_TYPE_SAMPLER;
        return true;
      }
      break;
    case GPU_BINDING_ACCELERATION_STRUCTURE:
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
      if (!dynamic) {
        *outType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        return true;
      }
#endif
      break;
    default:
      break;
  }

  return false;
}

#ifdef VK_EXT_descriptor_buffer
static VkDeviceSize
vk__descriptorByteSize(const GPUDeviceVk *device,
                       VkDescriptorType   type) {
  const VkPhysicalDeviceDescriptorBufferPropertiesEXT *properties;

  if (!device) {
    return 0u;
  }
  properties = &device->descriptorBufferProperties;
  switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
      return properties->samplerDescriptorSize;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return properties->sampledImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return properties->storageImageDescriptorSize;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      return properties->uniformBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      return properties->storageBufferDescriptorSize;
#ifdef VK_KHR_acceleration_structure
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      return properties->accelerationStructureDescriptorSize;
#endif
    default:
      return 0u;
  }
}

static VkDeviceSize
vk__alignDescriptorSize(VkDeviceSize value, VkDeviceSize alignment) {
  VkDeviceSize remainder;

  if (alignment == 0u) {
    return 0u;
  }
  remainder = value % alignment;
  if (remainder == 0u) {
    return value;
  }
  if (value > UINT64_MAX - (alignment - remainder)) {
    return 0u;
  }
  return value + alignment - remainder;
}

static void
vk__destroyDescriptorChunk(VkDevice device,
                           GPUDescriptorBufferChunkVk *chunk) {
  if (!chunk) {
    return;
  }
  if (device && chunk->memory && chunk->mapped) {
    vkUnmapMemory(device, chunk->memory);
  }
  if (device && chunk->buffer) {
    vkDestroyBuffer(device, chunk->buffer, NULL);
  }
  if (device && chunk->memory) {
    vkFreeMemory(device, chunk->memory, NULL);
  }
  free(chunk);
}
#endif

static VkShaderStageFlags
vk__descriptorStages(GPUShaderStageFlags visibility) {
  VkShaderStageFlags stages;

  stages = 0u;
  if ((visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if ((visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  if ((visibility & GPU_SHADER_STAGE_COMPUTE_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_COMPUTE_BIT;
  }
#ifdef VK_EXT_mesh_shader
  if ((visibility & GPU_SHADER_STAGE_TASK_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_TASK_BIT_EXT;
  }
  if ((visibility & GPU_SHADER_STAGE_MESH_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_MESH_BIT_EXT;
  }
#endif
#ifdef VK_KHR_ray_tracing_pipeline
  if ((visibility & GPU_SHADER_STAGE_RAY_GENERATION_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
  }
  if ((visibility & GPU_SHADER_STAGE_MISS_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_MISS_BIT_KHR;
  }
  if ((visibility & GPU_SHADER_STAGE_CLOSEST_HIT_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
  }
  if ((visibility & GPU_SHADER_STAGE_ANY_HIT_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
  }
  if ((visibility & GPU_SHADER_STAGE_INTERSECTION_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
  }
  if ((visibility & GPU_SHADER_STAGE_CALLABLE_BIT) != 0u) {
    stages |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
  }
#endif
  return stages;
}

static void
vk__descriptorPoolLock(GPUBindGroupLayoutVk *layout) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&layout->poolLock);
#else
  pthread_mutex_lock(&layout->poolLock);
#endif
}

static void
vk__descriptorPoolUnlock(GPUBindGroupLayoutVk *layout) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&layout->poolLock);
#else
  pthread_mutex_unlock(&layout->poolLock);
#endif
}

static void
vk__destroyBindGroupLayoutState(GPUBindGroupLayoutVk *native) {
  GPUDescriptorPoolVk *pool;

  if (!native) {
    return;
  }

  pool = native->descriptorPools;
  while (pool) {
    GPUDescriptorPoolVk *next;

    next = pool->next;
    if (native->device && pool->pool) {
      vkDestroyDescriptorPool(native->device, pool->pool, NULL);
    }
    free(pool);
    pool = next;
  }
#ifdef VK_EXT_descriptor_buffer
  while (native->descriptorChunks) {
    GPUDescriptorBufferChunkVk *chunk;

    chunk = native->descriptorChunks;
    native->descriptorChunks = chunk->next;
    vk__destroyDescriptorChunk(native->device, chunk);
  }
  if (native->device && native->descriptorLayout) {
    vkDestroyDescriptorSetLayout(native->device,
                                 native->descriptorLayout,
                                 NULL);
  }
#endif
  if (native->device && native->layout) {
    vkDestroyDescriptorSetLayout(native->device, native->layout, NULL);
  }
  if (native->device && native->immutableSamplers) {
    for (uint32_t i = 0u; i < native->immutableSamplerCount; i++) {
      if (native->immutableSamplers[i]) {
        vkDestroySampler(native->device, native->immutableSamplers[i], NULL);
      }
    }
  }
  free(native->dynamicOrder);
  free(native->immutableSamplers);
#ifdef VK_EXT_descriptor_buffer
  free(native->descriptorBindings);
#endif
#if defined(_WIN32) || defined(WIN32)
  if (native->poolLockInitialized) {
    DeleteCriticalSection(&native->poolLock);
  }
#else
  if (native->poolLockInitialized) {
    pthread_mutex_destroy(&native->poolLock);
  }
#endif
  free(native);
}

GPU_HIDE
GPUResult
vk_createBindGroupLayout(GPUDevice          *device,
                         GPUBindGroupLayout *layout) {
  typedef struct VkDynamicBindingOrder {
    uint32_t backendBinding;
    uint32_t arrayIndex;
    uint32_t callerIndex;
  } VkDynamicBindingOrder;

  GPUBindGroupLayoutVk              *native;
  GPUDeviceVk                       *deviceVk;
  const GPUBindGroupLayoutEntry     *entries;
  const uint32_t                    *backendBindings;
  VkDescriptorSetLayoutBinding      *bindings;
  VkDescriptorBindingFlags          *bindingFlags;
  VkDescriptorSetLayoutCreateInfo    info = {0};
  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {0};
  VkDynamicBindingOrder              dynamicBindings[GPU_VK_MAX_DYNAMIC_OFFSETS];
  uint32_t                           backendBindingCount;
  uint32_t                           entryCount;
  uint32_t                           immutableSamplerCount;
  uint64_t                           poolDescriptorCount;
  bool                               bindless;
  bool                               descriptorBuffer;

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceVk = device->_priv;
  entries = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  bindless = gpuBindGroupLayoutIsBindless(layout);
  backendBindings = gpuGetBindGroupLayoutBackendBindings(
    layout,
    &backendBindingCount
  );
  if (entryCount != backendBindingCount ||
      (entryCount > 0u && (!entries || !backendBindings))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  immutableSamplerCount = 0u;
  descriptorBuffer = deviceVk->descriptorBuffer && !bindless && entryCount > 0u;
  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].immutableSampler) {
      if (entries[i].arrayCount > UINT32_MAX - immutableSamplerCount) {
        return GPU_ERROR_UNSUPPORTED;
      }
      immutableSamplerCount += entries[i].arrayCount;
    }
    if (entries[i].immutableSampler || entries[i].hasDynamicOffset) {
      descriptorBuffer = false;
    }
  }

  native   = calloc(1, sizeof(*native));
  bindings = entryCount ? calloc(entryCount, sizeof(*bindings)) : NULL;
  if (!native || (entryCount > 0u && !bindings)) {
    free(bindings);
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->gpuDevice       = deviceVk;
  native->device          = deviceVk->device;
  native->descriptorBuffer = descriptorBuffer;
  native->poolSetCapacity = GPU_VK_DESCRIPTOR_POOL_SET_COUNT;
#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&native->poolLock);
  native->poolLockInitialized = true;
#else
  if (pthread_mutex_init(&native->poolLock, NULL) != 0) {
    free(bindings);
    vk__destroyBindGroupLayoutState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->poolLockInitialized = true;
#endif
  if (immutableSamplerCount > 0u) {
    native->immutableSamplers = calloc(immutableSamplerCount,
                                       sizeof(*native->immutableSamplers));
    if (!native->immutableSamplers) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }
  for (uint32_t i = 0u; i < entryCount; i++) {
    VkDescriptorType type;
    VkShaderStageFlags stages;

    stages = vk__descriptorStages(entries[i].visibility);
    if (entries[i].arrayCount == 0u || stages == 0u ||
        !vk__descriptorType(entries[i].bindingType,
                            entries[i].hasDynamicOffset,
                            &type) ||
        !vk__addPoolSize(native->poolSizes,
                         &native->poolSizeCount,
                         type,
                         entries[i].arrayCount)) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    bindings[i].binding         = backendBindings[i];
    bindings[i].descriptorType  = type;
    bindings[i].descriptorCount = entries[i].arrayCount;
    bindings[i].stageFlags      = stages;

    if (entries[i].immutableSampler) {
      VkSamplerCreateInfo samplerInfo;
      VkSampler          *samplers;

      if (entries[i].bindingType != GPU_BINDING_SAMPLER ||
          entries[i].hasDynamicOffset) {
        free(bindings);
        vk__destroyBindGroupLayoutState(native);
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      samplers = &native->immutableSamplers[native->immutableSamplerCount];
      vk_fillSamplerInfo(&entries[i].immutableSamplerDesc, &samplerInfo);
      for (uint32_t arrayIndex = 0u;
           arrayIndex < entries[i].arrayCount;
           arrayIndex++) {
        if (vkCreateSampler(native->device,
                            &samplerInfo,
                            NULL,
                            &samplers[arrayIndex]) != VK_SUCCESS) {
          free(bindings);
          vk__destroyBindGroupLayoutState(native);
          return GPU_ERROR_BACKEND_FAILURE;
        }
        native->immutableSamplerCount++;
      }
      bindings[i].pImmutableSamplers = samplers;
    }

    if (entries[i].hasDynamicOffset) {
      uint32_t callerBase;

      if (entries[i].arrayCount >
          GPU_VK_MAX_DYNAMIC_OFFSETS - native->dynamicCount) {
        free(bindings);
        vk__destroyBindGroupLayoutState(native);
        return GPU_ERROR_UNSUPPORTED;
      }

      callerBase = 0u;
      for (uint32_t j = 0u; j < entryCount; j++) {
        if (entries[j].hasDynamicOffset &&
            entries[j].binding < entries[i].binding) {
          callerBase += entries[j].arrayCount;
        }
      }
      for (uint32_t arrayIndex = 0u;
           arrayIndex < entries[i].arrayCount;
           arrayIndex++) {
        VkDynamicBindingOrder order;
        uint32_t              position;

        order.backendBinding = backendBindings[i];
        order.arrayIndex     = arrayIndex;
        order.callerIndex    = callerBase + arrayIndex;
        position             = native->dynamicCount;
        while (position > 0u &&
               (dynamicBindings[position - 1u].backendBinding >
                  order.backendBinding ||
                (dynamicBindings[position - 1u].backendBinding ==
                   order.backendBinding &&
                 dynamicBindings[position - 1u].arrayIndex >
                   order.arrayIndex))) {
          dynamicBindings[position] = dynamicBindings[position - 1u];
          position--;
        }
        dynamicBindings[position] = order;
        native->dynamicCount++;
      }
    }
  }

  poolDescriptorCount = 0u;
  for (uint32_t i = 0u; i < native->poolSizeCount; i++) {
    uint32_t capacity;

    poolDescriptorCount += native->poolSizes[i].descriptorCount;
    capacity = UINT32_MAX / native->poolSizes[i].descriptorCount;
    if (capacity < native->poolSetCapacity) {
      native->poolSetCapacity = capacity;
    }
  }
  if (poolDescriptorCount > 0u) {
    uint32_t capacity;

    capacity = poolDescriptorCount > GPU_VK_DESCRIPTOR_POOL_DESCRIPTOR_COUNT
                 ? 1u
                 : (uint32_t)(GPU_VK_DESCRIPTOR_POOL_DESCRIPTOR_COUNT /
                              poolDescriptorCount);
    if (capacity < native->poolSetCapacity) {
      native->poolSetCapacity = capacity;
    }
  }
  if (native->poolSetCapacity == 0u) {
    free(bindings);
    vk__destroyBindGroupLayoutState(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  if (native->dynamicCount > 0u) {
    native->dynamicOrder = calloc(native->dynamicCount,
                                  sizeof(*native->dynamicOrder));
    if (!native->dynamicOrder) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0u; i < native->dynamicCount; i++) {
      native->dynamicOrder[i] = dynamicBindings[i].callerIndex;
    }
  }

  bindingFlags = NULL;
  if (bindless && entryCount > 0u) {
    bindingFlags = calloc(entryCount, sizeof(*bindingFlags));
    if (!bindingFlags) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0u; i < entryCount; i++) {
      if (!entries[i].immutableSampler) {
        bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
      }
    }
    bindingFlagsInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount  = entryCount;
    bindingFlagsInfo.pBindingFlags = bindingFlags;
  }

  info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.pNext        = bindless ? &bindingFlagsInfo : NULL;
  info.bindingCount = entryCount;
  info.pBindings    = bindings;
  if (vkCreateDescriptorSetLayout(native->device,
                                  &info,
                                  NULL,
                                  &native->layout) != VK_SUCCESS) {
    free(bindingFlags);
    free(bindings);
    vk__destroyBindGroupLayoutState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

#ifdef VK_EXT_descriptor_buffer
  if (native->descriptorBuffer) {
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    if (vkCreateDescriptorSetLayout(native->device,
                                    &info,
                                    NULL,
                                    &native->descriptorLayout) != VK_SUCCESS) {
      native->descriptorBuffer = false;
    }
  }
  if (native->descriptorBuffer) {
    const VkPhysicalDeviceDescriptorBufferPropertiesEXT *properties;
    VkDeviceSize                                         slotCapacity;
    VkDeviceSize                                         maxRange;

    properties = &deviceVk->descriptorBufferProperties;
    deviceVk->getDescriptorSetLayoutSize(native->device,
                                         native->descriptorLayout,
                                         &native->descriptorSize);
    native->descriptorStride = vk__alignDescriptorSize(
      native->descriptorSize,
      properties->descriptorBufferOffsetAlignment
    );
    maxRange = properties->maxResourceDescriptorBufferRange;
    if (properties->maxSamplerDescriptorBufferRange < maxRange) {
      maxRange = properties->maxSamplerDescriptorBufferRange;
    }
    if (native->descriptorSize == 0u || native->descriptorStride == 0u ||
        native->descriptorStride > maxRange) {
      free(bindingFlags);
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    slotCapacity = maxRange / native->descriptorStride;
    if (slotCapacity > GPU_VK_DESCRIPTOR_BUFFER_SLOT_COUNT) {
      slotCapacity = GPU_VK_DESCRIPTOR_BUFFER_SLOT_COUNT;
    }
    native->descriptorSlotCapacity = (uint32_t)slotCapacity;
    native->descriptorBindings = calloc(entryCount,
                                         sizeof(*native->descriptorBindings));
    if (native->descriptorSlotCapacity == 0u ||
        !native->descriptorBindings) {
      free(bindingFlags);
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }
    native->descriptorBindingCount = entryCount;
    native->nonCoherentAtomSize = device->adapter && device->adapter->_priv
      ? ((GPUAdapterVk *)device->adapter->_priv)->props.limits.nonCoherentAtomSize
      : 1u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      GPUDescriptorBindingVk *binding;

      binding          = &native->descriptorBindings[i];
      binding->binding = bindings[i].binding;
      binding->type    = bindings[i].descriptorType;
      binding->size    = vk__descriptorByteSize(deviceVk, binding->type);
      binding->count   = bindings[i].descriptorCount;
      deviceVk->getDescriptorSetLayoutBindingOffset(native->device,
                                                    native->descriptorLayout,
                                                    binding->binding,
                                                    &binding->offset);
      if (binding->size == 0u || binding->offset > native->descriptorSize ||
          bindings[i].descriptorCount >
            (native->descriptorSize - binding->offset) / binding->size) {
        free(bindingFlags);
        free(bindings);
        vk__destroyBindGroupLayoutState(native);
        return GPU_ERROR_UNSUPPORTED;
      }
    }
  }
#endif

  free(bindingFlags);
  free(bindings);
  layout->_native = native;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyBindGroupLayout(GPUBindGroupLayout *layout) {
  if (!layout) {
    return;
  }

  vk__destroyBindGroupLayoutState(layout->_native);
  layout->_native = NULL;
}

GPU_HIDE
GPUResult
vk_createPipelineLayout(GPUDevice         *device,
                        GPUPipelineLayout *layout) {
  GPUPipelineLayoutVk         *native;
  GPUBindGroupLayout * const  *groups;
  VkDescriptorSetLayout        setLayouts[GPU_ENCODER_MAX_BIND_GROUPS];
  VkPushConstantRange          pushRange = {0};
  VkPipelineLayoutCreateInfo   info = {0};
  GPUShaderStageFlags          pushStages;
  uint32_t                     groupCount;
  uint32_t                     pushSize;
  bool                         descriptorBuffer;

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  descriptorBuffer = groupCount > 0u;
  if (groupCount > GPU_ENCODER_MAX_BIND_GROUPS ||
      (groupCount > 0u && !groups)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0u; i < groupCount; i++) {
    GPUBindGroupLayoutVk *group;

    group = groups[i] ? groups[i]->_native : NULL;
    if (!group || !group->layout) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    setLayouts[i] = group->layout;
#ifdef VK_EXT_descriptor_buffer
    descriptorBuffer = descriptorBuffer && group->descriptorBuffer &&
                       group->descriptorLayout;
#else
    descriptorBuffer = false;
#endif
  }
#ifdef VK_EXT_descriptor_buffer
  if (descriptorBuffer) {
    for (uint32_t i = 0u; i < groupCount; i++) {
      GPUBindGroupLayoutVk *group;

      group         = groups[i]->_native;
      setLayouts[i] = group->descriptorLayout;
    }
  }
#endif

  gpuGetPipelineLayoutPushConstants(layout, &pushSize, &pushStages);
  if (pushSize > 0u) {
    pushRange.stageFlags = vk__descriptorStages(pushStages);
    pushRange.size       = pushSize;
    if (pushRange.stageFlags == 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->device                  = ((GPUDeviceVk *)device->_priv)->device;
  native->descriptorBuffer        = descriptorBuffer;
  info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.setLayoutCount             = groupCount;
  info.pSetLayouts                = groupCount > 0u ? setLayouts : NULL;
  info.pushConstantRangeCount     = pushSize > 0u ? 1u : 0u;
  info.pPushConstantRanges        = pushSize > 0u ? &pushRange : NULL;
  if (vkCreatePipelineLayout(native->device,
                             &info,
                             NULL,
                             &native->layout) != VK_SUCCESS) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  layout->_native = native;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroyPipelineLayout(GPUPipelineLayout *layout) {
  GPUPipelineLayoutVk *native;

  native = layout ? layout->_native : NULL;
  if (!native) {
    return;
  }

  if (native->device && native->layout) {
    vkDestroyPipelineLayout(native->device, native->layout, NULL);
  }
  free(native);
  layout->_native = NULL;
}

GPU_HIDE
void
vk_destroyShaderLayout(GPUShaderLayoutVk *layout) {
  if (!layout) {
    return;
  }

  if (layout->device && layout->ownsLayout && layout->layout) {
    vkDestroyPipelineLayout(layout->device, layout->layout, NULL);
  }
  if (layout->device && layout->samplerPool) {
    vkDestroyDescriptorPool(layout->device, layout->samplerPool, NULL);
  }
  if (layout->device && layout->samplerLayout) {
    vkDestroyDescriptorSetLayout(layout->device,
                                 layout->samplerLayout,
                                 NULL);
  }
  if (layout->device && layout->emptyLayout) {
    vkDestroyDescriptorSetLayout(layout->device, layout->emptyLayout, NULL);
  }
  if (layout->device && layout->samplers) {
    for (uint32_t i = 0u; i < layout->samplerCount; i++) {
      if (layout->samplers[i]) {
        vkDestroySampler(layout->device, layout->samplers[i], NULL);
      }
    }
  }
  free(layout->samplers);
  memset(layout, 0, sizeof(*layout));
}

GPU_HIDE
GPUResult
vk_createShaderLayout(GPUDevice              *device,
                      GPUPipelineLayout       *layout,
                      const GPUShaderLibrary *library,
                      GPUShaderLayoutVk       *outLayout) {
  const GPUShaderStaticSamplerInfo *samplers;
  GPUBindGroupLayout * const        *groups;
  GPUPipelineLayoutVk               *base;
  VkDescriptorSetLayoutBinding      *bindings;
  VkDescriptorSetLayout              setLayouts[GPU_ENCODER_MAX_BIND_GROUPS + 1u];
  VkDescriptorSetLayoutCreateInfo    setInfo = {0};
  VkDescriptorSetAllocateInfo        allocationInfo = {0};
  VkDescriptorPoolSize               poolSize = {0};
  VkDescriptorPoolCreateInfo         poolInfo = {0};
  VkPipelineLayoutCreateInfo         pipelineInfo = {0};
  VkPushConstantRange                pushRange = {0};
  GPUShaderStageFlags                pushStages;
  uint32_t                           groupCount;
  uint32_t                           pushSize;
  uint32_t                           samplerCount;
  uint32_t                           samplerGroup;

  if (!device || !device->_priv || !layout || !library || !outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outLayout, 0, sizeof(*outLayout));

  base     = layout->_native;
  samplers = gpuGetShaderLibraryStaticSamplers(library, &samplerCount);
  if (!base || !base->layout ||
      base->device != ((GPUDeviceVk *)device->_priv)->device ||
      (samplerCount > 0u && !samplers)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outLayout->baseLayout = base;
  outLayout->device     = base->device;
  if (samplerCount == 0u) {
    outLayout->layout           = base->layout;
    outLayout->descriptorBuffer = base->descriptorBuffer;
    return GPU_OK;
  }

  samplerGroup = samplers[0].spirvGroup;
  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  if (samplerGroup < groupCount ||
      samplerGroup > GPU_ENCODER_MAX_BIND_GROUPS ||
      (groupCount > 0u && !groups)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  for (uint32_t i = 0u; i < samplerCount; i++) {
    if (samplers[i].spirvGroup != samplerGroup ||
        samplers[i].spirvBinding == UINT32_MAX) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  bindings = calloc(samplerCount, sizeof(*bindings));
  outLayout->samplers = calloc(samplerCount, sizeof(*outLayout->samplers));
  if (!bindings || !outLayout->samplers) {
    free(bindings);
    vk_destroyShaderLayout(outLayout);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  outLayout->samplerCount = samplerCount;
  outLayout->samplerGroup = samplerGroup;

  for (uint32_t i = 0u; i < samplerCount; i++) {
    VkSamplerCreateInfo samplerInfo;

    vk_fillStaticSamplerInfo(&samplers[i].desc, &samplerInfo);
    if (vkCreateSampler(outLayout->device,
                        &samplerInfo,
                        NULL,
                        &outLayout->samplers[i]) != VK_SUCCESS) {
      free(bindings);
      vk_destroyShaderLayout(outLayout);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    bindings[i].binding            = samplers[i].spirvBinding;
    bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[i].descriptorCount    = 1u;
    bindings[i].stageFlags         = vk__descriptorStages(
      samplers[i].visibility
    );
    bindings[i].pImmutableSamplers = &outLayout->samplers[i];
    if (bindings[i].stageFlags == 0u) {
      free(bindings);
      vk_destroyShaderLayout(outLayout);
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  setInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setInfo.bindingCount = samplerCount;
  setInfo.pBindings    = bindings;
  if (vkCreateDescriptorSetLayout(outLayout->device,
                                  &setInfo,
                                  NULL,
                                  &outLayout->samplerLayout) != VK_SUCCESS) {
    free(bindings);
    vk_destroyShaderLayout(outLayout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  free(bindings);

  for (uint32_t i = 0u; i < groupCount; i++) {
    GPUBindGroupLayoutVk *group;

    group = groups[i] ? groups[i]->_native : NULL;
    if (!group || !group->layout || group->device != outLayout->device) {
      vk_destroyShaderLayout(outLayout);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    setLayouts[i] = group->layout;
  }
  if (samplerGroup > groupCount) {
    VkDescriptorSetLayoutCreateInfo emptyInfo = {0};

    emptyInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    if (vkCreateDescriptorSetLayout(outLayout->device,
                                    &emptyInfo,
                                    NULL,
                                    &outLayout->emptyLayout) != VK_SUCCESS) {
      vk_destroyShaderLayout(outLayout);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    for (uint32_t i = groupCount; i < samplerGroup; i++) {
      setLayouts[i] = outLayout->emptyLayout;
    }
  }
  setLayouts[samplerGroup] = outLayout->samplerLayout;

  gpuGetPipelineLayoutPushConstants(layout, &pushSize, &pushStages);
  if (pushSize > 0u) {
    pushRange.stageFlags = vk__descriptorStages(pushStages);
    pushRange.size       = pushSize;
  }
  pipelineInfo.sType                  =
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.setLayoutCount         = samplerGroup + 1u;
  pipelineInfo.pSetLayouts            = setLayouts;
  pipelineInfo.pushConstantRangeCount = pushSize > 0u ? 1u : 0u;
  pipelineInfo.pPushConstantRanges    = pushSize > 0u ? &pushRange : NULL;
  if (vkCreatePipelineLayout(outLayout->device,
                             &pipelineInfo,
                             NULL,
                             &outLayout->layout) != VK_SUCCESS) {
    vk_destroyShaderLayout(outLayout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  outLayout->ownsLayout = true;

  poolSize.type            = VK_DESCRIPTOR_TYPE_SAMPLER;
  poolSize.descriptorCount = samplerCount;
  poolInfo.sType           = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets         = 1u;
  poolInfo.poolSizeCount   = 1u;
  poolInfo.pPoolSizes      = &poolSize;
  if (vkCreateDescriptorPool(outLayout->device,
                             &poolInfo,
                             NULL,
                             &outLayout->samplerPool) != VK_SUCCESS) {
    vk_destroyShaderLayout(outLayout);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  allocationInfo.sType              =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocationInfo.descriptorPool     = outLayout->samplerPool;
  allocationInfo.descriptorSetCount = 1u;
  allocationInfo.pSetLayouts        = &outLayout->samplerLayout;
  if (vkAllocateDescriptorSets(outLayout->device,
                               &allocationInfo,
                               &outLayout->samplerSet) != VK_SUCCESS) {
    vk_destroyShaderLayout(outLayout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

GPU_HIDE
void
vk_bindShaderSamplers(VkCommandBuffer          command,
                      VkPipelineBindPoint      bindPoint,
                      const GPUShaderLayoutVk *layout) {
  if (!command || !layout || !layout->samplerSet) {
    return;
  }

  vkCmdBindDescriptorSets(command,
                          bindPoint,
                          layout->layout,
                          layout->samplerGroup,
                          1u,
                          &layout->samplerSet,
                          0u,
                          NULL);
}

static bool
vk__addPoolSize(VkDescriptorPoolSize *sizes,
                uint32_t             *count,
                VkDescriptorType      type,
                uint32_t              descriptorCount) {
  if (!sizes || !count || descriptorCount == 0u) {
    return false;
  }

  for (uint32_t i = 0u; i < *count; i++) {
    if (sizes[i].type == type) {
      if (descriptorCount > UINT32_MAX - sizes[i].descriptorCount) {
        return false;
      }
      sizes[i].descriptorCount += descriptorCount;
      return true;
    }
  }

  if (*count >= GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT) {
    return false;
  }
  sizes[*count].type            = type;
  sizes[*count].descriptorCount = descriptorCount;
  (*count)++;
  return true;
}

static GPUResult
vk__descriptorResult(VkResult result) {
  if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
      result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  return GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
vk__growDescriptorPools(GPUBindGroupLayoutVk *layout,
                        GPUDescriptorPoolVk **outPool) {
  GPUDescriptorPoolVk       *pool;
  VkDescriptorPoolSize       sizes[GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT];
  VkDescriptorPoolCreateInfo info = {0};
  VkResult                   result;
  uint32_t                   capacity;

  if (!layout || !layout->device || !outPool) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPool = NULL;
  pool     = calloc(1, sizeof(*pool));
  if (!pool) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  capacity = layout->poolSetCapacity;
  for (;;) {
    for (uint32_t i = 0u; i < layout->poolSizeCount; i++) {
      sizes[i]                 = layout->poolSizes[i];
      sizes[i].descriptorCount *= capacity;
    }

    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = capacity;
    info.poolSizeCount = layout->poolSizeCount;
    info.pPoolSizes    = layout->poolSizeCount > 0u ? sizes : NULL;
    result = vkCreateDescriptorPool(layout->device,
                                    &info,
                                    NULL,
                                    &pool->pool);
    if (result == VK_SUCCESS) {
      break;
    }
    if (capacity == 1u) {
      free(pool);
      return vk__descriptorResult(result);
    }
    capacity /= 2u;
  }

  layout->poolSetCapacity = capacity;
  pool->next              = layout->descriptorPools;
  layout->descriptorPools = pool;
  *outPool                = pool;
  return GPU_OK;
}

static GPUResult
vk__allocateDescriptorSet(GPUBindGroupLayoutVk *layout,
                          GPUDescriptorPoolVk **outPool,
                          VkDescriptorSet      *outSet) {
  GPUDescriptorPoolVk         *pool;
  VkDescriptorSetAllocateInfo allocationInfo = {0};
  VkResult                     result;
  GPUResult                    gpuResult;

  if (!layout || !layout->layout || !outPool || !outSet) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPool = NULL;
  *outSet  = VK_NULL_HANDLE;
  allocationInfo.sType              =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocationInfo.descriptorSetCount = 1u;
  allocationInfo.pSetLayouts        = &layout->layout;

  vk__descriptorPoolLock(layout);
  for (pool = layout->descriptorPools; pool; pool = pool->next) {
    allocationInfo.descriptorPool = pool->pool;
    result = vkAllocateDescriptorSets(layout->device,
                                      &allocationInfo,
                                      outSet);
    if (result == VK_SUCCESS) {
      *outPool = pool;
      vk__descriptorPoolUnlock(layout);
      return GPU_OK;
    }
    if (result != VK_ERROR_OUT_OF_POOL_MEMORY &&
        result != VK_ERROR_FRAGMENTED_POOL) {
      vk__descriptorPoolUnlock(layout);
      return vk__descriptorResult(result);
    }
  }

  gpuResult = vk__growDescriptorPools(layout, &pool);
  if (gpuResult == GPU_OK) {
    allocationInfo.descriptorPool = pool->pool;
    result = vkAllocateDescriptorSets(layout->device,
                                      &allocationInfo,
                                      outSet);
    if (result == VK_SUCCESS) {
      *outPool = pool;
    } else {
      layout->descriptorPools = pool->next;
      vkDestroyDescriptorPool(layout->device, pool->pool, NULL);
      free(pool);
      gpuResult = vk__descriptorResult(result);
    }
  }
  vk__descriptorPoolUnlock(layout);
  return gpuResult;
}

static void
vk__freeDescriptorSet(GPUBindGroupVk *group) {
  GPUBindGroupLayoutVk *layout;

  layout = group ? group->layout : NULL;
  if (!layout || !group->pool || !group->set) {
    return;
  }

  vk__descriptorPoolLock(layout);
  (void)vkFreeDescriptorSets(layout->device,
                             group->pool->pool,
                             1u,
                             &group->set);
  vk__descriptorPoolUnlock(layout);
  group->set  = VK_NULL_HANDLE;
  group->pool = NULL;
}

#ifdef VK_EXT_descriptor_buffer
static GPUResult
vk__createDescriptorChunk(GPUDevice                    *device,
                          GPUBindGroupLayoutVk          *layout,
                          GPUDescriptorBufferChunkVk  **outChunk) {
  GPUDescriptorBufferChunkVk *chunk;
  VkBufferCreateInfo           bufferInfo = {0};
  VkMemoryAllocateFlagsInfo    allocationFlags = {0};
  VkMemoryAllocateInfo         allocationInfo = {0};
  VkBufferDeviceAddressInfo    addressInfo = {0};
  VkMemoryRequirements         requirements;
  VkMemoryPropertyFlags        memoryFlags;
  uint32_t                     memoryTypeIndex;

  if (!device || !layout || !layout->gpuDevice || !outChunk ||
      layout->descriptorStride == 0u ||
      layout->descriptorSlotCapacity == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outChunk = NULL;
  chunk = calloc(1, sizeof(*chunk));
  if (!chunk) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size  = layout->descriptorStride *
                     layout->descriptorSlotCapacity;
  bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                     VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(layout->device,
                     &bufferInfo,
                     NULL,
                     &chunk->buffer) != VK_SUCCESS) {
    vk__destroyDescriptorChunk(layout->device, chunk);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  vkGetBufferMemoryRequirements(layout->device,
                                chunk->buffer,
                                &requirements);
  if (!vk_findMemoryType(device,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &memoryTypeIndex,
                         &memoryFlags)) {
    vk__destroyDescriptorChunk(layout->device, chunk);
    return GPU_ERROR_UNSUPPORTED;
  }

  allocationFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  allocationFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  allocationInfo.sType  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.pNext  = &allocationFlags;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryTypeIndex;
  if (vkAllocateMemory(layout->device,
                       &allocationInfo,
                       NULL,
                       &chunk->memory) != VK_SUCCESS ||
      vkBindBufferMemory(layout->device,
                         chunk->buffer,
                         chunk->memory,
                         0u) != VK_SUCCESS ||
      vkMapMemory(layout->device,
                  chunk->memory,
                  0u,
                  VK_WHOLE_SIZE,
                  0u,
                  &chunk->mapped) != VK_SUCCESS) {
    vk__destroyDescriptorChunk(layout->device, chunk);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
  addressInfo.buffer = chunk->buffer;
  chunk->address = layout->gpuDevice->getBufferDeviceAddress(
    layout->device,
    &addressInfo
  );
  if (chunk->address == 0u) {
    vk__destroyDescriptorChunk(layout->device, chunk);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  chunk->allocationSize = requirements.size;
  chunk->coherent =
    (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
  *outChunk = chunk;
  return GPU_OK;
}

static GPUResult
vk__allocateDescriptorSlot(GPUDevice            *device,
                           GPUBindGroupLayoutVk  *layout,
                           GPUBindGroupVk        *group) {
  GPUDescriptorBufferChunkVk *chunk;
  GPUResult                    result;
  uint32_t                     slot;

  if (!device || !layout || !group || !layout->descriptorBuffer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  vk__descriptorPoolLock(layout);
  chunk = layout->descriptorChunks;
  while (chunk) {
    if (chunk->usedSlots !=
        (layout->descriptorSlotCapacity == 64u
           ? UINT64_MAX
           : ((1ull << layout->descriptorSlotCapacity) - 1ull))) {
      break;
    }
    chunk = chunk->next;
  }
  if (!chunk) {
    result = vk__createDescriptorChunk(device, layout, &chunk);
    if (result != GPU_OK) {
      vk__descriptorPoolUnlock(layout);
      return result;
    }
    chunk->next              = layout->descriptorChunks;
    layout->descriptorChunks = chunk;
  }

  slot = 0u;
  while ((chunk->usedSlots & (1ull << slot)) != 0u) {
    slot++;
  }
  chunk->usedSlots |= 1ull << slot;
  vk__descriptorPoolUnlock(layout);

  group->descriptorChunk  = chunk;
  group->descriptorSlot   = slot;
  group->descriptorOffset = layout->descriptorStride * slot;
  memset((uint8_t *)chunk->mapped + group->descriptorOffset,
         0,
         layout->descriptorSize);
  return GPU_OK;
}

static void
vk__freeDescriptorSlot(GPUBindGroupVk *group) {
  GPUBindGroupLayoutVk *layout;

  layout = group ? group->layout : NULL;
  if (!layout || !group->descriptorChunk ||
      group->descriptorSlot >= layout->descriptorSlotCapacity) {
    return;
  }
  vk__descriptorPoolLock(layout);
  group->descriptorChunk->usedSlots &= ~(1ull << group->descriptorSlot);
  vk__descriptorPoolUnlock(layout);
  group->descriptorChunk  = NULL;
  group->descriptorOffset = 0u;
  group->descriptorSlot   = 0u;
}

static const GPUDescriptorBindingVk *
vk__findDescriptorBinding(const GPUBindGroupLayoutVk *layout,
                          uint32_t                    binding,
                          VkDescriptorType            type) {
  if (!layout) {
    return NULL;
  }
  for (uint32_t i = 0u; i < layout->descriptorBindingCount; i++) {
    const GPUDescriptorBindingVk *candidate;

    candidate = &layout->descriptorBindings[i];
    if (candidate->binding == binding && candidate->type == type) {
      return candidate;
    }
  }
  return NULL;
}

static void
vk__flushDescriptorBuffer(GPUDescriptorBufferWriteVk *context) {
  GPUBindGroupVk              *group;
  GPUBindGroupLayoutVk        *layout;
  GPUDescriptorBufferChunkVk  *chunk;
  VkMappedMemoryRange          range = {0};
  VkDeviceSize                 begin;
  VkDeviceSize                 end;
  VkDeviceSize                 atom;

  if (!context || !context->valid ||
      context->dirtyBegin == UINT64_MAX) {
    return;
  }
  group  = context->group;
  layout = group ? group->layout : NULL;
  chunk  = group ? group->descriptorChunk : NULL;
  if (!layout || !chunk || chunk->coherent) {
    return;
  }

  atom  = layout->nonCoherentAtomSize ? layout->nonCoherentAtomSize : 1u;
  begin = group->descriptorOffset + context->dirtyBegin;
  end   = group->descriptorOffset + context->dirtyEnd;
  begin -= begin % atom;
  end = vk__alignDescriptorSize(end, atom);
  range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  range.memory = chunk->memory;
  range.offset = begin;
  range.size   = end >= chunk->allocationSize
                   ? VK_WHOLE_SIZE
                   : end - begin;
  if (vkFlushMappedMemoryRanges(group->device, 1u, &range) != VK_SUCCESS) {
    context->valid = false;
  }
}
#endif

static void
vk__flushDescriptorWrites(GPUDescriptorWriteVk *context) {
  if (!context || !context->valid || context->writeCount == 0u) {
    return;
  }

  vkUpdateDescriptorSets(context->group->device,
                         context->writeCount,
                         context->writes,
                         0u,
                         NULL);
  context->writeCount = 0u;
}

static void
vk__writeDescriptor(void *context,
                    const GPUBindGroupBindingView *binding) {
  GPUDescriptorWriteVk   *writeContext;
  GPUBufferVk            *buffer;
  GPUTexture             *gpuTexture;
  GPUTextureVk           *texture;
  GPUTextureViewVk       *view;
  GPUSamplerVk           *sampler;
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  GPUAccelerationStructureVk *accelerationStructure;
  VkWriteDescriptorSetAccelerationStructureKHR *accelerationInfo;
#endif
  VkDescriptorBufferInfo *bufferInfo;
  VkDescriptorImageInfo  *imageInfo;
  VkWriteDescriptorSet   *write;
  VkDescriptorType        type;
  uint32_t                writeIndex;

  writeContext = context;
  if (!writeContext || !writeContext->valid || !binding ||
      !vk__descriptorType(binding->bindingType,
                          binding->hasDynamicOffset,
                          &type)) {
    if (writeContext) {
      writeContext->valid = false;
    }
    return;
  }
  if ((binding->kind == GPUBindKindBuffer && !binding->buffer) ||
      (binding->kind == GPUBindKindTexture && !binding->textureView) ||
      (binding->kind == GPUBindKindSampler && !binding->sampler) ||
      (binding->kind == GPUBindKindAccelerationStructure &&
       !binding->accelerationStructure)) {
    return;
  }

  writeIndex = writeContext->writeCount;
  write      = &writeContext->writes[writeIndex];
  bufferInfo = &writeContext->bufferInfos[writeIndex];
  imageInfo  = &writeContext->imageInfos[writeIndex];
  memset(write, 0, sizeof(*write));
  memset(bufferInfo, 0, sizeof(*bufferInfo));
  memset(imageInfo, 0, sizeof(*imageInfo));

  write->sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write->dstSet          = writeContext->group->set;
  write->dstBinding      = binding->binding;
  write->dstArrayElement = binding->arrayIndex;
  write->descriptorCount = 1u;
  write->descriptorType  = type;

  switch (binding->kind) {
    case GPUBindKindBuffer:
      if (!binding->buffer ||
          binding->buffer->device != writeContext->device) {
        writeContext->valid = false;
        return;
      }
      buffer = binding->buffer->_priv;
      if (!buffer || !buffer->buffer) {
        writeContext->valid = false;
        return;
      }
      bufferInfo->buffer  = buffer->buffer;
      bufferInfo->offset  = binding->offset;
      bufferInfo->range   = binding->size;
      write->pBufferInfo  = bufferInfo;
      break;
    case GPUBindKindTexture:
      view       = binding->textureView ? binding->textureView->_priv : NULL;
      gpuTexture = binding->textureView
                     ? binding->textureView->_texture
                     : NULL;
      texture    = gpuTexture ? gpuTexture->_priv : NULL;
      if (!view || !view->view || !texture || !texture->image ||
          view->device != writeContext->group->device ||
          texture->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }
      imageInfo->imageView = view->view;
      imageInfo->imageLayout =
        binding->bindingType == GPU_BINDING_STORAGE_TEXTURE ||
        (gpuTexture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u
          ? VK_IMAGE_LAYOUT_GENERAL
          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      write->pImageInfo = imageInfo;
      break;
    case GPUBindKindSampler:
      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (!sampler || !sampler->sampler ||
          sampler->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }
      imageInfo->sampler = sampler->sampler;
      write->pImageInfo  = imageInfo;
      break;
    case GPUBindKindAccelerationStructure:
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
      accelerationStructure = binding->accelerationStructure
                                ? binding->accelerationStructure->_priv
                                : NULL;
      if (!accelerationStructure || !accelerationStructure->structure ||
          accelerationStructure->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }
      accelerationInfo = &writeContext->accelerationInfos[writeIndex];
      memset(accelerationInfo, 0, sizeof(*accelerationInfo));
      writeContext->accelerationStructures[writeIndex] =
        accelerationStructure->structure;
      accelerationInfo->sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
      accelerationInfo->accelerationStructureCount = 1u;
      accelerationInfo->pAccelerationStructures =
        &writeContext->accelerationStructures[writeIndex];
      write->pNext = accelerationInfo;
      break;
#else
      writeContext->valid = false;
      return;
#endif
    default:
      writeContext->valid = false;
      return;
  }

  writeContext->writeCount++;
  if (writeContext->writeCount == GPU_VK_DESCRIPTOR_WRITE_BATCH_COUNT) {
    vk__flushDescriptorWrites(writeContext);
  }
}

#ifdef VK_EXT_descriptor_buffer
static void
vk__writeDescriptorBuffer(void                          *context,
                          const GPUBindGroupBindingView *binding) {
  GPUDescriptorBufferWriteVk  *writeContext;
  GPUBindGroupVk              *group;
  GPUBindGroupLayoutVk        *layout;
  const GPUDescriptorBindingVk *descriptorBinding;
  GPUBufferVk                 *buffer;
  GPUTexture                  *gpuTexture;
  GPUTextureVk                *texture;
  GPUTextureViewVk            *view;
  GPUSamplerVk                *sampler;
  VkDescriptorAddressInfoEXT   addressInfo = {0};
  VkDescriptorImageInfo        imageInfo = {0};
  VkDescriptorGetInfoEXT       getInfo = {0};
  VkDescriptorType             type;
  VkDeviceSize                 offset;
  uint8_t                     *destination;
  VkSampler                    samplerHandle;

  writeContext = context;
  group        = writeContext ? writeContext->group : NULL;
  layout       = group ? group->layout : NULL;
  if (!writeContext || !writeContext->valid || !binding || !layout ||
      !layout->gpuDevice || !layout->gpuDevice->getDescriptor ||
      !vk__descriptorType(binding->bindingType,
                          binding->hasDynamicOffset,
                          &type)) {
    if (writeContext) {
      writeContext->valid = false;
    }
    return;
  }
  if ((binding->kind == GPUBindKindBuffer && !binding->buffer) ||
      (binding->kind == GPUBindKindTexture && !binding->textureView) ||
      (binding->kind == GPUBindKindSampler && !binding->sampler) ||
      (binding->kind == GPUBindKindAccelerationStructure &&
       !binding->accelerationStructure)) {
    return;
  }

  descriptorBinding = vk__findDescriptorBinding(layout,
                                                 binding->binding,
                                                 type);
  if (!descriptorBinding || binding->arrayIndex >= descriptorBinding->count) {
    writeContext->valid = false;
    return;
  }
  offset = descriptorBinding->offset +
           descriptorBinding->size * binding->arrayIndex;
  if (offset > layout->descriptorSize ||
      descriptorBinding->size > layout->descriptorSize - offset) {
    writeContext->valid = false;
    return;
  }
  destination = (uint8_t *)group->descriptorChunk->mapped +
                group->descriptorOffset + offset;
  getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
  getInfo.type  = type;

  switch (binding->kind) {
    case GPUBindKindBuffer:
      buffer = binding->buffer ? binding->buffer->_priv : NULL;
      if (!buffer || !buffer->buffer || binding->buffer->_gpuAddress == 0u ||
          binding->buffer->device != writeContext->device) {
        writeContext->valid = false;
        return;
      }
      addressInfo.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
      addressInfo.address = binding->buffer->_gpuAddress + binding->offset;
      addressInfo.range   = binding->size;
      addressInfo.format  = VK_FORMAT_UNDEFINED;
      if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        getInfo.data.pUniformBuffer = &addressInfo;
      } else {
        getInfo.data.pStorageBuffer = &addressInfo;
      }
      break;
    case GPUBindKindTexture:
      view       = binding->textureView ? binding->textureView->_priv : NULL;
      gpuTexture = binding->textureView ? binding->textureView->_texture : NULL;
      texture    = gpuTexture ? gpuTexture->_priv : NULL;
      if (!view || !view->view || !texture || !texture->image ||
          view->device != group->device || texture->device != group->device) {
        writeContext->valid = false;
        return;
      }
      imageInfo.imageView = view->view;
      imageInfo.imageLayout =
        binding->bindingType == GPU_BINDING_STORAGE_TEXTURE ||
        (gpuTexture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u
          ? VK_IMAGE_LAYOUT_GENERAL
          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        getInfo.data.pStorageImage = &imageInfo;
      } else {
        getInfo.data.pSampledImage = &imageInfo;
      }
      break;
    case GPUBindKindSampler:
      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (!sampler || !sampler->sampler || sampler->device != group->device) {
        writeContext->valid = false;
        return;
      }
      samplerHandle         = sampler->sampler;
      getInfo.data.pSampler = &samplerHandle;
      break;
    case GPUBindKindAccelerationStructure:
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
      {
        GPUAccelerationStructureVk *accelerationStructure;

        accelerationStructure = binding->accelerationStructure
                                  ? binding->accelerationStructure->_priv
                                  : NULL;
        if (!accelerationStructure || accelerationStructure->address == 0u ||
            accelerationStructure->device != group->device) {
          writeContext->valid = false;
          return;
        }
        getInfo.data.accelerationStructure = accelerationStructure->address;
      }
      break;
#else
      writeContext->valid = false;
      return;
#endif
    default:
      writeContext->valid = false;
      return;
  }

  layout->gpuDevice->getDescriptor(group->device,
                                   &getInfo,
                                   descriptorBinding->size,
                                   destination);
  if (offset < writeContext->dirtyBegin) {
    writeContext->dirtyBegin = offset;
  }
  if (offset + descriptorBinding->size > writeContext->dirtyEnd) {
    writeContext->dirtyEnd = offset + descriptorBinding->size;
  }
}
#endif

static void
vk__destroyBindGroupState(GPUBindGroupVk *native) {
  if (!native) {
    return;
  }

  vk__freeDescriptorSet(native);
#ifdef VK_EXT_descriptor_buffer
  vk__freeDescriptorSlot(native);
#endif
  free(native);
}

GPU_HIDE
GPUResult
vk_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  GPUBindGroupLayout   *layout;
  GPUBindGroupLayoutVk *layoutVk;
  GPUBindGroupVk       *native;
  GPUDeviceVk          *deviceVk;
  GPUDescriptorWriteVk  writeContext = {0};
#ifdef VK_EXT_descriptor_buffer
  GPUDescriptorBufferWriteVk bufferWriteContext = {0};
#endif
  GPUResult              result;

  if (!device || !device->_priv || !group) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceVk = device->_priv;
  layout   = gpuBindGroupGetLayout(group);
  layoutVk = layout ? layout->_native : NULL;
  if (!layoutVk || !layoutVk->layout ||
      layoutVk->device != deviceVk->device) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->layout = layoutVk;
  native->device = deviceVk->device;
  result = vk__allocateDescriptorSet(layoutVk,
                                     &native->pool,
                                     &native->set);
  if (result != GPU_OK) {
    vk__destroyBindGroupState(native);
    return result;
  }

  writeContext.device = device;
  writeContext.group  = native;
  writeContext.valid  = true;
  if (!gpuForEachBindGroupBinding(group,
                                  vk__writeDescriptor,
                                  &writeContext) ||
      !writeContext.valid) {
    vk__destroyBindGroupState(native);
    return GPU_ERROR_UNSUPPORTED;
  }
  vk__flushDescriptorWrites(&writeContext);

#ifdef VK_EXT_descriptor_buffer
  if (layoutVk->descriptorBuffer) {
    result = vk__allocateDescriptorSlot(device, layoutVk, native);
    if (result != GPU_OK) {
      vk__destroyBindGroupState(native);
      return result;
    }
    bufferWriteContext.device     = device;
    bufferWriteContext.group      = native;
    bufferWriteContext.dirtyBegin = UINT64_MAX;
    bufferWriteContext.valid      = true;
    if (!gpuForEachBindGroupBinding(group,
                                    vk__writeDescriptorBuffer,
                                    &bufferWriteContext) ||
        !bufferWriteContext.valid) {
      vk__destroyBindGroupState(native);
      return GPU_ERROR_UNSUPPORTED;
    }
    vk__flushDescriptorBuffer(&bufferWriteContext);
    if (!bufferWriteContext.valid) {
      vk__destroyBindGroupState(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }
#endif

  group->_native = native;
  return GPU_OK;
}

GPU_HIDE
bool
vk_updateBindGroup(GPUBindGroup            *group,
                   uint32_t                 entryCount,
                   const GPUBindGroupEntry *entries) {
  GPUBindGroupVk        *native;
  GPUDescriptorWriteVk   writeContext = {0};
#ifdef VK_EXT_descriptor_buffer
  GPUDescriptorBufferWriteVk bufferWriteContext = {0};
#endif

  native = group ? group->_native : NULL;
  if (!native || !native->device) {
    return false;
  }

  if (!native->set) {
    return false;
  }

  writeContext.device = group->_device;
  writeContext.group  = native;
  writeContext.valid  = true;
  if (!gpuForEachBindGroupEntry(group,
                                entryCount,
                                entries,
                                vk__writeDescriptor,
                                &writeContext) ||
      !writeContext.valid) {
    return false;
  }
  vk__flushDescriptorWrites(&writeContext);

#ifdef VK_EXT_descriptor_buffer
  if (native->layout && native->layout->descriptorBuffer) {
    if (!native->descriptorChunk) {
      return false;
    }
    bufferWriteContext.device     = group->_device;
    bufferWriteContext.group      = native;
    bufferWriteContext.dirtyBegin = UINT64_MAX;
    bufferWriteContext.valid      = true;
    if (!gpuForEachBindGroupEntry(group,
                                  entryCount,
                                  entries,
                                  vk__writeDescriptorBuffer,
                                  &bufferWriteContext) ||
        !bufferWriteContext.valid) {
      return false;
    }
    vk__flushDescriptorBuffer(&bufferWriteContext);
    if (!bufferWriteContext.valid) {
      return false;
    }
  }
#endif

  return true;
}

GPU_HIDE
void
vk_destroyBindGroup(GPUBindGroup *group) {
  if (!group) {
    return;
  }

  vk__destroyBindGroupState(group->_native);
  group->_native = NULL;
}

static bool
vk__bindGroup(VkCommandBuffer       command,
              VkPipelineBindPoint   bindPoint,
              VkPipelineLayout      encoderLayout,
              GPUBindGroupVk      **descriptorGroups,
              GPUPipelineLayout    *pipelineLayout,
              uint32_t              groupIndex,
              GPUBindGroup         *group,
              uint32_t              dynamicOffsetCount,
              const uint32_t       *dynamicOffsets,
              uint32_t             *reorderedOffsets) {
  GPUPipelineLayoutVk   *pipeline;
  GPUBindGroupVk        *groupVk;
  GPUBindGroupLayoutVk  *layoutVk;
  const uint32_t        *nativeOffsets;

  pipeline = pipelineLayout ? pipelineLayout->_native : NULL;
  groupVk  = group ? group->_native : NULL;
  layoutVk = groupVk ? groupVk->layout : NULL;
  if (!command || !pipeline || !pipeline->layout || !encoderLayout ||
      !descriptorGroups || !groupVk || !layoutVk ||
      groupVk->device != pipeline->device ||
      layoutVk->device != pipeline->device ||
      dynamicOffsetCount != layoutVk->dynamicCount ||
      dynamicOffsetCount > GPU_VK_MAX_DYNAMIC_OFFSETS ||
      (dynamicOffsetCount > 0u && (!dynamicOffsets || !reorderedOffsets))) {
    return false;
  }

#ifdef VK_EXT_descriptor_buffer
  if (pipeline->descriptorBuffer && encoderLayout == pipeline->layout) {
    GPUDeviceVk                     *deviceVk;
    VkDescriptorBufferBindingInfoEXT bufferInfos[GPU_ENCODER_MAX_BIND_GROUPS];
    GPUDescriptorBufferChunkVk      *chunks[GPU_ENCODER_MAX_BIND_GROUPS];
    uint32_t                         bufferCount;

    deviceVk = layoutVk->gpuDevice;
    if (!layoutVk->descriptorBuffer || !groupVk->descriptorChunk ||
        dynamicOffsetCount != 0u || !deviceVk ||
        !deviceVk->bindDescriptorBuffers ||
        !deviceVk->setDescriptorBufferOffsets) {
      return false;
    }
    descriptorGroups[groupIndex] = groupVk;
    bufferCount = 0u;
    for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
      GPUBindGroupVk              *boundGroup;
      GPUDescriptorBufferChunkVk  *chunk;
      bool                         found;

      boundGroup = descriptorGroups[i];
      chunk      = boundGroup ? boundGroup->descriptorChunk : NULL;
      if (!chunk) {
        continue;
      }
      found = false;
      for (uint32_t j = 0u; j < bufferCount; j++) {
        if (chunks[j] == chunk) {
          found = true;
          break;
        }
      }
      if (found) {
        continue;
      }
      chunks[bufferCount] = chunk;
      memset(&bufferInfos[bufferCount], 0, sizeof(bufferInfos[bufferCount]));
      bufferInfos[bufferCount].sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
      bufferInfos[bufferCount].address = chunk->address;
      bufferInfos[bufferCount].usage =
        VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
      bufferCount++;
    }
    deviceVk->bindDescriptorBuffers(command, bufferCount, bufferInfos);
    for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
      GPUBindGroupVk *boundGroup;
      uint32_t        bufferIndex;

      boundGroup = descriptorGroups[i];
      if (!boundGroup || !boundGroup->descriptorChunk) {
        continue;
      }
      bufferIndex = 0u;
      while (bufferIndex < bufferCount &&
             chunks[bufferIndex] != boundGroup->descriptorChunk) {
        bufferIndex++;
      }
      if (bufferIndex == bufferCount) {
        return false;
      }
      deviceVk->setDescriptorBufferOffsets(command,
                                           bindPoint,
                                           encoderLayout,
                                           i,
                                           1u,
                                           &bufferIndex,
                                           &boundGroup->descriptorOffset);
    }
    return true;
  }
#endif

  if (!groupVk->set) {
    return false;
  }
  descriptorGroups[groupIndex] = NULL;

  nativeOffsets = dynamicOffsets;
  for (uint32_t i = 0u; i < dynamicOffsetCount; i++) {
    if (layoutVk->dynamicOrder[i] != i) {
      for (uint32_t j = 0u; j < dynamicOffsetCount; j++) {
        reorderedOffsets[j] = dynamicOffsets[layoutVk->dynamicOrder[j]];
      }
      nativeOffsets = reorderedOffsets;
      break;
    }
  }

  vkCmdBindDescriptorSets(command,
                          bindPoint,
                          encoderLayout,
                          groupIndex,
                          1u,
                          &groupVk->set,
                          dynamicOffsetCount,
                          nativeOffsets);
  return true;
}

GPU_HIDE
bool
vk_bindRenderGroup(GPURenderCommandEncoder *pass,
                   GPUPipelineLayout       *pipelineLayout,
                   uint32_t                 groupIndex,
                   GPUBindGroup            *group,
                   uint32_t                 dynamicOffsetCount,
                   const uint32_t          *dynamicOffsets) {
  GPURenderEncoderVk *encoder;

  encoder = pass ? pass->_priv : NULL;
  return encoder && vk__bindGroup(encoder->command,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  encoder->pipelineLayout,
                                  encoder->descriptorGroups,
                                  pipelineLayout,
                                  groupIndex,
                                  group,
                                  dynamicOffsetCount,
                                  dynamicOffsets,
                                  encoder->dynamicOffsets);
}

GPU_HIDE
bool
vk_bindComputeGroup(GPUComputePassEncoder *pass,
                    GPUPipelineLayout     *pipelineLayout,
                    uint32_t               groupIndex,
                    GPUBindGroup          *group,
                    uint32_t               dynamicOffsetCount,
                    const uint32_t        *dynamicOffsets) {
  GPUComputeEncoderVk *encoder;

  encoder = pass ? pass->_priv : NULL;
  return encoder && vk__bindGroup(encoder->command,
                                  VK_PIPELINE_BIND_POINT_COMPUTE,
                                  encoder->pipelineLayout,
                                  encoder->descriptorGroups,
                                  pipelineLayout,
                                  groupIndex,
                                  group,
                                  dynamicOffsetCount,
                                  dynamicOffsets,
                                  encoder->dynamicOffsets);
}

GPU_HIDE
bool
vk_bindRayTracingGroup(GPURayTracingPassEncoderEXT *pass,
                       GPUPipelineLayout           *pipelineLayout,
                       uint32_t                     groupIndex,
                       GPUBindGroup                *group,
                       uint32_t                     dynamicOffsetCount,
                       const uint32_t              *dynamicOffsets) {
#ifdef VK_KHR_ray_tracing_pipeline
  GPURayTracingEncoderVk *encoder;

  encoder = pass ? pass->_priv : NULL;
  return encoder && vk__bindGroup(encoder->command,
                                  VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                  encoder->pipelineLayout,
                                  encoder->descriptorGroups,
                                  pipelineLayout,
                                  groupIndex,
                                  group,
                                  dynamicOffsetCount,
                                  dynamicOffsets,
                                  encoder->dynamicOffsets);
#else
  (void)pass;
  (void)pipelineLayout;
  (void)groupIndex;
  (void)group;
  (void)dynamicOffsetCount;
  (void)dynamicOffsets;
  return false;
#endif
}

GPU_HIDE
void
vk_initDescriptor(GPUApiDescriptor *api) {
  api->createBindGroupLayout  = vk_createBindGroupLayout;
  api->destroyBindGroupLayout = vk_destroyBindGroupLayout;
  api->createPipelineLayout   = vk_createPipelineLayout;
  api->destroyPipelineLayout  = vk_destroyPipelineLayout;
  api->createBindGroup        = vk_createBindGroup;
  api->updateBindGroup        = vk_updateBindGroup;
  api->destroyBindGroup       = vk_destroyBindGroup;
  api->bindRenderGroup        = vk_bindRenderGroup;
  api->bindComputeGroup       = vk_bindComputeGroup;
}
