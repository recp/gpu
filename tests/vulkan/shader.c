#include <gpu/gpu.h>

#include <stdio.h>
#include <stdlib.h>

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long size;

  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUShaderReflection   reflection   = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUShaderLibrary     *library;
  void                 *artifact;
  uint64_t              artifactSize;
  uint32_t              adapterCount;
  int                   ok;

  if (argc != 2) {
    fprintf(stderr, "usage: vulkan-shader artifact.us\n");
    return 1;
  }

  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  if (!artifact) {
    fprintf(stderr, "shader artifact read failed\n");
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "vulkan instance failed\n");
    free(artifact);
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "vulkan device failed\n");
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }

  library = NULL;
  ok = GPUCreateShaderLibraryFromUSL(device,
                                     artifact,
                                     artifactSize,
                                     &library) == GPU_OK &&
       library &&
       GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 1u &&
       reflection.pResources &&
       reflection.pResources[0].groupIndex == 0u &&
       reflection.pResources[0].binding == 0u &&
       reflection.pResources[0].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
       reflection.pResources[0].hasDynamicOffset;

  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    fprintf(stderr, "Vulkan USL shader module validation failed\n");
    return 1;
  }

  puts("Vulkan USL shader module validation passed");
  return 0;
}
