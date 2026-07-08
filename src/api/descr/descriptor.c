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
#include "../usl_target.h"

#define GPU_USL_BYTECODE_VERSION 2u

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

static void
gpu_usl_freeCompileOutput(USCompileOutput *output) {
  if (output) {
    us_free_compile_output(output);
    free(output);
  }
}

static int
gpu_usl_stageFromRuntimeStage(uint32_t stage, GPUBindStage *outStage);

static int
gpu_bindStageIsValid(GPUBindStage stage);

static int
gpu_bindKindIsValid(GPUBindKind kind);

static void
gpu_copyText(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) {
    return;
  }

  snprintf(dst, dstSize, "%s", src ? src : "");
}

static uint64_t
gpu_fnv1a64Append(const void *data, size_t size, uint64_t hash) {
  const uint8_t *p = data;

  for (size_t i = 0; p && i < size; i++) {
    hash ^= (uint64_t)p[i];
    hash *= UINT64_C(1099511628211);
  }

  return hash;
}

static uint64_t
gpu_fnv1a64AppendU32(uint64_t hash, uint32_t value) {
  return gpu_fnv1a64Append(&value, sizeof(value), hash);
}

static int
gpu_usl_capabilityAtomNeedsTextHash(const USLCapabilityAtomDesc *atom) {
  return atom &&
         (atom->family == USL_CAPABILITY_ATOM_FAMILY_CUSTOM ||
          (atom->family == USL_CAPABILITY_ATOM_FAMILY_EXTENSION &&
           atom->id == USL_CAPABILITY_EXTENSION_ID_CUSTOM));
}

static uint64_t
gpu_hashUSLEntryCapabilityRequirements(const USLBytecodeRuntimeInfo *runtimeInfo,
                                       const USLRuntimeEntryPoint *entry) {
  uint64_t hash;

  if (!runtimeInfo || !entry ||
      entry->capability_requirement_total_count == 0u) {
    return 0u;
  }

  hash = UINT64_C(14695981039346656037);
  hash = gpu_fnv1a64AppendU32(hash, entry->capability_requirement_total_count);
  hash = gpu_fnv1a64AppendU32(hash, entry->capability_requirement_count);
  hash = gpu_fnv1a64AppendU32(hash, entry->capability_requirement_flags);

  for (uint32_t i = 0; i < entry->capability_requirement_count; i++) {
    uint32_t index = entry->capability_requirement_start + i;
    const USLRuntimeCapabilityRequirement *requirement;

    if (index >= runtimeInfo->capability_requirement_count) {
      break;
    }

    requirement = &runtimeInfo->capability_requirements[index];
    hash = gpu_fnv1a64AppendU32(hash, requirement->clause_index);
    hash = gpu_fnv1a64AppendU32(hash, requirement->atom_total_count);
    hash = gpu_fnv1a64AppendU32(hash, requirement->atom_count);
    hash = gpu_fnv1a64AppendU32(hash, requirement->flags);

    for (uint32_t ai = 0; ai < requirement->atom_count; ai++) {
      const USLCapabilityAtomDesc *atom = &requirement->atoms[ai];
      hash = gpu_fnv1a64AppendU32(hash, atom->family);
      hash = gpu_fnv1a64AppendU32(hash, atom->id);
      hash = gpu_fnv1a64AppendU32(hash, atom->major);
      hash = gpu_fnv1a64AppendU32(hash, atom->minor);
      if (gpu_usl_capabilityAtomNeedsTextHash(atom)) {
        hash = gpu_fnv1a64Append(atom->text, strlen(atom->text), hash);
      }
    }
  }

  return hash;
}

static uint64_t
gpu_hashUSLEntryTargetAtoms(const USLBytecodeEntryTargetInfo *targetInfo) {
  uint64_t hash;

  if (!targetInfo) {
    return 0u;
  }

  hash = UINT64_C(14695981039346656037);
  hash = gpu_fnv1a64AppendU32(hash, targetInfo->target_atom_total_count);
  for (uint32_t i = 0; i < targetInfo->target_atom_count; i++) {
    const USLCapabilityAtomDesc *atom = &targetInfo->target_atoms[i];
    hash = gpu_fnv1a64AppendU32(hash, atom->family);
    hash = gpu_fnv1a64AppendU32(hash, atom->id);
    hash = gpu_fnv1a64AppendU32(hash, atom->major);
    hash = gpu_fnv1a64AppendU32(hash, atom->minor);
    if (gpu_usl_capabilityAtomNeedsTextHash(atom)) {
      hash = gpu_fnv1a64Append(atom->text,
                               strnlen(atom->text, sizeof(atom->text)),
                               hash);
      hash = gpu_fnv1a64AppendU32(hash, 0u);
    }
  }

  return hash;
}

