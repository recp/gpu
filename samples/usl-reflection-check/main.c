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
check_bind_group_validation(GPUDevice *device, GPUBindGroupLayout *layout) {
  unsigned char fakeBufferStorage;
  unsigned char fakeSamplerStorage;
  unsigned char fakeTextureStorage;
  GPUBuffer *fakeBuffer;
  GPUSampler *fakeSampler;
  GPUTextureView *fakeTexture;
  GPUBindGroupEntry validEntries[2];
  GPUBindGroupEntry missingEntries[1];
  GPUBindGroupEntry extraEntries[3];
  GPUBindGroupEntry duplicateEntries[3];
  GPUBindGroupEntry wrongTypeEntries[2];
  GPUBindGroupEntry ambiguousEntries[2];
  GPUBindGroupCreateInfo info;
  GPUBindGroup *group;

  fakeBuffer = (GPUBuffer *)(void *)&fakeBufferStorage;
  fakeSampler = (GPUSampler *)(void *)&fakeSamplerStorage;
  fakeTexture = (GPUTextureView *)(void *)&fakeTextureStorage;

  memset(validEntries, 0, sizeof(validEntries));
  validEntries[0].binding = 0u;
  validEntries[0].textureView = fakeTexture;
  validEntries[1].binding = 0u;
  validEntries[1].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  validEntries[1].buffer.buffer = fakeBuffer;
  validEntries[1].buffer.size = 16u;

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "reflection-valid-set0";
  info.layout = layout;
  info.entryCount = 2u;
  info.pEntries = validEntries;

  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) != GPU_OK || !group) {
    fprintf(stderr, "valid reflected bind group was rejected\n");
    return 0;
  }
  GPUDestroyBindGroup(group);

  memset(missingEntries, 0, sizeof(missingEntries));
  memcpy(missingEntries, validEntries, sizeof(missingEntries));
  info.label = "reflection-missing-set0";
  info.entryCount = 1u;
  info.pEntries = missingEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) == GPU_OK || group) {
    fprintf(stderr, "bind group accepted missing reflected entry\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  memset(extraEntries, 0, sizeof(extraEntries));
  memcpy(extraEntries, validEntries, sizeof(validEntries));
  extraEntries[2].binding = 1u;
  extraEntries[2].sampler = fakeSampler;
  info.label = "reflection-extra-set0";
  info.entryCount = 3u;
  info.pEntries = extraEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) == GPU_OK || group) {
    fprintf(stderr, "bind group accepted extra reflected entry\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  memset(duplicateEntries, 0, sizeof(duplicateEntries));
  memcpy(duplicateEntries, validEntries, sizeof(validEntries));
  duplicateEntries[2] = validEntries[0];
  info.label = "reflection-duplicate-set0";
  info.entryCount = 3u;
  info.pEntries = duplicateEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) == GPU_OK || group) {
    fprintf(stderr, "bind group accepted duplicate reflected entry\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  memset(wrongTypeEntries, 0, sizeof(wrongTypeEntries));
  memcpy(wrongTypeEntries, validEntries, sizeof(wrongTypeEntries));
  wrongTypeEntries[1].bindingType = GPU_BINDING_STORAGE_BUFFER;
  info.label = "reflection-wrong-type-set0";
  info.entryCount = 2u;
  info.pEntries = wrongTypeEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) == GPU_OK || group) {
    fprintf(stderr, "bind group accepted wrong buffer binding type\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  memset(ambiguousEntries, 0, sizeof(ambiguousEntries));
  memcpy(ambiguousEntries, validEntries, sizeof(ambiguousEntries));
  ambiguousEntries[0].sampler = fakeSampler;
  info.label = "reflection-ambiguous-set0";
  info.entryCount = 2u;
  info.pEntries = ambiguousEntries;
  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) == GPU_OK || group) {
    fprintf(stderr, "bind group accepted ambiguous reflected entry\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  return 1;
}

static int
check_shader_layout_after_library_destroy(GPUDevice *device,
                                          GPUShaderLayout *shaderLayout) {
  const GPUBindGroupLayoutEntry *layoutEntries;
  unsigned char fakeBufferStorage;
  unsigned char fakeFragmentTextureStorage;
  unsigned char fakeComputeTextureStorage;
  GPUBindGroupEntry entries[3];
  GPUBindGroupCreateInfo info;
  GPUBindGroup *group;
  uint32_t layoutCount;
  int ok;

  if (!shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->pipelineLayout) {
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0],
                                               &layoutCount);
  ok = layoutCount == 3u &&
       layout_has_typed_entry(layoutEntries,
                              layoutCount,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              0u) &&
       layout_has_typed_entry(layoutEntries,
                              layoutCount,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u) &&
       layout_has_typed_entry(layoutEntries,
                              layoutCount,
                              GPUBindStageCompute,
                              GPUBindKindTexture,
                              GPU_BINDING_STORAGE_TEXTURE,
                              0u);
  if (!ok) {
    fprintf(stderr, "shader layout changed after library destroy\n");
    return 0;
  }

  memset(entries, 0, sizeof(entries));
  entries[0].binding = 0u;
  entries[0].stage = GPUBindStageFragment;
  entries[0].kind = GPUBindKindTexture;
  entries[0].textureView = (GPUTextureView *)(void *)&fakeFragmentTextureStorage;

  entries[1].binding = 0u;
  entries[1].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  entries[1].stage = GPUBindStageFragment;
  entries[1].kind = GPUBindKindBuffer;
  entries[1].buffer.buffer = (GPUBuffer *)(void *)&fakeBufferStorage;
  entries[1].buffer.size = 16u;

  entries[2].binding = 0u;
  entries[2].stage = GPUBindStageCompute;
  entries[2].kind = GPUBindKindTexture;
  entries[2].textureView = (GPUTextureView *)(void *)&fakeComputeTextureStorage;

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "reflection-library-destroy-set0";
  info.layout = shaderLayout->bindGroupLayouts[0];
  info.entryCount = 3u;
  info.pEntries = entries;

  group = NULL;
  if (GPUCreateBindGroup(device, &info, &group) != GPU_OK || !group) {
    fprintf(stderr, "shader layout failed after library destroy\n");
    return 0;
  }

  GPUDestroyBindGroup(group);
  return 1;
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

static int
check_queue_submit_fence(GPUDevice *device);

static int
check_fence_create_validation(GPUDevice *device) {
  GPUFenceCreateInfo fenceInfo = {0};
  GPUFence *fence;

  if (GPUWaitFence(NULL, 0) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUIsFenceSignaled(NULL)) {
    fprintf(stderr, "null fence query behaved unexpectedly\n");
    return 0;
  }
  GPUResetFence(NULL);
  GPUDestroyFence(NULL);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);

  fence = (GPUFence *)(uintptr_t)1u;
  if (GPUCreateFence(NULL, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted null device\n");
    return 0;
  }
  if (GPUCreateFence(device, &fenceInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "fence create accepted null output\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted wrong sType\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = (uint32_t)(sizeof(fenceInfo) - 1u);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted short structSize\n");
    return 0;
  }

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK ||
      !fence ||
      GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_ERROR_TIMEOUT) {
    fprintf(stderr, "fence create default state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK ||
      !fence ||
      !GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_OK) {
    fprintf(stderr, "fence create signaled state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  return 1;
}

static int
check_queue_selection(GPUDevice *device) {
  GPUCommandQueue *graphics0;
  GPUCommandQueue *compute0;

  if (!device) {
    return 0;
  }

  graphics0 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  compute0 = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0);
  if (!graphics0 || !compute0) {
    fprintf(stderr, "default device missing index-0 queues\n");
    return 0;
  }
  if (GPUGetCommandQueue(device, GPU_QUEUE_GRAPHICS) != graphics0) {
    fprintf(stderr, "GPUGetCommandQueue is not the index-0 graphics alias\n");
    return 0;
  }
  if (GPUGetQueue(device, GPU_QUEUE_GRAPHICS, UINT32_MAX) ||
      GPUGetQueue(device, GPU_QUEUE_COMPUTE, UINT32_MAX) ||
      GPUGetQueue(device, 0, 0)) {
    fprintf(stderr, "queue lookup accepted invalid request\n");
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
  if ((GPUGetAvailableQueueBits(device) &
       (GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT)) !=
      (GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT)) {
    fprintf(stderr, "default device missing expected queue bits\n");
    return 0;
  }
  if (!check_queue_selection(device)) {
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
         check_bind_group_validation(device, reflectedLayouts[0]) &&
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
         shaderReflection.resourceCount == 3u &&
         shader_reflection_has_resource(&shaderReflection,
                                        GPU_BINDING_STORAGE_TEXTURE,
                                        GPU_SHADER_STAGE_COMPUTE_BIT,
                                        0u) &&
         GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
         shaderLayout != NULL &&
         shaderLayout->bindGroupLayoutCount == 1u &&
         shaderLayout->bindGroupLayouts[0] != NULL &&
         shaderLayout->pipelineLayout != NULL;
    GPUDestroyShaderLibrary(library);
    library = NULL;
    if (ok) {
      ok = check_shader_layout_after_library_destroy(device, shaderLayout);
    }
    GPUDestroyShaderLayout(shaderLayout);
    GPUFreeShaderReflection(&shaderReflection);
    if (library) {
      GPUDestroyShaderLibrary(library);
    }

    if (!ok) {
      fprintf(stderr, "unexpected canonical shader library data\n");
      return 0;
    }
  }

  return check_queue_submit_fence(device);
}

static int
check_queue_submit_fence(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUCommandBuffer *nullBuffers[1];
  GPUCommandBuffer *duplicateBuffers[2];
  GPUFence *fence;
  GPUFenceCreateInfo fenceInfo = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for fence test\n");
    return 0;
  }
  if (!check_fence_create_validation(device)) {
    return 0;
  }

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label = "reflection-submit-fence";
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create fence\n");
    return 0;
  }

  ok = GPUIsFenceSignaled(fence);
  GPUResetFence(fence);
  ok = ok && !GPUIsFenceSignaled(fence);
  ok = ok && GPUWaitFence(fence, 0) == GPU_ERROR_TIMEOUT;
  ok = ok && GPUQueueSubmit(queue, NULL) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(NULL, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  cmdb = NULL;
  if (ok &&
      (GPUAcquireCommandBuffer(queue, "reflection-fence-submit", &cmdb) != GPU_OK ||
       !cmdb)) {
    fprintf(stderr, "failed to acquire command buffer for fence test\n");
    ok = 0;
  }

  if (ok) {
    buffers[0] = cmdb;
    nullBuffers[0] = NULL;

    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted wrong sType\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = (uint32_t)(sizeof(submitInfo) - 1u);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted short structSize\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = nullBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted null command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    duplicateBuffers[0] = cmdb;
    duplicateBuffers[1] = cmdb;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 2u;
    submitInfo.ppCommandBuffers = duplicateBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted duplicate command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    buffers[0] = cmdb;
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    submitInfo.fence = fence;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
        !GPUIsFenceSignaled(fence)) {
      fprintf(stderr, "queue submit fence did not signal\n");
      ok = 0;
    }
  }

  GPUDestroyFence(fence);
  return ok;
}

int
main(int argc, char **argv) {
  uint32_t expectedSourceKind;
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

  expectedSourceKind = GPUShaderLibraryUSLSourceGenerated;
  if (argc >= 3) {
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
        check_storage_compute_entry(storageBytecode, storageBytecodeSize)) &&
       check_selected_shader_library(bytecode,
                                     bytecodeSize,
                                     expectedSourceKind);
  free(storageBytecode);
  free(bytecode);

  if (!ok) {
    return 1;
  }

  puts("USL reflection check passed");
  return 0;
}
