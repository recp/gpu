#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/compute_internal.h"
#include "../../src/api/texture_internal.h"

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
shader_reflection_has_resource(const GPUShaderReflection *reflection,
                               GPUBindingType bindingType,
                               GPUShaderStageFlags visibility,
                               uint32_t setIndex,
                               uint32_t binding,
                               int hasDynamicOffset) {
  if (!reflection || (!reflection->pResources && reflection->resourceCount > 0u)) {
    return 0;
  }

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *item = &reflection->pResources[i];
    if (item->setIndex == setIndex &&
        item->binding == binding &&
        item->bindingType == bindingType &&
        item->visibility == visibility &&
        item->arrayCount == 1u &&
        (item->hasDynamicOffset ? 1 : 0) == (hasDynamicOffset ? 1 : 0)) {
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
                       uint32_t binding,
                       int hasDynamicOffset) {
  if (!entries && count > 0u) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].stage == stage &&
        entries[i].kind == kind &&
        entries[i].bindingType == bindingType &&
        entries[i].binding == binding &&
        (entries[i].hasDynamicOffset ? 1 : 0) == (hasDynamicOffset ? 1 : 0)) {
      return 1;
    }
  }

  return 0;
}

static int
check_compute_pipeline_workgroup_size(GPUDevice *device,
                                      GPUShaderLibrary *library,
                                      GPUPipelineLayout *layout) {
  GPUComputePipelineCreateInfo info = {0};
  GPUComputePipelineState *state;
  GPUComputePipeline *pipeline;
  int ok;

  info.chain.sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "api-reflection-compute-workgroup";
  info.layout = layout;
  info.library = library;
  info.entryPoint = "reflect_cs";

  pipeline = NULL;
  if (GPUCreateComputePipeline(device, &info, &pipeline) != GPU_OK || !pipeline) {
    fprintf(stderr, "failed to create workgroup reflection compute pipeline\n");
    GPUDestroyComputePipeline(pipeline);
    return 0;
  }

  state = pipeline->_state;
  ok = state &&
       state->workgroupSize[0] == 8u &&
       state->workgroupSize[1] == 8u &&
       state->workgroupSize[2] == 1u;
  GPUDestroyComputePipeline(pipeline);

  if (!ok) {
    fprintf(stderr, "compute pipeline did not inherit reflected workgroup size\n");
    return 0;
  }

  return 1;
}

