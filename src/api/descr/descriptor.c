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

#include "../../common.h"
#include "../render/rce_internal.h"
#include "descriptor_internal.h"
#include "../usl_target.h"

#define GPU_USL_BYTECODE_VERSION 2u
#define GPU_PUSH_CONSTANT_MAX_SIZE_BYTES 4096u

typedef struct GPUBindGroupLayoutPriv {
  uint32_t count;
  GPUBindGroupLayoutEntry *entries;
} GPUBindGroupLayoutPriv;

typedef struct GPUBindGroupBindingPriv {
  GPUBindStage stage;
  uint32_t binding;
  GPUBindKind kind;
  GPUBuffer *buffer;
  GPUTextureView *textureView;
  GPUSampler *sampler;
  uint64_t offset;
  uint64_t size;
} GPUBindGroupBindingPriv;

typedef struct GPUBindGroupPriv {
  GPUBindGroupLayout *layout;
  uint32_t count;
  GPUBindGroupBindingPriv *bindings;
} GPUBindGroupPriv;

typedef struct GPUPipelineLayoutPriv {
  uint32_t bindGroupLayoutCount;
  GPUBindGroupLayout **bindGroupLayouts;
  uint32_t pushConstantSizeBytes;
  GPUShaderStageFlags pushConstantStages;
} GPUPipelineLayoutPriv;

typedef struct GPUBindRenderContext {
  GPURenderPassEncoder *pass;
} GPUBindRenderContext;

static GPUBindGroupLayoutPriv *
gpu_layoutPriv(GPUBindGroupLayout *layout) {
  return layout ? layout->_priv : NULL;
}

static GPUBindGroupPriv *
gpu_groupPriv(GPUBindGroup *group) {
  return group ? group->_priv : NULL;
}

static GPUPipelineLayoutPriv *
gpu_pipelineLayoutPriv(GPUPipelineLayout *layout) {
  return layout ? layout->_priv : NULL;
}

GPU_HIDE
void
gpuGetPipelineLayoutPushConstants(GPUPipelineLayout *layout,
                                  uint32_t *outSizeBytes,
                                  GPUShaderStageFlags *outStages) {
  GPUPipelineLayoutPriv *priv;

  priv = gpu_pipelineLayoutPriv(layout);
  if (outSizeBytes) {
    *outSizeBytes = priv ? priv->pushConstantSizeBytes : 0u;
  }
  if (outStages) {
    *outStages = priv ? priv->pushConstantStages : 0u;
  }
}

static int
gpu_bindStageIsValid(GPUBindStage stage);

static int
gpu_bindKindIsValid(GPUBindKind kind);

static int
gpu_u64Add(uint64_t a, uint64_t b, uint64_t *out) {
  if (!out || b > UINT64_MAX - a) {
    return 0;
  }

  *out = a + b;
  return 1;
}

static int
gpu_samplerDescIsValid(const GPUSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  return (desc->minFilter == GPU_FILTER_NEAREST ||
          desc->minFilter == GPU_FILTER_LINEAR) &&
         (desc->magFilter == GPU_FILTER_NEAREST ||
          desc->magFilter == GPU_FILTER_LINEAR) &&
         (desc->mipFilter == GPU_MIP_FILTER_NEAREST ||
          desc->mipFilter == GPU_MIP_FILTER_LINEAR) &&
         (desc->addressU == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressU == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressV == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressV == GPU_ADDRESS_MODE_CLAMP_TO_EDGE) &&
         (desc->addressW == GPU_ADDRESS_MODE_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_MIRRORED_REPEAT ||
          desc->addressW == GPU_ADDRESS_MODE_CLAMP_TO_EDGE);
}

