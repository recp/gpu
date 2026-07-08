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

GPU_EXPORT
int
GPUCreateShaderLibrary(GPUDevice *device,
                       const GPUShaderLibraryCreateInfo *info,
                       GPUShaderLibrary **outLibrary) {
  GPUApi *api;
  char *source;

  if (!device || !info || !outLibrary || !info->sourceData) {
    return -1;
  }

  if (!(api = gpuActiveGPUApi()))
    return -2;

  if (info->sourceKind != GPU_SHADER_SOURCE_MSL_TEXT ||
      !api->library.newLibraryWithSource) {
    return -3;
  }

  source = calloc(1, (size_t)info->sourceSize + 1u);
  if (!source) {
    return -4;
  }

  memcpy(source, info->sourceData, (size_t)info->sourceSize);
  source[info->sourceSize] = '\0';

  *outLibrary = api->library.newLibraryWithSource(device, source, info->sourceSize);
  free(source);

  return *outLibrary ? 0 : -5;
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
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = compileOutput.backend_data;
  info.sourceSize = compileOutput.backend_size;
  info.sourcePathHint = NULL;
  rc = GPUCreateShaderLibrary(device, &info, outLibrary);
  if (rc == 0 && outLibrary && *outLibrary) {
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
void
GPUDestroyShaderLibrary(GPUShaderLibrary *library) {
  GPUApi *api;

  if (!library)
    return;

  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->library.destroyLibrary) {
    api->library.destroyLibrary(library);
  } else {
    free(library);
  }
}