static int
check_shader_layout_after_library_destroy(GPUDevice *device,
                                          GPUShaderLayout *shaderLayout) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBuffer fakeBufferStorage;
  GPUTexture fakeFragmentTextureStorage;
  GPUTexture fakeComputeTextureStorage;
  GPUTextureView fakeFragmentTextureView;
  GPUTextureView fakeComputeTextureView;
  GPUBindGroupEntry set0Entries[1];
  GPUBindGroupEntry set1Entries[2];
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroup *set0Group;
  GPUBindGroup *set1Group;
  uint32_t count;
  int ok;

  if (!shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0], &count);
  ok = count == 1u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              0u,
                              0);
  if (ok) {
    entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[1], &count);
    ok = count == 2u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u,
                              1) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageCompute,
                              GPUBindKindTexture,
                              GPU_BINDING_STORAGE_TEXTURE,
                              0u,
                              0);
  }
  if (!ok) {
    fprintf(stderr, "unexpected canonical shader layout entries\n");
    return 0;
  }

  memset(set0Entries, 0, sizeof(set0Entries));
  memset(set1Entries, 0, sizeof(set1Entries));
  memset(&fakeBufferStorage, 0, sizeof(fakeBufferStorage));
  memset(&fakeFragmentTextureStorage, 0, sizeof(fakeFragmentTextureStorage));
  memset(&fakeComputeTextureStorage, 0, sizeof(fakeComputeTextureStorage));
  memset(&fakeFragmentTextureView, 0, sizeof(fakeFragmentTextureView));
  memset(&fakeComputeTextureView, 0, sizeof(fakeComputeTextureView));
  fakeBufferStorage.sizeBytes = 16u;
  fakeBufferStorage.usage = GPU_BUFFER_USAGE_UNIFORM;
  fakeFragmentTextureStorage.usage = GPU_TEXTURE_USAGE_SAMPLED;
  fakeComputeTextureStorage.usage = GPU_TEXTURE_USAGE_STORAGE;
  fakeFragmentTextureView._texture = &fakeFragmentTextureStorage;
  fakeComputeTextureView._texture = &fakeComputeTextureStorage;

  set0Entries[0].binding = 0u;
  set0Entries[0].stage = GPUBindStageFragment;
  set0Entries[0].kind = GPUBindKindTexture;
  set0Entries[0].textureView = &fakeFragmentTextureView;

  set1Entries[0].binding = 0u;
  set1Entries[0].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  set1Entries[0].stage = GPUBindStageFragment;
  set1Entries[0].kind = GPUBindKindBuffer;
  set1Entries[0].buffer.buffer = &fakeBufferStorage;
  set1Entries[0].buffer.size = 16u;

  set1Entries[1].binding = 0u;
  set1Entries[1].stage = GPUBindStageCompute;
  set1Entries[1].kind = GPUBindKindTexture;
  set1Entries[1].textureView = &fakeComputeTextureView;

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "api-shader-layout-lifetime-set0";
  groupInfo.layout = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(set0Entries);
  groupInfo.pEntries = set0Entries;

  set0Group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &set0Group) != GPU_OK || !set0Group) {
    fprintf(stderr, "shader layout set0 failed after library destroy\n");
    GPUDestroyBindGroup(set0Group);
    return 0;
  }

  groupInfo.label = "api-shader-layout-lifetime-set1";
  groupInfo.layout = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(set1Entries);
  groupInfo.pEntries = set1Entries;

  set1Group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &set1Group) != GPU_OK || !set1Group) {
    fprintf(stderr, "shader layout set1 failed after library destroy\n");
    GPUDestroyBindGroup(set1Group);
    GPUDestroyBindGroup(set0Group);
    return 0;
  }

  GPUDestroyBindGroup(set1Group);
  GPUDestroyBindGroup(set0Group);
  return 1;
}

static int
check_reflection_layout_api(GPUDevice *device, GPUShaderLibrary *library) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindGroupLayout *layouts[2] = {0};
  GPUBindGroupLayout *smallLayouts[1] = {0};
  GPUBindGroupLayout *reversedLayouts[2] = {0};
  GPUPipelineLayout *pipelineLayout;
  uint32_t count;
  int ok;

  count = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(device, NULL, &count, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateBindGroupLayoutsFromReflection(device, library, NULL, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "reflection layout accepted null input\n");
    return 0;
  }

  if (GPUCreatePipelineLayoutFromReflection(device,
                                            NULL,
                                            0u,
                                            NULL,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            0u,
                                            NULL,
                                            NULL) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "reflection pipeline layout accepted null input\n");
    return 0;
  }

  count = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(device, library, &count, NULL) !=
        GPU_OK ||
      count != 2u) {
    fprintf(stderr, "reflection layout count query failed\n");
    return 0;
  }

  count = 1u;
  if (GPUCreateBindGroupLayoutsFromReflection(device,
                                              library,
                                              &count,
                                              smallLayouts) !=
        GPU_ERROR_INSUFFICIENT_CAPACITY ||
      count != 2u) {
    fprintf(stderr, "reflection layout capacity validation failed\n");
    return 0;
  }

  count = (uint32_t)GPU_ARRAY_LEN(layouts);
  if (GPUCreateBindGroupLayoutsFromReflection(device, library, &count, layouts) !=
        GPU_OK ||
      count != 2u ||
      !layouts[0] ||
      !layouts[1]) {
    fprintf(stderr, "reflection layout fill failed\n");
    ok = 0;
    goto cleanup;
  }

  entries = GPUGetBindGroupLayoutEntries(layouts[0], &count);
  ok = count == 1u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              0u,
                              0);
  if (ok) {
    entries = GPUGetBindGroupLayoutEntries(layouts[1], &count);
    ok = count == 2u &&
         layout_has_typed_entry(entries,
                                count,
                                GPUBindStageFragment,
                                GPUBindKindBuffer,
                                GPU_BINDING_UNIFORM_BUFFER,
                                0u,
                                1) &&
         layout_has_typed_entry(entries,
                                count,
                                GPUBindStageCompute,
                                GPUBindKindTexture,
                                GPU_BINDING_STORAGE_TEXTURE,
                                0u,
                                0);
  }
  if (!ok) {
    fprintf(stderr, "reflection layouts are not ordered by set index\n");
    goto cleanup;
  }

  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            1u,
                                            layouts,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "reflection pipeline layout accepted too few layouts\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    ok = 0;
    goto cleanup;
  }

  pipelineLayout = NULL;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            2u,
                                            layouts,
                                            &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "reflection pipeline layout rejected ordered layouts\n");
    ok = 0;
    goto cleanup;
  }
  GPUDestroyPipelineLayout(pipelineLayout);
  pipelineLayout = NULL;

  reversedLayouts[0] = layouts[1];
  reversedLayouts[1] = layouts[0];
  pipelineLayout = (GPUPipelineLayout *)(uintptr_t)1u;
  if (GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            2u,
                                            reversedLayouts,
                                            &pipelineLayout) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      pipelineLayout != NULL) {
    fprintf(stderr, "reflection pipeline layout accepted reversed layouts\n");
    GPUDestroyPipelineLayout(pipelineLayout);
    ok = 0;
    goto cleanup;
  }

  ok = 1;

