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
#include "device_internal.h"
#include "library_internal.h"
#include "usl_target.h"

typedef struct GPUShaderEntryInfo {
  char *name;
  uint64_t nameHash;
  uint32_t stage;
  uint32_t workgroupSize[3];
  uint32_t resourceStart;
  uint32_t resourceCount;
  uint32_t nameLength;
} GPUShaderEntryInfo;

typedef struct GPUShaderEntryInfoList {
  uint32_t count;
  GPUShaderEntryInfo entries[];
} GPUShaderEntryInfoList;

typedef struct GPUShaderResourceBindingInfo {
  uint32_t groupIndex;
  uint32_t binding;
  GPUBindingType bindingType;
  uint32_t backendBinding;
} GPUShaderResourceBindingInfo;

typedef struct GPUShaderResourceBindingInfoList {
  uint32_t count;
  GPUShaderResourceBindingInfo entries[];
} GPUShaderResourceBindingInfoList;

static void
gpu_clearShaderMetadata(GPUShaderLibrary *library) {
  if (!library) {
    return;
  }

  free(library->_metadata);
  library->_metadata         = NULL;
  library->_entryInfo        = NULL;
  library->_entryResources   = NULL;
  library->_resourceBindings = NULL;
  library->_staticSamplers   = NULL;
  memset(&library->_reflection, 0, sizeof(library->_reflection));
}

static void
gpu_clearShaderReflection(GPUShaderReflection *reflection) {
  if (!reflection) {
    return;
  }

  free((void *)reflection->pResources);
  memset(reflection, 0, sizeof(*reflection));
}

static int
gpu_uslRuntimeInfoIsUsable(const USLBytecodeRuntimeInfo *runtimeInfo) {
  return runtimeInfo &&
         runtimeInfo->abi_version == USL_RUNTIME_INFO_VERSION;
}

static int
gpu_shaderVisibilityFromUSLStage(uint32_t stage, GPUShaderStageFlags *outVisibility) {
  if (!outVisibility) {
    return 0;
  }

  switch (stage) {
    case USL_RUNTIME_STAGE_VERTEX:
      *outVisibility = GPU_SHADER_STAGE_VERTEX_BIT;
      return 1;
    case USL_RUNTIME_STAGE_FRAGMENT:
      *outVisibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
      return 1;
    case USL_RUNTIME_STAGE_COMPUTE:
      *outVisibility = GPU_SHADER_STAGE_COMPUTE_BIT;
      return 1;
    default:
      return 0;
  }
}

