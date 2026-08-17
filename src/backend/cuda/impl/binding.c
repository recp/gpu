/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

typedef struct GPUCudaBindContext {
  GPUComputePassEncoder *pass;
  GPUBindGroup          *group;
  uint32_t               groupIndex;
  bool                   valid;
} GPUCudaBindContext;

static bool
cuda__paramRangeValid(const GPUComputePipelineCuda *pipeline,
                      uint32_t                      offset,
                      uint32_t                      size) {
  return pipeline && size <= pipeline->paramDataSize &&
         offset <= pipeline->paramDataSize - size;
}

static const GPUBindGroupBindingPriv *
cuda__findBinding(GPUBindGroup *group,
                  uint32_t      binding,
                  uint32_t      arrayIndex,
                  GPUBindKind   kind) {
  GPUBindGroupPriv *priv;

  priv = group ? group->_priv : NULL;
  if (!priv) {
    return NULL;
  }
  for (uint32_t i = 0u; i < priv->count; i++) {
    const GPUBindGroupBindingPriv *item;

    item = &priv->bindings[i];
    if (item->binding == binding && item->arrayIndex == arrayIndex &&
        item->kind == kind) {
      return item;
    }
  }
  return NULL;
}

static GPUBindGroup *
cuda__boundGroup(GPUComputePassEncoder *pass,
                 uint32_t               activeGroupIndex,
                 GPUBindGroup          *activeGroup,
                 uint32_t               groupIndex) {
  if (!pass || groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS) {
    return NULL;
  }
  return groupIndex == activeGroupIndex
           ? activeGroup
           : pass->_boundGroups[groupIndex];
}

static uint32_t
cuda__findParam(const GPUComputePipelineCuda *pipeline,
                uint32_t                      groupIndex,
                uint32_t                      binding,
                uint32_t                      arrayIndex,
                GPUShaderPTXParamKind         kind) {
  if (!pipeline) {
    return UINT32_MAX;
  }
  for (uint32_t i = 0u; i < pipeline->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &pipeline->params[i];
    if (param->kind == kind && param->groupIndex == groupIndex &&
        param->binding == binding && param->arrayIndex == arrayIndex) {
      return i;
    }
  }
  return UINT32_MAX;
}

static void
cuda__bindTextureMetadata(GPUCommandCuda                  *command,
                          const GPUShaderPTXParamInfo      *textureParam,
                          const GPUTextureView             *view) {
  GPUCudaTextureMetadata metadata;
  const GPUTexture       *texture;

  texture = view ? view->_texture : NULL;
  if (!command || !command->pipeline || !textureParam || !view || !texture) {
    return;
  }
  metadata.mipLevelCount   = view->mipLevelCount;
  metadata.arrayLayerCount = view->arrayLayerCount;
  metadata.sampleCount     = texture->sampleCount;
  metadata.reserved        = 0u;
  for (uint32_t i = 0u; i < command->pipeline->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &command->pipeline->params[i];
    if (param->kind != GPUShaderPTXParamTextureMetadata ||
        param->groupIndex != textureParam->groupIndex ||
        param->binding != textureParam->binding ||
        param->arrayIndex != textureParam->arrayIndex) {
      continue;
    }
    if (!cuda__paramRangeValid(command->pipeline,
                               param->dataOffset,
                               sizeof(metadata))) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
      return;
    }
    memcpy(command->boundParams + param->dataOffset,
           &metadata,
           sizeof(metadata));
    command->boundParamMask[i / 64u] |= UINT64_C(1) << (i % 64u);
  }
}

void
cuda_setComputeBuffer(GPUComputePassEncoder *encoder,
                      GPUBuffer             *buffer,
                      uint64_t               offset,
                      uint32_t               index) {
  GPUCommandCuda              *command;
  GPUBufferCuda               *native;
  const GPUShaderPTXParamInfo *param;
  GPUBufferUsageFlags          usage;
  CUdeviceptr                  address;

  command = encoder ? encoder->_priv : NULL;
  native  = buffer ? buffer->_priv : NULL;
  param   = command && command->pipeline &&
            index < command->pipeline->paramCount
              ? &command->pipeline->params[index]
              : NULL;
  usage   = param && param->bindingType == GPU_BINDING_UNIFORM_BUFFER
              ? GPU_BUFFER_USAGE_UNIFORM
              : GPU_BUFFER_USAGE_STORAGE;
  if (!command || !native || !param ||
      param->kind != GPUShaderPTXParamBuffer ||
      buffer->device != encoder->_device ||
      !gpuBufferHasUsage(buffer, usage) ||
      !gpuBufferOffsetValid(buffer, offset) || offset == buffer->sizeBytes ||
      native->address > UINT64_MAX - offset ||
      !cuda__paramRangeValid(command->pipeline,
                             param->dataOffset,
                             sizeof(address))) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }
  address = native->address + offset;
  memcpy(command->boundParams + param->dataOffset,
         &address,
         sizeof(address));
  command->boundParamMask[index / 64u] |= UINT64_C(1) << (index % 64u);
}