static void
gpu_setBindGroupLayoutUSLInfo(GPUBindGroupLayout *layout,
                              const USLBytecodeRuntimeInfo *runtimeInfo,
                              const USLRuntimeEntryPoint *entry,
                              const USLBytecodeEntryTargetInfo *targetInfo,
                              uint32_t resourceBindingCount) {
  GPUBindGroupLayoutUSLInfo *info;
  GPUBindStage stage;

  if (!layout || !runtimeInfo || !entry) {
    return;
  }
  if (!gpu_usl_stageFromRuntimeStage(entry->stage, &stage)) {
    return;
  }

  info = &layout->_uslInfo;
  memset(info, 0, sizeof(*info));
  info->abiVersion = GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION;
  info->runtimeInfoVersion = runtimeInfo->abi_version;
  info->bytecodeVersion = runtimeInfo->bytecode_version;
  info->stage = stage;
  info->resourceBindingCount = resourceBindingCount;
  info->bytecodeSize = runtimeInfo->bytecode_size;
  info->bytecodeDataSize = runtimeInfo->bytecode_data_size;
  info->bytecodeContentHash = runtimeInfo->content_hash;
  info->capabilityRequirementCount = entry->capability_requirement_count;
  info->capabilityRequirementTotalCount =
    entry->capability_requirement_total_count;
  info->capabilityRequirementFlags = entry->capability_requirement_flags;
  info->capabilityRequirementHash =
    gpu_hashUSLEntryCapabilityRequirements(runtimeInfo, entry);
  if (targetInfo) {
    info->entryTargetInfoVersion = targetInfo->abi_version;
    info->targetBackend = (uint32_t)targetInfo->backend;
    info->targetSupported = targetInfo->supported;
    info->targetSupportStatus = targetInfo->status;
    info->targetAtomCount = targetInfo->target_atom_count;
    info->targetAtomTotalCount = targetInfo->target_atom_total_count;
    info->targetInfoFlags = targetInfo->flags;
    info->targetAtomHash = gpu_hashUSLEntryTargetAtoms(targetInfo);
  }
  gpu_copyText(info->entryPointName, sizeof(info->entryPointName), entry->name);
}

static int
gpu_appendLayoutEntry(GPUBindGroupLayoutEntry **entries,
                      uint32_t *count,
                      GPUBindStage stage,
                      GPUBindKind kind,
                      uint32_t binding) {
  GPUBindGroupLayoutEntry *grown;
  size_t nextCount;

  if (!entries || !count) {
    return 0;
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**entries)) {
    return 0;
  }

  grown = realloc(*entries, nextCount * sizeof(**entries));
  if (!grown) {
    return 0;
  }

  *entries = grown;
  memset(&grown[*count], 0, sizeof(grown[*count]));
  grown[*count].stage = stage;
  grown[*count].kind = kind;
  grown[*count].binding = binding;
  grown[*count].visibility =
    stage == GPUBindStageVertex ? GPU_SHADER_STAGE_VERTEX_BIT :
    stage == GPUBindStageFragment ? GPU_SHADER_STAGE_FRAGMENT_BIT :
    GPU_SHADER_STAGE_COMPUTE_BIT;
  grown[*count].bindingType =
    kind == GPUBindKindTexture ? GPU_BINDING_SAMPLED_TEXTURE :
    kind == GPUBindKindSampler ? GPU_BINDING_SAMPLER :
    GPU_BINDING_UNIFORM_BUFFER;
  grown[*count].arrayCount = 1;
  (*count)++;
  return 1;
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

