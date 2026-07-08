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
#include <gpu/api/compute.h>
#include <gpu/api/pass.h>
#include <gpu/api/rce.h>
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

  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &info, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted short structSize\n");
    GPUDestroyBindGroup(group);
    return 0;
  }
  info.chain.structSize = sizeof(info);

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

static GPUSamplerDesc
valid_sampler_desc(void) {
  GPUSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter = GPU_FILTER_LINEAR;
  desc.magFilter = GPU_FILTER_LINEAR;
  desc.mipFilter = GPU_MIP_FILTER_LINEAR;
  desc.addressU = GPU_ADDRESS_MODE_REPEAT;
  desc.addressV = GPU_ADDRESS_MODE_REPEAT;
  desc.addressW = GPU_ADDRESS_MODE_REPEAT;
  return desc;
}

static int
check_sampler_validation(GPUDevice *device) {
  GPUSamplerCreateInfo info;
  GPUSampler *sampler;

  memset(&info, 0, sizeof(info));
  info.chain.sType = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "reflection-sampler";
  info.desc = valid_sampler_desc();

  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(NULL, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted null device\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  if (GPUCreateSampler(device, &info, false, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "sampler create accepted null output\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted wrong sType\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted short structSize\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.desc.minFilter = (GPUFilter)99;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted invalid min filter\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.desc = valid_sampler_desc();
  info.desc.addressV = (GPUAddressMode)99;
  sampler = (GPUSampler *)(uintptr_t)1u;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_ERROR_INVALID_ARGUMENT ||
      sampler != NULL) {
    fprintf(stderr, "sampler create accepted invalid address mode\n");
    GPUDestroySampler(sampler);
    return 0;
  }

  info.desc = valid_sampler_desc();
  sampler = NULL;
  if (GPUCreateSampler(device, &info, false, &sampler) != GPU_OK || !sampler) {
    fprintf(stderr, "sampler create rejected valid dynamic sampler\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  GPUDestroySampler(sampler);

  sampler = NULL;
  if (GPUCreateSampler(device, &info, true, &sampler) != GPU_OK || !sampler) {
    fprintf(stderr, "sampler create rejected valid static-if-supported sampler\n");
    GPUDestroySampler(sampler);
    return 0;
  }
  GPUDestroySampler(sampler);

  return 1;
}

static int
check_bind_group_layout_validation(GPUDevice *device) {
  unsigned char fakeSamplerStorage;
  GPUBindGroupLayoutEntry entry;
  GPUBindGroupLayoutCreateInfo layoutInfo;
  GPUBindGroupLayout *layout;
  GPUBindGroupEntry runtimeSamplerEntry;
  GPUBindGroupCreateInfo groupInfo;
  GPUBindGroup *group;
  GPUPipelineLayoutCreateInfo pipelineInfo;
  GPUBindGroupLayout *layouts[1];
  GPUPipelineLayout *pipelineLayout;

  memset(&entry, 0, sizeof(entry));
  entry.binding = 0u;
  entry.bindingType = GPU_BINDING_SAMPLER;
  entry.visibility = GPU_SHADER_STAGE_FRAGMENT_BIT;
  entry.arrayCount = 1u;
  entry.immutableSampler = true;
  entry.immutableSamplerDesc = valid_sampler_desc();

  memset(&layoutInfo, 0, sizeof(layoutInfo));
  layoutInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label = "immutable-sampler-layout";
  layoutInfo.entryCount = 1u;
  layoutInfo.pEntries = &entry;

  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  layoutInfo.chain.structSize = (uint32_t)(sizeof(layoutInfo) - 1u);
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted short structSize\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layoutInfo.chain.structSize = sizeof(layoutInfo);
  entry.bindingType = GPU_BINDING_UNIFORM_BUFFER;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted immutable non-sampler\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.bindingType = GPU_BINDING_SAMPLER;
  entry.immutableSamplerDesc.mipFilter = (GPUMipFilter)99;
  layout = (GPUBindGroupLayout *)(uintptr_t)1u;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_ERROR_INVALID_ARGUMENT ||
      layout != NULL) {
    fprintf(stderr, "bind group layout accepted invalid immutable sampler desc\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  entry.immutableSamplerDesc = valid_sampler_desc();
  layout = NULL;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK || !layout) {
    fprintf(stderr, "bind group layout rejected valid immutable sampler\n");
    return 0;
  }

  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "immutable-sampler-group";
  groupInfo.layout = layout;

  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "bind group rejected omitted immutable sampler binding\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }
  GPUDestroyBindGroup(group);

  memset(&runtimeSamplerEntry, 0, sizeof(runtimeSamplerEntry));
  runtimeSamplerEntry.binding = 0u;
  runtimeSamplerEntry.sampler = (GPUSampler *)(void *)&fakeSamplerStorage;
  groupInfo.entryCount = 1u;
  groupInfo.pEntries = &runtimeSamplerEntry;
  group = (GPUBindGroup *)(uintptr_t)1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_ERROR_INVALID_ARGUMENT ||
      group != NULL) {
    fprintf(stderr, "bind group accepted runtime immutable sampler binding\n");
    GPUDestroyBindGroup(group);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  layouts[0] = layout;
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  pipelineInfo.chain.sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize = (uint32_t)(sizeof(pipelineInfo) - 1u);
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts = layouts;
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "pipeline layout accepted short structSize\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineLayout = NULL;
  if (GPUCreatePipelineLayout(device, &pipelineInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "pipeline layout rejected valid bind group layout\n");
    GPUDestroyBindGroupLayout(layout);
    return 0;
  }

  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(layout);
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
check_render_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUTextureView fakeView = {0};
  GPURenderPassColorAttachment colors[9] = {0};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo rp = {0};

  if (GPUBeginRenderPass(NULL, &rp) ||
      GPUBeginRenderPass(&fakeCmdb, NULL)) {
    fprintf(stderr, "render pass accepted null input\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  rp.chain.structSize = sizeof(rp);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted wrong sType\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.chain.structSize = (uint32_t)(sizeof(rp) - 1u);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted short structSize\n");
    return 0;
  }

  rp.chain.structSize = sizeof(rp);
  rp.colorAttachmentCount = 1u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted missing color attachments\n");
    return 0;
  }

  rp.pColorAttachments = colors;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null color view\n");
    return 0;
  }

  colors[0].view = &fakeView;
  colors[0].loadOp = (GPULoadOp)99;
  colors[0].storeOp = GPU_STORE_OP_STORE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color load op\n");
    return 0;
  }

  colors[0].loadOp = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp = (GPUStoreOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color store op\n");
    return 0;
  }

  colors[0].storeOp = GPU_STORE_OP_STORE;
  rp.colorAttachmentCount = (uint32_t)GPU_ARRAY_LEN(colors);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted too many color attachments\n");
    return 0;
  }

  rp.colorAttachmentCount = 0u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted no attachments\n");
    return 0;
  }

  rp.pDepthStencilAttachment = &depthStencil;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null depth-stencil view\n");
    return 0;
  }

  depthStencil.view = &fakeView;
  depthStencil.depthLoadOp = (GPULoadOp)99;
  depthStencil.depthStoreOp = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
  depthStencil.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid depth load op\n");
    return 0;
  }

  depthStencil.depthLoadOp = GPU_LOAD_OP_CLEAR;
  fakeCmdb._submitted = true;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted submitted command buffer\n");
    return 0;
  }

  return 1;
}