void
cuda_setComputeTexture(GPUComputePassEncoder *encoder,
                       GPUTextureView        *view,
                       uint32_t               index) {
  GPUCommandCuda              *command;
  GPUTextureViewCuda          *native;
  GPUTexture                  *texture;
  GPUTextureCuda              *textureNative;
  const GPUShaderPTXParamInfo *param;
  CUsurfObject                 surface;

  command       = encoder ? encoder->_priv : NULL;
  native        = view ? view->_priv : NULL;
  texture       = view ? view->_texture : NULL;
  textureNative = texture ? texture->_priv : NULL;
  param         = command && command->pipeline &&
                  index < command->pipeline->paramCount
                    ? &command->pipeline->params[index]
                    : NULL;
  if (!command || !native || !native->surface || !texture ||
      !textureNative || !param ||
      param->kind != GPUShaderPTXParamSurface ||
      param->bindingType != GPU_BINDING_STORAGE_TEXTURE ||
      texture->device != encoder->_device ||
      (texture->usage & GPU_TEXTURE_USAGE_STORAGE) == 0u ||
      (textureNative->format.flags & GPU_CUDA_FORMAT_STORAGE_BIT) == 0u ||
      !cuda_textureStorageViewSupported(view->viewType) ||
      !cuda__paramRangeValid(command->pipeline,
                             param->dataOffset,
                             sizeof(surface))) {
    if (command) {
      command->recordResult = GPU_ERROR_INVALID_ARGUMENT;
    }
    return;
  }

  surface = native->surface;
  memcpy(command->boundParams + param->dataOffset,
         &surface,
         sizeof(surface));
  command->boundParamMask[index / 64u] |= UINT64_C(1) << (index % 64u);
  cuda__bindTextureMetadata(command, param, view);
}

static void
cuda__bindGroupResource(void                           *ctx,
                        const GPUBindGroupBindingView *binding) {
  GPUCudaBindContext *bind;
  GPUCommandCuda     *command;
  GPUBindGroupPriv   *group;
  GPUBindGroupLayoutPriv *layout;
  uint32_t            logicalBinding;
  uint32_t            paramIndex;

  bind    = ctx;
  command = bind && bind->pass ? bind->pass->_priv : NULL;
  if (!bind || !binding || !bind->valid ||
      (binding->visibility & GPU_SHADER_STAGE_COMPUTE_BIT) == 0u) {
    return;
  }
  group  = bind->group ? bind->group->_priv : NULL;
  layout = group && group->layout ? group->layout->_priv : NULL;
  if (!command || !command->pipeline || !layout ||
      binding->layoutEntryIndex >= layout->count) {
    bind->valid = false;
    return;
  }
  logicalBinding = layout->entries[binding->layoutEntryIndex].binding;
  if (binding->kind == GPUBindKindBuffer && binding->buffer) {
    paramIndex = cuda__findParam(command->pipeline,
                                 bind->groupIndex,
                                 logicalBinding,
                                 binding->arrayIndex,
                                 GPUShaderPTXParamBuffer);
    if (paramIndex == UINT32_MAX) {
      return;
    }
    cuda_setComputeBuffer(bind->pass,
                          binding->buffer,
                          binding->offset,
                          paramIndex);
  } else if (binding->kind == GPUBindKindTexture &&
             binding->bindingType == GPU_BINDING_STORAGE_TEXTURE &&
             binding->textureView) {
    paramIndex = cuda__findParam(command->pipeline,
                                 bind->groupIndex,
                                 logicalBinding,
                                 binding->arrayIndex,
                                 GPUShaderPTXParamSurface);
    if (paramIndex == UINT32_MAX) {
      return;
    }
    cuda_setComputeTexture(bind->pass,
                           binding->textureView,
                           paramIndex);
  } else if ((binding->kind == GPUBindKindTexture &&
              binding->bindingType == GPU_BINDING_SAMPLED_TEXTURE) ||
             (binding->kind == GPUBindKindSampler &&
              binding->bindingType == GPU_BINDING_SAMPLER)) {
    return;
  } else {
    bind->valid = false;
  }
  if (!command || command->recordResult != GPU_OK) {
    bind->valid = false;
  }
}