static int
gpu_usl_bindKindFromResourceKind(GPUUSLResourceKind kind, GPUBindKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (kind) {
    case GPUUSLResourceKindBuffer:
      *outKind = GPUBindKindBuffer;
      return 1;
    case GPUUSLResourceKindTexture:
      *outKind = GPUBindKindTexture;
      return 1;
    case GPUUSLResourceKindSampler:
      *outKind = GPUBindKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_usl_appendResourceBinding(GPUUSLResourceBindingDesc **items,
                              uint32_t *count,
                              GPUUSLStage stage,
                              GPUUSLResourceKind kind,
                              uint32_t binding) {
  GPUUSLResourceBindingDesc *grown;
  size_t nextCount;

  if (!items || !count) {
    return 0;
  }

  for (uint32_t i = 0; i < *count; i++) {
    if ((*items)[i].stage == stage &&
        (*items)[i].kind == kind &&
        (*items)[i].binding == binding) {
      return 1;
    }
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**items)) {
    return 0;
  }

  grown = realloc(*items, nextCount * sizeof(**items));
  if (!grown) {
    return 0;
  }

  *items = grown;
  grown[*count].stage = stage;
  grown[*count].kind = kind;
  grown[*count].binding = binding;
  (*count)++;
  return 1;
}

static int
gpu_usl_stageFromRuntimeStage(uint32_t stage, GPUBindStage *outStage) {
  if (!outStage) {
    return 0;
  }

  switch (stage) {
    case USL_RUNTIME_STAGE_VERTEX:
      *outStage = GPUBindStageVertex;
      return 1;
    case USL_RUNTIME_STAGE_FRAGMENT:
      *outStage = GPUBindStageFragment;
      return 1;
    case USL_RUNTIME_STAGE_COMPUTE:
      *outStage = GPUBindStageCompute;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_usl_resourceKindFromRuntimeKind(uint32_t kind, GPUUSLResourceKind *outKind) {
  if (!outKind) {
    return 0;
  }

  switch (kind) {
    case USL_RUNTIME_RESOURCE_BUFFER:
      *outKind = GPUUSLResourceKindBuffer;
      return 1;
    case USL_RUNTIME_RESOURCE_TEXTURE:
    case USL_RUNTIME_RESOURCE_IMAGE:
      *outKind = GPUUSLResourceKindTexture;
      return 1;
    case USL_RUNTIME_RESOURCE_SAMPLER:
      *outKind = GPUUSLResourceKindSampler;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_usl_runtimeInfoIsUsable(const USLBytecodeRuntimeInfo *runtimeInfo) {
  return runtimeInfo &&
         runtimeInfo->abi_version == USL_RUNTIME_INFO_VERSION &&
         runtimeInfo->bytecode_version == GPU_USL_BYTECODE_VERSION;
}

static const USLRuntimeEntryPoint *
gpu_usl_findRuntimeEntry(const USLBytecodeRuntimeInfo *runtimeInfo,
                         const char *entryPointName) {
  if (!gpu_usl_runtimeInfoIsUsable(runtimeInfo) || !entryPointName) {
    return NULL;
  }

  for (uint32_t i = 0; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *entry = &runtimeInfo->entry_points[i];
    if (strcmp(entry->name, entryPointName) == 0) {
      return entry;
    }
  }

  return NULL;
}

static int
gpu_usl_compileReflectionForEntry(const void *bytecodeData,
                                  uint64_t bytecodeSize,
                                  const char *entryPointName,
                                  USCompileOutput **outOutput) {
  USLTargetSpec target;
  USCompileInput input;
  USCompileOutput *output;
  const char *entryNames[1];
  const USLBytecodeEntryTargetInfo *entryInfo;

  if (!outOutput) {
    return 0;
  }
  *outOutput = NULL;

  if (!bytecodeData || bytecodeSize == 0u || !entryPointName ||
      !gpu_uslDefaultMetalTarget(&target)) {
    return 0;
  }

  output = calloc(1, sizeof(*output));
  if (!output) {
    return 0;
  }

  entryNames[0] = entryPointName;
  memset(&input, 0, sizeof(input));
  input.abi_version = US_COMPILE_INPUT_VERSION;
  input.flags = US_COMPILE_INPUT_FLAG_REFLECT_ONLY;
  input.artifact = bytecodeData;
  input.artifact_size = (size_t)bytecodeSize;
  input.target = &target;
  input.entry_point_names = entryNames;
  input.entry_point_count = 1u;

  if (us_compile(&input, output) != USLOk ||
      output->abi_version != US_COMPILE_OUTPUT_VERSION ||
      (output->flags & US_COMPILE_OUTPUT_FLAG_REFLECTION_ONLY) == 0u ||
      output->reflection.entry_target_count != 1u ||
      !gpu_usl_runtimeInfoIsUsable(&output->reflection.runtime)) {
    gpu_usl_freeCompileOutput(output);
    return 0;
  }

  entryInfo = &output->reflection.entry_targets[0];
  if (entryInfo->abi_version != USL_RUNTIME_ENTRY_TARGET_INFO_VERSION ||
      entryInfo->bytecode_version != GPU_USL_BYTECODE_VERSION ||
      (uint64_t)entryInfo->bytecode_size != bytecodeSize ||
      entryInfo->backend != target.backend ||
      entryInfo->backend_id != (uint32_t)target.backend ||
      entryInfo->content_hash == 0u ||
      strcmp(entryInfo->entry_point, entryPointName) != 0) {
    gpu_usl_freeCompileOutput(output);
    return 0;
  }

  *outOutput = output;
  return 1;
}

static int
gpu_usl_collectResourceBindingsFromRuntimeInfo(const USLBytecodeRuntimeInfo *runtimeInfo,
                                               const char *entryPointName,
                                               GPUUSLResourceBindingDesc **outBindings,
                                               uint32_t *outCount) {
  if (!gpu_usl_runtimeInfoIsUsable(runtimeInfo) ||
      !entryPointName || !outBindings || !outCount ||
      (runtimeInfo->flags & USL_BYTECODE_RUNTIME_INFO_FLAG_RESOURCE_OVERFLOW) != 0u) {
    return 0;
  }

  for (uint32_t i = 0; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *resource;
    GPUUSLResourceKind resourceKind;
    GPUBindStage stage;

    resource = &runtimeInfo->resources[i];
    if (!resource->used || strcmp(resource->entry, entryPointName) != 0) {
      continue;
    }
    if (resource->slot < 0 ||
        !gpu_usl_stageFromRuntimeStage(resource->stage, &stage) ||
        !gpu_usl_resourceKindFromRuntimeKind(resource->kind, &resourceKind)) {
      return 0;
    }

    if (!gpu_usl_appendResourceBinding(outBindings,
                                       outCount,
                                       (GPUUSLStage)stage,
                                       resourceKind,
                                       (uint32_t)resource->slot)) {
      return 0;
    }
  }

  return 1;
}

static int
gpu_usl_staticSamplerEqual(const GPUUSLStaticSamplerDesc *a,
                           const GPUUSLStaticSamplerDesc *b) {
  return a && b &&
         a->minFilter == b->minFilter &&
         a->magFilter == b->magFilter &&
         a->mipFilter == b->mipFilter &&
         a->addressMode == b->addressMode &&
         a->coordSpace == b->coordSpace &&
         a->compareFunc == b->compareFunc &&
         a->hasCompare == b->hasCompare &&
         a->maxAnisotropy == b->maxAnisotropy;
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
gpu_usl_appendStaticSampler(GPUUSLStaticSamplerDesc **items,
                            uint32_t *count,
                            GPUUSLStaticSamplerDesc desc) {
  GPUUSLStaticSamplerDesc *grown;
  size_t nextCount;

  if (!items || !count) {
    return 0;
  }

  if (!GPUUSLStaticSamplerDescIsValid(&desc)) {
    return 0;
  }

  for (uint32_t i = 0; i < *count; i++) {
    if (gpu_usl_staticSamplerEqual(&(*items)[i], &desc)) {
      return 1;
    }
  }

  if (*count == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)*count + 1u;
  if (nextCount > SIZE_MAX / sizeof(**items)) {
    return 0;
  }

  grown = realloc(*items, nextCount * sizeof(**items));
  if (!grown) {
    return 0;
  }

  *items = grown;
  desc.logicalIndex = *count;
  grown[*count] = desc;
  (*count)++;
  return 1;
}

static GPUUSLStaticSamplerDesc
gpu_usl_staticSamplerFromRuntime(const USLRuntimeStaticSampler *sampler) {
  GPUUSLStaticSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  if (!sampler) {
    return desc;
  }

  desc.logicalIndex = sampler->id;
  desc.minFilter = sampler->min_filter;
  desc.magFilter = sampler->mag_filter;
  desc.mipFilter = sampler->mip_filter;
  desc.addressMode = sampler->address_mode;
  desc.coordSpace = sampler->coord_space;
  desc.compareFunc = sampler->compare_func;
  desc.hasCompare = sampler->has_compare;
  desc.maxAnisotropy = sampler->max_anisotropy;
  return desc;
}

static int
gpu_usl_collectStaticSamplersFromRuntimeInfo(const USLBytecodeRuntimeInfo *runtimeInfo,
                                             const char *entryPointName,
                                             GPUUSLStaticSamplerDesc **outSamplers,
                                             uint32_t *outCount) {
  if (!runtimeInfo || !entryPointName || !outSamplers || !outCount) {
    return 0;
  }

  if (!gpu_usl_runtimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags & USL_BYTECODE_RUNTIME_INFO_FLAG_STATIC_SAMPLER_OVERFLOW) != 0u) {
    return 0;
  }

  for (uint32_t i = 0; i < runtimeInfo->static_sampler_count; i++) {
    const USLRuntimeStaticSampler *sampler;
    GPUUSLStaticSamplerDesc desc;

    sampler = &runtimeInfo->static_samplers[i];
    if (strcmp(sampler->entry, entryPointName) != 0) {
      continue;
    }

    desc = gpu_usl_staticSamplerFromRuntime(sampler);
    if (!gpu_usl_appendStaticSampler(outSamplers, outCount, desc)) {
      free(*outSamplers);
      *outSamplers = NULL;
      *outCount = 0;
      return 0;
    }
  }

  return 1;
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
    GPUBindKind entryKind;

    if (entries[i].binding != layoutEntry->binding) {
      continue;
    }
    if (gpu_kindFromBindingType(entries[i].bindingType, &entryKind)) {
      if (entryKind == layoutEntry->kind &&
          entries[i].bindingType == layoutEntry->bindingType) {
        return &entries[i];
      }
      continue;
    }
    if (entries[i].stage == layoutEntry->stage &&
        entries[i].kind == layoutEntry->kind) {
      return &entries[i];
    }
  }

  return NULL;
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

GPU_EXPORT
GPUDescriptorPool*
GPUCreateDescriptorPool(GPUDevice *__restrict device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->descriptor.createDescriptorPool(api, device);
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
int
GPUCreateBindGroupLayoutFromUSLBytecode(const void *bytecodeData,
                                        uint64_t bytecodeSize,
                                        const char *entryPointName,
GPUBindGroupLayout **outLayout) {
  USCompileOutput *compileOutput;
  USReflection *usReflection;
  USLBytecodeRuntimeInfo *runtimeInfo;
  const USLRuntimeEntryPoint *entry;
  const USLBytecodeEntryTargetInfo *targetInfo;
  GPUBindGroupLayoutEntry *entries;
  GPUUSLResourceBindingDesc *bindings;
  GPUBindStage stage;
  uint32_t entryCount;
  uint32_t bindingCount;
  GPUResult rc;

  if (!outLayout) {
    return -1;
  }

  *outLayout = NULL;
  if (!bytecodeData || !entryPointName) {
    return -1;
  }

  if (!gpu_usl_compileReflectionForEntry(bytecodeData,
                                         bytecodeSize,
                                         entryPointName,
                                         &compileOutput)) {
    return -3;
  }
  usReflection = &compileOutput->reflection;
  runtimeInfo = &usReflection->runtime;
  targetInfo = &usReflection->entry_targets[0];

  entry = gpu_usl_findRuntimeEntry(runtimeInfo, entryPointName);
  if (!entry) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -11;
  }

  if (!gpu_usl_stageFromRuntimeStage(entry->stage, &stage)) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -4;
  }

  if (!targetInfo->supported) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -13;
  }

  entries = NULL;
  bindings = NULL;
  entryCount = 0;
  bindingCount = 0;

  if (!gpu_usl_collectResourceBindingsFromRuntimeInfo(runtimeInfo,
                                                      entryPointName,
                                                      &bindings,
                                                      &bindingCount)) {
    gpu_usl_freeCompileOutput(compileOutput);
    free(bindings);
    free(entries);
    return -5;
  }

  for (uint32_t i = 0; i < bindingCount; i++) {
    GPUBindKind bindKind;
    GPUBindStage bindStage;

    bindStage = (GPUBindStage)bindings[i].stage;
    if (!gpu_bindStageIsValid(bindStage) ||
        !gpu_usl_bindKindFromResourceKind(bindings[i].kind, &bindKind)) {
      gpu_usl_freeCompileOutput(compileOutput);
      free(bindings);
      free(entries);
      return -6;
    }

    if (!gpu_appendLayoutEntry(&entries,
                               &entryCount,
                               bindStage,
                               bindKind,
                               bindings[i].binding)) {
      gpu_usl_freeCompileOutput(compileOutput);
      free(bindings);
      free(entries);
      return -10;
    }
  }

  GPUBindGroupLayoutCreateInfo layoutInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupLayoutCreateInfo) },
    .label = entryPointName,
    .entryCount = entryCount,
    .pEntries = entries
  };

  rc = GPUCreateBindGroupLayout(NULL, &layoutInfo, outLayout);
  if (rc == GPU_OK && outLayout && *outLayout) {
    gpu_setBindGroupLayoutUSLInfo(*outLayout,
                                  runtimeInfo,
                                  entry,
                                  targetInfo,
                                  bindingCount);
  }
  gpu_usl_freeCompileOutput(compileOutput);
  free(bindings);
  free(entries);
  return rc;
}