static uint64_t
gpu_shaderNameHash(const char *name, size_t length) {
  uint64_t hash = UINT64_C(1469598103934665603);

  for (size_t i = 0u; i < length; i++) {
    hash ^= (uint8_t)name[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static const GPUShaderEntryInfo *
gpu_findShaderEntry(const GPUShaderLibrary *library, const char *entryPoint) {
  const GPUShaderEntryInfoList *list;
  uint64_t hash;
  size_t length;

  if (!library || !entryPoint || !library->_entryInfo) {
    return NULL;
  }

  length = strlen(entryPoint);
  if (length > UINT32_MAX) {
    return NULL;
  }
  hash = gpu_shaderNameHash(entryPoint, length);
  list = library->_entryInfo;
  for (uint32_t i = 0u; i < list->count; i++) {
    const GPUShaderEntryInfo *entry = &list->entries[i];

    if (entry->nameHash == hash &&
        entry->nameLength == (uint32_t)length &&
        memcmp(entry->name, entryPoint, length) == 0) {
      return entry;
    }
  }
  return NULL;
}

GPU_HIDE
int
gpuGetShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                        const char *entryPoint,
                                        uint32_t outSize[3]) {
  const GPUShaderEntryInfo *entry;

  if (outSize) {
    outSize[0] = 1u;
    outSize[1] = 1u;
    outSize[2] = 1u;
  }
  if (!outSize) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry || entry->stage != GPU_SHADER_STAGE_COMPUTE_BIT) {
    return 0;
  }

  outSize[0] = entry->workgroupSize[0];
  outSize[1] = entry->workgroupSize[1];
  outSize[2] = entry->workgroupSize[2];
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryEntryStage(const GPUShaderLibrary *library,
                              const char *entryPoint,
                              GPUShaderStageFlags *outStage) {
  const GPUShaderEntryInfo *entry;

  if (outStage) {
    *outStage = 0u;
  }
  if (!outStage) {
    return 0;
  }

  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry) {
    return 0;
  }

  *outStage = entry->stage;
  return 1;
}

GPU_HIDE
int
gpuShaderLibraryHasEntryResourceInfo(const GPUShaderLibrary *library) {
  return library && library->_entryInfo;
}

GPU_HIDE
const GPUShaderReflection *
gpuShaderReflectionView(const GPUShaderLibrary *library) {
  return library ? &library->_reflection : NULL;
}

GPU_HIDE
int
gpuShaderEntryView(const GPUShaderLibrary *library,
                   const char *entryPoint,
                   GPUShaderStageFlags *outStage,
                   GPUShaderReflection *outReflection) {
  const GPUShaderResourceReflection *resources;
  const GPUShaderEntryInfo *entry;

  if (!library || !entryPoint || !outStage || !outReflection ||
      !library->_entryInfo) {
    return 0;
  }

  memset(outReflection, 0, sizeof(*outReflection));
  *outStage = 0u;
  resources = library->_entryResources;
  entry = gpu_findShaderEntry(library, entryPoint);
  if (!entry) {
    return 0;
  }

  *outStage = entry->stage;
  outReflection->resourceCount = entry->resourceCount;
  outReflection->pResources = entry->resourceCount > 0u
                                ? resources + entry->resourceStart
                                : NULL;
  outReflection->pushConstantSizeBytes =
    library->_reflection.pushConstantSizeBytes;
  return 1;
}

GPU_HIDE
int
gpuGetShaderResourceBackendBinding(const GPUShaderLibrary *library,
                                   const GPUShaderResourceReflection *resource,
                                   uint32_t *outBinding) {
  const GPUShaderResourceBindingInfoList *list;

  if (!library || !resource || !outBinding) {
    return 0;
  }

  list = library->_resourceBindings;
  if (!list) {
    return 0;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    const GPUShaderResourceBindingInfo *entry = &list->entries[i];

    if (entry->groupIndex == resource->groupIndex &&
        entry->binding == resource->binding &&
        entry->bindingType == resource->bindingType) {
      *outBinding = entry->backendBinding;
      return 1;
    }
  }

  return 0;
}

GPU_HIDE
const GPUShaderStaticSamplerInfo *
gpuGetShaderLibraryStaticSamplers(const GPUShaderLibrary *library,
                                  uint32_t *outCount) {
  if (outCount) {
    *outCount = library && library->_staticSamplers
                  ? library->_staticSamplers->count
                  : 0u;
  }
  return library && library->_staticSamplers
           ? library->_staticSamplers->items
           : NULL;
}

static int
gpu_bindingTypeFromUSLResource(const USLRuntimeResource *resource,
                               GPUBindingType *outType) {
  if (!resource || !outType) {
    return 0;
  }

  switch (resource->kind) {
    case USL_RUNTIME_RESOURCE_BUFFER:
      *outType = resource->access != USL_RUNTIME_IMAGE_ACCESS_READ ||
                 resource->type.kind == USL_RUNTIME_TYPE_ARRAY ?
        GPU_BINDING_STORAGE_BUFFER :
        GPU_BINDING_UNIFORM_BUFFER;
      return 1;
    case USL_RUNTIME_RESOURCE_TEXTURE:
      *outType = GPU_BINDING_SAMPLED_TEXTURE;
      return 1;
    case USL_RUNTIME_RESOURCE_IMAGE:
      *outType = resource->access == USL_RUNTIME_IMAGE_ACCESS_READ ?
        GPU_BINDING_SAMPLED_TEXTURE :
        GPU_BINDING_STORAGE_TEXTURE;
      return 1;
    case USL_RUNTIME_RESOURCE_SAMPLER:
      *outType = GPU_BINDING_SAMPLER;
      return 1;
    default:
      return 0;
  }
}

static int
gpu_shaderPublicBindingFromUSLResource(const USLRuntimeResource *resource,
                                       uint32_t *outGroupIndex,
                                       uint32_t *outBinding) {
  if (!resource || !outGroupIndex || !outBinding || resource->binding < 0) {
    return 0;
  }

  *outGroupIndex = resource->group;
  *outBinding = (uint32_t)resource->binding;
  return 1;
}

static int
gpu_shaderBackendBindingFromUSLResource(GPUBackend backend,
                                        const USLRuntimeResource *resource,
                                        uint32_t *outBinding) {
  if (!resource || !outBinding || resource->binding < 0) {
    return 0;
  }

  switch (backend) {
    case GPU_BACKEND_METAL:
      *outBinding = resource->metal_index >= 0
                      ? (uint32_t)resource->metal_index
                      : (uint32_t)resource->binding;
      break;
    case GPU_BACKEND_VULKAN:
      *outBinding = resource->spirv_binding >= 0
                      ? (uint32_t)resource->spirv_binding
                      : (uint32_t)resource->binding;
      break;
    case GPU_BACKEND_DX12:
      *outBinding = resource->hlsl_index >= 0
                      ? (uint32_t)resource->hlsl_index
                      : (uint32_t)resource->binding;
      break;
    default:
      *outBinding = (uint32_t)resource->binding;
      break;
  }
  return 1;
}

static int
gpu_staticSamplerDescEqual(const GPUUSLStaticSamplerDesc *a,
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

static int
gpu_reserveMetadata(size_t *totalSize,
                    size_t alignment,
                    size_t headerSize,
                    size_t itemCount,
                    size_t itemSize,
                    size_t *outOffset) {
  size_t alignedSize;
  size_t blockSize;

  if (!totalSize || !outOffset || alignment == 0u || itemSize == 0u ||
      (alignment & (alignment - 1u)) != 0u ||
      itemCount > (SIZE_MAX - headerSize) / itemSize) {
    return 0;
  }

  blockSize = headerSize + itemCount * itemSize;
  if (blockSize == 0u) {
    *outOffset = SIZE_MAX;
    return 1;
  }
  if (*totalSize > SIZE_MAX - (alignment - 1u)) {
    return 0;
  }

  alignedSize = (*totalSize + alignment - 1u) & ~(alignment - 1u);
  if (alignedSize > SIZE_MAX - blockSize) {
    return 0;
  }

  *outOffset = alignedSize;
  *totalSize = alignedSize + blockSize;
  return 1;
}

static int
gpu_runtimeTextSize(const char text[USL_RUNTIME_NAME_TEXT_MAX],
                    size_t *outSize) {
  if (!text || !outSize) {
    return 0;
  }

  for (size_t i = 0u; i < USL_RUNTIME_NAME_TEXT_MAX; i++) {
    if (text[i] == '\0') {
      *outSize = i + 1u;
      return i > 0u;
    }
  }
  return 0;
}

static char *
gpu_storeMetadataText(char **cursor, const char *text, size_t size) {
  char *stored;

  stored = *cursor;
  memcpy(stored, text, size);
  *cursor += size;
  return stored;
}

static int
gpu_setShaderLibraryMetadata(GPUShaderLibrary *library,
                             const USReflection *usReflection) {
  const USLBytecodeRuntimeInfo *runtimeInfo;
  GPUShaderResourceReflection *entryResources;
  GPUShaderResourceBindingInfoList *resourceBindings;
  GPUShaderStaticSamplerInfoList *staticSamplers;
  GPUShaderResourceReflection *resources;
  GPUShaderEntryInfoList *entryInfo;
  GPUBackend backend;
  uint8_t *metadata;
  char *textCursor;
  size_t entryInfoOffset;
  size_t entryResourceOffset;
  size_t resourceBindingOffset;
  size_t resourceOffset;
  size_t staticSamplerOffset;
  size_t textOffset;
  size_t textSize;
  size_t totalSize;
  uint32_t usedResourceCount;
  uint32_t entryResourceCount;
  uint32_t resourceCount;
  uint32_t flags;

  if (!library || !usReflection ||
      usReflection->abi_version != US_REFLECTION_VERSION) {
    return 0;
  }

  runtimeInfo = &usReflection->runtime;
  flags = USL_BYTECODE_RUNTIME_INFO_FLAG_ENTRY_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_RESOURCE_OVERFLOW |
          USL_BYTECODE_RUNTIME_INFO_FLAG_STATIC_SAMPLER_OVERFLOW;
  if (!gpu_uslRuntimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags & flags) != 0u ||
      runtimeInfo->entry_point_count > USL_RUNTIME_MAX_ENTRY_POINTS ||
      runtimeInfo->resource_count > USL_RUNTIME_MAX_RESOURCES ||
      runtimeInfo->static_sampler_count > USL_RUNTIME_MAX_STATIC_SAMPLERS) {
    return 0;
  }

  textSize          = 0u;
  usedResourceCount = 0u;
  for (uint32_t i = 0u; i < runtimeInfo->entry_point_count; i++) {
    size_t size;

    if (!gpu_runtimeTextSize(runtimeInfo->entry_points[i].name, &size) ||
        textSize > SIZE_MAX - size) {
      return 0;
    }
    textSize += size;
  }
  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *resource;
    size_t paramSize;

    resource = &runtimeInfo->resources[i];
    if (!resource->used) {
      continue;
    }
    if (resource->entry_index >= runtimeInfo->entry_point_count ||
        !gpu_runtimeTextSize(resource->param, &paramSize) ||
        textSize > SIZE_MAX - paramSize) {
      return 0;
    }
    textSize += paramSize;
    usedResourceCount++;
  }

  totalSize = 0u;
  if (!gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderEntryInfoList),
                           sizeof(*entryInfo),
                           runtimeInfo->entry_point_count,
                           sizeof(entryInfo->entries[0]),
                           &entryInfoOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceReflection),
                           0u,
                           usedResourceCount,
                           sizeof(entryResources[0]),
                           &entryResourceOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceBindingInfoList),
                           sizeof(*resourceBindings),
                           usedResourceCount,
                           sizeof(resourceBindings->entries[0]),
                           &resourceBindingOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderResourceReflection),
                           0u,
                           usedResourceCount,
                           sizeof(resources[0]),
                           &resourceOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(GPUShaderStaticSamplerInfoList),
                           sizeof(*staticSamplers),
                           runtimeInfo->static_sampler_count,
                           sizeof(staticSamplers->items[0]),
                           &staticSamplerOffset) ||
      !gpu_reserveMetadata(&totalSize,
                           _Alignof(char),
                           0u,
                           textSize,
                           sizeof(char),
                           &textOffset)) {
    return 0;
  }

  metadata = calloc(1, totalSize ? totalSize : 1u);
  if (!metadata) {
    return 0;
  }

  entryInfo = entryInfoOffset != SIZE_MAX
                ? (GPUShaderEntryInfoList *)(metadata + entryInfoOffset)
                : NULL;
  entryResources = entryResourceOffset != SIZE_MAX
                     ? (GPUShaderResourceReflection *)(metadata +
                                                       entryResourceOffset)
                     : NULL;
  resourceBindings = resourceBindingOffset != SIZE_MAX
                       ? (GPUShaderResourceBindingInfoList *)(metadata +
                                                              resourceBindingOffset)
                       : NULL;
  resources = resourceOffset != SIZE_MAX
                ? (GPUShaderResourceReflection *)(metadata + resourceOffset)
                : NULL;
  staticSamplers = staticSamplerOffset != SIZE_MAX
                     ? (GPUShaderStaticSamplerInfoList *)(metadata +
                                                          staticSamplerOffset)
                     : NULL;
  textCursor = textOffset != SIZE_MAX ? (char *)(metadata + textOffset) : NULL;
  entryResourceCount = 0u;
  resourceCount      = 0u;

  for (uint32_t i = 0u; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *src;
    GPUShaderEntryInfo *dst;
    GPUShaderStageFlags stage;
    size_t nameSize;

    src = &runtimeInfo->entry_points[i];
    if (!gpu_shaderVisibilityFromUSLStage(src->stage, &stage) ||
        !gpu_runtimeTextSize(src->name, &nameSize)) {
      free(metadata);
      return 0;
    }

    dst                   = &entryInfo->entries[entryInfo->count++];
    dst->name             = gpu_storeMetadataText(&textCursor,
                                                  src->name,
                                                  nameSize);
    dst->nameHash         = gpu_shaderNameHash(src->name, nameSize - 1u);
    dst->nameLength       = (uint32_t)(nameSize - 1u);
    dst->stage            = stage;
    dst->workgroupSize[0] = src->workgroup_size[0]
                              ? src->workgroup_size[0] : 1u;
    dst->workgroupSize[1] = src->workgroup_size[1]
                              ? src->workgroup_size[1] : 1u;
    dst->workgroupSize[2] = src->workgroup_size[2]
                              ? src->workgroup_size[2] : 1u;
  }

  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *src = &runtimeInfo->resources[i];

    if (src->used) {
      entryInfo->entries[src->entry_index].resourceCount++;
    }
  }
  for (uint32_t i = 0u; i < entryInfo->count; i++) {
    GPUShaderEntryInfo *entry = &entryInfo->entries[i];
    uint32_t capacity = entry->resourceCount;

    entry->resourceStart = entryResourceCount;
    entry->resourceCount = 0u;
    entryResourceCount  += capacity;
  }
  if (entryResourceCount != usedResourceCount) {
    free(metadata);
    return 0;
  }

  backend = library->_api ? library->_api->backend : GPU_BACKEND_NULL;
  for (uint32_t i = 0u; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *src;
    GPUShaderEntryInfo *shaderEntry;
    GPUShaderResourceReflection *canonical;
    GPUShaderResourceReflection *entryResource;
    GPUShaderResourceBindingInfo *bindingInfo;
    GPUShaderStageFlags visibility;
    GPUBindingType bindingType;
    uint32_t groupIndex;
    uint32_t binding;
    uint32_t backendBinding;
    uint32_t arrayCount;
    size_t paramSize;

    src = &runtimeInfo->resources[i];
    if (!src->used) {
      continue;
    }
    shaderEntry = &entryInfo->entries[src->entry_index];
    arrayCount  = src->descriptor_count;
    if (arrayCount == 0u ||
        !gpu_shaderVisibilityFromUSLStage(src->stage, &visibility) ||
        visibility != shaderEntry->stage ||
        !gpu_bindingTypeFromUSLResource(src, &bindingType) ||
        !gpu_shaderPublicBindingFromUSLResource(src,
                                                &groupIndex,
                                                &binding) ||
        !gpu_shaderBackendBindingFromUSLResource(backend,
                                                 src,
                                                 &backendBinding) ||
        !gpu_runtimeTextSize(src->param, &paramSize)) {
      free(metadata);
      return 0;
    }

    bindingInfo = NULL;
    for (uint32_t j = 0u; j < resourceBindings->count; j++) {
      GPUShaderResourceBindingInfo *item = &resourceBindings->entries[j];

      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        bindingInfo = item;
        break;
      }
    }
    if (bindingInfo) {
      if (bindingInfo->backendBinding != backendBinding) {
        free(metadata);
        return 0;
      }
    } else {
      bindingInfo = &resourceBindings->entries[resourceBindings->count++];
      bindingInfo->groupIndex     = groupIndex;
      bindingInfo->binding        = binding;
      bindingInfo->bindingType    = bindingType;
      bindingInfo->backendBinding = backendBinding;
    }

    canonical = NULL;
    for (uint32_t j = 0u; j < resourceCount; j++) {
      GPUShaderResourceReflection *item = &resources[j];

      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        canonical = item;
        break;
      }
    }
    if (canonical) {
      if (canonical->arrayCount != arrayCount) {
        free(metadata);
        return 0;
      }
      canonical->visibility       |= visibility;
      canonical->hasDynamicOffset  = canonical->hasDynamicOffset ||
                                     src->dynamic_offset != 0u;
    } else {
      canonical = &resources[resourceCount++];
      canonical->name = gpu_storeMetadataText(&textCursor,
                                              src->param,
                                              paramSize);
      canonical->groupIndex       = groupIndex;
      canonical->binding          = binding;
      canonical->bindingType      = bindingType;
      canonical->visibility       = visibility;
      canonical->arrayCount       = arrayCount;
      canonical->hasDynamicOffset = src->dynamic_offset != 0u;
    }

    entryResource = NULL;
    for (uint32_t j = 0u; j < shaderEntry->resourceCount; j++) {
      GPUShaderResourceReflection *item;

      item = &entryResources[shaderEntry->resourceStart + j];
      if (item->groupIndex == groupIndex &&
          item->binding == binding &&
          item->bindingType == bindingType) {
        entryResource = item;
        break;
      }
    }
    if (entryResource) {
      if (entryResource->arrayCount != arrayCount) {
        free(metadata);
        return 0;
      }
      entryResource->visibility |= visibility;
      entryResource->hasDynamicOffset = entryResource->hasDynamicOffset ||
                                        src->dynamic_offset != 0u;
    } else {
      entryResource = &entryResources[shaderEntry->resourceStart +
                                      shaderEntry->resourceCount++];
      *entryResource                  = *canonical;
      entryResource->visibility       = visibility;
      entryResource->hasDynamicOffset = src->dynamic_offset != 0u;
    }
  }

  for (uint32_t i = 0u; i < runtimeInfo->static_sampler_count; i++) {
    const USLRuntimeStaticSampler *src;
    GPUShaderStaticSamplerInfo item;
    GPUShaderStageFlags visibility;
    uint32_t duplicate;

    src = &runtimeInfo->static_samplers[i];
    if (!gpu_shaderVisibilityFromUSLStage(src->stage, &visibility)) {
      free(metadata);
      return 0;
    }

    memset(&item, 0, sizeof(item));
    item.desc.logicalIndex  = src->id;
    item.desc.minFilter     = src->min_filter;
    item.desc.magFilter     = src->mag_filter;
    item.desc.mipFilter     = src->mip_filter;
    item.desc.addressMode   = src->address_mode;
    item.desc.coordSpace    = src->coord_space;
    item.desc.compareFunc   = src->compare_func;
    item.desc.hasCompare    = src->has_compare;
    item.desc.maxAnisotropy = src->max_anisotropy;
    item.visibility         = visibility;
    item.hlslIndex          = src->hlsl_index >= 0
                                ? (uint32_t)src->hlsl_index
                                : UINT32_MAX;
    item.spirvGroup         = src->spirv_set;
    item.spirvBinding = src->spirv_binding >= 0
                          ? (uint32_t)src->spirv_binding
                          : UINT32_MAX;
    if (!GPUUSLStaticSamplerDescIsValid(&item.desc)) {
      free(metadata);
      return 0;
    }

    duplicate = UINT32_MAX;
    for (uint32_t j = 0u; j < staticSamplers->count; j++) {
      if (staticSamplers->items[j].hlslIndex == item.hlslIndex &&
          staticSamplers->items[j].spirvGroup == item.spirvGroup &&
          staticSamplers->items[j].spirvBinding == item.spirvBinding &&
          gpu_staticSamplerDescEqual(&staticSamplers->items[j].desc,
                                     &item.desc)) {
        duplicate = j;
        break;
      }
    }
    if (duplicate != UINT32_MAX) {
      staticSamplers->items[duplicate].visibility |= visibility;
      continue;
    }
    staticSamplers->items[staticSamplers->count++] = item;
  }

  gpu_clearShaderMetadata(library);
  library->_metadata         = metadata;
  library->_entryInfo        = entryInfo;
  library->_entryResources   = entryResources;
  library->_resourceBindings = resourceBindings;
  library->_staticSamplers   = staticSamplers;
  library->_reflection.resourceCount = resourceCount;
  library->_reflection.pResources = resources;
  return 1;
}