static bool
cuda__resolveSampledTexture(GPUComputePassEncoder          *pass,
                            uint32_t                        activeGroupIndex,
                            GPUBindGroup                   *activeGroup,
                            uint32_t                        paramIndex,
                            const GPUShaderPTXParamInfo    *param) {
  GPUComputePipelineCuda       *pipeline;
  GPUCommandCuda               *command;
  const GPUBindGroupBindingPriv *textureBinding;
  const GPUBindGroupBindingPriv *samplerBinding;
  GPUBindGroup                 *textureGroup;
  GPUBindGroup                 *samplerGroup;
  const GPUSamplerCuda         *samplerNative;
  const CUDA_TEXTURE_DESC      *desc;
  CUtexObject                   textureObject;
  GPUResult                     result;

  command  = pass ? pass->_priv : NULL;
  pipeline = command ? command->pipeline : NULL;
  if (!pipeline || !param || paramIndex >= pipeline->paramCount ||
      param->kind != GPUShaderPTXParamSampledTexture) {
    return false;
  }

  textureGroup = cuda__boundGroup(pass,
                                  activeGroupIndex,
                                  activeGroup,
                                  param->groupIndex);
  textureBinding = cuda__findBinding(textureGroup,
                                     param->binding,
                                     param->arrayIndex,
                                     GPUBindKindTexture);
  if (!textureBinding || !textureBinding->textureView) {
    return true;
  }

  if (param->staticSamplerId != UINT32_MAX) {
    if (param->staticSamplerId >= pipeline->staticSamplerCount) {
      return false;
    }
    desc = &pipeline->staticSamplers[param->staticSamplerId];
  } else {
    samplerGroup = cuda__boundGroup(pass,
                                    activeGroupIndex,
                                    activeGroup,
                                    param->samplerGroupIndex);
    samplerBinding = cuda__findBinding(samplerGroup,
                                       param->samplerBinding,
                                       param->samplerArrayIndex,
                                       GPUBindKindSampler);
    if (!samplerBinding || !samplerBinding->sampler) {
      return true;
    }
    samplerNative = samplerBinding->sampler->_priv;
    if (!samplerNative) {
      return false;
    }
    desc = &samplerNative->desc;
  }

  result = cuda_getTextureObject(textureBinding->textureView,
                                 desc,
                                 false,
                                 &textureObject);
  if (result != GPU_OK ||
      !cuda__paramRangeValid(pipeline,
                             param->dataOffset,
                             sizeof(textureObject))) {
    command->recordResult = result != GPU_OK
                              ? result
                              : GPU_ERROR_INVALID_ARGUMENT;
    return false;
  }
  memcpy(command->boundParams + param->dataOffset,
         &textureObject,
         sizeof(textureObject));
  command->boundParamMask[paramIndex / 64u] |=
    UINT64_C(1) << (paramIndex % 64u);
  cuda__bindTextureMetadata(command, param, textureBinding->textureView);
  return command->recordResult == GPU_OK;
}

static bool
cuda__resolveTexture(GPUComputePassEncoder       *pass,
                     uint32_t                     activeGroupIndex,
                     GPUBindGroup                *activeGroup,
                     uint32_t                     paramIndex,
                     const GPUShaderPTXParamInfo *param) {
  GPUComputePipelineCuda        *pipeline;
  GPUCommandCuda                *command;
  const GPUBindGroupBindingPriv *binding;
  GPUBindGroup                  *group;
  const CUDA_TEXTURE_DESC       *desc;
  CUtexObject                    textureObject;
  GPUResult                      result;

  command  = pass ? pass->_priv : NULL;
  pipeline = command ? command->pipeline : NULL;
  if (!pipeline || !param || paramIndex >= pipeline->paramCount ||
      param->kind != GPUShaderPTXParamTexture) {
    return false;
  }

  group = cuda__boundGroup(pass,
                           activeGroupIndex,
                           activeGroup,
                           param->groupIndex);
  binding = cuda__findBinding(group,
                              param->binding,
                              param->arrayIndex,
                              GPUBindKindTexture);
  if (!binding || !binding->textureView) {
    return true;
  }

  desc   = cuda_exactTextureDesc();
  result = cuda_getTextureObject(binding->textureView,
                                 desc,
                                 true,
                                 &textureObject);
  if (result != GPU_OK ||
      !cuda__paramRangeValid(pipeline,
                             param->dataOffset,
                             sizeof(textureObject))) {
    command->recordResult = result != GPU_OK
                              ? result
                              : GPU_ERROR_INVALID_ARGUMENT;
    return false;
  }
  memcpy(command->boundParams + param->dataOffset,
         &textureObject,
         sizeof(textureObject));
  command->boundParamMask[paramIndex / 64u] |=
    UINT64_C(1) << (paramIndex % 64u);
  cuda__bindTextureMetadata(command, param, binding->textureView);
  return command->recordResult == GPU_OK;
}

