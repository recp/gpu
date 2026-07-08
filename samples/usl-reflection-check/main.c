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

#include <gpu/bindgroup.h>
#include <gpu/gpu.h>
#include <gpu/usl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *
read_file(const char *path, uint64_t *outSize) {
  unsigned char *bytes;
  long length;
  FILE *file;

  if (!path || !outSize) {
    return NULL;
  }

  *outSize = 0;
  file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  length = ftell(file);
  if (length <= 0) {
    fclose(file);
    return NULL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  bytes = malloc((size_t)length);
  if (!bytes) {
    fclose(file);
    return NULL;
  }

  if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    free(bytes);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)length;
  return bytes;
}

static int
reflection_has_binding(const GPUUSLEntryReflection *reflection,
                       GPUUSLStage stage,
                       GPUUSLResourceKind kind,
                       uint32_t binding) {
  if (!reflection) {
    return 0;
  }

  for (uint32_t i = 0; i < reflection->resourceBindingCount; i++) {
    const GPUUSLResourceBindingDesc *item = &reflection->resourceBindings[i];
    if (item->stage == stage && item->kind == kind && item->binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
layout_has_entry(const GPUBindGroupLayoutEntry *entries,
                 uint32_t count,
                 GPUBindStage stage,
                 GPUBindKind kind,
                 uint32_t binding) {
  if (!entries && count > 0) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].kind == kind &&
        entries[i].binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
layout_has_typed_entry(const GPUBindGroupLayoutEntry *entries,
                       uint32_t count,
                       GPUBindStage stage,
                       GPUBindKind kind,
                       GPUBindingType bindingType,
                       uint32_t binding) {
  if (!entries && count > 0) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].kind == kind &&
        entries[i].bindingType == bindingType &&
        entries[i].binding == binding) {
      return 1;
    }
  }

  return 0;
}