static int
check_render_encoder_validation(void) {
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPUBuffer *fakeBuffer = (GPUBuffer *)(uintptr_t)1u;
  GPUBufferBinding binding = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};

  GPUBindRenderPipeline(NULL, NULL);
  GPUBindVertexBuffers(NULL, 0u, 0u, NULL);
  GPUBindIndexBuffer(NULL, fakeBuffer, 0u, GPUIndexTypeUInt16);
  GPUDraw(NULL, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(NULL, 1u, 1u, 0u, 0, 0u);
  GPUApplyDynamicState(NULL, &dynamicState);
  GPUApplyDynamicState(&pass, NULL);
  GPUDraw(&pass, 1u, 1u, 0u, 0u);

  binding.buffer = fakeBuffer;
  GPUBindVertexBuffers(&pass, UINT32_MAX, 2u, &binding);
  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, (GPUIndexType)99);
  if (pass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted invalid index type\n");
    return 0;
  }

  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (!pass._hasIndexBuffer ||
      pass._indexBuffer != fakeBuffer ||
      pass._indexType != GPUIndexTypeUInt16) {
    fprintf(stderr, "render encoder rejected valid index binding\n");
    return 0;
  }

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  dynamicState.chain.structSize = sizeof(dynamicState);
  GPUApplyDynamicState(&pass, &dynamicState);

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  dynamicState.chain.structSize = (uint32_t)(sizeof(dynamicState) - 1u);
  GPUApplyDynamicState(&pass, &dynamicState);

  endedPass._ended = true;
  GPUBindIndexBuffer(&endedPass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (endedPass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted index binding after end\n");
    return 0;
  }
  GPUBindVertexBuffers(&endedPass, 0u, 1u, &binding);
  GPUSetViewport(&endedPass, &dynamicState.viewport);
  GPUDraw(&endedPass, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(&endedPass, 1u, 1u, 0u, 0, 0u);
  GPUApplyDynamicState(&endedPass, &dynamicState);
  GPUEndRenderPass(&endedPass);

  return 1;
}

static int
check_compute_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUComputePassEncoder fakePass = {0};
  GPUComputePipeline fakePipeline = {0};
  GPUBuffer *fakeBuffer = (GPUBuffer *)(uintptr_t)1u;
  uint32_t dynamicOffset = 0u;

  if (GPUBeginComputePass(NULL, "null")) {
    fprintf(stderr, "compute pass accepted null command buffer\n");
    return 0;
  }

  fakeCmdb._submitted = true;
  if (GPUBeginComputePass(&fakeCmdb, "submitted")) {
    fprintf(stderr, "compute pass accepted submitted command buffer\n");
    return 0;
  }

  GPUBindComputePipeline(NULL, NULL);
  GPUBindComputePipeline(&fakePass, &fakePipeline);
  GPUBindComputeGroup(NULL, 0u, NULL, 0u, NULL);
  GPUBindComputeGroup(&fakePass, 1u, NULL, 0u, NULL);
  GPUBindComputeGroup(&fakePass, 0u, NULL, 1u, &dynamicOffset);
  GPUDispatch(NULL, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatch(&fakePass, 0u, 1u, 1u);
  GPUDispatch(&fakePass, 1u, 0u, 1u);
  GPUDispatch(&fakePass, 1u, 1u, 0u);
  GPUDispatchIndirect(NULL, fakeBuffer, 0u);
  GPUDispatchIndirect(&fakePass, NULL, 0u);
  GPUEndComputePass(NULL);

  fakePass._ended = true;
  GPUBindComputePipeline(&fakePass, &fakePipeline);
  GPUBindComputeGroup(&fakePass, 0u, NULL, 0u, NULL);
  GPUDispatch(&fakePass, 1u, 1u, 1u);
  GPUDispatchIndirect(&fakePass, fakeBuffer, 0u);
  GPUEndComputePass(&fakePass);

  return 1;
}

static int
check_pipeline_create_validation(GPUDevice *device, GPUShaderLibrary *library) {
  GPUColorTargetState colorTargets[9] = {0};
  GPUVertexAttribute attr = {0};
  GPUVertexBufferLayout vertexLayout = {0};
  GPURenderPipelineCreateInfo renderInfo = {0};
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipeline *renderPipeline;
  GPUComputePipeline *computePipeline;

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;

  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(NULL, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted null device\n");
    return 0;
  }
  if (GPUCreateRenderPipeline(device, &renderInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "render pipeline create accepted null output\n");
    return 0;
  }

  renderInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  renderInfo.chain.structSize = sizeof(renderInfo);
  renderInfo.library = library;
  renderInfo.vertexEntry = "reflect_vs";
  renderInfo.fragmentEntry = "reflect_fs";
  renderInfo.colorTargetCount = 1u;
  renderInfo.pColorTargets = colorTargets;
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted wrong sType\n");
    return 0;
  }

  renderInfo.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize = (uint32_t)(sizeof(renderInfo) - 1u);
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted short structSize\n");
    return 0;
  }

  renderInfo.chain.structSize = sizeof(renderInfo);
  renderInfo.library = NULL;
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted null library\n");
    return 0;
  }

  renderInfo.library = library;
  renderInfo.colorTargetCount = 0u;
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted no color target\n");
    return 0;
  }

  renderInfo.colorTargetCount = (uint32_t)GPU_ARRAY_LEN(colorTargets);
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted too many color targets\n");
    return 0;
  }

  renderInfo.colorTargetCount = 1u;
  colorTargets[0].format = GPU_FORMAT_UNDEFINED;
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted undefined color format\n");
    return 0;
  }

  colorTargets[0].format = GPU_FORMAT_BGRA8_UNORM;
  attr.shaderLocation = 0u;
  attr.format = GPUUnknown;
  attr.offset = 0u;
  vertexLayout.strideBytes = 8u;
  vertexLayout.stepMode = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount = 1u;
  vertexLayout.pAttributes = &attr;
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.vertex.pBufferLayouts = &vertexLayout;
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted invalid vertex format\n");
    return 0;
  }

  renderInfo.vertex.bufferLayoutCount = 0u;
  renderInfo.vertex.pBufferLayouts = NULL;
  renderInfo.vertexEntry = "reflect_fs";
  renderInfo.fragmentEntry = "reflect_vs";
  renderPipeline = (GPURenderPipeline *)(uintptr_t)1u;
  if (GPUCreateRenderPipeline(device, &renderInfo, &renderPipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      renderPipeline != NULL) {
    fprintf(stderr, "render pipeline create accepted entry stage mismatch\n");
    return 0;
  }

  computePipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(NULL, &computeInfo, &computePipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      computePipeline != NULL) {
    fprintf(stderr, "compute pipeline create accepted null device\n");
    return 0;
  }
  if (GPUCreateComputePipeline(device, &computeInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "compute pipeline create accepted null output\n");
    return 0;
  }

  computeInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.library = library;
  computeInfo.entryPoint = "reflect_vs";
  computePipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(device, &computeInfo, &computePipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      computePipeline != NULL) {
    fprintf(stderr, "compute pipeline create accepted wrong sType\n");
    return 0;
  }

  computeInfo.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = (uint32_t)(sizeof(computeInfo) - 1u);
  computePipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(device, &computeInfo, &computePipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      computePipeline != NULL) {
    fprintf(stderr, "compute pipeline create accepted short structSize\n");
    return 0;
  }

  computeInfo.chain.structSize = sizeof(computeInfo);
  computePipeline = (GPUComputePipeline *)(uintptr_t)1u;
  if (GPUCreateComputePipeline(device, &computeInfo, &computePipeline) != GPU_ERROR_INVALID_ARGUMENT ||
      computePipeline != NULL) {
    fprintf(stderr, "compute pipeline create accepted non-compute entry\n");
    return 0;
  }

  return 1;
}

