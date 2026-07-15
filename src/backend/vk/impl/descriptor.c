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

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

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
  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].immutableSampler) {
      if (entries[i].arrayCount > UINT32_MAX - immutableSamplerCount) {
        return GPU_ERROR_UNSUPPORTED;
      }
      immutableSamplerCount += entries[i].arrayCount;
    }
  }

  native   = calloc(1, sizeof(*native));
  bindings = entryCount ? calloc(entryCount, sizeof(*bindings)) : NULL;
  if (!native || (entryCount > 0u && !bindings)) {
    free(bindings);
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->device          = ((GPUDeviceVk *)device->_priv)->device;
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

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
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
  }

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

  outLayout->device = base->device;
  if (samplerCount == 0u) {
    outLayout->layout = base->layout;
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

static void
vk__destroyBindGroupState(GPUBindGroupVk *native) {
  if (!native) {
    return;
  }

  vk__freeDescriptorSet(native);
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

  native = group ? group->_native : NULL;
  if (!native || !native->set || !native->device) {
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
              GPUPipelineLayout    *pipelineLayout,
              uint32_t              groupIndex,
              GPUBindGroup         *group,
              uint32_t              dynamicOffsetCount,
              const uint32_t       *dynamicOffsets,
              uint32_t             *reorderedOffsets) {
  GPUPipelineLayoutVk   *pipeline;
  GPUBindGroupVk        *groupVk;
  GPUBindGroupLayout    *layout;
  GPUBindGroupLayoutVk  *layoutVk;
  const uint32_t        *nativeOffsets;

  pipeline = pipelineLayout ? pipelineLayout->_native : NULL;
  groupVk  = group ? group->_native : NULL;
  layout   = gpuBindGroupGetLayout(group);
  layoutVk = layout ? layout->_native : NULL;
  if (!command || !pipeline || !pipeline->layout || !encoderLayout ||
      !groupVk || !groupVk->set || !layoutVk ||
      groupVk->device != pipeline->device ||
      layoutVk->device != pipeline->device ||
      dynamicOffsetCount != layoutVk->dynamicCount ||
      dynamicOffsetCount > GPU_VK_MAX_DYNAMIC_OFFSETS ||
      (dynamicOffsetCount > 0u && (!dynamicOffsets || !reorderedOffsets))) {
    return false;
  }

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
                                  pipelineLayout,
                                  groupIndex,
                                  group,
                                  dynamicOffsetCount,
                                  dynamicOffsets,
                                  encoder->dynamicOffsets);
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