cleanup:
  GPUDestroyBindGroupLayout(layouts[1]);
  GPUDestroyBindGroupLayout(layouts[0]);
  return ok;
}

static int
check_canonical_shader_library(GPUDevice *device,
                               const void *bytecode,
                               uint64_t bytecodeSize) {
  GPUShaderLibraryCreateInfo createInfo = {0};
  GPUShaderReflection reflection;
  GPUShaderLayout *shaderLayout;
  GPUShaderLibrary *library;
  int ok;

  createInfo.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  createInfo.chain.structSize = sizeof(createInfo);
  createInfo.label = "api-reflection.us";
  createInfo.sourceKind = GPU_SHADER_SOURCE_USL_BYTECODE;
  createInfo.sourceData = bytecode;
  createInfo.sourceSize = bytecodeSize;
  createInfo.generateReflection = true;

  library = NULL;
  if (GPUCreateShaderLibrary(device, &createInfo, &library) != GPU_OK || !library) {
    fprintf(stderr, "failed to create canonical shader library\n");
    return 0;
  }

  memset(&reflection, 0, sizeof(reflection));
  shaderLayout = NULL;
  ok = GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 3u &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_SAMPLED_TEXTURE,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u,
                                      0u,
                                      0) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_UNIFORM_BUFFER,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      1u,
                                      0u,
                                      1) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_STORAGE_TEXTURE,
                                      GPU_SHADER_STAGE_COMPUTE_BIT,
                                      1u,
                                      0u,
                                      0) &&
       check_reflection_layout_api(device, library) &&
       GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
       shaderLayout != NULL &&
       check_compute_pipeline_workgroup_size(device,
                                             library,
                                             shaderLayout->pipelineLayout);

  GPUDestroyShaderLibrary(library);
  library = NULL;

  if (ok) {
    ok = check_shader_layout_after_library_destroy(device, shaderLayout);
  }

  GPUDestroyShaderLayout(shaderLayout);
  GPUFreeShaderReflection(&reflection);

  if (!ok) {
    fprintf(stderr, "unexpected canonical shader library data\n");
    return 0;
  }

  return 1;
}

int
gpu_test_shader(GPUDevice *device, const char *bytecodePath) {
  uint64_t bytecodeSize;
  void *bytecode;
  int ok;

  bytecode = read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "failed to read shader bytecode: %s\n", bytecodePath);
    return 0;
  }

  ok = check_canonical_shader_library(device,
                                      bytecode,
                                      bytecodeSize);

  free(bytecode);
  return ok;
}
