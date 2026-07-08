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
shader_reflection_has_resource(const GPUShaderReflection *reflection,
                               GPUBindingType bindingType,
                               GPUShaderStageFlags visibility,
                               uint32_t binding) {
  if (!reflection || (!reflection->pResources && reflection->resourceCount > 0u)) {
    return 0;
  }

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *item = &reflection->pResources[i];
    if (item->setIndex == 0u &&
        item->binding == binding &&
        item->bindingType == bindingType &&
        item->visibility == visibility &&
        item->arrayCount == 1u) {
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
check_selected_shader_library(const void *bytecode,
                              uint64_t bytecodeSize,
                              uint32_t expectedSourceKind) {
  const char *entries[] = { "reflect_vs", "reflect_fs" };
  const char *emptyNameEntries[] = { "reflect_vs", "" };
  const char *missingEntries[] = { "reflect_vs", "missing_entry" };
  GPUShaderLibraryUSLInfo info;
  GPUShaderReflection shaderReflection;
  GPUBindGroupLayout *reflectedLayouts[1] = { NULL };
  GPUShaderLayout *shaderLayout;
  GPUPipelineLayout *pipelineLayout;
  GPUPhysicalDevice *physicalDevice;
  GPUShaderLibrary *library;
  GPUFunction *vertexFunc;
  GPUFunction *fragmentFunc;
  GPUDevice *device;
  uint32_t reflectedLayoutCount;
  int ok;

  physicalDevice = GPUGetAutoSelectedPhysicalDevice(NULL);
  if (!physicalDevice) {
    fprintf(stderr, "failed to get physical device\n");
    return 0;
  }

  device = GPUCreateDeviceWithDefaultQueues(physicalDevice);
  if (!device) {
    fprintf(stderr, "failed to create device\n");
    return 0;
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSLEntries(device,
                                           bytecode,
                                           bytecodeSize,
                                           entries,
                                           0u,
                                           &library) == GPU_OK ||
      library != NULL) {
    fprintf(stderr, "selected-entry shader library accepted empty entry list\n");
    if (library) {
      GPUDestroyShaderLibrary(library);
    }
    return 0;
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSLEntries(device,
                                           bytecode,
                                           bytecodeSize,
                                           emptyNameEntries,
                                           2u,
                                           &library) == GPU_OK ||
      library != NULL) {
    fprintf(stderr, "selected-entry shader library accepted empty entry name\n");
    if (library) {
      GPUDestroyShaderLibrary(library);
    }
    return 0;
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSLEntries(device,
                                           bytecode,
                                           bytecodeSize,
                                           missingEntries,
                                           2u,
                                           &library) == GPU_OK ||
      library != NULL) {
    fprintf(stderr, "selected-entry shader library accepted missing entry\n");
    if (library) {
      GPUDestroyShaderLibrary(library);
    }
    return 0;
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSLEntries(device,
                                           bytecode,
                                           bytecodeSize,
                                           entries,
                                           2u,
                                           &library) != GPU_OK ||
      !library) {
    fprintf(stderr, "failed to create selected-entry shader library\n");
    return 0;
  }

  vertexFunc = GPUShaderFunction(library, "reflect_vs");
  fragmentFunc = GPUShaderFunction(library, "reflect_fs");
  memset(&shaderReflection, 0, sizeof(shaderReflection));
  shaderLayout = NULL;
  reflectedLayoutCount = 0u;
  pipelineLayout = NULL;

  ok = GPUGetShaderLibraryUSLInfo(library, &info) == 0 &&
       info.abiVersion == GPU_SHADER_LIBRARY_USL_INFO_VERSION &&
       info.sourceKind == expectedSourceKind &&
       info.bytecodeSize == bytecodeSize &&
       info.bytecodeContentHash != 0u &&
       info.targetBackend != 0u &&
       info.targetAtomCount >= 1u &&
       info.targetAtomTotalCount >= info.targetAtomCount &&
       info.targetInfoFlags == 0u &&
       info.targetAtomHash != 0u &&
       info.backendContentHash != 0u &&
       info.selectedEntryCount == 2u &&
       info.entryTargetInfoVersion != 0u &&
       info.targetSupported == 1u &&
       info.targetSupportStatus == 1u &&
       info.selectedEntryHash != 0u &&
       vertexFunc != NULL &&
       fragmentFunc != NULL &&
       GPUGetShaderReflection(library, &shaderReflection) == GPU_OK &&
       shaderReflection.resourceCount == 2u &&
       shader_reflection_has_resource(&shaderReflection,
                                      GPU_BINDING_SAMPLED_TEXTURE,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u) &&
       shader_reflection_has_resource(&shaderReflection,
                                      GPU_BINDING_UNIFORM_BUFFER,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u) &&
       GPUCreateBindGroupLayoutsFromReflection(device,
                                               library,
                                               &reflectedLayoutCount,
                                               NULL) == GPU_OK &&
       reflectedLayoutCount == 1u;
  if (ok) {
    reflectedLayoutCount = 0u;
    ok = GPUCreateBindGroupLayoutsFromReflection(device,
                                                 library,
                                                 &reflectedLayoutCount,
                                                 reflectedLayouts) == GPU_ERROR_INSUFFICIENT_CAPACITY &&
         reflectedLayoutCount == 1u;
  }
  if (ok) {
    ok = GPUCreateBindGroupLayoutsFromReflection(device,
                                                 library,
                                                 &reflectedLayoutCount,
                                                 reflectedLayouts) == GPU_OK &&
         reflectedLayoutCount == 1u &&
         reflectedLayouts[0] != NULL &&
         GPUCreatePipelineLayoutFromReflection(device,
                                               library,
                                               reflectedLayoutCount,
                                               reflectedLayouts,
                                               &pipelineLayout) == GPU_OK &&
         pipelineLayout != NULL;
  }
  if (pipelineLayout) {
    GPUDestroyPipelineLayout(pipelineLayout);
  }
  if (reflectedLayouts[0]) {
    GPUDestroyBindGroupLayout(reflectedLayouts[0]);
  }
  GPUFreeShaderReflection(&shaderReflection);
  GPUDestroyShaderLibrary(library);

  if (!ok) {
    fprintf(stderr, "unexpected selected-entry shader library data\n");
    return 0;
  }

  {
    GPUShaderLibraryCreateInfo createInfo = {0};

    createInfo.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
    createInfo.chain.structSize = sizeof(createInfo);
    createInfo.label = "reflection.us";
    createInfo.sourceKind = GPU_SHADER_SOURCE_USL_BYTECODE;
    createInfo.sourceData = bytecode;
    createInfo.sourceSize = bytecodeSize;
    createInfo.generateReflection = true;

    library = NULL;
    if (GPUCreateShaderLibrary(device, &createInfo, &library) != GPU_OK ||
        !library) {
      fprintf(stderr, "failed to create canonical shader library\n");
      return 0;
    }

    memset(&shaderReflection, 0, sizeof(shaderReflection));
    ok = GPUGetShaderLibraryUSLInfo(library, &info) == 0 &&
         info.abiVersion == GPU_SHADER_LIBRARY_USL_INFO_VERSION &&
         info.sourceKind == expectedSourceKind &&
         info.bytecodeSize == bytecodeSize &&
         info.bytecodeContentHash != 0u &&
         info.backendContentHash != 0u &&
         info.selectedEntryCount == 0u &&
         GPUGetShaderReflection(library, &shaderReflection) == GPU_OK &&
         shaderReflection.resourceCount == 2u &&
         GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
         shaderLayout != NULL &&
         shaderLayout->bindGroupLayoutCount == 1u &&
         shaderLayout->bindGroupLayouts[0] != NULL &&
         shaderLayout->pipelineLayout != NULL;
    GPUDestroyShaderLayout(shaderLayout);
    GPUFreeShaderReflection(&shaderReflection);
    GPUDestroyShaderLibrary(library);

    if (!ok) {
      fprintf(stderr, "unexpected canonical shader library data\n");
      return 0;
    }
  }

  return 1;
}

int
main(int argc, char **argv) {
  uint32_t expectedSourceKind;
  uint64_t bytecodeSize;
  void *bytecode;
  int ok;

  if (argc != 2 && argc != 3) {
    fprintf(stderr, "usage: %s <reflection.us> [generated|embedded]\n", argv[0]);
    return 2;
  }

  expectedSourceKind = GPUShaderLibraryUSLSourceGenerated;
  if (argc == 3) {
    if (strcmp(argv[2], "generated") == 0) {
      expectedSourceKind = GPUShaderLibraryUSLSourceGenerated;
    } else if (strcmp(argv[2], "embedded") == 0) {
      expectedSourceKind = GPUShaderLibraryUSLSourceEmbedded;
    } else {
      fprintf(stderr, "unknown expected source kind: %s\n", argv[2]);
      return 2;
    }
  }

  bytecode = read_file(argv[1], &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "failed to read bytecode: %s\n", argv[1]);
    return 2;
  }

  ok = check_fragment_entry(bytecode, bytecodeSize) &&
       check_vertex_entry(bytecode, bytecodeSize) &&
       check_selected_shader_library(bytecode,
                                     bytecodeSize,
                                     expectedSourceKind);
  free(bytecode);

  if (!ok) {
    return 1;
  }

  puts("USL reflection check passed");
  return 0;
}