static int
check_swapchain_validation(GPUDevice *device) {
  GPUSurface *fakeSurface;
  GPUSwapchain *swapchain;
  GPUSwapchainCreateInfo info = {0};

  fakeSurface = (GPUSurface *)(uintptr_t)1u;

  swapchain = (GPUSwapchain *)(uintptr_t)1u;
  if (GPUCreateSwapchain(NULL, &info, &swapchain) != GPU_ERROR_INVALID_ARGUMENT ||
      swapchain != NULL) {
    fprintf(stderr, "swapchain create accepted null device\n");
    return 0;
  }
  if (GPUCreateSwapchain(device, &info, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "swapchain create accepted null output\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize = sizeof(info);
  info.surface = fakeSurface;
  info.width = 640u;
  info.height = 480u;
  swapchain = (GPUSwapchain *)(uintptr_t)1u;
  if (GPUCreateSwapchain(device, &info, &swapchain) != GPU_ERROR_INVALID_ARGUMENT ||
      swapchain != NULL) {
    fprintf(stderr, "swapchain create accepted wrong sType\n");
    return 0;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO;
  info.chain.structSize = (uint32_t)(sizeof(info) - 1u);
  swapchain = (GPUSwapchain *)(uintptr_t)1u;
  if (GPUCreateSwapchain(device, &info, &swapchain) != GPU_ERROR_INVALID_ARGUMENT ||
      swapchain != NULL) {
    fprintf(stderr, "swapchain create accepted short structSize\n");
    return 0;
  }

  info.chain.structSize = sizeof(info);
  info.width = 0u;
  swapchain = (GPUSwapchain *)(uintptr_t)1u;
  if (GPUCreateSwapchain(device, &info, &swapchain) != GPU_ERROR_INVALID_ARGUMENT ||
      swapchain != NULL) {
    fprintf(stderr, "swapchain create accepted zero width\n");
    return 0;
  }

  if (GPUCreateSwapchainDefault(NULL, NULL, 1u, 1u) ||
      GPUCreateSwapchainDefault(device, NULL, 1u, 1u) ||
      GPUCreateSwapchainDefault(device, fakeSurface, 0u, 1u)) {
    fprintf(stderr, "swapchain default accepted invalid input\n");
    return 0;
  }

  return 1;
}

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
check_resource_validation(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTextureWriteRegion region = {0};
  GPUBuffer *buffer;
  GPUTexture *texture;
  GPUTextureView *view;
  uint32_t writeWords[4] = { 1u, 2u, 3u, 4u };
  uint32_t readWords[4] = { 0u, 0u, 0u, 0u };
  uint8_t pixels[4u * 4u * 4u] = {0};

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for resource test\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(writeWords);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(NULL, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted null device\n");
    return 0;
  }
  if (GPUCreateBuffer(device, &bufferInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "buffer create accepted null output\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted wrong sType\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = (uint32_t)(sizeof(bufferInfo) - 1u);
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted short structSize\n");
    return 0;
  }

  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage = 0u;
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted zero usage\n");
    return 0;
  }

  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  buffer = NULL;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "buffer create failed\n");
    return 0;
  }
  if (GPUQueueWriteBuffer(queue, buffer, 0u, writeWords, sizeof(writeWords)) != GPU_OK ||
      GPUQueueReadBuffer(queue, buffer, 0u, readWords, sizeof(readWords)) != GPU_OK ||
      memcmp(writeWords, readWords, sizeof(writeWords)) != 0) {
    fprintf(stderr, "buffer write/read failed\n");
    GPUDestroyBuffer(buffer);
    return 0;
  }
  if (GPUQueueWriteBuffer(queue, buffer, 12u, writeWords, 8u) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueReadBuffer(queue, buffer, 12u, readWords, 8u) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "buffer write/read accepted out-of-bounds range\n");
    GPUDestroyBuffer(buffer);
    return 0;
  }
  GPUDestroyBuffer(buffer);

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted wrong sType\n");
    return 0;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = (uint32_t)(sizeof(textureInfo) - 1u);
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted short structSize\n");
    return 0;
  }

  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = (GPUTextureDimension)99;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted invalid dimension\n");
    return 0;
  }

  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.usage = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero usage\n");
    return 0;
  }

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  texture = NULL;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "texture create failed\n");
    return 0;
  }

  region.width = 4u;
  region.height = 4u;
  region.depth = 1u;
  region.layerCount = 1u;
  region.bytesPerRow = 4u * 4u;
  region.rowsPerImage = 4u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "texture write failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  region.mipLevel = 1u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted invalid mip level\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.mipLevel = 0u;
  region.layerCount = 2u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted invalid layer range\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.layerCount = 1u;

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType = GPU_TEXTURE_VIEW_2D;
  viewInfo.format = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount = 1u;
  viewInfo.arrayLayerCount = 1u;

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted wrong sType\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = (uint32_t)(sizeof(viewInfo) - 1u);
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted short structSize\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.baseMipLevel = 1u;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted invalid mip range\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.baseMipLevel = 0u;
  view = NULL;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "texture view create failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);

  return 1;
}

