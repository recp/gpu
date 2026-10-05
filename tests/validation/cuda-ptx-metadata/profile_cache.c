/*
 * Copyright (C) 2026 Recep Aslantas
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

#include "test.h"

#include <stdio.h>

enum {
  ProfileCount = 7
};

static const uint32_t architectures[ProfileCount] = {
  80u, 89u, 90u, 80u, 121u, 121u, 121u
};

static const uint32_t versions[ProfileCount] = {
  0u, 0u, 0u, 0u, 808u, 903u, 903u
};

static const char *const targets[ProfileCount] = {
  ".target sm_80",
  ".target sm_89",
  ".target sm_90a",
  ".target sm_80",
  ".target sm_121a",
  ".target sm_121a",
  ".target sm_121a"
};

static const char *const isa[ProfileCount] = {
  ".version 7.0",
  ".version 7.8",
  ".version 8.0",
  ".version 7.0",
  ".version 8.8",
  ".version 9.3",
  ".version 9.3"
};

static int
create_profile_library(const void        *artifact,
                       uint64_t           artifactSize,
                       uint32_t           architecture,
                       uint32_t           version,
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
  device->uslTargetVersion = version;
  *outLibrary              = NULL;
  result                   = GPUCreateShaderLibraryFromUSL(device,
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
  GPUShaderLibrary *libraries[ProfileCount] = {0};
  GPUDevice         devices[ProfileCount];
  GPUAdapter        adapters[ProfileCount];
  GPUInstance       instances[ProfileCount];
  GPUApi            apis[ProfileCount];
  const char       *sources[ProfileCount] = {0};
  int               valid;
  uint32_t          createIndex;
  uint32_t          destroyIndex;

  valid = 1;

  for (createIndex = 0u; valid && createIndex < ProfileCount; createIndex++) {
    valid                = create_profile_library(artifact,
                                                  artifactSize,
                                                  architectures[createIndex],
                                                  versions[createIndex],
                                                  &devices[createIndex],
                                                  &adapters[createIndex],
                                                  &instances[createIndex],
                                                  &apis[createIndex],
                                                  &libraries[createIndex]);
    sources[createIndex] = valid ? ptx_source(libraries[createIndex]) : NULL;
    valid                = valid && sources[createIndex] && strstr(sources[createIndex], targets[createIndex])
                           && strstr(sources[createIndex], isa[createIndex]);
  }

  valid = valid && strcmp(sources[0], sources[1]) != 0
          && strcmp(sources[1], sources[2]) != 0
          && strcmp(sources[0], sources[2]) != 0
          && strcmp(sources[0], sources[3]) == 0
          && strcmp(sources[4], sources[5]) != 0
          && strcmp(sources[5], sources[6]) == 0;

  if (!valid) {
    fprintf(stderr, "CUDA PTX profile/cache identity mismatch\n");
  }

  for (destroyIndex = 0u; destroyIndex < ProfileCount; destroyIndex++) {
    GPUDestroyShaderLibrary(libraries[destroyIndex]);
  }

  return valid;
}
