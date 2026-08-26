/* Test-only USL shader-library creation controls. */

#ifndef gpu_validation_usl_test_h
#define gpu_validation_usl_test_h

#include <gpu/gpu.h>

#include <stdlib.h>

static inline GPUResult
gpu_test_create_shader_library_from_usl(GPUDevice          *device,
                                        const void         *artifact,
                                        uint64_t            artifactSize,
                                        GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};

  if (!getenv("GPU_USL_TEST_DISABLE_DISK_CACHE")) {
    return GPUCreateShaderLibraryFromUSL(device,
                                         artifact,
                                         artifactSize,
                                         outLibrary);
  }
  info.chain.sType      = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sourceData       = artifact;
  info.sourceSize       = artifactSize;
  info.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.disableDiskCache = true;
  return GPUCreateShaderLibrary(device, &info, outLibrary);
}

#endif /* gpu_validation_usl_test_h */