GPU_EXPORT
int
GPUReflectUSLBytecodeEntry(const void *bytecodeData,
                           uint64_t bytecodeSize,
                           const char *entryPointName,
                           GPUUSLEntryReflection **outReflection) {
  USCompileOutput *compileOutput;
  USReflection *usReflection;
  USLBytecodeRuntimeInfo *runtimeInfo;
  const USLRuntimeEntryPoint *entry;
  const USLBytecodeEntryTargetInfo *targetInfo;
  GPUUSLEntryReflection *reflection;
  GPUBindStage stage;

  if (!outReflection) {
    return -1;
  }

  *outReflection = NULL;
  if (!bytecodeData || !entryPointName) {
    return -1;
  }

  if (!gpu_usl_compileReflectionForEntry(bytecodeData,
                                         bytecodeSize,
                                         entryPointName,
                                         &compileOutput)) {
    return -3;
  }
  usReflection = &compileOutput->reflection;
  runtimeInfo = &usReflection->runtime;
  targetInfo = &usReflection->entry_targets[0];

  entry = gpu_usl_findRuntimeEntry(runtimeInfo, entryPointName);
  if (!entry) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -3;
  }

  if (!gpu_usl_stageFromRuntimeStage(entry->stage, &stage)) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -4;
  }

  reflection = calloc(1, sizeof(*reflection));
  if (!reflection) {
    gpu_usl_freeCompileOutput(compileOutput);
    return -5;
  }

  reflection->stage = (GPUUSLStage)stage;
  reflection->runtimeInfoVersion = runtimeInfo->abi_version;
  reflection->bytecodeVersion = runtimeInfo->bytecode_version;
  reflection->bytecodeSize = runtimeInfo->bytecode_size;
  reflection->bytecodeDataSize = runtimeInfo->bytecode_data_size;
  reflection->bytecodeContentHash = runtimeInfo->content_hash;
  reflection->capabilityRequirementCount = entry->capability_requirement_count;
  reflection->capabilityRequirementTotalCount =
    entry->capability_requirement_total_count;
  reflection->capabilityRequirementFlags = entry->capability_requirement_flags;
  reflection->capabilityRequirementHash =
    gpu_hashUSLEntryCapabilityRequirements(runtimeInfo, entry);
  reflection->entryTargetInfoVersion = targetInfo->abi_version;
  reflection->targetBackend = (uint32_t)targetInfo->backend;
  reflection->targetSupported = targetInfo->supported;
  reflection->targetSupportStatus = targetInfo->status;
  reflection->targetAtomCount = targetInfo->target_atom_count;
  reflection->targetAtomTotalCount = targetInfo->target_atom_total_count;
  reflection->targetInfoFlags = targetInfo->flags;
  reflection->targetAtomHash = gpu_hashUSLEntryTargetAtoms(targetInfo);
  if (stage == GPUBindStageCompute) {
    reflection->workgroupSize[0] = entry->workgroup_size[0];
    reflection->workgroupSize[1] = entry->workgroup_size[1];
    reflection->workgroupSize[2] = entry->workgroup_size[2];
  }

  if (!gpu_usl_collectResourceBindingsFromRuntimeInfo(runtimeInfo,
                                                      entryPointName,
                                                      &reflection->resourceBindings,
                                                      &reflection->resourceBindingCount)) {
    gpu_usl_freeCompileOutput(compileOutput);
    free(reflection);
    return -7;
  }

  if (!gpu_usl_collectStaticSamplersFromRuntimeInfo(runtimeInfo,
                                                    entryPointName,
                                                    &reflection->staticSamplers,
                                                    &reflection->staticSamplerCount)) {
    gpu_usl_freeCompileOutput(compileOutput);
    free(reflection->resourceBindings);
    free(reflection);
    return -9;
  }

  gpu_usl_freeCompileOutput(compileOutput);
  *outReflection = reflection;
  return 0;
}