static int
gpu_stageFromVisibility(GPUShaderStageFlags visibility, GPUBindStage *outStage) {
  if (!outStage) {
    return 0;
  }

  if (visibility == 0 ||
      (visibility & ~(GPU_SHADER_STAGE_VERTEX_BIT |
                      GPU_SHADER_STAGE_FRAGMENT_BIT |
                      GPU_SHADER_STAGE_COMPUTE_BIT)) != 0) {
    return 0;
  }

  if ((visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0) {
    *outStage = GPUBindStageVertex;
    return 1;
  }
  if ((visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
    *outStage = GPUBindStageFragment;
    return 1;
  }

  *outStage = GPUBindStageCompute;
  return 1;
}

static int
gpu_kindFromBindingType(GPUBindingType type, GPUBindKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (type) {
    case GPU_BINDING_UNIFORM_BUFFER:
    case GPU_BINDING_STORAGE_BUFFER:
      *outKind = GPUBindKindBuffer;
      return 1;
    case GPU_BINDING_SAMPLED_TEXTURE:
    case GPU_BINDING_STORAGE_TEXTURE:
      *outKind = GPUBindKindTexture;
      return 1;
    case GPU_BINDING_SAMPLER:
      *outKind = GPUBindKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_bindingTypeFromKind(GPUBindKind kind, GPUBindingType *outType) {
  if (!outType) {
    return 0;
  }

  switch (kind) {
    case GPUBindKindBuffer:
      *outType = GPU_BINDING_UNIFORM_BUFFER;
      return 1;
    case GPUBindKindTexture:
      *outType = GPU_BINDING_SAMPLED_TEXTURE;
      return 1;
    case GPUBindKindSampler:
      *outType = GPU_BINDING_SAMPLER;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_visibilityFromStage(GPUBindStage stage, GPUShaderStageFlags *outVisibility) {
  if (!outVisibility) {
    return 0;
  }

  switch (stage) {
    case GPUBindStageVertex:
      *outVisibility = GPU_SHADER_STAGE_VERTEX_BIT;
      return 1;
    case GPUBindStageFragment:
      *outVisibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
      return 1;
    case GPUBindStageCompute:
      *outVisibility = GPU_SHADER_STAGE_COMPUTE_BIT;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_normalizeLayoutEntry(const GPUBindGroupLayoutEntry *src,
                         GPUBindGroupLayoutEntry *dst) {
  GPUBindStage stage;
  GPUBindKind kind;
  GPUBindingType type;
  GPUShaderStageFlags visibility;

  if (!src || !dst) {
    return 0;
  }

  *dst = *src;
  visibility = src->visibility;
  type = src->bindingType;

  if (visibility == 0) {
    if (!gpu_visibilityFromStage(src->stage, &visibility)) {
      return 0;
    }
  }
  if (!gpu_stageFromVisibility(visibility, &stage)) {
    return 0;
  }

  if (!gpu_kindFromBindingType(type, &kind)) {
    if (!gpu_bindKindIsValid(src->kind) ||
        !gpu_bindingTypeFromKind(src->kind, &type)) {
      return 0;
    }
    kind = src->kind;
  }

  if (src->arrayCount == 0) {
    dst->arrayCount = 1;
  }

  dst->visibility = visibility;
  dst->bindingType = type;
  dst->stage = stage;
  dst->kind = kind;
  return 1;
}

static int
gpu_bindStageIsValid(GPUBindStage stage) {
  return stage == GPUBindStageVertex ||
         stage == GPUBindStageFragment ||
         stage == GPUBindStageCompute;
}

static int
gpu_bindKindIsValid(GPUBindKind kind) {
  return kind == GPUBindKindBuffer ||
         kind == GPUBindKindTexture ||
         kind == GPUBindKindSampler;
}

static int
gpu_layoutEntryDuplicateExists(const GPUBindGroupLayoutEntry *entries,
                               uint32_t count,
                               GPUBindStage stage,
                               GPUBindKind kind,
                               uint32_t binding) {
  uint32_t i;

  if (!entries) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    GPUBindGroupLayoutEntry entry;

    if (gpu_normalizeLayoutEntry(&entries[i], &entry) &&
        entry.stage == stage &&
        entry.kind == kind &&
        entry.binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
gpu_validateLayoutEntries(const GPUBindGroupLayoutEntry *entries,
                          uint32_t count) {
  uint32_t i;

  if (!entries && count > 0) {
    return 0;
  }

  for (i = 0; i < count; i++) {
    GPUBindGroupLayoutEntry entry;

    if (!gpu_normalizeLayoutEntry(&entries[i], &entry) ||
        !gpu_bindStageIsValid(entry.stage) ||
        !gpu_bindKindIsValid(entry.kind) ||
        (entry.hasDynamicOffset && entry.kind != GPUBindKindBuffer) ||
        (entry.immutableSampler &&
         (entry.kind != GPUBindKindSampler ||
          !gpu_samplerDescIsValid(&entry.immutableSamplerDesc))) ||
        gpu_layoutEntryDuplicateExists(entries,
                                       i,
                                       entry.stage,
                                       entry.kind,
                                       entry.binding)) {
      return 0;
    }
  }

  return 1;
}

GPU_EXPORT
int
GPUUSLStaticSamplerDescIsValid(const GPUUSLStaticSamplerDesc *desc) {
  if (!desc) {
    return 0;
  }

  if (desc->minFilter > GPUUSLSamplerFilterLinear ||
      desc->magFilter > GPUUSLSamplerFilterLinear ||
      desc->mipFilter > GPUUSLSamplerFilterLinear) {
    return 0;
  }

  if (desc->addressMode > GPUUSLSamplerAddressClampToBorder ||
      desc->coordSpace > GPUUSLSamplerCoordPixel ||
      desc->compareFunc > GPUUSLSamplerCompareAlways ||
      desc->hasCompare > 1u ||
      desc->maxAnisotropy > 255u) {
    return 0;
  }

  return 1;
}

static int
gpu_bindGroupEntryHasResource(const GPUBindGroupLayoutEntry *layoutEntry,
                              const GPUBindGroupEntry *entry) {
  if (!layoutEntry) {
    return 0;
  }

  if (!entry) {
    return layoutEntry->immutableSampler &&
           layoutEntry->kind == GPUBindKindSampler;
  }

  switch (layoutEntry->kind) {
    case GPUBindKindBuffer:
      return entry->buffer.buffer != NULL;
    case GPUBindKindTexture:
      return entry->textureView != NULL;
    case GPUBindKindSampler:
      return entry->sampler != NULL;
    default:
      return 0;
  }
}

static int
gpu_bindGroupEntryResourceKind(const GPUBindGroupEntry *entry,
                               GPUBindKind *outKind) {
  GPUBindKind kind;
  uint32_t resourceCount;

  if (!entry || !outKind) {
    return 0;
  }

  kind = GPUBindKindBuffer;
  resourceCount = 0;
  if (entry->buffer.buffer) {
    kind = GPUBindKindBuffer;
    resourceCount++;
  }
  if (entry->textureView) {
    kind = GPUBindKindTexture;
    resourceCount++;
  }
  if (entry->sampler) {
    kind = GPUBindKindSampler;
    resourceCount++;
  }

  if (resourceCount != 1) {
    return 0;
  }

  *outKind = kind;
  return 1;
}

static int
gpu_bindGroupEntryMatchesLayout(const GPUBindGroupLayoutEntry *layoutEntry,
                                const GPUBindGroupEntry *entry) {
  GPUBindKind resourceKind;

  if (!layoutEntry || !entry ||
      entry->binding != layoutEntry->binding ||
      !gpu_bindGroupEntryResourceKind(entry, &resourceKind) ||
      resourceKind != layoutEntry->kind) {
    return 0;
  }

  if (layoutEntry->immutableSampler &&
      layoutEntry->kind == GPUBindKindSampler) {
    return 0;
  }
  if (entry->stage != 0 &&
      entry->stage != layoutEntry->stage) {
    return 0;
  }
  if (entry->kind != 0 &&
      entry->kind != layoutEntry->kind) {
    return 0;
  }
  if (layoutEntry->kind == GPUBindKindBuffer &&
      entry->bindingType != layoutEntry->bindingType) {
    return 0;
  }

  return gpu_bindGroupEntryHasResource(layoutEntry, entry);
}

static const GPUBindGroupEntry *
gpu_findBindGroupEntry(const GPUBindGroupEntry *entries,
                       uint32_t count,
                       const GPUBindGroupLayoutEntry *layoutEntry) {
  uint32_t i;

  if (!entries || !layoutEntry) {
    return NULL;
  }

  for (i = 0; i < count; i++) {
    if (gpu_bindGroupEntryMatchesLayout(layoutEntry, &entries[i])) {
      return &entries[i];
    }
  }

  return NULL;
}

static int
gpu_bindGroupLayoutEntryNeedsBinding(const GPUBindGroupLayoutEntry *entry) {
  if (!entry) {
    return 0;
  }

  return !(entry->immutableSampler && entry->kind == GPUBindKindSampler);
}

static GPUResult
gpu_validateBindGroupEntries(const GPUBindGroupLayoutPriv *layoutPriv,
                             const GPUBindGroupEntry *entries,
                             uint32_t count) {
  uint8_t *matched;

  if (!layoutPriv || (!entries && count > 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  matched = NULL;
  if (layoutPriv->count > 0) {
    matched = calloc(layoutPriv->count, sizeof(*matched));
    if (!matched) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    uint32_t matchCount;
    uint32_t matchIndex;

    matchCount = 0;
    matchIndex = UINT32_MAX;
    for (uint32_t j = 0; j < layoutPriv->count; j++) {
      if (!gpu_bindGroupEntryMatchesLayout(&layoutPriv->entries[j], &entries[i])) {
        continue;
      }
      matchCount++;
      matchIndex = j;
    }

    if (matchCount != 1 || matched[matchIndex]) {
      free(matched);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    matched[matchIndex] = 1u;
  }

  for (uint32_t i = 0; i < layoutPriv->count; i++) {
    if (gpu_bindGroupLayoutEntryNeedsBinding(&layoutPriv->entries[i]) &&
        !matched[i]) {
      free(matched);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  free(matched);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayout(GPUDevice *device,
                         const GPUBindGroupLayoutCreateInfo *info,
                         GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayout *layout;
  GPUBindGroupLayoutPriv *priv;
  const GPUBindGroupLayoutEntry *entries;
  uint32_t count;

  (void)device;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  entries = info->pEntries;
  count = info->entryCount;
  if (!gpu_validateLayoutEntries(entries, count)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)count > SIZE_MAX / sizeof(*priv->entries)) {
    free(priv);
    free(layout);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (count > 0) {
    priv->entries = calloc(count, sizeof(*priv->entries));
    if (!priv->entries) {
      free(priv);
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t i = 0; i < count; i++) {
      if (!gpu_normalizeLayoutEntry(&entries[i], &priv->entries[i])) {
        free(priv->entries);
        free(priv);
        free(layout);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
    }
  }

  priv->count = count;
  layout->_priv = priv;
  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
const GPUBindGroupLayoutEntry *
GPUGetBindGroupLayoutEntries(GPUBindGroupLayout *layout, uint32_t *outCount) {
  GPUBindGroupLayoutPriv *priv;

  priv = gpu_layoutPriv(layout);
  if (outCount) {
    *outCount = priv ? priv->count : 0;
  }

  return priv ? priv->entries : NULL;
}

GPU_EXPORT
void
GPUDestroyBindGroupLayout(GPUBindGroupLayout *layout) {
  GPUBindGroupLayoutPriv *priv;

  if (!layout) {
    return;
  }

  priv = gpu_layoutPriv(layout);
  if (priv) {
    free(priv->entries);
    free(priv);
  }

  free(layout);
}

GPU_EXPORT
GPUResult
GPUCreatePipelineLayout(GPUDevice *device,
                        const GPUPipelineLayoutCreateInfo *info,
                        GPUPipelineLayout **outLayout) {
  GPUPipelineLayout *layout;
  GPUPipelineLayoutPriv *priv;

  (void)device;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->bindGroupLayoutCount > 0 && !info->ppBindGroupLayouts) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->pushConstantSizeBytes > 0 && info->pushConstantStages == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->pushConstantSizeBytes > GPU_PUSH_CONSTANT_MAX_SIZE_BYTES) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  priv = calloc(1, sizeof(*priv));
  if (!layout || !priv) {
    free(layout);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)info->bindGroupLayoutCount > SIZE_MAX / sizeof(*priv->bindGroupLayouts)) {
    free(priv);
    free(layout);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (info->bindGroupLayoutCount > 0) {
    priv->bindGroupLayouts = calloc(info->bindGroupLayoutCount,
                                    sizeof(*priv->bindGroupLayouts));
    if (!priv->bindGroupLayouts) {
      free(priv);
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    for (uint32_t i = 0; i < info->bindGroupLayoutCount; i++) {
      if (!info->ppBindGroupLayouts[i]) {
        free(priv->bindGroupLayouts);
        free(priv);
        free(layout);
        return GPU_ERROR_INVALID_ARGUMENT;
      }
      priv->bindGroupLayouts[i] = info->ppBindGroupLayouts[i];
    }
  }

  priv->bindGroupLayoutCount = info->bindGroupLayoutCount;
  priv->pushConstantSizeBytes = info->pushConstantSizeBytes;
  priv->pushConstantStages = info->pushConstantStages;
  layout->_priv = priv;
  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyPipelineLayout(GPUPipelineLayout *layout) {
  GPUPipelineLayoutPriv *priv;

  if (!layout) {
    return;
  }

  priv = gpu_pipelineLayoutPriv(layout);
  if (priv) {
    free(priv->bindGroupLayouts);
    free(priv);
  }

  free(layout);
}

static uint32_t
gpu_reflectionLayoutCount(const GPUShaderReflection *reflection) {
  uint32_t maxSet;

  if (!reflection || reflection->resourceCount == 0u || !reflection->pResources) {
    return 0u;
  }

  maxSet = 0u;
  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (reflection->pResources[i].setIndex > maxSet) {
      maxSet = reflection->pResources[i].setIndex;
    }
  }

  return maxSet + 1u;
}

static uint32_t
gpu_reflectionResourceCountForSet(const GPUShaderReflection *reflection,
                                  uint32_t setIndex) {
  uint32_t count;

  if (!reflection || !reflection->pResources) {
    return 0u;
  }

  count = 0u;
  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (reflection->pResources[i].setIndex == setIndex) {
      count++;
    }
  }

  return count;
}

static GPUResult
gpu_createLayoutForReflectionSet(GPUDevice *device,
                                 const GPUShaderReflection *reflection,
                                 uint32_t setIndex,
                                 GPUBindGroupLayout **outLayout) {
  GPUBindGroupLayoutCreateInfo info;
  GPUBindGroupLayoutEntry *entries;
  uint32_t entryCount;
  uint32_t cursor;
  GPUResult rc;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  entryCount = gpu_reflectionResourceCountForSet(reflection, setIndex);
  entries = NULL;
  if (entryCount > 0u) {
    entries = calloc(entryCount, sizeof(*entries));
    if (!entries) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  cursor = 0u;
  for (uint32_t i = 0; reflection && i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection->pResources[i];
    if (resource->setIndex != setIndex) {
      continue;
    }

    entries[cursor].binding = resource->binding;
    entries[cursor].bindingType = resource->bindingType;
    entries[cursor].visibility = resource->visibility;
    entries[cursor].arrayCount = resource->arrayCount ? resource->arrayCount : 1u;
    entries[cursor].hasDynamicOffset = resource->hasDynamicOffset;
    cursor++;
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.entryCount = entryCount;
  info.pEntries = entries;
  rc = GPUCreateBindGroupLayout(device, &info, outLayout);
  free(entries);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateBindGroupLayoutsFromReflection(GPUDevice *device,
                                        const GPUShaderLibrary *library,
                                        uint32_t *inoutLayoutCount,
                                        GPUBindGroupLayout **outLayouts) {
  GPUShaderReflection reflection;
  uint32_t requiredCount;
  GPUResult rc;

  if (!library || !inoutLayoutCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&reflection, 0, sizeof(reflection));
  rc = GPUGetShaderReflection(library, &reflection);
  if (rc != GPU_OK) {
    return rc;
  }

  requiredCount = gpu_reflectionLayoutCount(&reflection);
  if (!outLayouts) {
    *inoutLayoutCount = requiredCount;
    GPUFreeShaderReflection(&reflection);
    return GPU_OK;
  }

  if (*inoutLayoutCount < requiredCount) {
    *inoutLayoutCount = requiredCount;
    GPUFreeShaderReflection(&reflection);
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    outLayouts[i] = NULL;
  }

  for (uint32_t i = 0; i < requiredCount; i++) {
    rc = gpu_createLayoutForReflectionSet(device, &reflection, i, &outLayouts[i]);
    if (rc != GPU_OK) {
      for (uint32_t j = 0; j < i; j++) {
        GPUDestroyBindGroupLayout(outLayouts[j]);
        outLayouts[j] = NULL;
      }
      GPUFreeShaderReflection(&reflection);
      return rc;
    }
  }

  *inoutLayoutCount = requiredCount;
  GPUFreeShaderReflection(&reflection);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreatePipelineLayoutFromReflection(GPUDevice *device,
                                      const GPUShaderLibrary *library,
                                      uint32_t bindGroupLayoutCount,
                                      GPUBindGroupLayout * const *ppLayouts,
                                      GPUPipelineLayout **outLayout) {
  GPUPipelineLayoutCreateInfo info;
  GPUShaderReflection reflection;
  uint32_t requiredCount;
  GPUResult rc;

  if (!library || !outLayout ||
      (bindGroupLayoutCount > 0u && !ppLayouts)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&reflection, 0, sizeof(reflection));
  rc = GPUGetShaderReflection(library, &reflection);
  if (rc != GPU_OK) {
    return rc;
  }

  requiredCount = gpu_reflectionLayoutCount(&reflection);
  if (bindGroupLayoutCount < requiredCount) {
    GPUFreeShaderReflection(&reflection);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.bindGroupLayoutCount = bindGroupLayoutCount;
  info.ppBindGroupLayouts = ppLayouts;
  info.pushConstantSizeBytes = reflection.pushConstantSizeBytes;
  info.pushConstantStages = reflection.pushConstantSizeBytes > 0u
                               ? (GPU_SHADER_STAGE_VERTEX_BIT |
                                  GPU_SHADER_STAGE_FRAGMENT_BIT |
                                  GPU_SHADER_STAGE_COMPUTE_BIT)
                               : 0u;
  rc = GPUCreatePipelineLayout(device, &info, outLayout);
  GPUFreeShaderReflection(&reflection);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLayout(GPUDevice *device,
                      const GPUShaderLibrary *library,
                      GPUShaderLayout **outLayout) {
  GPUShaderLayout *layout;
  uint32_t layoutCount;
  GPUResult rc;

  if (!outLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLayout = NULL;
  if (!library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = calloc(1, sizeof(*layout));
  if (!layout) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  layoutCount = 0u;
  rc = GPUCreateBindGroupLayoutsFromReflection(device, library, &layoutCount, NULL);
  if (rc != GPU_OK) {
    free(layout);
    return rc;
  }

  if (layoutCount > 0u) {
    if ((size_t)layoutCount > SIZE_MAX / sizeof(*layout->bindGroupLayouts)) {
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    layout->bindGroupLayouts = calloc(layoutCount, sizeof(*layout->bindGroupLayouts));
    if (!layout->bindGroupLayouts) {
      free(layout);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    rc = GPUCreateBindGroupLayoutsFromReflection(device,
                                                 library,
                                                 &layoutCount,
                                                 layout->bindGroupLayouts);
    if (rc != GPU_OK) {
      GPUDestroyShaderLayout(layout);
      return rc;
    }
  }

  layout->bindGroupLayoutCount = layoutCount;
  rc = GPUCreatePipelineLayoutFromReflection(device,
                                             library,
                                             layout->bindGroupLayoutCount,
                                             layout->bindGroupLayouts,
                                             &layout->pipelineLayout);
  if (rc != GPU_OK) {
    GPUDestroyShaderLayout(layout);
    return rc;
  }

  *outLayout = layout;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyShaderLayout(GPUShaderLayout *layout) {
  if (!layout) {
    return;
  }

  if (layout->pipelineLayout) {
    GPUDestroyPipelineLayout(layout->pipelineLayout);
  }
  if (layout->bindGroupLayouts) {
    for (uint32_t i = 0; i < layout->bindGroupLayoutCount; i++) {
      GPUDestroyBindGroupLayout(layout->bindGroupLayouts[i]);
    }
  }

  free(layout->bindGroupLayouts);
  free(layout);
}

GPU_EXPORT
GPUResult
GPUCreateBindGroup(GPUDevice *device,
                   const GPUBindGroupCreateInfo *info,
                   GPUBindGroup **outGroup) {
  GPUBindGroup *group;
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layoutPriv;
  GPUBindGroupLayout *layout;
  const GPUBindGroupEntry *entries;
  uint32_t count;
  uint32_t i;

  (void)device;

  if (!outGroup) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outGroup = NULL;
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layout = info->layout;
  entries = info->pEntries;
  count = info->entryCount;
  if (!layout || (!entries && count > 0)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  layoutPriv = gpu_layoutPriv(layout);
  if (!layoutPriv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  {
    GPUResult validationResult;

    validationResult = gpu_validateBindGroupEntries(layoutPriv, entries, count);
    if (validationResult != GPU_OK) {
      return validationResult;
    }
  }

  group = calloc(1, sizeof(*group));
  priv = calloc(1, sizeof(*priv));
  if (!group || !priv) {
    free(group);
    free(priv);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if ((size_t)layoutPriv->count > SIZE_MAX / sizeof(*priv->bindings)) {
    free(priv);
    free(group);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (layoutPriv->count > 0) {
    priv->bindings = calloc(layoutPriv->count, sizeof(*priv->bindings));
    if (!priv->bindings) {
      free(priv);
      free(group);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  for (i = 0; i < layoutPriv->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupEntry *entry;

    layoutEntry = &layoutPriv->entries[i];
    entry = gpu_findBindGroupEntry(entries,
                                   count,
                                   layoutEntry);
    if (!gpu_bindGroupEntryHasResource(layoutEntry, entry)) {
      free(priv->bindings);
      free(priv);
      free(group);
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    priv->bindings[i].stage = layoutEntry->stage;
    priv->bindings[i].binding = layoutEntry->binding;
    priv->bindings[i].kind = layoutEntry->kind;
    if (entry) {
      priv->bindings[i].buffer = entry->buffer.buffer;
      priv->bindings[i].textureView = entry->textureView;
      priv->bindings[i].sampler = entry->sampler;
      priv->bindings[i].offset = entry->buffer.offset;
      priv->bindings[i].size = entry->buffer.size;
    }
  }

  priv->layout = layout;
  priv->count = layoutPriv->count;
  group->_priv = priv;
  *outGroup = group;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyBindGroup(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;

  if (!group) {
    return;
  }

  priv = gpu_groupPriv(group);
  if (priv) {
    free(priv->bindings);
    free(priv);
  }

  free(group);
}

static void
gpuBindRenderBinding(void *ctx, const GPUBindGroupBindingView *binding) {
  GPUBindRenderContext *bindCtx;

  if (!ctx || !binding) {
    return;
  }

  bindCtx = ctx;
  if ((binding->visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderVertexBuffer(bindCtx->pass,
                               binding->buffer,
                               binding->offset,
                               binding->binding);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderVertexTexture(bindCtx->pass,
                                binding->textureView,
                                binding->binding);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderVertexSampler(bindCtx->pass,
                                binding->sampler,
                                binding->binding);
    }
  }

  if ((binding->visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
    if (binding->kind == GPUBindKindBuffer && binding->buffer) {
      gpuSetRenderFragmentBuffer(bindCtx->pass,
                                 binding->buffer,
                                 binding->offset,
                                 binding->binding);
    } else if (binding->kind == GPUBindKindTexture && binding->textureView) {
      gpuSetRenderFragmentTexture(bindCtx->pass,
                                  binding->textureView,
                                  binding->binding);
    } else if (binding->kind == GPUBindKindSampler && binding->sampler) {
      gpuSetRenderFragmentSampler(bindCtx->pass,
                                  binding->sampler,
                                  binding->binding);
    }
  }
}

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderPassEncoder *pass,
                   uint32_t setIndex,
                   GPUBindGroup *group,
                   uint32_t dynamicOffsetCount,
                   const uint32_t *pDynamicOffsets) {
  GPUBindRenderContext ctx;

  if (!pass || pass->_ended || setIndex != 0 || !group) {
    return;
  }

  ctx.pass = pass;
  gpuForEachBindGroupBindingWithDynamicOffsets(group,
                                               dynamicOffsetCount,
                                               pDynamicOffsets,
                                               gpuBindRenderBinding,
                                               &ctx);
}

GPU_HIDE
int
gpuForEachBindGroupBindingWithDynamicOffsets(GPUBindGroup *group,
                                             uint32_t dynamicOffsetCount,
                                             const uint32_t *pDynamicOffsets,
                                             GPUBindGroupBindingFn fn,
                                             void *ctx) {
  GPUBindGroupPriv *priv;
  GPUBindGroupLayoutPriv *layout;
  uint32_t dynamicIndex;
  uint32_t dynamicRequired;

  if (!group || !fn) {
    return 0;
  }
  if (dynamicOffsetCount > 0u && !pDynamicOffsets) {
    return 0;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout || priv->count < layout->count) {
    return 0;
  }

  dynamicRequired = 0u;
  for (uint32_t i = 0; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    uint64_t effectiveOffset;

    layoutEntry = &layout->entries[i];
    binding = &priv->bindings[i];
    if (!layoutEntry->hasDynamicOffset) {
      continue;
    }
    if (layoutEntry->kind != GPUBindKindBuffer ||
        dynamicRequired >= dynamicOffsetCount ||
        !gpu_u64Add(binding->offset,
                    pDynamicOffsets[dynamicRequired],
                    &effectiveOffset)) {
      return 0;
    }
    dynamicRequired++;
  }
  if (dynamicRequired != dynamicOffsetCount) {
    return 0;
  }

  dynamicIndex = 0u;
  for (uint32_t i = 0; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;
    const GPUBindGroupBindingPriv *binding;
    GPUBindGroupBindingView view;
    uint64_t effectiveOffset;

    layoutEntry = &layout->entries[i];
    binding = &priv->bindings[i];
    effectiveOffset = binding->offset;
    if (layoutEntry->hasDynamicOffset) {
      if (layoutEntry->kind != GPUBindKindBuffer ||
          dynamicIndex >= dynamicOffsetCount ||
          !gpu_u64Add(effectiveOffset,
                      pDynamicOffsets[dynamicIndex],
                      &effectiveOffset)) {
        return 0;
      }
      dynamicIndex++;
    }

    memset(&view, 0, sizeof(view));
    view.stage = binding->stage;
    view.kind = binding->kind;
    view.binding = binding->binding;
    view.buffer = binding->buffer;
    view.textureView = binding->textureView;
    view.sampler = binding->sampler;
    view.offset = effectiveOffset;
    view.size = binding->size;
    view.visibility = layoutEntry->visibility;
    fn(ctx, &view);
  }

  return dynamicIndex == dynamicOffsetCount;
}

GPU_HIDE
int
gpuForEachBindGroupBinding(GPUBindGroup *group,
                           GPUBindGroupBindingFn fn,
                           void *ctx) {
  return gpuForEachBindGroupBindingWithDynamicOffsets(group, 0u, NULL, fn, ctx);
}
