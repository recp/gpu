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

GPUResult
gpu_webgpuInitPushConstants(GPUDeviceWebGPU *device) {
  WGPUBindGroupLayoutDescriptor descriptor =
    WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;

  if (!device || !device->device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (device->pushConstantLayout) {
    return GPU_OK;
  }

  entry.binding    = GPU_WEBGPU_PUSH_CONSTANT_BINDING;
  entry.visibility = WGPUShaderStage_Vertex |
                     WGPUShaderStage_Fragment |
                     WGPUShaderStage_Compute;
  entry.buffer = (WGPUBufferBindingLayout)WGPU_BUFFER_BINDING_LAYOUT_INIT;
  entry.buffer.type             = WGPUBufferBindingType_Uniform;
  entry.buffer.hasDynamicOffset = true;
  entry.buffer.minBindingSize   = GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT;
  descriptor.label      = gpu_webgpuString("gpu-push-constants");
  descriptor.entryCount = 1u;
  descriptor.entries    = &entry;
  device->pushConstantLayout =
    wgpuDeviceCreateBindGroupLayout(device->device, &descriptor);
  return device->pushConstantLayout ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

void
gpu_webgpuDestroyPushConstants(GPUDeviceWebGPU *device) {
  if (!device) {
    return;
  }
  for (uint32_t i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
    GPUCommandWebGPU *command = &device->commands[i];

    if (command->pushConstantGroup) {
      wgpuBindGroupRelease(command->pushConstantGroup);
      command->pushConstantGroup = NULL;
    }
    if (command->pushConstantBuffer) {
      wgpuBufferDestroy(command->pushConstantBuffer);
      wgpuBufferRelease(command->pushConstantBuffer);
      command->pushConstantBuffer = NULL;
    }
    command->pushConstantCursor = 0u;
  }
  if (device->pushConstantLayout) {
    wgpuBindGroupLayoutRelease(device->pushConstantLayout);
    device->pushConstantLayout = NULL;
  }
}

bool
gpu_webgpuUploadPushConstants(GPUCommandWebGPU *command,
                              const void        *data,
                              uint32_t           sizeBytes,
                              uint32_t          *outDynamicOffset) {
  WGPUBufferDescriptor bufferInfo = WGPU_BUFFER_DESCRIPTOR_INIT;
  WGPUBindGroupDescriptor groupInfo = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
  GPUDeviceWebGPU *device;
  uint32_t offset;

  device = gpu_webgpuDevice(gpuCommandBufferDevice(
    command ? &command->command : NULL
  ));
  if (!command || !device || !device->device || !device->queue ||
      !device->pushConstantLayout || !data || !outDynamicOffset ||
      sizeBytes == 0u ||
      sizeBytes > GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT) {
    return false;
  }

  if (!command->pushConstantBuffer) {
    bufferInfo.label = gpu_webgpuString("gpu-push-constant-upload");
    bufferInfo.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bufferInfo.size  = GPU_WEBGPU_PUSH_CONSTANT_CAPACITY;
    command->pushConstantBuffer =
      wgpuDeviceCreateBuffer(device->device, &bufferInfo);
    if (!command->pushConstantBuffer) {
      return false;
    }

    entry.binding = GPU_WEBGPU_PUSH_CONSTANT_BINDING;
    entry.buffer  = command->pushConstantBuffer;
    entry.offset  = 0u;
    entry.size    = GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT;
    groupInfo.label      = gpu_webgpuString("gpu-push-constant-group");
    groupInfo.layout     = device->pushConstantLayout;
    groupInfo.entryCount = 1u;
    groupInfo.entries    = &entry;
    command->pushConstantGroup =
      wgpuDeviceCreateBindGroup(device->device, &groupInfo);
    if (!command->pushConstantGroup) {
      wgpuBufferDestroy(command->pushConstantBuffer);
      wgpuBufferRelease(command->pushConstantBuffer);
      command->pushConstantBuffer = NULL;
      return false;
    }
  }

  offset = command->pushConstantCursor;
  if (offset > GPU_WEBGPU_PUSH_CONSTANT_CAPACITY -
                 GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT) {
    gpuDeviceReportError(gpuCommandBufferDevice(&command->command),
                         GPU_DEVICE_ERROR_OUT_OF_MEMORY,
                         GPU_DEVICE_LOST_REASON_UNKNOWN,
                         GPU_ERROR_OUT_OF_MEMORY,
                         "WebGPU push-constant upload ring exhausted");
    return false;
  }
  wgpuQueueWriteBuffer(device->queue,
                       command->pushConstantBuffer,
                       offset,
                       data,
                       sizeBytes);
  command->pushConstantCursor += GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT;
  *outDynamicOffset = offset;
  return true;
}

static WGPUTextureViewDimension
webgpu_bindingViewDimension(GPUTextureViewType type) {
  static const WGPUTextureViewDimension dimensions[] = {
    [GPU_TEXTURE_VIEW_1D]         = WGPUTextureViewDimension_1D,
    [GPU_TEXTURE_VIEW_1D_ARRAY]   = WGPUTextureViewDimension_Undefined,
    [GPU_TEXTURE_VIEW_2D]         = WGPUTextureViewDimension_2D,
    [GPU_TEXTURE_VIEW_2D_ARRAY]   = WGPUTextureViewDimension_2DArray,
    [GPU_TEXTURE_VIEW_CUBE]       = WGPUTextureViewDimension_Cube,
    [GPU_TEXTURE_VIEW_CUBE_ARRAY] = WGPUTextureViewDimension_CubeArray,
    [GPU_TEXTURE_VIEW_3D]         = WGPUTextureViewDimension_3D
  };

  return (uint32_t)type < GPU_ARRAY_LEN(dimensions)
           ? dimensions[type]
           : WGPUTextureViewDimension_Undefined;
}

static WGPUTextureSampleType
webgpu_textureSampleType(GPUTextureSampleType type) {
  static const WGPUTextureSampleType types[] = {
    [GPU_TEXTURE_SAMPLE_TYPE_FLOAT] = WGPUTextureSampleType_Float,
    [GPU_TEXTURE_SAMPLE_TYPE_UNFILTERABLE_FLOAT] =
      WGPUTextureSampleType_UnfilterableFloat,
    [GPU_TEXTURE_SAMPLE_TYPE_DEPTH] = WGPUTextureSampleType_Depth,
    [GPU_TEXTURE_SAMPLE_TYPE_SINT]  = WGPUTextureSampleType_Sint,
    [GPU_TEXTURE_SAMPLE_TYPE_UINT]  = WGPUTextureSampleType_Uint
  };

  return (uint32_t)type < GPU_ARRAY_LEN(types)
           ? types[type]
           : WGPUTextureSampleType_Undefined;
}

static WGPUStorageTextureAccess
webgpu_storageTextureAccess(GPUStorageTextureAccess access) {
  static const WGPUStorageTextureAccess values[] = {
    [GPU_STORAGE_TEXTURE_ACCESS_WRITE_ONLY] =
      WGPUStorageTextureAccess_WriteOnly,
    [GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY] =
      WGPUStorageTextureAccess_ReadOnly,
    [GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE] =
      WGPUStorageTextureAccess_ReadWrite
  };

  return (uint32_t)access < GPU_ARRAY_LEN(values)
           ? values[access]
           : WGPUStorageTextureAccess_Undefined;
}

static WGPUSamplerBindingType
webgpu_samplerBindingType(GPUSamplerBindingType type) {
  static const WGPUSamplerBindingType types[] = {
    [GPU_SAMPLER_BINDING_FILTERING] = WGPUSamplerBindingType_Filtering,
    [GPU_SAMPLER_BINDING_NON_FILTERING] =
      WGPUSamplerBindingType_NonFiltering,
    [GPU_SAMPLER_BINDING_COMPARISON] = WGPUSamplerBindingType_Comparison
  };

  return (uint32_t)type < GPU_ARRAY_LEN(types)
           ? types[type]
           : WGPUSamplerBindingType_Undefined;
}

static GPUResult
webgpu_createBindGroupLayout(GPUDevice          *device,
                             GPUBindGroupLayout *layout) {
  WGPUBindGroupLayoutDescriptor descriptor =
    WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupLayoutEntry *nativeEntries;
  const GPUBindGroupLayoutEntry *entries;
  GPUBindGroupLayoutWebGPU      *state;
  GPUDeviceWebGPU               *native;
  GPUResult                      result;
  size_t                         stateSize;
  uint32_t                       count;
  uint32_t                       immutableCount;

  native  = gpu_webgpuDevice(device);
  entries = GPUGetBindGroupLayoutEntries(layout, &count);
  if (!native || !native->device || (count > 0u && !entries)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  immutableCount = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    immutableCount += entries[i].immutableSampler ? 1u : 0u;
  }
  if ((size_t)immutableCount >
      (SIZE_MAX - sizeof(*state)) / sizeof(*state->immutableSamplers)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  stateSize     = sizeof(*state) +
                  (size_t)immutableCount * sizeof(*state->immutableSamplers);
  state         = calloc(1, stateSize);
  nativeEntries = count ? calloc(count, sizeof(*nativeEntries)) : NULL;
  result        = GPU_ERROR_BACKEND_FAILURE;
  if (!state || (count > 0u && !nativeEntries)) {
    free(state);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  state->immutableSamplers = immutableCount ? (WGPUSampler *)(state + 1) : NULL;

  for (uint32_t i = 0u; i < count; i++) {
    WGPUBindGroupLayoutEntry *entry;

    if (entries[i].arrayCount != 1u) {
      goto unsupported;
    }

    entry             = &nativeEntries[i];
    *entry            = (WGPUBindGroupLayoutEntry)
                          WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry->binding    = entries[i].binding;
    entry->visibility = webgpu_shaderStages(entries[i].visibility);
    if (entry->visibility == WGPUShaderStage_None) {
      goto unsupported;
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
        entry->texture.sampleType =
          webgpu_textureSampleType(entries[i].sampledTexture.sampleType);
        entry->texture.viewDimension =
          webgpu_bindingViewDimension(entries[i].sampledTexture.viewType);
        entry->texture.multisampled = entries[i].sampledTexture.multisampled;
        if (entry->texture.sampleType == WGPUTextureSampleType_Undefined ||
            entry->texture.viewDimension ==
              WGPUTextureViewDimension_Undefined) {
          goto unsupported;
        }
        break;
      case GPU_BINDING_STORAGE_TEXTURE:
        entry->storageTexture = (WGPUStorageTextureBindingLayout)
                                  WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry->storageTexture.access =
          webgpu_storageTextureAccess(entries[i].storageTexture.access);
        entry->storageTexture.format =
          gpu_webgpuFormat(entries[i].storageTexture.format);
        entry->storageTexture.viewDimension =
          webgpu_bindingViewDimension(entries[i].storageTexture.viewType);
        if (entry->storageTexture.access ==
              WGPUStorageTextureAccess_Undefined ||
            entry->storageTexture.format == WGPUTextureFormat_Undefined ||
            entry->storageTexture.viewDimension ==
              WGPUTextureViewDimension_Undefined) {
          goto unsupported;
        }
        break;
      case GPU_BINDING_SAMPLER:
        entry->sampler = (WGPUSamplerBindingLayout)
                           WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        entry->sampler.type = webgpu_samplerBindingType(entries[i].sampler.type);
        if (entry->sampler.type == WGPUSamplerBindingType_Undefined) {
          goto unsupported;
        }
        if (entries[i].immutableSampler) {
          WGPUSampler sampler;

          sampler = gpu_webgpuCreateSampler(device,
                                            &entries[i].immutableSamplerDesc,
                                            NULL);
          if (!sampler) {
            goto fail;
          }
          state->immutableSamplers[state->immutableSamplerCount++] = sampler;
        }
        break;
      default:
        goto unsupported;
    }
  }

  descriptor.entryCount = count;
  descriptor.entries    = nativeEntries;
  state->layout = wgpuDeviceCreateBindGroupLayout(native->device,
                                                   &descriptor);
  if (!state->layout) {
    goto fail;
  }
  free(nativeEntries);
  layout->_native = state;
  return GPU_OK;

unsupported:
  result = GPU_ERROR_UNSUPPORTED;

fail:
  for (uint32_t i = 0u; i < state->immutableSamplerCount; i++) {
    wgpuSamplerRelease(state->immutableSamplers[i]);
  }
  free(nativeEntries);
  free(state);
  return result;
}

static void
webgpu_destroyBindGroupLayout(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutWebGPU *state;

  state = layout ? layout->_native : NULL;
  if (!state) {
    return;
  }
  for (uint32_t i = 0u; i < state->immutableSamplerCount; i++) {
    wgpuSamplerRelease(state->immutableSamplers[i]);
  }
  if (state->layout) {
    wgpuBindGroupLayoutRelease(state->layout);
  }
  free(state);
  layout->_native = NULL;
}

static GPUResult
webgpu_createPipelineLayout(GPUDevice *device, GPUPipelineLayout *layout) {
  GPUPipelineLayoutWebGPU state;
  uint32_t                groupCount;
  uint32_t                requiredGroupMask;
  GPUResult               result;

  gpuGetPipelineLayoutGroups(layout, &groupCount);
  requiredGroupMask = groupCount > 0u ? (1u << groupCount) - 1u : 0u;
  result = gpu_webgpuCreatePipelineLayout(device,
                                          layout,
                                          requiredGroupMask,
                                          &state);
  if (result != GPU_OK) {
    return result;
  }
  layout->_native = state.layout;
  state.layout    = NULL;
  gpu_webgpuDestroyPipelineLayout(&state);
  return GPU_OK;
}

static void
webgpu_destroyPipelineLayout(GPUPipelineLayout *layout) {
  if (layout && layout->_native) {
    wgpuPipelineLayoutRelease(layout->_native);
    layout->_native = NULL;
  }
}

GPUResult
gpu_webgpuCreatePipelineLayout(GPUDevice               *device,
                               GPUPipelineLayout       *logicalLayout,
                               uint32_t                 requiredGroupMask,
                               GPUPipelineLayoutWebGPU *outLayout) {
  WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupLayoutDescriptor emptyLayoutInfo =
    WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  WGPUBindGroupDescriptor emptyGroupInfo = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  WGPUBindGroupLayout nativeGroups[GPU_ENCODER_MAX_BIND_GROUPS];
  WGPUBindGroupLayout emptyLayouts[GPU_ENCODER_MAX_BIND_GROUPS];
  GPUBindGroupLayout *const *groups;
  GPUDeviceWebGPU    *native;
  uint32_t            groupCount;
  uint32_t            logicalGroupCount;
  uint32_t            pushConstantSize;
  GPUShaderStageFlags pushConstantStages;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outLayout, 0, sizeof(*outLayout));
  memset(nativeGroups, 0, sizeof(nativeGroups));
  memset(emptyLayouts, 0, sizeof(emptyLayouts));
  native = gpu_webgpuDevice(device);
  groups = gpuGetPipelineLayoutGroups(logicalLayout, &logicalGroupCount);
  gpuGetPipelineLayoutPushConstants(logicalLayout,
                                    &pushConstantSize,
                                    &pushConstantStages);
  if (!native || !native->device || !logicalLayout ||
      (logicalGroupCount > 0u && !groups) ||
      (requiredGroupMask >> GPU_ENCODER_MAX_BIND_GROUPS) != 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  groupCount = 0u;
  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    if ((requiredGroupMask & (1u << i)) != 0u) {
      groupCount = i + 1u;
    }
  }
  if (groupCount > logicalGroupCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (pushConstantSize > 0u) {
    const GPUShaderStageFlags supportedStages =
      GPU_SHADER_STAGE_VERTEX_BIT |
      GPU_SHADER_STAGE_FRAGMENT_BIT |
      GPU_SHADER_STAGE_COMPUTE_BIT;

    if (!native->pushConstantLayout ||
        pushConstantSize > GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT ||
        (pushConstantStages & ~supportedStages) != 0u ||
        logicalGroupCount > GPU_WEBGPU_PUSH_CONSTANT_GROUP ||
        (requiredGroupMask &
          (1u << GPU_WEBGPU_PUSH_CONSTANT_GROUP)) != 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
    groupCount = GPU_WEBGPU_PUSH_CONSTANT_GROUP + 1u;
    outLayout->pushConstantSizeBytes = pushConstantSize;
  }

  for (uint32_t i = 0u; i < groupCount; i++) {
    if (pushConstantSize > 0u &&
        i == GPU_WEBGPU_PUSH_CONSTANT_GROUP) {
      nativeGroups[i] = native->pushConstantLayout;
      continue;
    }
    if ((requiredGroupMask & (1u << i)) != 0u) {
      GPUBindGroupLayoutWebGPU *group;

      group = groups[i] ? groups[i]->_native : NULL;
      nativeGroups[i] = group ? group->layout : NULL;
      if (!nativeGroups[i]) {
        goto fail;
      }
      continue;
    }

    emptyLayouts[i] = wgpuDeviceCreateBindGroupLayout(native->device,
                                                       &emptyLayoutInfo);
    if (!emptyLayouts[i]) {
      goto fail;
    }
    nativeGroups[i]       = emptyLayouts[i];
    emptyGroupInfo.layout = emptyLayouts[i];
    outLayout->emptyGroups[i] = wgpuDeviceCreateBindGroup(native->device,
                                                           &emptyGroupInfo);
    if (!outLayout->emptyGroups[i]) {
      goto fail;
    }
    outLayout->emptyGroupMask |= 1u << i;
  }

  descriptor.bindGroupLayoutCount = groupCount;
  descriptor.bindGroupLayouts     = groupCount ? nativeGroups : NULL;
  outLayout->layout = wgpuDeviceCreatePipelineLayout(native->device,
                                                      &descriptor);
  for (uint32_t i = 0u; i < groupCount; i++) {
    if (emptyLayouts[i]) {
      wgpuBindGroupLayoutRelease(emptyLayouts[i]);
    }
  }
  if (!outLayout->layout) {
    gpu_webgpuDestroyPipelineLayout(outLayout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;

fail:
  for (uint32_t i = 0u; i < groupCount; i++) {
    if (outLayout->emptyGroups[i]) {
      wgpuBindGroupRelease(outLayout->emptyGroups[i]);
    }
    if (emptyLayouts[i]) {
      wgpuBindGroupLayoutRelease(emptyLayouts[i]);
    }
  }
  memset(outLayout, 0, sizeof(*outLayout));
  return GPU_ERROR_BACKEND_FAILURE;
}

void
gpu_webgpuDestroyPipelineLayout(GPUPipelineLayoutWebGPU *layout) {
  if (!layout) {
    return;
  }
  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    if (layout->emptyGroups[i]) {
      wgpuBindGroupRelease(layout->emptyGroups[i]);
    }
  }
  if (layout->layout) {
    wgpuPipelineLayoutRelease(layout->layout);
  }
  memset(layout, 0, sizeof(*layout));
}

void
gpu_webgpuBindRenderEmptyGroups(GPURenderPassEncoder          *pass,
                                const GPUPipelineLayoutWebGPU *layout) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(pass ? pass->_cmdb : NULL);
  if (!command || !command->renderEncoder || !layout) {
    return;
  }
  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    if ((layout->emptyGroupMask & (1u << i)) != 0u) {
      wgpuRenderPassEncoderSetBindGroup(command->renderEncoder,
                                        i,
                                        layout->emptyGroups[i],
                                        0u,
                                        NULL);
    }
  }
}

void
gpu_webgpuBindComputeEmptyGroups(GPUComputePassEncoder         *pass,
                                 const GPUPipelineLayoutWebGPU *layout) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(pass ? pass->_cmdb : NULL);
  if (!command || !command->computeEncoder || !layout) {
    return;
  }
  for (uint32_t i = 0u; i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    if ((layout->emptyGroupMask & (1u << i)) != 0u) {
      wgpuComputePassEncoderSetBindGroup(command->computeEncoder,
                                         i,
                                         layout->emptyGroups[i],
                                         0u,
                                         NULL);
    }
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
  GPUBindGroupLayoutWebGPU *layoutState;
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUDeviceWebGPU        *native;
  uint32_t                count;
  uint32_t                immutableCursor;

  native = gpu_webgpuDevice(device);
  layout = gpuBindGroupGetLayout(group);
  layoutEntries = GPUGetBindGroupLayoutEntries(layout, &count);
  layoutState   = layout ? layout->_native : NULL;
  if (!native || !native->device || !layout || !layoutState ||
      !layoutState->layout || (count > 0u && !layoutEntries)) {
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
  immutableCursor = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    WGPUBindGroupEntry *entry;

    if (!layoutEntries[i].immutableSampler) {
      continue;
    }
    if (immutableCursor >= layoutState->immutableSamplerCount ||
        write.count >= write.capacity) {
      write.valid = false;
      break;
    }
    entry          = &write.entries[write.count++];
    *entry         = (WGPUBindGroupEntry)WGPU_BIND_GROUP_ENTRY_INIT;
    entry->binding = layoutEntries[i].binding;
    entry->sampler = layoutState->immutableSamplers[immutableCursor++];
  }
  if (!gpuForEachBindGroupBinding(group,
                                  webgpu_writeBindGroupEntry,
                                  &write) ||
      !write.valid || write.count != count ||
      immutableCursor != layoutState->immutableSamplerCount) {
    free(entries);
    return GPU_ERROR_UNSUPPORTED;
  }

  descriptor.layout     = layoutState->layout;
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

static bool
webgpu_bindComputeGroup(GPUComputePassEncoder *pass,
                        GPUPipelineLayout      *pipelineLayout,
                        uint32_t                groupIndex,
                        GPUBindGroup           *group,
                        uint32_t                dynamicOffsetCount,
                        const uint32_t         *dynamicOffsets) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(pipelineLayout);
  command = pass ? pass->_priv : NULL;
  if (!command || !command->computeEncoder || !group || !group->_native) {
    return false;
  }

  wgpuComputePassEncoderSetBindGroup(command->computeEncoder,
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
  api->bindComputeGroup       = webgpu_bindComputeGroup;
}