GPU_EXPORT
void
GPUFreeUSLEntryReflection(GPUUSLEntryReflection *reflection) {
  if (!reflection) {
    return;
  }

  free(reflection->resourceBindings);
  free(reflection->staticSamplers);
  free(reflection);
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
int
GPUGetBindGroupLayoutUSLInfo(GPUBindGroupLayout *layout,
                             GPUBindGroupLayoutUSLInfo *outInfo) {
  if (!layout || !outInfo) {
    return -1;
  }

  memset(outInfo, 0, sizeof(*outInfo));
  if (layout->_uslInfo.abiVersion != GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION) {
    return -2;
  }

  *outInfo = layout->_uslInfo;
  return 0;
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
  if (info->bindGroupLayoutCount > 0 && !info->ppBindGroupLayouts) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->pushConstantSizeBytes > 0 && info->pushConstantStages == 0) {
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
  info.pushConstantStages = 0u;
  rc = GPUCreatePipelineLayout(device, &info, outLayout);
  GPUFreeShaderReflection(&reflection);
  return rc;
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

GPU_EXPORT
void
GPUBindRenderGroup(GPURenderPassEncoder *pass,
                   uint32_t setIndex,
                   GPUBindGroup *group,
                   uint32_t dynamicOffsetCount,
                   const uint32_t *pDynamicOffsets) {
  const GPUBindGroupBindingPriv *binding;
  GPUBindGroupLayoutPriv *layout;
  GPUBindGroupPriv *priv;
  uint32_t i;

  (void)pDynamicOffsets;

  if (!pass || setIndex != 0 || dynamicOffsetCount != 0 || !group) {
    return;
  }

  priv = gpu_groupPriv(group);
  layout = gpu_layoutPriv(priv ? priv->layout : NULL);
  if (!priv || !layout || priv->count < layout->count) {
    return;
  }

  for (i = 0; i < layout->count; i++) {
    const GPUBindGroupLayoutEntry *layoutEntry;

    layoutEntry = &layout->entries[i];
    binding = &priv->bindings[i];

    if ((layoutEntry->visibility & GPU_SHADER_STAGE_VERTEX_BIT) != 0) {
      if (layoutEntry->kind == GPUBindKindBuffer && binding->buffer) {
        GPUSetVertexBuffer(pass,
                           binding->buffer,
                           binding->offset,
                           layoutEntry->binding);
      } else if (layoutEntry->kind == GPUBindKindTexture && binding->textureView) {
        GPUSetVertexTexture(pass,
                            binding->textureView,
                            layoutEntry->binding);
      } else if (layoutEntry->kind == GPUBindKindSampler && binding->sampler) {
        GPUSetVertexSampler(pass,
                            binding->sampler,
                            layoutEntry->binding);
      }
    }

    if ((layoutEntry->visibility & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0) {
      if (layoutEntry->kind == GPUBindKindBuffer && binding->buffer) {
        GPUSetFragmentBuffer(pass,
                             binding->buffer,
                             binding->offset,
                             layoutEntry->binding);
      } else if (layoutEntry->kind == GPUBindKindTexture && binding->textureView) {
        GPUSetFragmentTexture(pass,
                              binding->textureView,
                              layoutEntry->binding);
      } else if (layoutEntry->kind == GPUBindKindSampler && binding->sampler) {
        GPUSetFragmentSampler(pass,
                              binding->sampler,
                              layoutEntry->binding);
      }
    }
  }
}
