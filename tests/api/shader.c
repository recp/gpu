#include "test.h"

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
layout_has_typed_entry(const GPUBindGroupLayoutEntry *entries,
                       uint32_t count,
                       GPUBindStage stage,
                       GPUBindKind kind,
                       GPUBindingType bindingType,
                       uint32_t binding) {
  if (!entries && count > 0u) {
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
check_shader_layout_after_library_destroy(GPUDevice *device,
                                          GPUShaderLayout *shaderLayout) {
  const GPUBindGroupLayoutEntry *entries;
  unsigned char fakeBufferStorage;
  unsigned char fakeFragmentTextureStorage;
  unsigned char fakeComputeTextureStorage;
  GPUBindGroupEntry groupEntries[3];
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBindGroup *group;
  uint32_t count;
  int ok;

  if (!shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->pipelineLayout) {
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0], &count);
  ok = count == 3u &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindTexture,
                              GPU_BINDING_SAMPLED_TEXTURE,
                              0u) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageFragment,
                              GPUBindKindBuffer,
                              GPU_BINDING_UNIFORM_BUFFER,
                              0u) &&
       layout_has_typed_entry(entries,
                              count,
                              GPUBindStageCompute,
                              GPUBindKindTexture,
                              GPU_BINDING_STORAGE_TEXTURE,
                              0u);
  if (!ok) {
    fprintf(stderr, "unexpected canonical shader layout entries\n");
    return 0;
  }

  memset(groupEntries, 0, sizeof(groupEntries));
  groupEntries[0].binding = 0u;
  groupEntries[0].stage = GPUBindStageFragment;
  groupEntries[0].kind = GPUBindKindTexture;
  groupEntries[0].textureView = (GPUTextureView *)(void *)&fakeFragmentTextureStorage;

  groupEntries[1].binding = 0u;
  groupEntries[1].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  groupEntries[1].stage = GPUBindStageFragment;
  groupEntries[1].kind = GPUBindKindBuffer;
  groupEntries[1].buffer.buffer = (GPUBuffer *)(void *)&fakeBufferStorage;
  groupEntries[1].buffer.size = 16u;

  groupEntries[2].binding = 0u;
  groupEntries[2].stage = GPUBindStageCompute;
  groupEntries[2].kind = GPUBindKindTexture;
  groupEntries[2].textureView = (GPUTextureView *)(void *)&fakeComputeTextureStorage;

  groupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label = "api-shader-layout-lifetime";
  groupInfo.layout = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount = (uint32_t)GPU_ARRAY_LEN(groupEntries);
  groupInfo.pEntries = groupEntries;

  group = NULL;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "shader layout failed after library destroy\n");
    GPUDestroyBindGroup(group);
    return 0;
  }

  GPUDestroyBindGroup(group);
  return 1;
}

static int
check_canonical_shader_library(GPUDevice *device,
                               const void *bytecode,
                               uint64_t bytecodeSize,
                               uint32_t expectedSourceKind) {
  GPUShaderLibraryCreateInfo createInfo = {0};
  GPUShaderLibraryUSLInfo info;
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
  ok = GPUGetShaderLibraryUSLInfo(library, &info) == 0 &&
       info.abiVersion == GPU_SHADER_LIBRARY_USL_INFO_VERSION &&
       info.sourceKind == expectedSourceKind &&
       info.bytecodeSize == bytecodeSize &&
       info.bytecodeContentHash != 0u &&
       info.backendContentHash != 0u &&
       info.selectedEntryCount == 0u &&
       GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 3u &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_SAMPLED_TEXTURE,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_UNIFORM_BUFFER,
                                      GPU_SHADER_STAGE_FRAGMENT_BIT,
                                      0u) &&
       shader_reflection_has_resource(&reflection,
                                      GPU_BINDING_STORAGE_TEXTURE,
                                      GPU_SHADER_STAGE_COMPUTE_BIT,
                                      0u) &&
       GPUCreateShaderLayout(device, library, &shaderLayout) == GPU_OK &&
       shaderLayout != NULL;

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
gpu_test_shader(GPUDevice *device,
                const char *bytecodePath,
                uint32_t expectedSourceKind) {
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
                                      bytecodeSize,
                                      expectedSourceKind);

  free(bytecode);
  return ok;
}
