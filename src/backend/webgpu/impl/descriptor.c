/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

typedef struct WebGPUBindGroupWrite {
  WGPUBindGroupEntry *entries;
  uint32_t            capacity;
  uint32_t            count;
  bool                valid;
} WebGPUBindGroupWrite;

static WGPUShaderStage
webgpu_shaderStages(GPUShaderStageFlags stages) {
  const GPUShaderStageFlags supported = GPU_SHADER_STAGE_VERTEX_BIT |
                                         GPU_SHADER_STAGE_FRAGMENT_BIT |
                                         GPU_SHADER_STAGE_COMPUTE_BIT;
  WGPUShaderStage result;

  if (stages & ~supported) {
    return WGPUShaderStage_None;
  }

  result = WGPUShaderStage_None;
  if (stages & GPU_SHADER_STAGE_VERTEX_BIT)
    result |= WGPUShaderStage_Vertex;
  if (stages & GPU_SHADER_STAGE_FRAGMENT_BIT)
    result |= WGPUShaderStage_Fragment;
  if (stages & GPU_SHADER_STAGE_COMPUTE_BIT)
    result |= WGPUShaderStage_Compute;
  return result;
}

static GPUResult
webgpu_createBindGroupLayout(GPUDevice          *device,
                             GPUBindGroupLayout *layout) {
  WGPUBindGroupLayoutDescriptor descriptor =
    WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupLayoutEntry *nativeEntries;
  const GPUBindGroupLayoutEntry *entries;
  GPUDeviceWebGPU               *native;
  uint32_t                       count;

  native  = gpu_webgpuDevice(device);
  entries = GPUGetBindGroupLayoutEntries(layout, &count);
  if (!native || !native->device || (count > 0u && !entries)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  nativeEntries = count ? calloc(count, sizeof(*nativeEntries)) : NULL;
  if (count > 0u && !nativeEntries) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0u; i < count; i++) {
    WGPUBindGroupLayoutEntry *entry;

    if (entries[i].arrayCount != 1u || entries[i].immutableSampler) {
      free(nativeEntries);
      return GPU_ERROR_UNSUPPORTED;
    }

    entry             = &nativeEntries[i];
    *entry            = (WGPUBindGroupLayoutEntry)
                          WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry->binding    = entries[i].binding;
    entry->visibility = webgpu_shaderStages(entries[i].visibility);
    if (entry->visibility == WGPUShaderStage_None) {
      free(nativeEntries);
      return GPU_ERROR_UNSUPPORTED;
    }

    switch (entries[i].bindingType) {
      case GPU_BINDING_UNIFORM_BUFFER:
        entry->buffer = (WGPUBufferBindingLayout)
                          WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry->buffer.type = WGPUBufferBindingType_Uniform;
        entry->buffer.hasDynamicOffset = entries[i].hasDynamicOffset;
        break;
      case GPU_BINDING_READ_ONLY_STORAGE_BUFFER:
        entry->buffer = (WGPUBufferBindingLayout)
                          WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry->buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entry->buffer.hasDynamicOffset = entries[i].hasDynamicOffset;
        break;
      case GPU_BINDING_STORAGE_BUFFER:
        entry->buffer = (WGPUBufferBindingLayout)
                          WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry->buffer.type = WGPUBufferBindingType_Storage;
        entry->buffer.hasDynamicOffset = entries[i].hasDynamicOffset;
        break;
      case GPU_BINDING_SAMPLED_TEXTURE:
        entry->texture = (WGPUTextureBindingLayout)
                           WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry->texture.sampleType    = WGPUTextureSampleType_Float;
        entry->texture.viewDimension = WGPUTextureViewDimension_2D;
        break;
      case GPU_BINDING_SAMPLER:
        entry->sampler = (WGPUSamplerBindingLayout)
                           WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry->sampler.type = WGPUSamplerBindingType_Filtering;
        break;
      default:
        free(nativeEntries);
        return GPU_ERROR_UNSUPPORTED;
    }
  }

  descriptor.entryCount = count;
  descriptor.entries    = nativeEntries;
  layout->_native = wgpuDeviceCreateBindGroupLayout(native->device,
                                                     &descriptor);
  free(nativeEntries);
  return layout->_native ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyBindGroupLayout(GPUBindGroupLayout *layout) {
  if (layout && layout->_native) {
    wgpuBindGroupLayoutRelease(layout->_native);
    layout->_native = NULL;
  }
}

static GPUResult
webgpu_createPipelineLayout(GPUDevice *device, GPUPipelineLayout *layout) {
  WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupLayout           nativeGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBindGroupLayout *const    *groups;
  GPUDeviceWebGPU              *native;
  uint32_t                      groupCount;
  uint32_t                      pushConstantSize;

  native = gpu_webgpuDevice(device);
  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  gpuGetPipelineLayoutPushConstants(layout, &pushConstantSize, NULL);
  if (!native || !native->device ||
      groupCount > GPU_ARRAY_LEN(nativeGroups) ||
      (groupCount > 0u && !groups) || pushConstantSize != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  for (uint32_t i = 0u; i < groupCount; i++) {
    nativeGroups[i] = groups[i] ? groups[i]->_native : NULL;
    if (!nativeGroups[i]) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  descriptor.bindGroupLayoutCount = groupCount;
  descriptor.bindGroupLayouts     = groupCount ? nativeGroups : NULL;
  layout->_native = wgpuDeviceCreatePipelineLayout(native->device,
                                                    &descriptor);
  return layout->_native ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyPipelineLayout(GPUPipelineLayout *layout) {
  if (layout && layout->_native) {
    wgpuPipelineLayoutRelease(layout->_native);
    layout->_native = NULL;
  }
}

static void
webgpu_writeBindGroupEntry(void *context,
                           const GPUBindGroupBindingView *binding) {
  WebGPUBindGroupWrite *write;
  WGPUBindGroupEntry   *entry;

  write = context;
  if (!write || !binding || !write->valid ||
      write->count >= write->capacity || binding->arrayIndex != 0u) {
    if (write) {
      write->valid = false;
    }
    return;
  }

  entry          = &write->entries[write->count++];
  *entry         = (WGPUBindGroupEntry)WGPU_BIND_GROUP_ENTRY_INIT;
  entry->binding = binding->binding;
  switch (binding->kind) {
    case GPUBindKindBuffer:
      if (!binding->buffer || !binding->buffer->_priv) {
        write->valid = false;
        return;
      }
      entry->buffer = binding->buffer->_priv;
      entry->offset = binding->offset;
      entry->size   = binding->size;
      break;
    case GPUBindKindTexture:
      if (!binding->textureView || !binding->textureView->_priv) {
        write->valid = false;
        return;
      }
      entry->textureView = binding->textureView->_priv;
      break;
    case GPUBindKindSampler:
      if (!binding->sampler || !binding->sampler->_priv) {
        write->valid = false;
        return;
      }
      entry->sampler = binding->sampler->_priv;
      break;
    default:
      write->valid = false;
      break;
  }
}

static GPUResult
webgpu_createBindGroup(GPUDevice *device, GPUBindGroup *group) {
  WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  WebGPUBindGroupWrite    write;
  WGPUBindGroupEntry     *entries;
  GPUBindGroupLayout     *layout;
  GPUDeviceWebGPU        *native;
  uint32_t                count;

  native = gpu_webgpuDevice(device);
  layout = gpuBindGroupGetLayout(group);
  (void)GPUGetBindGroupLayoutEntries(layout, &count);
  if (!native || !native->device || !layout || !layout->_native) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  entries = count ? calloc(count, sizeof(*entries)) : NULL;
  if (count > 0u && !entries) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  write.entries  = entries;
  write.capacity = count;
  write.count    = 0u;
  write.valid    = true;
  if (!gpuForEachBindGroupBinding(group,
                                  webgpu_writeBindGroupEntry,
                                  &write) ||
      !write.valid || write.count != count) {
    free(entries);
    return GPU_ERROR_UNSUPPORTED;
  }

  descriptor.layout     = layout->_native;
  descriptor.entryCount = write.count;
  descriptor.entries    = entries;
  group->_native = wgpuDeviceCreateBindGroup(native->device, &descriptor);
  free(entries);
  return group->_native ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static bool
webgpu_updateBindGroup(GPUBindGroup            *group,
                       uint32_t                 entryCount,
                       const GPUBindGroupEntry *entries) {
  GPU__UNUSED(group);
  GPU__UNUSED(entryCount);
  GPU__UNUSED(entries);
  return false;
}

static void
webgpu_destroyBindGroup(GPUBindGroup *group) {
  if (group && group->_native) {
    wgpuBindGroupRelease(group->_native);
    group->_native = NULL;
  }
}

static bool
webgpu_bindRenderGroup(GPURenderPassEncoder *pass,
                       GPUPipelineLayout     *pipelineLayout,
                       uint32_t               groupIndex,
                       GPUBindGroup          *group,
                       uint32_t               dynamicOffsetCount,
                       const uint32_t        *dynamicOffsets) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(pipelineLayout);
  command = pass ? pass->_priv : NULL;
  if (!command || !command->renderEncoder || !group || !group->_native) {
    return false;
  }

  wgpuRenderPassEncoderSetBindGroup(command->renderEncoder,
                                    groupIndex,
                                    group->_native,
                                    dynamicOffsetCount,
                                    dynamicOffsets);
  return true;
}

void
webgpu_initDescriptor(GPUApiDescriptor *api) {
  api->createBindGroupLayout  = webgpu_createBindGroupLayout;
  api->destroyBindGroupLayout = webgpu_destroyBindGroupLayout;
  api->createPipelineLayout   = webgpu_createPipelineLayout;
  api->destroyPipelineLayout  = webgpu_destroyPipelineLayout;
  api->createBindGroup        = webgpu_createBindGroup;
  api->updateBindGroup        = webgpu_updateBindGroup;
  api->destroyBindGroup       = webgpu_destroyBindGroup;
  api->bindRenderGroup        = webgpu_bindRenderGroup;
}
