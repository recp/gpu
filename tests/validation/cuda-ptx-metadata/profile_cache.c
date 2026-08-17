#include "test.h"

#include <stdio.h>

enum {
  ProfileCount = 4
};

static int
create_profile_library(const void        *artifact,
                       uint64_t           artifactSize,
                       uint32_t           architecture,
                       GPUDevice         *device,
                       GPUAdapter        *adapter,
                       GPUInstance       *instance,
                       GPUApi            *api,
                       GPUShaderLibrary **outLibrary) {
  GPUResult result;

  ptx_init_device(device,
                  adapter,
                  instance,
                  api,
                  UINT64_C(1) << GPU_FEATURE_COMPUTE,
                  architecture);
  *outLibrary = NULL;
  result = GPUCreateShaderLibraryFromUSL(device,
                                         artifact,
                                         artifactSize,
                                         outLibrary);
  if (result != GPU_OK || !*outLibrary) {
    fprintf(stderr,
            "CUDA PTX sm_%u profile creation failed (%d)\n",
            architecture,
            result);
    return 0;
  }
  return 1;
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  static const uint32_t architectures[ProfileCount] = {80u, 89u, 90u, 80u};
  static const char * const targets[ProfileCount] = {
    ".target sm_80",
    ".target sm_89",
    ".target sm_90a",
    ".target sm_80"
  };
  GPUShaderLibrary *libraries[ProfileCount] = {0};
  GPUDevice          devices[ProfileCount];
  GPUAdapter         adapters[ProfileCount];
  GPUInstance        instances[ProfileCount];
  GPUApi             apis[ProfileCount];
  const char        *sources[ProfileCount] = {0};
  int                valid;

  valid = 1;
  for (uint32_t i = 0u; valid && i < ProfileCount; i++) {
    valid = create_profile_library(artifact,
                                   artifactSize,
                                   architectures[i],
                                   &devices[i],
                                   &adapters[i],
                                   &instances[i],
                                   &apis[i],
                                   &libraries[i]);
    sources[i] = valid ? ptx_source(libraries[i]) : NULL;
    valid = valid && sources[i] && strstr(sources[i], targets[i]);
  }

  valid = valid && strcmp(sources[0], sources[1]) != 0 &&
          strcmp(sources[1], sources[2]) != 0 &&
          strcmp(sources[0], sources[2]) != 0 &&
          strcmp(sources[0], sources[3]) == 0;
  if (!valid) {
    fprintf(stderr, "CUDA PTX profile/cache identity mismatch\n");
  }

  for (uint32_t i = 0u; i < ProfileCount; i++) {
    GPUDestroyShaderLibrary(libraries[i]);
  }
  return valid;
}