static bool
cuda__resolveTextureMetadata(GPUComputePassEncoder       *pass,
                             uint32_t                     activeGroupIndex,
                             GPUBindGroup                *activeGroup,
                             uint32_t                     paramIndex,
                             const GPUShaderPTXParamInfo *param) {
  GPUComputePipelineCuda        *pipeline;
  GPUCommandCuda                *command;
  const GPUBindGroupBindingPriv *binding;
  GPUBindGroup                  *group;

  command  = pass ? pass->_priv : NULL;
  pipeline = command ? command->pipeline : NULL;
  if (!pipeline || !param || paramIndex >= pipeline->paramCount ||
      param->kind != GPUShaderPTXParamTextureMetadata) {
    return false;
  }

  group = cuda__boundGroup(pass,
                           activeGroupIndex,
                           activeGroup,
                           param->groupIndex);
  binding = cuda__findBinding(group,
                              param->binding,
                              param->arrayIndex,
                              GPUBindKindTexture);
  if (!binding || !binding->textureView) {
    return true;
  }

  cuda__bindTextureMetadata(command, param, binding->textureView);
  return command->recordResult == GPU_OK;
}

bool
cuda_bindComputeGroup(GPUComputePassEncoder *pass,
                      GPUPipelineLayout     *pipelineLayout,
                      uint32_t               groupIndex,
                      GPUBindGroup          *group,
                      uint32_t               dynamicOffsetCount,
                      const uint32_t        *dynamicOffsets) {
  GPUCudaBindContext      bind;
  GPUCommandCuda         *command;
  GPUComputePipelineCuda *pipeline;

  command  = pass ? pass->_priv : NULL;
  pipeline = command ? command->pipeline : NULL;
  if (!pass || !pipelineLayout || !group || !command || !pipeline ||
      groupIndex >= GPU_ENCODER_MAX_BIND_GROUPS) {
    return false;
  }

  bind.pass       = pass;
  bind.group      = group;
  bind.groupIndex = groupIndex;
  bind.valid      = true;
  for (uint32_t i = 0u; i < pipeline->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &pipeline->params[i];
    if (param->kind == GPUShaderPTXParamTexture &&
        param->groupIndex == groupIndex) {
      command->boundParamMask[i / 64u] &=
        ~(UINT64_C(1) << (i % 64u));
    } else if (param->kind == GPUShaderPTXParamSampledTexture &&
        (param->groupIndex == groupIndex ||
         param->samplerGroupIndex == groupIndex)) {
      command->boundParamMask[i / 64u] &=
        ~(UINT64_C(1) << (i % 64u));
    } else if (param->kind == GPUShaderPTXParamTextureMetadata &&
               param->groupIndex == groupIndex) {
      command->boundParamMask[i / 64u] &=
        ~(UINT64_C(1) << (i % 64u));
    }
  }
  if (!gpuForEachBindGroupBindingWithDynamicOffsets(pipelineLayout,
                                                     groupIndex,
                                                     group,
                                                     dynamicOffsetCount,
                                                     dynamicOffsets,
                                                     cuda__bindGroupResource,
                                                     &bind) ||
      !bind.valid) {
    return false;
  }
  for (uint32_t i = 0u; i < pipeline->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &pipeline->params[i];
    if (param->kind == GPUShaderPTXParamTexture &&
        param->groupIndex == groupIndex &&
        !cuda__resolveTexture(pass, groupIndex, group, i, param)) {
      return false;
    }
    if (param->kind == GPUShaderPTXParamSampledTexture &&
        (param->groupIndex == groupIndex ||
         param->samplerGroupIndex == groupIndex) &&
        !cuda__resolveSampledTexture(pass,
                                     groupIndex,
                                     group,
                                     i,
                                     param)) {
      return false;
    }
    if (param->kind == GPUShaderPTXParamTextureMetadata &&
        param->groupIndex == groupIndex &&
        (command->boundParamMask[i / 64u] &
         (UINT64_C(1) << (i % 64u))) == 0u &&
        !cuda__resolveTextureMetadata(pass, groupIndex, group, i, param)) {
      return false;
    }
  }
  return command->recordResult == GPU_OK;
}

void
cuda_rebindComputeGroups(GPUComputePassEncoder *pass) {
  for (uint32_t i = 0u; pass && i < GPU_ENCODER_MAX_BIND_GROUPS; i++) {
    GPUBindGroup *group;

    group = pass->_boundGroups[i];
    if (!group) {
      continue;
    }
    if (cuda_bindComputeGroup(pass,
                              pass->_pipelineLayout,
                              i,
                              group,
                              pass->_boundDynamicOffsetCounts[i],
                              pass->_boundDynamicOffsets[i])) {
      gpuFrameStatsRecordBindEmission(pass->_stats);
      continue;
    }

    pass->_boundGroups[i]              = NULL;
    pass->_boundGroupLayouts[i]        = NULL;
    pass->_boundDynamicOffsetCounts[i] = 0u;
  }
}

void
cuda_initDescriptor(GPUApiDescriptor *api) {
  api->bindComputeGroup = cuda_bindComputeGroup;
}
