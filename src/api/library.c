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
#include "usl_target.h"

#define GPU_USL_BYTECODE_VERSION 2u

static uint64_t
gpu_fnv1a64Append(uint64_t hash, const void *data, uint64_t size) {
  const uint8_t *bytes = (const uint8_t *)data;

  for (uint64_t i = 0; bytes && i < size; i++) {
    hash ^= (uint64_t)bytes[i];
    hash *= 1099511628211ull;
  }

  return hash;
}

static uint64_t
gpu_fnv1a64AppendU32(uint64_t hash, uint32_t value) {
  uint8_t bytes[4];

  bytes[0] = (uint8_t)(value & 0xffu);
  bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
  bytes[2] = (uint8_t)((value >> 16u) & 0xffu);
  bytes[3] = (uint8_t)((value >> 24u) & 0xffu);
  return gpu_fnv1a64Append(hash, bytes, sizeof(bytes));
}

static int
gpu_uslCapabilityAtomNeedsTextHash(const USLCapabilityAtomDesc *atom) {
  return atom &&
         (atom->family == USL_CAPABILITY_ATOM_FAMILY_CUSTOM ||
          (atom->family == USL_CAPABILITY_ATOM_FAMILY_EXTENSION &&
           atom->id == USL_CAPABILITY_EXTENSION_ID_CUSTOM));
}

static uint64_t
gpu_hashUSLTargetAtoms(const USLBytecodeTargetInfo *targetInfo) {
  uint64_t hash = 14695981039346656037ull;

  if (!targetInfo) {
    return 0;
  }

  hash = gpu_fnv1a64AppendU32(hash, targetInfo->target_atom_total_count);
  for (uint32_t i = 0; i < targetInfo->target_atom_count; i++) {
    const USLCapabilityAtomDesc *atom = &targetInfo->target_atoms[i];
    hash = gpu_fnv1a64AppendU32(hash, atom->family);
    hash = gpu_fnv1a64AppendU32(hash, atom->id);
    hash = gpu_fnv1a64AppendU32(hash, atom->major);
    hash = gpu_fnv1a64AppendU32(hash, atom->minor);
    if (gpu_uslCapabilityAtomNeedsTextHash(atom)) {
      hash = gpu_fnv1a64Append(hash, atom->text, strnlen(atom->text, sizeof(atom->text)));
      hash = gpu_fnv1a64AppendU32(hash, 0u);
    }
  }

  return hash;
}

static uint64_t
gpu_hashUSLEntryTargetAtoms(const USLBytecodeEntryTargetInfo *targetInfo) {
  uint64_t hash = 14695981039346656037ull;

  if (!targetInfo) {
    return 0;
  }

  hash = gpu_fnv1a64AppendU32(hash, targetInfo->target_atom_total_count);
  for (uint32_t i = 0; i < targetInfo->target_atom_count; i++) {
    const USLCapabilityAtomDesc *atom = &targetInfo->target_atoms[i];
    hash = gpu_fnv1a64AppendU32(hash, atom->family);
    hash = gpu_fnv1a64AppendU32(hash, atom->id);
    hash = gpu_fnv1a64AppendU32(hash, atom->major);
    hash = gpu_fnv1a64AppendU32(hash, atom->minor);
    if (gpu_uslCapabilityAtomNeedsTextHash(atom)) {
      hash = gpu_fnv1a64Append(hash, atom->text, strnlen(atom->text, sizeof(atom->text)));
      hash = gpu_fnv1a64AppendU32(hash, 0u);
    }
  }

  return hash;
}

