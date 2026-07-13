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

static char *
gpu_dupText(const char *text) {
  char *copy;
  size_t len;

  if (!text) {
    text = "";
  }

  len = strlen(text);
  copy = malloc(len + 1u);
  if (!copy) {
    return NULL;
  }

  memcpy(copy, text, len + 1u);
  return copy;
}

typedef struct GPUShaderEntryInfo {
  char *name;
  uint32_t stage;
  uint32_t workgroupSize[3];
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

typedef struct GPUShaderEntryResourceInfo {
  char *entry;
  GPUShaderResourceReflection resource;
} GPUShaderEntryResourceInfo;

typedef struct GPUShaderEntryResourceInfoList {
  uint32_t count;
  GPUShaderEntryResourceInfo entries[];
} GPUShaderEntryResourceInfoList;

static void
gpu_clearShaderEntryInfo(GPUShaderLibrary *library) {
  GPUShaderEntryInfoList *list;

  if (!library || !library->_entryInfo) {
    return;
  }

  list = library->_entryInfo;
  for (uint32_t i = 0; i < list->count; i++) {
    free(list->entries[i].name);
  }
  free(list);
  library->_entryInfo = NULL;
}

static void
gpu_clearShaderEntryResourceInfo(GPUShaderLibrary *library) {
  GPUShaderEntryResourceInfoList *list;

  if (!library || !library->_entryResources) {
    return;
  }

  list = library->_entryResources;
  for (uint32_t i = 0; i < list->count; i++) {
    free(list->entries[i].entry);
    free((char *)list->entries[i].resource.name);
  }
  free(list);
  library->_entryResources = NULL;
}

static void
gpu_clearShaderResourceBindingInfo(GPUShaderLibrary *library) {
  if (!library || !library->_resourceBindings) {
    return;
  }

  free(library->_resourceBindings);
  library->_resourceBindings = NULL;
}

static void
gpu_clearShaderStaticSamplers(GPUShaderLibrary *library) {
  if (!library) {
    return;
  }

  free(library->_staticSamplers);
  library->_staticSamplers = NULL;
}

static void
gpu_clearShaderReflection(GPUShaderReflection *reflection) {
  GPUShaderResourceReflection *resources;

  if (!reflection) {
    return;
  }

  resources = (GPUShaderResourceReflection *)reflection->pResources;
  if (resources) {
    for (uint32_t i = 0; i < reflection->resourceCount; i++) {
      free((char *)resources[i].name);
    }
    free(resources);
  }

  memset(reflection, 0, sizeof(*reflection));
}

static int
gpu_copyShaderResourceReflection(const GPUShaderResourceReflection *src,
                                 GPUShaderResourceReflection *dst) {
  if (!src || !dst) {
    return 0;
  }

  *dst = *src;
  dst->name = gpu_dupText(src->name);
  if (!dst->name) {
    memset(dst, 0, sizeof(*dst));
    return 0;
  }

  return 1;
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

static int
gpu_setShaderLibraryEntryInfo(GPUShaderLibrary *library,
                              const USReflection *usReflection) {
  const USLBytecodeRuntimeInfo *runtimeInfo;
  GPUShaderEntryInfoList *list;
  uint32_t count;

  if (!library || !usReflection ||
      usReflection->abi_version != US_REFLECTION_VERSION) {
    return 0;
  }

  runtimeInfo = &usReflection->runtime;
  if (!gpu_uslRuntimeInfoIsUsable(runtimeInfo)) {
    return 0;
  }

  gpu_clearShaderEntryInfo(library);
  count = runtimeInfo->entry_point_count;
  if (count == 0u) {
    return 1;
  }
  if ((size_t)count > (SIZE_MAX - sizeof(*list)) / sizeof(list->entries[0])) {
    return 0;
  }

  list = calloc(1, sizeof(*list) + (size_t)count * sizeof(list->entries[0]));
  if (!list) {
    return 0;
  }

  list->count = count;
  count = 0u;
  for (uint32_t i = 0; i < runtimeInfo->entry_point_count; i++) {
    const USLRuntimeEntryPoint *entry = &runtimeInfo->entry_points[i];
    GPUShaderStageFlags stage;
    GPUShaderEntryInfo *dst;

    if (!gpu_shaderVisibilityFromUSLStage(entry->stage, &stage)) {
      for (uint32_t j = 0; j < count; j++) {
        free(list->entries[j].name);
      }
      free(list);
      return 0;
    }

    dst = &list->entries[count++];
    dst->name = gpu_dupText(entry->name);
    if (!dst->name) {
      for (uint32_t j = 0; j < count; j++) {
        free(list->entries[j].name);
      }
      free(list);
      return 0;
    }

    dst->stage = stage;
    dst->workgroupSize[0] = entry->workgroup_size[0] ? entry->workgroup_size[0] : 1u;
    dst->workgroupSize[1] = entry->workgroup_size[1] ? entry->workgroup_size[1] : 1u;
    dst->workgroupSize[2] = entry->workgroup_size[2] ? entry->workgroup_size[2] : 1u;
  }

  library->_entryInfo = list;
  return 1;
}

GPU_HIDE
int
gpuGetShaderLibraryComputeWorkgroupSize(const GPUShaderLibrary *library,
                                        const char *entryPoint,
                                        uint32_t outSize[3]) {
  const GPUShaderEntryInfoList *list;

  if (outSize) {
    outSize[0] = 1u;
    outSize[1] = 1u;
    outSize[2] = 1u;
  }
  if (!library || !entryPoint || !outSize || !library->_entryInfo) {
    return 0;
  }

  list = library->_entryInfo;
  for (uint32_t i = 0; i < list->count; i++) {
    const GPUShaderEntryInfo *entry = &list->entries[i];

    if (entry->stage != GPU_SHADER_STAGE_COMPUTE_BIT ||
        !entry->name ||
        strcmp(entry->name, entryPoint) != 0) {
      continue;
    }

    outSize[0] = entry->workgroupSize[0] ? entry->workgroupSize[0] : 1u;
    outSize[1] = entry->workgroupSize[1] ? entry->workgroupSize[1] : 1u;
    outSize[2] = entry->workgroupSize[2] ? entry->workgroupSize[2] : 1u;
    return 1;
  }

  return 0;
}

GPU_HIDE
int
gpuGetShaderLibraryEntryStage(const GPUShaderLibrary *library,
                              const char *entryPoint,
                              GPUShaderStageFlags *outStage) {
  const GPUShaderEntryInfoList *list;

  if (outStage) {
    *outStage = 0u;
  }
  if (!library || !entryPoint || !outStage || !library->_entryInfo) {
    return 0;
  }

  list = library->_entryInfo;
  for (uint32_t i = 0; i < list->count; i++) {
    const GPUShaderEntryInfo *entry = &list->entries[i];

    if (!entry->name || strcmp(entry->name, entryPoint) != 0) {
      continue;
    }

    *outStage = entry->stage;
    return 1;
  }

  return 0;
}

GPU_HIDE
int
gpuShaderLibraryHasEntryResourceInfo(const GPUShaderLibrary *library) {
  return library && library->_entryResources;
}

GPU_HIDE
GPUResult
gpuGetShaderEntryReflection(const GPUShaderLibrary *library,
                            const char *entryPoint,
                            GPUShaderReflection *outReflection) {
  const GPUShaderEntryResourceInfoList *list;
  GPUShaderStageFlags stage;
  uint32_t count;

  if (!library || !entryPoint || !outReflection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outReflection, 0, sizeof(*outReflection));
  if (!gpuGetShaderLibraryEntryStage(library, entryPoint, &stage)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  list = library->_entryResources;
  if (!list) {
    return GPU_OK;
  }

  count = 0u;
  for (uint32_t i = 0; i < list->count; i++) {
    if (list->entries[i].entry &&
        strcmp(list->entries[i].entry, entryPoint) == 0) {
      count++;
    }
  }
  if (count == 0u) {
    return GPU_OK;
  }
  if ((size_t)count > SIZE_MAX / sizeof(GPUShaderResourceReflection)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  outReflection->pResources = calloc(count, sizeof(GPUShaderResourceReflection));
  if (!outReflection->pResources) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    const GPUShaderEntryResourceInfo *entry;

    entry = &list->entries[i];
    if (!entry->entry || strcmp(entry->entry, entryPoint) != 0) {
      continue;
    }
    if (!gpu_copyShaderResourceReflection(
          &entry->resource,
          (GPUShaderResourceReflection *)&outReflection->pResources[outReflection->resourceCount])) {
      outReflection->resourceCount++;
      gpu_clearShaderReflection(outReflection);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    outReflection->resourceCount++;
  }

  return GPU_OK;
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
gpu_appendShaderResourceBindingInfo(GPUShaderLibrary *library,
                                    uint32_t groupIndex,
                                    uint32_t binding,
                                    GPUBindingType bindingType,
                                    uint32_t backendBinding) {
  GPUShaderResourceBindingInfoList *list;
  GPUShaderResourceBindingInfoList *grown;
  size_t nextCount;

  if (!library) {
    return 0;
  }

  list = library->_resourceBindings;
  for (uint32_t i = 0; list && i < list->count; i++) {
    GPUShaderResourceBindingInfo *entry = &list->entries[i];

    if (entry->groupIndex == groupIndex &&
        entry->binding == binding &&
        entry->bindingType == bindingType) {
      return entry->backendBinding == backendBinding;
    }
  }

  if (list && list->count == UINT32_MAX) {
    return 0;
  }

  nextCount = list ? (size_t)list->count + 1u : 1u;
  if (nextCount > (SIZE_MAX - sizeof(*list)) / sizeof(list->entries[0])) {
    return 0;
  }

  grown = realloc(list, sizeof(*list) + nextCount * sizeof(list->entries[0]));
  if (!grown) {
    return 0;
  }

  if (!list) {
    grown->count = 0u;
  }
  library->_resourceBindings = grown;
  list = grown;
  memset(&list->entries[list->count], 0, sizeof(list->entries[0]));
  list->entries[list->count].groupIndex = groupIndex;
  list->entries[list->count].binding = binding;
  list->entries[list->count].bindingType = bindingType;
  list->entries[list->count].backendBinding = backendBinding;
  list->count++;
  return 1;
}

static int
gpu_appendShaderEntryResourceInfo(GPUShaderLibrary *library,
                                  const USLRuntimeResource *resource,
                                  GPUBindingType bindingType,
                                  GPUShaderStageFlags visibility,
                                  uint32_t groupIndex,
                                  uint32_t binding) {
  GPUShaderEntryResourceInfoList *list;
  GPUShaderEntryResourceInfoList *grown;
  uint32_t resourceArrayCount;
  size_t nextCount;

  if (!library || !resource || !resource->entry[0] || visibility == 0u) {
    return 0;
  }

  resourceArrayCount = 1u;
  list = library->_entryResources;
  for (uint32_t i = 0; list && i < list->count; i++) {
    GPUShaderEntryResourceInfo *entry = &list->entries[i];

    if (entry->entry &&
        strcmp(entry->entry, resource->entry) == 0 &&
        entry->resource.groupIndex == groupIndex &&
        entry->resource.binding == binding &&
        entry->resource.bindingType == bindingType) {
      entry->resource.visibility |= visibility;
      entry->resource.hasDynamicOffset = entry->resource.hasDynamicOffset ||
                                         resource->dynamic_offset != 0u;
      return 1;
    }
  }

  if (list && list->count == UINT32_MAX) {
    return 0;
  }

  nextCount = list ? (size_t)list->count + 1u : 1u;
  if (nextCount > (SIZE_MAX - sizeof(*list)) / sizeof(list->entries[0])) {
    return 0;
  }

  grown = realloc(list, sizeof(*list) + nextCount * sizeof(list->entries[0]));
  if (!grown) {
    return 0;
  }

  if (!list) {
    grown->count = 0u;
  }
  library->_entryResources = grown;
  list = grown;

  memset(&list->entries[list->count], 0, sizeof(list->entries[0]));
  list->entries[list->count].entry = gpu_dupText(resource->entry);
  list->entries[list->count].resource.name = gpu_dupText(resource->param);
  if (!list->entries[list->count].entry ||
      !list->entries[list->count].resource.name) {
    free(list->entries[list->count].entry);
    free((char *)list->entries[list->count].resource.name);
    memset(&list->entries[list->count], 0, sizeof(list->entries[0]));
    return 0;
  }

  list->entries[list->count].resource.groupIndex = groupIndex;
  list->entries[list->count].resource.binding = binding;
  list->entries[list->count].resource.bindingType = bindingType;
  list->entries[list->count].resource.visibility = visibility;
  list->entries[list->count].resource.arrayCount = resourceArrayCount;
  list->entries[list->count].resource.hasDynamicOffset =
    resource->dynamic_offset != 0u;
  list->count++;
  return 1;
}

static int
gpu_appendShaderResourceReflection(GPUShaderLibrary *library,
                                   const USLRuntimeResource *resource,
                                   GPUBindingType bindingType,
                                   GPUShaderStageFlags visibility) {
  GPUShaderReflection *reflection;
  GPUShaderResourceReflection *resources;
  GPUShaderResourceReflection *grown;
  GPUBackend backend;
  uint32_t binding;
  uint32_t backendBinding;
  uint32_t groupIndex;
  size_t nextCount;

  if (!library || !resource || visibility == 0) {
    return 0;
  }

  backend = library->_api ? library->_api->backend : GPU_BACKEND_NULL;
  if (!gpu_shaderPublicBindingFromUSLResource(resource, &groupIndex, &binding) ||
      !gpu_shaderBackendBindingFromUSLResource(backend,
                                               resource,
                                               &backendBinding) ||
      !gpu_appendShaderResourceBindingInfo(library,
                                           groupIndex,
                                           binding,
                                           bindingType,
                                           backendBinding) ||
      !gpu_appendShaderEntryResourceInfo(library,
                                         resource,
                                         bindingType,
                                         visibility,
                                         groupIndex,
                                         binding)) {
    return 0;
  }

  reflection = &library->_reflection;
  resources = (GPUShaderResourceReflection *)reflection->pResources;

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (resources[i].groupIndex == groupIndex &&
        resources[i].binding == binding &&
        resources[i].bindingType == bindingType) {
      resources[i].visibility |= visibility;
      resources[i].hasDynamicOffset = resources[i].hasDynamicOffset ||
                                      resource->dynamic_offset != 0u;
      return 1;
    }
  }

  if (reflection->resourceCount == UINT32_MAX) {
    return 0;
  }

  nextCount = (size_t)reflection->resourceCount + 1u;
  if (nextCount > SIZE_MAX / sizeof(*resources)) {
    return 0;
  }

  grown = realloc(resources, nextCount * sizeof(*resources));
  if (!grown) {
    return 0;
  }

  reflection->pResources = grown;
  resources = grown;
  memset(&resources[reflection->resourceCount], 0, sizeof(resources[0]));
  resources[reflection->resourceCount].name = gpu_dupText(resource->param);
  if (!resources[reflection->resourceCount].name) {
    return 0;
  }
  resources[reflection->resourceCount].groupIndex = groupIndex;
  resources[reflection->resourceCount].binding = binding;
  resources[reflection->resourceCount].bindingType = bindingType;
  resources[reflection->resourceCount].visibility = visibility;
  resources[reflection->resourceCount].arrayCount = 1u;
  resources[reflection->resourceCount].hasDynamicOffset =
    resource->dynamic_offset != 0u;
  reflection->resourceCount++;
  return 1;
}

static int
gpu_setShaderLibraryReflection(GPUShaderLibrary *library,
                               const USReflection *usReflection) {
  const USLBytecodeRuntimeInfo *runtimeInfo;

  if (!library || !usReflection ||
      usReflection->abi_version != US_REFLECTION_VERSION) {
    return 0;
  }

  runtimeInfo = &usReflection->runtime;
  if (!gpu_uslRuntimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags & USL_BYTECODE_RUNTIME_INFO_FLAG_RESOURCE_OVERFLOW) != 0u) {
    return 0;
  }

  gpu_clearShaderReflection(&library->_reflection);
  gpu_clearShaderEntryResourceInfo(library);
  gpu_clearShaderResourceBindingInfo(library);
  for (uint32_t i = 0; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *resource = &runtimeInfo->resources[i];
    GPUShaderStageFlags visibility;
    GPUBindingType bindingType;

    if (!resource->used) {
      continue;
    }

    if (!gpu_shaderVisibilityFromUSLStage(resource->stage, &visibility) ||
        !gpu_bindingTypeFromUSLResource(resource, &bindingType) ||
        !gpu_appendShaderResourceReflection(library,
                                            resource,
                                            bindingType,
                                            visibility)) {
      gpu_clearShaderReflection(&library->_reflection);
      gpu_clearShaderEntryResourceInfo(library);
      gpu_clearShaderResourceBindingInfo(library);
      return 0;
    }
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
gpu_setShaderLibraryStaticSamplers(GPUShaderLibrary *library,
                                   const USReflection *usReflection) {
  const USLBytecodeRuntimeInfo *runtimeInfo;
  GPUShaderStaticSamplerInfoList *list;
  uint32_t count;

  if (!library || !usReflection ||
      usReflection->abi_version != US_REFLECTION_VERSION) {
    return 0;
  }

  runtimeInfo = &usReflection->runtime;
  if (!gpu_uslRuntimeInfoIsUsable(runtimeInfo) ||
      (runtimeInfo->flags &
       USL_BYTECODE_RUNTIME_INFO_FLAG_STATIC_SAMPLER_OVERFLOW) != 0u) {
    return 0;
  }

  gpu_clearShaderStaticSamplers(library);
  count = runtimeInfo->static_sampler_count;
  if (count == 0u) {
    return 1;
  }
  if ((size_t)count > (SIZE_MAX - sizeof(*list)) / sizeof(list->items[0])) {
    return 0;
  }

  list = calloc(1, sizeof(*list) + (size_t)count * sizeof(list->items[0]));
  if (!list) {
    return 0;
  }

  for (uint32_t i = 0u; i < count; i++) {
    const USLRuntimeStaticSampler *src;
    GPUShaderStaticSamplerInfo item;
    GPUShaderStageFlags visibility;
    uint32_t duplicate;

    src = &runtimeInfo->static_samplers[i];
    if (!gpu_shaderVisibilityFromUSLStage(src->stage, &visibility)) {
      free(list);
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
      free(list);
      return 0;
    }

    duplicate = UINT32_MAX;
    for (uint32_t j = 0u; j < list->count; j++) {
      if (list->items[j].hlslIndex == item.hlslIndex &&
          list->items[j].spirvGroup == item.spirvGroup &&
          list->items[j].spirvBinding == item.spirvBinding &&
          gpu_staticSamplerDescEqual(&list->items[j].desc, &item.desc)) {
        duplicate = j;
        break;
      }
    }
    if (duplicate != UINT32_MAX) {
      list->items[duplicate].visibility |= visibility;
      continue;
    }
    list->items[list->count++] = item;
  }

  library->_staticSamplers = list;
  return 1;
}

static GPUResult
gpu_copyShaderReflection(const GPUShaderReflection *src,
                         GPUShaderReflection *dst) {
  const GPUShaderResourceReflection *srcResources;
  GPUShaderResourceReflection *dstResources;

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

  srcResources = src->pResources;
  dstResources = calloc(src->resourceCount, sizeof(*dstResources));
  if (!dstResources) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  for (uint32_t i = 0; i < src->resourceCount; i++) {
    dstResources[i] = srcResources[i];
    dstResources[i].name = gpu_dupText(srcResources[i].name);
    if (!dstResources[i].name) {
      dst->resourceCount = i + 1u;
      dst->pResources = dstResources;
      gpu_clearShaderReflection(dst);
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  dst->resourceCount = src->resourceCount;
  dst->pResources = dstResources;
  return GPU_OK;
}

GPU_EXPORT
GPULibrary*
GPUDefaultLibrary(GPUDevice *device) {
  GPUApi     *api;
  GPULibrary *library;

  if (!(api = gpuDeviceApi(device)) || !api->library.defaultLibrary)
    return NULL;

  library = api->library.defaultLibrary(device);
  if (library) {
    library->_api = api;
  }
  return library;
}

GPU_EXPORT
GPUFunction*
GPUShaderFunction(GPULibrary *lib, const char *name) {
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
    if (!gpu_setShaderLibraryReflection(*outLibrary,
                                         &compileOutput->reflection) ||
        !gpu_setShaderLibraryEntryInfo(*outLibrary,
                                       &compileOutput->reflection) ||
        !gpu_setShaderLibraryStaticSamplers(*outLibrary,
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

  gpu_clearShaderReflection(&library->_reflection);
  gpu_clearShaderEntryInfo(library);
  gpu_clearShaderEntryResourceInfo(library);
  gpu_clearShaderResourceBindingInfo(library);
  gpu_clearShaderStaticSamplers(library);
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
