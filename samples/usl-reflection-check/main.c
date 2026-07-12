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

#include <gpu/gpu.h>

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

static GPUAdapter *
select_adapter(GPUInstance *instance) {
  GPUAdapter *adapter = NULL;
  uint32_t adapterCount = 1;
  GPUResult result;

  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static int
reflection_has_resource(const GPUShaderReflection *reflection,
                        GPUBindingType bindingType,
                        GPUShaderStageFlags visibility,
                        uint32_t groupIndex,
                        uint32_t binding,
                        int hasDynamicOffset) {
  if (!reflection || (!reflection->pResources && reflection->resourceCount > 0u)) {
    return 0;
  }

  for (uint32_t i = 0; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *item = &reflection->pResources[i];
    if (item->groupIndex == groupIndex &&
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
layout_has_entry(const GPUBindGroupLayoutEntry *entries,
                 uint32_t count,
                 GPUBindingType bindingType,
                 GPUShaderStageFlags visibility,
                 uint32_t binding,
                 int hasDynamicOffset) {
  if (!entries && count > 0u) {
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (entries[i].binding == binding &&
        entries[i].bindingType == bindingType &&
        entries[i].visibility == visibility &&
        entries[i].arrayCount == 1u &&
        (entries[i].hasDynamicOffset ? 1 : 0) == (hasDynamicOffset ? 1 : 0)) {
      return 1;
    }
  }

  return 0;
}

static int
check_layout_from_reflection(GPUDevice *device,
                             GPUShaderLibrary *library,
                             uint32_t expectedLayoutCount,
                             const GPUShaderReflection *reflection) {
  GPUBindGroupLayout **layouts;
  GPUPipelineLayout *pipelineLayout;
  uint32_t layoutCount;
  GPUResult rc;
  int ok;

  layoutCount = 0u;
  rc = GPUCreateBindGroupLayoutsFromReflection(device, library, &layoutCount, NULL);
  if (rc != GPU_OK || layoutCount != expectedLayoutCount) {
    fprintf(stderr, "unexpected reflected bind group layout count\n");
    return 0;
  }

  pipelineLayout = NULL;
  if (layoutCount == 0u) {
    rc = GPUCreatePipelineLayoutFromReflection(device,
                                               library,
                                               0u,
                                               NULL,
                                               &pipelineLayout);
    ok = rc == GPU_OK && pipelineLayout != NULL;
    GPUDestroyPipelineLayout(pipelineLayout);
    return ok;
  }

  layouts = calloc(layoutCount, sizeof(*layouts));
  if (!layouts) {
    return 0;
  }

  rc = GPUCreateBindGroupLayoutsFromReflection(device, library, &layoutCount, layouts);
  if (rc != GPU_OK || layoutCount != expectedLayoutCount) {
    fprintf(stderr, "failed to create reflected bind group layout\n");
    free(layouts);
    return 0;
  }

  ok = 1;
  for (uint32_t groupIndex = 0; ok && groupIndex < layoutCount; groupIndex++) {
    const GPUBindGroupLayoutEntry *entries;
    uint32_t expectedEntryCount;
    uint32_t layoutEntryCount;

    if (!layouts[groupIndex]) {
      ok = 0;
      break;
    }

    expectedEntryCount = 0u;
    for (uint32_t i = 0; reflection && i < reflection->resourceCount; i++) {
      if (reflection->pResources[i].groupIndex == groupIndex) {
        expectedEntryCount++;
      }
    }

    entries = GPUGetBindGroupLayoutEntries(layouts[groupIndex], &layoutEntryCount);
    ok = layoutEntryCount == expectedEntryCount;
    for (uint32_t i = 0; ok && reflection && i < reflection->resourceCount; i++) {
      const GPUShaderResourceReflection *resource = &reflection->pResources[i];
      if (resource->groupIndex != groupIndex) {
        continue;
      }

      ok = layout_has_entry(entries,
                            layoutEntryCount,
                            resource->bindingType,
                            resource->visibility,
                            resource->binding,
                            resource->hasDynamicOffset);
    }
  }

  rc = GPUCreatePipelineLayoutFromReflection(device,
                                             library,
                                             layoutCount,
                                             layouts,
                                             &pipelineLayout);
  ok = ok && rc == GPU_OK && pipelineLayout != NULL;

  GPUDestroyPipelineLayout(pipelineLayout);
  for (uint32_t i = 0; i < layoutCount; i++) {
    GPUDestroyBindGroupLayout(layouts[i]);
  }
  free(layouts);
  return ok;
}

static int
check_shader_artifact(GPUDevice *device,
                      const void *bytecode,
                      uint64_t bytecodeSize,
                      uint32_t expectedResourceCount,
                      uint32_t expectedLayoutCount,
                      int storageOnly) {
  GPUShaderReflection reflection;
  GPUShaderLibrary *library;
  int ok;

  library = NULL;
  if (GPUCreateShaderLibraryFromUSL(device, bytecode, bytecodeSize, &library) !=
        GPU_OK ||
      !library) {
    fprintf(stderr, "failed to create shader library\n");
    return 0;
  }

  memset(&reflection, 0, sizeof(reflection));

  ok = GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == expectedResourceCount;

  if (ok && storageOnly) {
    ok = reflection_has_resource(&reflection,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 GPU_SHADER_STAGE_COMPUTE_BIT,
                                 0u,
                                 0u,
                                 0);
  } else if (ok) {
    ok = reflection_has_resource(&reflection,
                                 GPU_BINDING_UNIFORM_BUFFER,
                                 GPU_SHADER_STAGE_FRAGMENT_BIT,
                                 0u,
                                 0u,
                                 0) &&
         reflection_has_resource(&reflection,
                                 GPU_BINDING_SAMPLED_TEXTURE,
                                 GPU_SHADER_STAGE_FRAGMENT_BIT,
                                 0u,
                                 1u,
                                 0) &&
         reflection_has_resource(&reflection,
                                 GPU_BINDING_UNIFORM_BUFFER,
                                 GPU_SHADER_STAGE_FRAGMENT_BIT,
                                 1u,
                                 0u,
                                 1) &&
         reflection_has_resource(&reflection,
                                 GPU_BINDING_STORAGE_TEXTURE,
                                 GPU_SHADER_STAGE_COMPUTE_BIT,
                                 1u,
                                 1u,
                                 0) &&
         GPUShaderFunction(library, "reflect_vs") != NULL &&
         GPUShaderFunction(library, "reflect_fs") != NULL &&
         GPUShaderFunction(library, "reflect_cs") != NULL &&
         GPUShaderFunction(library, "missing_entry") == NULL;
  }

  ok = ok && check_layout_from_reflection(device,
                                          library,
                                          expectedLayoutCount,
                                          &reflection);

  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  if (!ok) {
    fprintf(stderr, "unexpected public shader reflection data\n");
    return 0;
  }

  return 1;
}

int
main(int argc, char **argv) {
  GPUAdapter  *adapter;
  GPUInstance *instance;
  GPUDevice   *device;
  void        *storageBytecode;
  void        *bytecode;
  uint64_t storageBytecodeSize;
  uint64_t bytecodeSize;
  int ok;

  if (argc < 2 || argc > 3) {
    fprintf(stderr,
            "usage: %s <reflection.us> [storage.us]\n",
            argv[0]);
    return 2;
  }

  bytecode = read_file(argv[1], &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "failed to read bytecode: %s\n", argv[1]);
    return 2;
  }

  storageBytecode = NULL;
  storageBytecodeSize = 0u;
  if (argc == 3) {
    storageBytecode = read_file(argv[2], &storageBytecodeSize);
    if (!storageBytecode) {
      fprintf(stderr, "failed to read storage bytecode: %s\n", argv[2]);
      free(bytecode);
      return 2;
    }
  }

  instance = NULL;
  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "failed to create instance\n");
    free(storageBytecode);
    free(bytecode);
    return 1;
  }

  adapter = select_adapter(instance);
  if (!adapter) {
    fprintf(stderr, "failed to get adapter\n");
    GPUDestroyInstance(instance);
    free(storageBytecode);
    free(bytecode);
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "failed to create device\n");
    GPUDestroyInstance(instance);
    free(storageBytecode);
    free(bytecode);
    return 1;
  }

  ok = check_shader_artifact(device, bytecode, bytecodeSize, 4u, 2u, 0) &&
       (!storageBytecode ||
        check_shader_artifact(device,
                              storageBytecode,
                              storageBytecodeSize,
                              1u,
                              1u,
                              1));

  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(storageBytecode);
  free(bytecode);

  if (!ok) {
    return 1;
  }

  puts("USL reflection check passed");
  return 0;
}