static GPUResult
gpu_copyShaderReflection(const GPUShaderReflection *src,
                         GPUShaderReflection *dst) {
  const GPUShaderResourceReflection *srcResources;
  GPUShaderResourceReflection *dstResources;
  uint8_t *storage;
  char *text;
  size_t resourceBytes;
  size_t textBytes;

  if (!src || !dst) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(dst, 0, sizeof(*dst));
  dst->pushConstantSizeBytes = src->pushConstantSizeBytes;
  if (src->resourceCount == 0u) {
    return GPU_OK;
  }
  if (!src->pResources ||
      (size_t)src->resourceCount > SIZE_MAX / sizeof(*dstResources)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  srcResources  = src->pResources;
  resourceBytes = (size_t)src->resourceCount * sizeof(*dstResources);
  textBytes     = 0u;
  for (uint32_t i = 0u; i < src->resourceCount; i++) {
    const char *name = srcResources[i].name ? srcResources[i].name : "";
    size_t size = strlen(name) + 1u;

    if (textBytes > SIZE_MAX - size) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    textBytes += size;
  }
  if (resourceBytes > SIZE_MAX - textBytes) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  storage = calloc(1, resourceBytes + textBytes);
  if (!storage) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  dstResources = (GPUShaderResourceReflection *)storage;
  text         = (char *)(storage + resourceBytes);
  for (uint32_t i = 0u; i < src->resourceCount; i++) {
    const char *name = srcResources[i].name ? srcResources[i].name : "";
    size_t size = strlen(name) + 1u;

    dstResources[i] = srcResources[i];
    dstResources[i].name = text;
    memcpy(text, name, size);
    text += size;
  }

  dst->resourceCount = src->resourceCount;
  dst->pResources = dstResources;
  return GPU_OK;
}

GPU_HIDE
GPUShaderFunction*
gpuShaderFunction(GPUShaderLibrary *lib, const char *name) {
  GPUApi *api;

  if (!lib || !name || !(api = lib->_api) || !api->library.newFunction)
    return NULL;

  return api->library.newFunction(lib, name);
}

static GPUResult
gpu_createShaderLibraryFromUSLImpl(GPUDevice *device,
                                   const void *bytecodeData,
                                   uint64_t bytecodeSize,
                                   GPUShaderLibrary **outLibrary);

static GPUResult
gpu_createShaderLibraryFromBackendText(GPUDevice        *device,
                                       const void       *sourceData,
                                       uint64_t          sourceSize,
                                       uint32_t          defineCount,
                                       GPUShaderLibrary **outLibrary) {
  GPUApi *api;
  char *source;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!api->library.newLibraryWithSource || defineCount > 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!sourceData || sourceSize == 0u ||
      sourceSize > (uint64_t)SIZE_MAX - 1u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  source = calloc(1, (size_t)sourceSize + 1u);
  if (!source) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(source, sourceData, (size_t)sourceSize);
  source[(size_t)sourceSize] = '\0';

  *outLibrary = api->library.newLibraryWithSource(device, source, sourceSize);
  free(source);

  if (*outLibrary) {
    (*outLibrary)->_api = api;
  }

  return *outLibrary ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static GPUResult
gpu_createShaderLibraryFromMSLText(GPUDevice *device,
                                   const GPUShaderLibraryCreateInfo *info,
                                   GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  api = gpuDeviceApi(device);
  if (!api || api->backend != GPU_BACKEND_METAL) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromBackendText(device,
                                                info->sourceData,
                                                info->sourceSize,
                                                info->defineCount,
                                                outLibrary);
}

static GPUResult
gpu_createShaderLibraryFromBinary(GPUDevice *device,
                                  const GPUShaderLibraryCreateInfo *info,
                                  GPUShaderLibrary **outLibrary) {
  GPUApi *api;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  if (!api->library.newLibraryWithBinary || info->defineCount > 0u ||
      info->sourceSize > (uint64_t)SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = api->library.newLibraryWithBinary(device,
                                                   info->sourceData,
                                                   info->sourceSize);
  if (*outLibrary) {
    (*outLibrary)->_api = api;
  }
  return *outLibrary ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibrary(GPUDevice *device,
                       const GPUShaderLibraryCreateInfo *info,
                       GPUShaderLibrary **outLibrary) {
  if (!outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = NULL;
  if (!device || !info || !info->sourceData || info->sourceSize == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  switch (info->sourceKind) {
    case GPU_SHADER_SOURCE_MSL_TEXT:
      return gpu_createShaderLibraryFromMSLText(device, info, outLibrary);
    case GPU_SHADER_SOURCE_USL_BYTECODE:
      return gpu_createShaderLibraryFromUSLImpl(device,
                                                info->sourceData,
                                                info->sourceSize,
                                                outLibrary);
    case GPU_SHADER_SOURCE_USL_TEXT:
      return GPU_ERROR_INVALID_ARGUMENT;
    case GPU_SHADER_SOURCE_SPIRV_BINARY:
      return gpu_createShaderLibraryFromBinary(device, info, outLibrary);
    default:
      return GPU_ERROR_INVALID_ARGUMENT;
  }
}

static GPUResult
gpu_createShaderLibraryFromUSLImpl(GPUDevice *device,
                                   const void *bytecodeData,
                                   uint64_t bytecodeSize,
                                   GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};
  GPUApi                   *api;
  USCompileOutput          *compileOutput;
  USLTargetSpec             target;
  USCompileInput            compileInput;
  GPUResult                 rc;
  uint32_t                  encoding;

  if (!device || !bytecodeData || !outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outLibrary = NULL;

  if (!(api = gpuDeviceApi(device))) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!gpu_uslDefaultTarget(api->backend, &target)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  encoding   = target.backend == USL_BACKEND_SPIRV
                 ? USL_RUNTIME_EMBEDDED_BLOB_ENCODING_BINARY
                 : USL_RUNTIME_EMBEDDED_BLOB_ENCODING_TEXT;
  compileOutput = calloc(1, sizeof(*compileOutput));
  if (!compileOutput) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  memset(&compileInput, 0, sizeof(compileInput));
  compileInput.abi_version   = US_COMPILE_INPUT_VERSION;
  compileInput.artifact      = bytecodeData;
  compileInput.artifact_size = (size_t)bytecodeSize;
  compileInput.target        = &target;
  if (us_compile(&compileInput, compileOutput) != USLOk ||
      compileOutput->backend != target.backend ||
      compileOutput->encoding != encoding ||
      !compileOutput->backend_data ||
      compileOutput->backend_size == 0) {
    rc = GPU_ERROR_BACKEND_FAILURE;
    goto cleanup;
  }
  if (!compileOutput->reflection.target_info_valid) {
    rc = GPU_ERROR_BACKEND_FAILURE;
    goto cleanup;
  }

  if (target.backend == USL_BACKEND_SPIRV) {
    info.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.sourceKind       = GPU_SHADER_SOURCE_SPIRV_BINARY;
    info.sourceData       = compileOutput->backend_data;
    info.sourceSize       = compileOutput->backend_size;
    rc = gpu_createShaderLibraryFromBinary(device, &info, outLibrary);
  } else if (target.backend == USL_BACKEND_METAL ||
             target.backend == USL_BACKEND_HLSL) {
    rc = gpu_createShaderLibraryFromBackendText(device,
                                                compileOutput->backend_data,
                                                compileOutput->backend_size,
                                                0u,
                                                outLibrary);
  } else {
    rc = GPU_ERROR_UNSUPPORTED;
  }
  if (rc == GPU_OK && outLibrary && *outLibrary) {
    if (!gpu_setShaderLibraryMetadata(*outLibrary,
                                      &compileOutput->reflection)) {
      GPUDestroyShaderLibrary(*outLibrary);
      *outLibrary = NULL;
      rc = GPU_ERROR_BACKEND_FAILURE;
    }
  }

cleanup:
  us_free_compile_output(compileOutput);
  free(compileOutput);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibraryFromUSL(GPUDevice *device,
                              const void *artifactData,
                              uint64_t artifactSize,
                              GPUShaderLibrary **outLibrary) {
  if (!outLibrary) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outLibrary = NULL;
  if (!device || !artifactData || artifactSize == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromUSLImpl(device,
                                            artifactData,
                                            artifactSize,
                                            outLibrary);
}

GPU_EXPORT
GPUResult
GPUGetShaderReflection(const GPUShaderLibrary *library,
                       GPUShaderReflection *outReflection) {
  if (!library || !outReflection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_copyShaderReflection(&library->_reflection, outReflection);
}

GPU_EXPORT
void
GPUFreeShaderReflection(GPUShaderReflection *reflection) {
  gpu_clearShaderReflection(reflection);
}

GPU_EXPORT
void
GPUDestroyShaderLibrary(GPUShaderLibrary *library) {
  GPUApi *api;

  if (!library)
    return;

  gpu_clearShaderMetadata(library);
  if (!(api = library->_api)) {
    free(library);
    return;
  }

  if (api->library.destroyLibrary) {
    api->library.destroyLibrary(library);
  } else {
    free(library);
  }
}
