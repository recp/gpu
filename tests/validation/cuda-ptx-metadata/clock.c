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

#if !defined(GPU_PTX_CLOCK_ENTRY) || !defined(GPU_PTX_CLOCK_FEATURE) || \
    !defined(GPU_PTX_CLOCK_SOURCE)
#error PTX clock contract requires entry, feature, and source definitions
#endif

static int
validate_contract(const GPUShaderLibrary *library) {
  char                    entry[96];
  char                    clock[64];
  const GPUShaderPTXInfo *info;
  const char             *ptx;

  if (!library || library->_reflection.resourceCount != 1u
      || !ptx_validate_buffer_resource(&library->_reflection,
                                       "output",
                                       0u,
                                       0u,
                                       GPU_BINDING_STORAGE_BUFFER,
                                       8u,
                                       8u)) {
    return 0;
  }

  info = library->_ptxInfo;

  if (!info || info->entryCount != 1u || info->paramCount != 1u
      || info->entries[0].paramStart != 0u
      || info->entries[0].paramCount != 1u
      || info->entries[0].paramDataSize != 8u
      || !ptx_validate_buffer_param(&info->params[0],
                                    0u,
                                    0u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    0u)) {
    return 0;
  }

  snprintf(entry, sizeof(entry), ".visible .entry %s(", GPU_PTX_CLOCK_ENTRY);
  snprintf(clock, sizeof(clock), ", %%%s;", GPU_PTX_CLOCK_SOURCE);
  ptx = ptx_source(library);
  return ptx && strstr(ptx, ".version 6.0")
         && strstr(ptx, ".target sm_70") && strstr(ptx, entry)
         && strstr(ptx, ".reqntid 1, 1, 1")
         && ptx_count(ptx, "mov.u64") == 1u && ptx_count(ptx, clock) == 1u
         && ptx_count(ptx, "st.global.u64") == 2u;
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUShaderLibrary *library;
  GPUDevice         device;
  GPUAdapter        adapter;
  GPUInstance       instance;
  GPUApi            api;
  GPUResult         result;
  uint64_t          featureMask;
  int               valid;

  featureMask = (UINT64_C(1) << GPU_FEATURE_COMPUTE) |
                (UINT64_C(1) << GPU_PTX_CLOCK_FEATURE);
  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  featureMask,
                  70u);

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);

  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX clock metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);

  if (!valid) {
    fprintf(stderr, "CUDA PTX clock contract mismatch\n");
  }

  GPUDestroyShaderLibrary(library);
  return valid;
}
