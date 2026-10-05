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

/* test-only USL shader-library creation controls. */

#ifndef gpu_validation_usl_test_h
#define gpu_validation_usl_test_h

#include <gpu/gpu.h>

#include <stdlib.h>

static inline GPUResult
gpu_test_create_shader_library_from_usl(GPUDevice           *device,
                                        const void         *artifact,
                                        uint64_t            artifactSize,
                                        GPUShaderLibrary  **outLibrary) {
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
