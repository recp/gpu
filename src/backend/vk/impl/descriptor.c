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
#include "../../../api/sampler_internal.h"
#include "../../../api/texture_internal.h"

typedef struct GPUDescriptorWriteVk {
  GPUDevice      *device;
  GPUBindGroupVk *group;
  bool            valid;
} GPUDescriptorWriteVk;

enum {
  GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT = 7u
};

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
  return stages;
}

static void
vk__destroyBindGroupLayoutState(GPUBindGroupLayoutVk *native) {
  if (!native) {
    return;
  }

  if (native->device && native->layout) {
    vkDestroyDescriptorSetLayout(native->device, native->layout, NULL);
  }
  free(native->dynamicOrder);
  free(native);
}

GPU_HIDE
GPUResult
vk_createBindGroupLayout(GPUDevice          *device,
                         GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutVk              *native;
  const GPUBindGroupLayoutEntry     *entries;
  const uint32_t                    *backendBindings;
  VkDescriptorSetLayoutBinding      *bindings;
  VkDescriptorSetLayoutCreateInfo    info = {0};
  uint32_t                           dynamicBindings[GPU_VK_MAX_DYNAMIC_OFFSETS];
  uint32_t                           backendBindingCount;
  uint32_t                           entryCount;

  if (!device || !device->_priv || !layout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  entries = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  backendBindings = gpuGetBindGroupLayoutBackendBindings(
    layout,
    &backendBindingCount
  );
  if (entryCount != backendBindingCount ||
      (entryCount > 0u && (!entries || !backendBindings))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native = calloc(1, sizeof(*native));
  bindings = entryCount ? calloc(entryCount, sizeof(*bindings)) : NULL;
  if (!native || (entryCount > 0u && !bindings)) {
    free(bindings);
    free(native);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->device = ((GPUDeviceVk *)device->_priv)->device;
  for (uint32_t i = 0u; i < entryCount; i++) {
    VkDescriptorType type;
    VkShaderStageFlags stages;

    stages = vk__descriptorStages(entries[i].visibility);
    if (entries[i].arrayCount != 1u || entries[i].immutableSampler ||
        stages == 0u ||
        !vk__descriptorType(entries[i].bindingType,
                            entries[i].hasDynamicOffset,
                            &type)) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_UNSUPPORTED;
    }

    bindings[i].binding         = backendBindings[i];
    bindings[i].descriptorType  = type;
    bindings[i].descriptorCount = 1u;
    bindings[i].stageFlags      = stages;

    if (entries[i].hasDynamicOffset) {
      uint32_t position;

      if (native->dynamicCount >= GPU_VK_MAX_DYNAMIC_OFFSETS) {
        free(bindings);
        vk__destroyBindGroupLayoutState(native);
        return GPU_ERROR_UNSUPPORTED;
      }

      position = native->dynamicCount;
      while (position > 0u &&
             dynamicBindings[position - 1u] > backendBindings[i]) {
        dynamicBindings[position] = dynamicBindings[position - 1u];
        position--;
      }
      dynamicBindings[position] = backendBindings[i];
      native->dynamicCount++;
    }
  }

  if (native->dynamicCount > 0u) {
    uint32_t callerIndex;

    native->dynamicOrder = calloc(native->dynamicCount,
                                  sizeof(*native->dynamicOrder));
    if (!native->dynamicOrder) {
      free(bindings);
      vk__destroyBindGroupLayoutState(native);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    callerIndex = 0u;
    for (uint32_t i = 0u; i < entryCount; i++) {
      if (entries[i].hasDynamicOffset) {
        for (uint32_t j = 0u; j < native->dynamicCount; j++) {
          if (dynamicBindings[j] == backendBindings[i]) {
            native->dynamicOrder[j] = callerIndex;
            break;
          }
        }
        callerIndex++;
      }
    }
  }

  info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = entryCount;
  info.pBindings    = bindings;
  if (vkCreateDescriptorSetLayout(native->device,
                                  &info,
                                  NULL,
                                  &native->layout) != VK_SUCCESS) {
    free(bindings);
    vk__destroyBindGroupLayoutState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

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

static bool
vk__addPoolSize(VkDescriptorPoolSize *sizes,
                uint32_t *count,
                VkDescriptorType type,
                uint32_t descriptorCount) {
  if (!sizes || !count || descriptorCount == 0u) {
    return false;
  }

  for (uint32_t i = 0u; i < *count; i++) {
    if (sizes[i].type == type) {
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

static void
vk__writeDescriptor(void *context,
                    const GPUBindGroupBindingView *binding) {
  GPUDescriptorWriteVk  *writeContext;
  GPUBufferVk           *buffer;
  GPUTextureVk          *texture;
  GPUTextureViewVk      *view;
  GPUSamplerVk          *sampler;
  VkDescriptorBufferInfo bufferInfo = {0};
  VkDescriptorImageInfo  imageInfo = {0};
  VkWriteDescriptorSet   write = {0};
  VkDescriptorType       type;

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

  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet          = writeContext->group->set;
  write.dstBinding      = binding->binding;
  write.descriptorCount = 1u;
  write.descriptorType  = type;

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
      bufferInfo.buffer = buffer->buffer;
      bufferInfo.offset = binding->offset;
      bufferInfo.range  = binding->size;
      write.pBufferInfo = &bufferInfo;
      break;
    case GPUBindKindTexture:
      view = binding->textureView ? binding->textureView->_priv : NULL;
      texture = binding->textureView && binding->textureView->_texture
                  ? binding->textureView->_texture->_priv
                  : NULL;
      if (!view || !view->view || !texture || !texture->image ||
          view->device != writeContext->group->device ||
          texture->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }
      imageInfo.imageView = view->view;
      imageInfo.imageLayout = binding->bindingType == GPU_BINDING_STORAGE_TEXTURE
                                ? VK_IMAGE_LAYOUT_GENERAL
                                : texture->layout;
      if (imageInfo.imageLayout != VK_IMAGE_LAYOUT_GENERAL &&
          imageInfo.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        writeContext->valid = false;
        return;
      }
      write.pImageInfo = &imageInfo;
      break;
    case GPUBindKindSampler:
      sampler = binding->sampler ? binding->sampler->_priv : NULL;
      if (!sampler || !sampler->sampler ||
          sampler->device != writeContext->group->device) {
        writeContext->valid = false;
        return;
      }
      imageInfo.sampler = sampler->sampler;
      write.pImageInfo  = &imageInfo;
      break;
    default:
      writeContext->valid = false;
      return;
  }

  vkUpdateDescriptorSets(writeContext->group->device,
                         1u,
                         &write,
                         0u,
                         NULL);
}

static void
vk__destroyBindGroupState(GPUBindGroupVk *native) {
  if (!native) {
    return;
  }

  if (native->device && native->pool) {
    vkDestroyDescriptorPool(native->device, native->pool, NULL);
  }
  free(native);
}

GPU_HIDE
GPUResult
vk_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  GPUBindGroupLayout          *layout;
  GPUBindGroupLayoutVk        *layoutVk;
  GPUBindGroupVk              *native;
  const GPUBindGroupLayoutEntry *entries;
  VkDescriptorPoolSize         poolSizes[GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT] = {{0}};
  VkDescriptorPoolCreateInfo   poolInfo = {0};
  VkDescriptorSetAllocateInfo  allocationInfo = {0};
  GPUDescriptorWriteVk         writeContext;
  uint32_t                     entryCount;
  uint32_t                     poolSizeCount;

  if (!device || !device->_priv || !group) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout   = gpuBindGroupGetLayout(group);
  layoutVk = layout ? layout->_native : NULL;
  entries  = GPUGetBindGroupLayoutEntries(layout, &entryCount);
  if (!layoutVk || !layoutVk->layout || (entryCount > 0u && !entries)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  poolSizeCount = 0u;
  for (uint32_t i = 0u; i < entryCount; i++) {
    VkDescriptorType type;

    if (!vk__descriptorType(entries[i].bindingType,
                            entries[i].hasDynamicOffset,
                            &type) ||
        !vk__addPoolSize(poolSizes,
                        &poolSizeCount,
                        type,
                        entries[i].arrayCount)) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native->device          = ((GPUDeviceVk *)device->_priv)->device;
  poolInfo.sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets        = 1u;
  poolInfo.poolSizeCount  = poolSizeCount;
  poolInfo.pPoolSizes     = poolSizeCount > 0u ? poolSizes : NULL;
  if (vkCreateDescriptorPool(native->device,
                             &poolInfo,
                             NULL,
                             &native->pool) != VK_SUCCESS) {
    vk__destroyBindGroupState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  allocationInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocationInfo.descriptorPool     = native->pool;
  allocationInfo.descriptorSetCount = 1u;
  allocationInfo.pSetLayouts        = &layoutVk->layout;
  if (vkAllocateDescriptorSets(native->device,
                               &allocationInfo,
                               &native->set) != VK_SUCCESS) {
    vk__destroyBindGroupState(native);
    return GPU_ERROR_BACKEND_FAILURE;
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

  group->_native = native;
  return GPU_OK;
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

GPU_HIDE
bool
vk_bindRenderGroup(GPURenderCommandEncoder *pass,
                   GPUPipelineLayout       *pipelineLayout,
                   uint32_t                 groupIndex,
                   GPUBindGroup            *group,
                   uint32_t                 dynamicOffsetCount,
                   const uint32_t          *dynamicOffsets) {
  GPURenderEncoderVk    *encoder;
  GPUPipelineLayoutVk   *pipeline;
  GPUBindGroupVk        *groupVk;
  GPUBindGroupLayout    *layout;
  GPUBindGroupLayoutVk  *layoutVk;
  const uint32_t        *nativeOffsets;

  encoder  = pass ? pass->_priv : NULL;
  pipeline = pipelineLayout ? pipelineLayout->_native : NULL;
  groupVk  = group ? group->_native : NULL;
  layout   = gpuBindGroupGetLayout(group);
  layoutVk = layout ? layout->_native : NULL;
  if (!encoder || !pipeline || !pipeline->layout ||
      encoder->pipelineLayout != pipeline->layout ||
      !groupVk || !groupVk->set || !layoutVk ||
      dynamicOffsetCount != layoutVk->dynamicCount ||
      dynamicOffsetCount > GPU_VK_MAX_DYNAMIC_OFFSETS ||
      (dynamicOffsetCount > 0u && !dynamicOffsets)) {
    return false;
  }

  nativeOffsets = dynamicOffsets;
  for (uint32_t i = 0u; i < dynamicOffsetCount; i++) {
    if (layoutVk->dynamicOrder[i] != i) {
      for (uint32_t j = 0u; j < dynamicOffsetCount; j++) {
        encoder->dynamicOffsets[j] = dynamicOffsets[layoutVk->dynamicOrder[j]];
      }
      nativeOffsets = encoder->dynamicOffsets;
      break;
    }
  }

  vkCmdBindDescriptorSets(encoder->command,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->layout,
                          groupIndex,
                          1u,
                          &groupVk->set,
                          dynamicOffsetCount,
                          nativeOffsets);
  return true;
}

GPU_HIDE
void
vk_initDescriptor(GPUApiDescriptor *api) {
  api->createBindGroupLayout  = vk_createBindGroupLayout;
  api->destroyBindGroupLayout = vk_destroyBindGroupLayout;
  api->createPipelineLayout   = vk_createPipelineLayout;
  api->destroyPipelineLayout  = vk_destroyPipelineLayout;
  api->createBindGroup        = vk_createBindGroup;
  api->destroyBindGroup       = vk_destroyBindGroup;
  api->bindRenderGroup        = vk_bindRenderGroup;
}