static int
check_fragment_entry(const void *bytecode, uint64_t bytecodeSize) {
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBindGroupLayoutUSLInfo layoutInfo;
  GPUUSLEntryReflection *reflection;
  GPUBindGroupLayout *layout;
  uint32_t layoutCount;
  int ok;

  reflection = NULL;
  if (GPUReflectUSLBytecodeEntry(bytecode,
                                 bytecodeSize,
                                 "reflect_fs",
                                 &reflection) != 0 ||
      !reflection) {
    fprintf(stderr, "failed to reflect fragment entry\n");
    return 0;
  }

  ok = reflection->stage == GPUUSLStageFragment &&
       reflection->resourceBindingCount == 2u &&
       reflection->bytecodeSize == bytecodeSize &&
       reflection->bytecodeContentHash != 0u &&
       reflection->capabilityRequirementCount == 1u &&
       reflection->capabilityRequirementTotalCount == 1u &&
       reflection->capabilityRequirementFlags == 0u &&
       reflection->capabilityRequirementHash != 0u &&
       reflection->entryTargetInfoVersion != 0u &&
       reflection->targetBackend != 0u &&
       reflection->targetSupported == 1u &&
       reflection->targetSupportStatus == 1u &&
       reflection->targetAtomCount >= 1u &&
       reflection->targetAtomTotalCount >= reflection->targetAtomCount &&
       reflection->targetInfoFlags == 0u &&
       reflection->targetAtomHash != 0u &&
       reflection_has_binding(reflection,
                              GPUUSLStageFragment,
                              GPUUSLResourceKindTexture,
                              0u) &&
       reflection_has_binding(reflection,
                              GPUUSLStageFragment,
                              GPUUSLResourceKindBuffer,
                              0u) &&
       reflection->staticSamplerCount == 1u &&
       reflection->staticSamplers[0].minFilter == GPUUSLSamplerFilterLinear &&
       reflection->staticSamplers[0].magFilter == GPUUSLSamplerFilterLinear &&
       reflection->staticSamplers[0].mipFilter == GPUUSLSamplerFilterLinear &&
       reflection->staticSamplers[0].addressMode == GPUUSLSamplerAddressRepeat &&
       GPUUSLStaticSamplerDescIsValid(&reflection->staticSamplers[0]);
  GPUFreeUSLEntryReflection(reflection);
  if (!ok) {
    fprintf(stderr, "unexpected fragment reflection data\n");
    return 0;
  }

  layout = NULL;
  if (GPUCreateBindGroupLayoutFromUSLBytecode(bytecode,
                                              bytecodeSize,
                                              "reflect_fs",
                                              &layout) != 0 ||
      !layout) {
    fprintf(stderr, "failed to create fragment bind layout\n");
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(layout, &layoutCount);
  ok = layoutCount == 2u &&
       GPUGetBindGroupLayoutUSLInfo(layout, &layoutInfo) == 0 &&
       layoutInfo.abiVersion == GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION &&
       layoutInfo.stage == GPUBindStageFragment &&
       strcmp(layoutInfo.entryPointName, "reflect_fs") == 0 &&
       layoutInfo.resourceBindingCount == 2u &&
       layoutInfo.bytecodeSize == bytecodeSize &&
       layoutInfo.bytecodeContentHash != 0u &&
       layoutInfo.capabilityRequirementCount == 1u &&
       layoutInfo.capabilityRequirementTotalCount == 1u &&
       layoutInfo.capabilityRequirementFlags == 0u &&
       layoutInfo.capabilityRequirementHash != 0u &&
       layoutInfo.entryTargetInfoVersion != 0u &&
       layoutInfo.targetBackend != 0u &&
       layoutInfo.targetSupported == 1u &&
       layoutInfo.targetSupportStatus == 1u &&
       layoutInfo.targetAtomCount >= 1u &&
       layoutInfo.targetAtomTotalCount >= layoutInfo.targetAtomCount &&
       layoutInfo.targetInfoFlags == 0u &&
       layoutInfo.targetAtomHash != 0u &&
       layout_has_entry(layoutEntries,
                        layoutCount,
                        GPUBindStageFragment,
                        GPUBindKindTexture,
                        0u) &&
       layout_has_entry(layoutEntries,
                        layoutCount,
                        GPUBindStageFragment,
                        GPUBindKindBuffer,
                        0u);
  GPUDestroyBindGroupLayout(layout);
  if (!ok) {
    fprintf(stderr, "unexpected fragment bind layout\n");
    return 0;
  }

  return 1;
}

static int
check_vertex_entry(const void *bytecode, uint64_t bytecodeSize) {
  GPUUSLEntryReflection *reflection;
  GPUBindGroupLayoutUSLInfo layoutInfo;
  GPUBindGroupLayout *layout;
  uint32_t layoutCount;
  int ok;

  reflection = NULL;
  if (GPUReflectUSLBytecodeEntry(bytecode,
                                 bytecodeSize,
                                 "reflect_vs",
                                 &reflection) != 0 ||
      !reflection) {
    fprintf(stderr, "failed to reflect vertex entry\n");
    return 0;
  }

  ok = reflection->stage == GPUUSLStageVertex &&
       reflection->bytecodeSize == bytecodeSize &&
       reflection->bytecodeContentHash != 0u &&
       reflection->capabilityRequirementCount == 0u &&
       reflection->capabilityRequirementTotalCount == 0u &&
       reflection->capabilityRequirementFlags == 0u &&
       reflection->capabilityRequirementHash == 0u &&
       reflection->entryTargetInfoVersion != 0u &&
       reflection->targetBackend != 0u &&
       reflection->targetSupported == 1u &&
       reflection->targetSupportStatus == 1u &&
       reflection->targetAtomCount >= 1u &&
       reflection->targetAtomTotalCount >= reflection->targetAtomCount &&
       reflection->targetInfoFlags == 0u &&
       reflection->targetAtomHash != 0u &&
       reflection->resourceBindingCount == 0u &&
       reflection->staticSamplerCount == 0u;
  GPUFreeUSLEntryReflection(reflection);
  if (!ok) {
    fprintf(stderr, "unexpected vertex reflection data\n");
    return 0;
  }

  layout = NULL;
  if (GPUCreateBindGroupLayoutFromUSLBytecode(bytecode,
                                              bytecodeSize,
                                              "reflect_vs",
                                              &layout) != 0 ||
      !layout) {
    fprintf(stderr, "failed to create vertex bind layout\n");
    return 0;
  }

  (void)GPUGetBindGroupLayoutEntries(layout, &layoutCount);
  ok = layoutCount == 0u &&
       GPUGetBindGroupLayoutUSLInfo(layout, &layoutInfo) == 0 &&
       layoutInfo.abiVersion == GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION &&
       layoutInfo.stage == GPUBindStageVertex &&
       strcmp(layoutInfo.entryPointName, "reflect_vs") == 0 &&
       layoutInfo.resourceBindingCount == 0u &&
       layoutInfo.bytecodeSize == bytecodeSize &&
       layoutInfo.bytecodeContentHash != 0u &&
       layoutInfo.capabilityRequirementCount == 0u &&
       layoutInfo.capabilityRequirementTotalCount == 0u &&
       layoutInfo.capabilityRequirementFlags == 0u &&
       layoutInfo.capabilityRequirementHash == 0u &&
       layoutInfo.entryTargetInfoVersion != 0u &&
       layoutInfo.targetBackend != 0u &&
       layoutInfo.targetSupported == 1u &&
       layoutInfo.targetSupportStatus == 1u &&
       layoutInfo.targetAtomCount >= 1u &&
       layoutInfo.targetAtomTotalCount >= layoutInfo.targetAtomCount &&
       layoutInfo.targetInfoFlags == 0u &&
       layoutInfo.targetAtomHash != 0u;
  GPUDestroyBindGroupLayout(layout);
  if (!ok) {
    fprintf(stderr, "unexpected vertex bind layout\n");
    return 0;
  }

  return 1;
}

static int
check_compute_entry(const void *bytecode, uint64_t bytecodeSize) {
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBindGroupLayoutUSLInfo layoutInfo;
  GPUUSLEntryReflection *reflection;
  GPUBindGroupLayout *layout;
  uint32_t layoutCount;
  int ok;

  reflection = NULL;
  if (GPUReflectUSLBytecodeEntry(bytecode,
                                 bytecodeSize,
                                 "reflect_cs",
                                 &reflection) != 0 ||
      !reflection) {
    fprintf(stderr, "failed to reflect compute entry\n");
    return 0;
  }

  ok = reflection->stage == GPUUSLStageCompute &&
       reflection->workgroupSize[0] == 8u &&
       reflection->workgroupSize[1] == 8u &&
       reflection->workgroupSize[2] == 1u &&
       reflection->resourceBindingCount == 1u &&
       reflection->bytecodeSize == bytecodeSize &&
       reflection->bytecodeContentHash != 0u &&
       reflection->capabilityRequirementCount == 0u &&
       reflection->capabilityRequirementTotalCount == 0u &&
       reflection->capabilityRequirementFlags == 0u &&
       reflection->capabilityRequirementHash == 0u &&
       reflection->entryTargetInfoVersion != 0u &&
       reflection->targetBackend != 0u &&
       reflection->targetSupported == 1u &&
       reflection->targetSupportStatus == 1u &&
       reflection->targetAtomCount >= 1u &&
       reflection->targetAtomTotalCount >= reflection->targetAtomCount &&
       reflection->targetInfoFlags == 0u &&
       reflection->targetAtomHash != 0u &&
       reflection_has_binding(reflection,
                              GPUUSLStageCompute,
                              GPUUSLResourceKindTexture,
                              0u) &&
       reflection->staticSamplerCount == 0u;
  GPUFreeUSLEntryReflection(reflection);
  if (!ok) {
    fprintf(stderr, "unexpected compute reflection data\n");
    return 0;
  }

  layout = NULL;
  if (GPUCreateBindGroupLayoutFromUSLBytecode(bytecode,
                                              bytecodeSize,
                                              "reflect_cs",
                                              &layout) != 0 ||
      !layout) {
    fprintf(stderr, "failed to create compute bind layout\n");
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(layout, &layoutCount);
  ok = layoutCount == 1u &&
       GPUGetBindGroupLayoutUSLInfo(layout, &layoutInfo) == 0 &&
       layoutInfo.abiVersion == GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION &&
       layoutInfo.stage == GPUBindStageCompute &&
       strcmp(layoutInfo.entryPointName, "reflect_cs") == 0 &&
       layoutInfo.resourceBindingCount == 1u &&
       layoutInfo.bytecodeSize == bytecodeSize &&
       layoutInfo.bytecodeContentHash != 0u &&
       layoutInfo.capabilityRequirementCount == 0u &&
       layoutInfo.capabilityRequirementTotalCount == 0u &&
       layoutInfo.capabilityRequirementFlags == 0u &&
       layoutInfo.capabilityRequirementHash == 0u &&
       layoutInfo.entryTargetInfoVersion != 0u &&
       layoutInfo.targetBackend != 0u &&
       layoutInfo.targetSupported == 1u &&
       layoutInfo.targetSupportStatus == 1u &&
       layoutInfo.targetAtomCount >= 1u &&
       layoutInfo.targetAtomTotalCount >= layoutInfo.targetAtomCount &&
       layoutInfo.targetInfoFlags == 0u &&
       layoutInfo.targetAtomHash != 0u &&
       layout_has_entry(layoutEntries,
                        layoutCount,
                        GPUBindStageCompute,
                        GPUBindKindTexture,
                        0u);
  GPUDestroyBindGroupLayout(layout);
  if (!ok) {
    fprintf(stderr, "unexpected compute bind layout\n");
    return 0;
  }

  return 1;
}

static int
check_storage_compute_entry(const void *bytecode, uint64_t bytecodeSize) {
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBindGroupLayoutUSLInfo layoutInfo;
  GPUUSLEntryReflection *reflection;
  GPUBindGroupLayout *layout;
  uint32_t layoutCount;
  int ok;

  reflection = NULL;
  if (GPUReflectUSLBytecodeEntry(bytecode,
                                 bytecodeSize,
                                 "reflect_storage_cs",
                                 &reflection) != 0 ||
      !reflection) {
    fprintf(stderr, "failed to reflect storage compute entry\n");
    return 0;
  }

  ok = reflection->stage == GPUUSLStageCompute &&
       reflection->workgroupSize[0] == 1u &&
       reflection->workgroupSize[1] == 1u &&
       reflection->workgroupSize[2] == 1u &&
       reflection->resourceBindingCount == 1u &&
       reflection_has_binding(reflection,
                              GPUUSLStageCompute,
                              GPUUSLResourceKindBuffer,
                              0u) &&
       reflection->staticSamplerCount == 0u;
  GPUFreeUSLEntryReflection(reflection);
  if (!ok) {
    fprintf(stderr, "unexpected storage compute reflection data\n");
    return 0;
  }

  layout = NULL;
  if (GPUCreateBindGroupLayoutFromUSLBytecode(bytecode,
                                              bytecodeSize,
                                              "reflect_storage_cs",
                                              &layout) != 0 ||
      !layout) {
    fprintf(stderr, "failed to create storage compute bind layout\n");
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(layout, &layoutCount);
  ok = layoutCount == 1u &&
       GPUGetBindGroupLayoutUSLInfo(layout, &layoutInfo) == 0 &&
       layoutInfo.abiVersion == GPU_BIND_GROUP_LAYOUT_USL_INFO_VERSION &&
       layoutInfo.stage == GPUBindStageCompute &&
       strcmp(layoutInfo.entryPointName, "reflect_storage_cs") == 0 &&
       layoutInfo.resourceBindingCount == 1u &&
       layout_has_typed_entry(layoutEntries,
                              layoutCount,
                              GPUBindStageCompute,
                              GPUBindKindBuffer,
                              GPU_BINDING_STORAGE_BUFFER,
                              0u);
  GPUDestroyBindGroupLayout(layout);
  if (!ok) {
    fprintf(stderr, "unexpected storage compute bind layout\n");
    return 0;
  }

  return 1;
}

int
main(int argc, char **argv) {
  uint64_t storageBytecodeSize;
  uint64_t bytecodeSize;
  void *storageBytecode;
  void *bytecode;
  int ok;

  if (argc < 2 || argc > 4) {
    fprintf(stderr,
            "usage: %s <reflection.us> [generated|embedded] [storage.us]\n",
            argv[0]);
    return 2;
  }

  if (argc >= 3) {
    if (strcmp(argv[2], "generated") != 0 &&
        strcmp(argv[2], "embedded") != 0) {
      fprintf(stderr, "unknown expected source kind: %s\n", argv[2]);
      return 2;
    }
  }

  bytecode = read_file(argv[1], &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "failed to read bytecode: %s\n", argv[1]);
    return 2;
  }

  storageBytecode = NULL;
  storageBytecodeSize = 0u;
  if (argc == 4) {
    storageBytecode = read_file(argv[3], &storageBytecodeSize);
    if (!storageBytecode) {
      fprintf(stderr, "failed to read storage bytecode: %s\n", argv[3]);
      free(bytecode);
      return 2;
    }
  }

  ok = check_fragment_entry(bytecode, bytecodeSize) &&
       check_vertex_entry(bytecode, bytecodeSize) &&
       check_compute_entry(bytecode, bytecodeSize) &&
       (!storageBytecode ||
        check_storage_compute_entry(storageBytecode, storageBytecodeSize));
  free(storageBytecode);
  free(bytecode);

  if (!ok) {
    return 1;
  }

  puts("USL reflection check passed");
  return 0;
}