static uint64_t
gpu_hashUSLSelectedEntries(const char * const *entryPointNames,
                           uint32_t entryPointCount) {
  uint64_t hash = 14695981039346656037ull;

  if (!entryPointNames || entryPointCount == 0u) {
    return 0;
  }

  hash = gpu_fnv1a64AppendU32(hash, entryPointCount);
  for (uint32_t i = 0; i < entryPointCount; i++) {
    const char *name = entryPointNames[i];
    uint64_t len;

    if (!name || name[0] == '\0') {
      return 0;
    }

    len = (uint64_t)strlen(name);
    hash = gpu_fnv1a64Append(hash, name, len);
    hash = gpu_fnv1a64AppendU32(hash, 0u);
  }

  return hash;
}

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
gpu_uslRuntimeInfoIsUsable(const USLBytecodeRuntimeInfo *runtimeInfo) {
  return runtimeInfo &&
         runtimeInfo->abi_version == USL_RUNTIME_INFO_VERSION &&
         runtimeInfo->bytecode_version == GPU_USL_BYTECODE_VERSION;
}

static int
gpu_uslEntrySelected(const char *entry,
                     const char * const *entryPointNames,
                     uint32_t entryPointCount) {
  if (!entry || entry[0] == '\0') {
    return 0;
  }
  if (!entryPointNames || entryPointCount == 0u) {
    return 1;
  }

  for (uint32_t i = 0; i < entryPointCount; i++) {
    if (entryPointNames[i] && strcmp(entry, entryPointNames[i]) == 0) {
      return 1;
    }
  }

  return 0;
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
gpu_bindingTypeFromUSLResource(const USLRuntimeResource *resource,
                               GPUBindingType *outType) {
  if (!resource || !outType) {
    return 0;
  }

  switch (resource->kind) {
    case USL_RUNTIME_RESOURCE_BUFFER:
      *outType = GPU_BINDING_UNIFORM_BUFFER;
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
gpu_appendShaderResourceReflection(GPUShaderReflection *reflection,
                                   const USLRuntimeResource *resource,
                                   GPUBindingType bindingType,
                                   GPUShaderStageFlags visibility) {
  GPUShaderResourceReflection *resources;
  GPUShaderResourceReflection *grown;
  uint32_t binding;
  uint32_t setIndex;
  size_t nextCount;

  if (!reflection || !resource || resource->slot < 0 || visibility == 0) {
    return 0;
  }

  binding = (uint32_t)resource->slot;
  setIndex = 0u;
  resources = (GPUShaderResourceReflection *)reflection->pResources;

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    if (resources[i].setIndex == setIndex &&
        resources[i].binding == binding &&
        resources[i].bindingType == bindingType) {
      resources[i].visibility |= visibility;
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
  resources[reflection->resourceCount].setIndex = setIndex;
  resources[reflection->resourceCount].binding = binding;
  resources[reflection->resourceCount].bindingType = bindingType;
  resources[reflection->resourceCount].visibility = visibility;
  resources[reflection->resourceCount].arrayCount = 1u;
  reflection->resourceCount++;
  return 1;
}

static int
gpu_setShaderLibraryReflection(GPUShaderLibrary *library,
                               const USReflection *usReflection,
                               const char * const *entryPointNames,
                               uint32_t entryPointCount) {
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
  for (uint32_t i = 0; i < runtimeInfo->resource_count; i++) {
    const USLRuntimeResource *resource = &runtimeInfo->resources[i];
    GPUShaderStageFlags visibility;
    GPUBindingType bindingType;

    if (!resource->used ||
        !gpu_uslEntrySelected(resource->entry, entryPointNames, entryPointCount)) {
      continue;
    }

    if (!gpu_shaderVisibilityFromUSLStage(resource->stage, &visibility) ||
        !gpu_bindingTypeFromUSLResource(resource, &bindingType) ||
        !gpu_appendShaderResourceReflection(&library->_reflection,
                                            resource,
                                            bindingType,
                                            visibility)) {
      gpu_clearShaderReflection(&library->_reflection);
      return 0;
    }
  }

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

static void
gpu_setShaderLibraryUSLInfo(GPUShaderLibrary *library,
                            const USLBytecodeTargetInfo *targetInfo,
                            uint32_t sourceKind,
                            uint64_t backendContentHash) {
  GPUShaderLibraryUSLInfo *info;

  if (!library || !targetInfo) {
    return;
  }

  info = &library->_uslInfo;
  memset(info, 0, sizeof(*info));
  info->abiVersion = GPU_SHADER_LIBRARY_USL_INFO_VERSION;
  info->targetBackend = (uint32_t)targetInfo->backend;
  info->bytecodeVersion = targetInfo->bytecode_version;
  info->sourceKind = sourceKind;
  info->targetAtomCount = targetInfo->target_atom_count;
  info->targetAtomTotalCount = targetInfo->target_atom_total_count;
  info->targetInfoFlags = targetInfo->flags;
  info->targetSupported = targetInfo->supported;
  info->targetSupportStatus = targetInfo->status;
  info->bytecodeSize = targetInfo->bytecode_size;
  info->bytecodeDataSize = targetInfo->bytecode_data_size;
  info->bytecodeContentHash = targetInfo->content_hash;
  info->targetAtomHash = gpu_hashUSLTargetAtoms(targetInfo);
  info->backendContentHash = backendContentHash;
}

static void
gpu_setShaderLibraryUSLInfoForEntries(GPUShaderLibrary *library,
                                      const USLBytecodeEntryTargetInfo *targetInfo,
                                      uint32_t sourceKind,
                                      uint64_t backendContentHash,
                                      uint32_t selectedEntryCount,
                                      uint64_t selectedEntryHash) {
  GPUShaderLibraryUSLInfo *info;

  if (!library || !targetInfo) {
    return;
  }

  info = &library->_uslInfo;
  memset(info, 0, sizeof(*info));
  info->abiVersion = GPU_SHADER_LIBRARY_USL_INFO_VERSION;
  info->targetBackend = (uint32_t)targetInfo->backend;
  info->bytecodeVersion = targetInfo->bytecode_version;
  info->sourceKind = sourceKind;
  info->targetAtomCount = targetInfo->target_atom_count;
  info->targetAtomTotalCount = targetInfo->target_atom_total_count;
  info->targetInfoFlags = targetInfo->flags;
  info->selectedEntryCount = selectedEntryCount;
  info->entryTargetInfoVersion = targetInfo->abi_version;
  info->targetSupported = targetInfo->supported;
  info->targetSupportStatus = targetInfo->status;
  info->bytecodeSize = targetInfo->bytecode_size;
  info->bytecodeDataSize = targetInfo->bytecode_data_size;
  info->bytecodeContentHash = targetInfo->content_hash;
  info->targetAtomHash = gpu_hashUSLEntryTargetAtoms(targetInfo);
  info->backendContentHash = backendContentHash;
  info->selectedEntryHash = selectedEntryHash;
}

//GPU_EXPORT
//USLibrary*
//GPUDefaultShaderLibrary(GPUDevice *device) {
//  GPUApi *api;
//
//  if (!(api = gpuActiveGPUApi()))
//    return NULL;
//
//  return NULL;
//}

GPU_EXPORT
GPULibrary*
GPUDefaultLibrary(GPUDevice *device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->library.defaultLibrary(device);
}

GPU_EXPORT
GPUFunction*
GPUShaderFunction(GPULibrary *lib, const char *name) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->library.newFunction(lib, name);
}

static int
gpu_createShaderLibraryFromUSLBytecodeImpl(GPUDevice *device,
                                           const void *bytecodeData,
                                           uint64_t bytecodeSize,
                                           const char * const *entryPointNames,
                                           uint32_t entryPointCount,
                                           GPUShaderLibrary **outLibrary);

static GPUResult
gpu_createShaderLibraryFromMSLText(GPUDevice *device,
                                   const GPUShaderLibraryCreateInfo *info,
                                   GPUShaderLibrary **outLibrary) {
  GPUApi *api;
  char *source;

  if (!(api = gpuActiveGPUApi())) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (!api->library.newLibraryWithSource || info->defineCount > 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->sourceSize > (uint64_t)SIZE_MAX - 1u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  source = calloc(1, (size_t)info->sourceSize + 1u);
  if (!source) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  memcpy(source, info->sourceData, (size_t)info->sourceSize);
  source[(size_t)info->sourceSize] = '\0';

  *outLibrary = api->library.newLibraryWithSource(device, source, info->sourceSize);
  free(source);

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
      return gpu_createShaderLibraryFromUSLBytecodeImpl(device,
                                                        info->sourceData,
                                                        info->sourceSize,
                                                        NULL,
                                                        0u,
                                                        outLibrary) == 0
        ? GPU_OK
        : GPU_ERROR_BACKEND_FAILURE;
    case GPU_SHADER_SOURCE_USL_TEXT:
    case GPU_SHADER_SOURCE_SPIRV_BINARY:
    default:
      return GPU_ERROR_INVALID_ARGUMENT;
  }
}

static int
gpu_createShaderLibraryFromUSLBytecodeImpl(GPUDevice *device,
                                           const void *bytecodeData,
                                           uint64_t bytecodeSize,
                                           const char * const *entryPointNames,
                                           uint32_t entryPointCount,
                                           GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};
  GPUApi *api;
  USLTargetSpec target;
  USCompileInput compileInput;
  USCompileOutput compileOutput;
  uint64_t selectedEntryHash = 0;
  int useSelectedEntries;
  int rc;

  if (!device || !bytecodeData || !outLibrary) {
    return -1;
  }
  *outLibrary = NULL;
  memset(&compileOutput, 0, sizeof(compileOutput));

  useSelectedEntries = entryPointCount > 0u;
  if (useSelectedEntries) {
    selectedEntryHash = gpu_hashUSLSelectedEntries(entryPointNames, entryPointCount);
    if (selectedEntryHash == 0u) {
      return -1;
    }
  } else if (entryPointNames) {
    return -1;
  }

  if (!(api = gpuActiveGPUApi())) {
    return -2;
  }

  if (api->backend != GPU_BACKEND_METAL ||
      !gpu_uslDefaultMetalTarget(&target)) {
    return -3;
  }

  memset(&compileInput, 0, sizeof(compileInput));
  compileInput.abi_version = US_COMPILE_INPUT_VERSION;
  compileInput.artifact = bytecodeData;
  compileInput.artifact_size = (size_t)bytecodeSize;
  compileInput.target = &target;
  compileInput.entry_point_names = entryPointNames;
  compileInput.entry_point_count = entryPointCount;
  if (us_compile(&compileInput, &compileOutput) != USLOk ||
      compileOutput.backend != USL_BACKEND_METAL ||
      compileOutput.encoding != USL_RUNTIME_EMBEDDED_BLOB_ENCODING_TEXT ||
      !compileOutput.backend_data ||
      compileOutput.backend_size == 0) {
    us_free_compile_output(&compileOutput);
    return -4;
  }
  if ((useSelectedEntries && compileOutput.reflection.entry_target_count == 0u) ||
      (!useSelectedEntries && !compileOutput.reflection.target_info_valid)) {
    us_free_compile_output(&compileOutput);
    return -6;
  }

  info.label = (compileOutput.flags & US_COMPILE_OUTPUT_FLAG_EMBEDDED_BLOB) != 0u
                 ? "embedded-usl-metal"
                 : "compiled-usl-metal";
  info.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = compileOutput.backend_data;
  info.sourceSize = compileOutput.backend_size;
  info.sourcePathHint = NULL;
  rc = GPUCreateShaderLibrary(device, &info, outLibrary);
  if (rc == 0 && outLibrary && *outLibrary) {
    if (!gpu_setShaderLibraryReflection(*outLibrary,
                                         &compileOutput.reflection,
                                         entryPointNames,
                                         entryPointCount)) {
      GPUDestroyShaderLibrary(*outLibrary);
      *outLibrary = NULL;
      us_free_compile_output(&compileOutput);
      return -7;
    }

    if (useSelectedEntries) {
      gpu_setShaderLibraryUSLInfoForEntries(*outLibrary,
                                            &compileOutput.reflection.entry_targets[0],
                                            (compileOutput.flags & US_COMPILE_OUTPUT_FLAG_EMBEDDED_BLOB) != 0u
                                              ? GPUShaderLibraryUSLSourceEmbedded
                                              : GPUShaderLibraryUSLSourceGenerated,
                                            compileOutput.backend_hash,
                                            entryPointCount,
                                            selectedEntryHash);
    } else {
      gpu_setShaderLibraryUSLInfo(*outLibrary,
                                  &compileOutput.reflection.target_info,
                                  (compileOutput.flags & US_COMPILE_OUTPUT_FLAG_EMBEDDED_BLOB) != 0u
                                    ? GPUShaderLibraryUSLSourceEmbedded
                                    : GPUShaderLibraryUSLSourceGenerated,
                                  compileOutput.backend_hash);
    }
  }
  us_free_compile_output(&compileOutput);
  return rc;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibraryFromUSL(GPUDevice *device,
                              const void *artifactData,
                              uint64_t artifactSize,
                              GPUShaderLibrary **outLibrary) {
  return gpu_createShaderLibraryFromUSLBytecodeImpl(device,
                                                    artifactData,
                                                    artifactSize,
                                                    NULL,
                                                    0u,
                                                    outLibrary) == 0
    ? GPU_OK
    : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
GPUResult
GPUCreateShaderLibraryFromUSLEntries(GPUDevice *device,
                                     const void *artifactData,
                                     uint64_t artifactSize,
                                     const char * const *entryPointNames,
                                     uint32_t entryPointCount,
                                     GPUShaderLibrary **outLibrary) {
  if (!entryPointNames || entryPointCount == 0u) {
    if (outLibrary) {
      *outLibrary = NULL;
    }
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return gpu_createShaderLibraryFromUSLBytecodeImpl(device,
                                                    artifactData,
                                                    artifactSize,
                                                    entryPointNames,
                                                    entryPointCount,
                                                    outLibrary) == 0
    ? GPU_OK
    : GPU_ERROR_BACKEND_FAILURE;
}

GPU_EXPORT
int
GPUCreateShaderLibraryFromUSLBytecode(GPUDevice *device,
                                      const void *bytecodeData,
                                      uint64_t bytecodeSize,
                                      GPUShaderLibrary **outLibrary) {
  return gpu_createShaderLibraryFromUSLBytecodeImpl(device,
                                                    bytecodeData,
                                                    bytecodeSize,
                                                    NULL,
                                                    0u,
                                                    outLibrary);
}

GPU_EXPORT
int
GPUCreateShaderLibraryFromUSLBytecodeForEntries(
                                      GPUDevice *device,
                                      const void *bytecodeData,
                                      uint64_t bytecodeSize,
                                      const char * const *entryPointNames,
                                      uint32_t entryPointCount,
                                      GPUShaderLibrary **outLibrary) {
  if (!entryPointNames || entryPointCount == 0u) {
    if (outLibrary) {
      *outLibrary = NULL;
    }
    return -1;
  }

  return gpu_createShaderLibraryFromUSLBytecodeImpl(device,
                                                    bytecodeData,
                                                    bytecodeSize,
                                                    entryPointNames,
                                                    entryPointCount,
                                                    outLibrary);
}

GPU_EXPORT
int
GPUGetShaderLibraryUSLInfo(GPUShaderLibrary *library,
                           GPUShaderLibraryUSLInfo *outInfo) {
  if (!library || !outInfo) {
    return -1;
  }

  memset(outInfo, 0, sizeof(*outInfo));
  if (library->_uslInfo.abiVersion != GPU_SHADER_LIBRARY_USL_INFO_VERSION) {
    return -2;
  }

  *outInfo = library->_uslInfo;
  return 0;
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
  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->library.destroyLibrary) {
    api->library.destroyLibrary(library);
  } else {
    free(library);
  }
}