static int
check_copy_pass_validation(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer fakeCmdb = {0};
  GPUCopyPassEncoder endedPass = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence *fence;
  GPUCopyPassEncoder *copyPass;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUBufferCopyRegion bufferRegion = {0};
  GPUBufferTextureCopyRegion bufferTextureRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUBuffer *sourceBuffer;
  GPUBuffer *bufferCopy;
  GPUBuffer *textureReadback;
  GPUTexture *textureA;
  GPUTexture *textureB;
  uint8_t pixels[4u * 4u * 4u];
  uint8_t bufferCopyBytes[sizeof(pixels)] = {0};
  uint8_t textureBytes[sizeof(pixels)] = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for copy test\n");
    return 0;
  }

  if (GPUBeginCopyPass(NULL, "null")) {
    fprintf(stderr, "copy pass accepted null command buffer\n");
    return 0;
  }
  fakeCmdb._submitted = true;
  if (GPUBeginCopyPass(&fakeCmdb, "submitted")) {
    fprintf(stderr, "copy pass accepted submitted command buffer\n");
    return 0;
  }

  for (uint32_t i = 0; i < (uint32_t)sizeof(pixels); i++) {
    pixels[i] = (uint8_t)(i * 3u + 1u);
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  sourceBuffer = NULL;
  bufferCopy = NULL;
  textureReadback = NULL;
  textureA = NULL;
  textureB = NULL;
  fence = NULL;
  cmdb = NULL;
  copyPass = NULL;
  ok = GPUCreateBuffer(device, &bufferInfo, &sourceBuffer) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &bufferCopy) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &textureReadback) == GPU_OK &&
       GPUQueueWriteBuffer(queue, sourceBuffer, 0u, pixels, sizeof(pixels)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test buffer setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED |
                      GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test texture setup failed\n");
    goto cleanup;
  }

  endedPass._ended = true;
  GPUCopyBufferToBuffer(&endedPass, sourceBuffer, bufferCopy, &bufferRegion);
  GPUCopyBufferToTexture(&endedPass, sourceBuffer, textureA, &bufferTextureRegion);
  GPUCopyTextureToBuffer(&endedPass, textureB, textureReadback, &bufferTextureRegion);
  GPUCopyTextureToTexture(&endedPass, textureA, textureB, &textureRegion);
  GPUEndCopyPass(&endedPass);

  ok = GPUAcquireCommandBuffer(queue, "reflection-copy-pass", &cmdb) == GPU_OK && cmdb;
  if (!ok) {
    fprintf(stderr, "failed to acquire command buffer for copy test\n");
    goto cleanup;
  }

  copyPass = GPUBeginCopyPass(cmdb, "reflection-copy");
  if (!copyPass) {
    fprintf(stderr, "failed to begin copy pass\n");
    ok = 0;
    goto cleanup;
  }

  bufferRegion.sizeBytes = sizeof(pixels);
  GPUCopyBufferToBuffer(copyPass, sourceBuffer, bufferCopy, &bufferRegion);

  bufferTextureRegion.bytesPerRow = 4u * 4u;
  bufferTextureRegion.rowsPerImage = 4u;
  bufferTextureRegion.texture.width = 4u;
  bufferTextureRegion.texture.height = 4u;
  bufferTextureRegion.texture.depth = 1u;
  bufferTextureRegion.texture.layerCount = 1u;
  GPUCopyBufferToTexture(copyPass, sourceBuffer, textureA, &bufferTextureRegion);

  textureRegion.width = 4u;
  textureRegion.height = 4u;
  textureRegion.depth = 1u;
  textureRegion.layerCount = 1u;
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);
  GPUCopyTextureToBuffer(copyPass, textureB, textureReadback, &bufferTextureRegion);

  GPUCopyBufferToBuffer(NULL, sourceBuffer, bufferCopy, &bufferRegion);
  GPUCopyBufferToTexture(copyPass, NULL, textureA, &bufferTextureRegion);
  GPUCopyTextureToBuffer(copyPass, textureB, NULL, &bufferTextureRegion);
  GPUCopyTextureToTexture(copyPass, textureA, textureB, NULL);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  ok = GPUCreateFence(device, NULL, &fence) == GPU_OK && fence;
  if (!ok) {
    fprintf(stderr, "failed to create fence for copy test\n");
    goto cleanup;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok) {
    fprintf(stderr, "copy pass submit failed\n");
    goto cleanup;
  }

  ok = GPUQueueReadBuffer(queue,
                          bufferCopy,
                          0u,
                          bufferCopyBytes,
                          sizeof(bufferCopyBytes)) == GPU_OK &&
       GPUQueueReadBuffer(queue,
                          textureReadback,
                          0u,
                          textureBytes,
                          sizeof(textureBytes)) == GPU_OK &&
       memcmp(pixels, bufferCopyBytes, sizeof(pixels)) == 0 &&
       memcmp(pixels, textureBytes, sizeof(pixels)) == 0;
  if (!ok) {
    fprintf(stderr, "copy pass readback mismatch\n");
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(bufferCopy);
  GPUDestroyBuffer(sourceBuffer);
  return ok;
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
  if (!check_sampler_validation(device)) {
    return 0;
  }
  if (!check_bind_group_layout_validation(device)) {
    return 0;
  }
  if (!check_resource_validation(device)) {
    return 0;
  }
  if (!check_copy_pass_validation(device)) {
    return 0;
  }
  if (!check_swapchain_validation(device)) {
    return 0;
  }
  if (!check_render_pass_validation()) {
    return 0;
  }
  if (!check_render_encoder_validation()) {
    return 0;
  }
  if (!check_compute_pass_validation()) {
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
    ok = check_pipeline_create_validation(device, library);
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
  GPUCommandBuffer submittedCmdb = {0};
  GPUFrame fakeFrame = {0};
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

  submittedCmdb._submitted = true;
  submittedCmdb._priv = (void *)(uintptr_t)0xdeadbeefu;
  fakeFrame.drawable = (void *)(uintptr_t)0xdeadbeefu;
  GPUSchedulePresent(&submittedCmdb, &fakeFrame);
  GPUPresent(&submittedCmdb, &fakeFrame);

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
