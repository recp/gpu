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

#include "../cuda-ptx-metadata/test.h"

#include <stdio.h>

static int
validate_contract(const GPUShaderLibrary *library) {
  const ShaderPTXInfo    *info;
  const char             *ptx;

  if (!library || library->_reflection.resourceCount != 1u
      || !ptx_validate_buffer_resource(&library->_reflection,
                                       "values",
                                       0u,
                                       0u,
                                       GPU_BINDING_STORAGE_BUFFER,
                                       4u,
                                       4u)) {
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

  ptx = ptx_source(library);
  return ptx && strstr(ptx, ".version 6.0")
         && strstr(ptx, ".target sm_70")
         && strstr(ptx, ".shared .align 4 .b8 wg0[512];")
         && strstr(ptx, ".visible .entry verify_atomics(")
         && strstr(ptx, ".reqntid 64, 1, 1")
         && strstr(ptx, "atom.shared.add.u32")
         && strstr(ptx, "atom.shared.min.u32")
         && strstr(ptx, "atom.shared.max.u32")
         && strstr(ptx, "atom.shared.exch.b32")
         && strstr(ptx, "atom.shared.cas.b32")
         && strstr(ptx, "atom.shared.or.b32")
         && strstr(ptx, "atom.global.add.u32")
         && strstr(ptx, "atom.global.exch.b32")
         && strstr(ptx, "atom.global.cas.b32")
         && strstr(ptx, "atom.global.or.b32")
         && ptx_count(ptx, "bar.sync 0;") == 3u
         && ptx_count(ptx, "membar.gl;") == 1u
         && !strstr(ptx, "atom.shared.add.u64")
         && !strstr(ptx, "atom.global.add.u64");
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUShaderLibrary *library;
  GPUDevice         device;
  GPUAdapter        adapter;
  GPUInstance       instance;
  Api               api;
  GPUResult         result;
  int               valid;

  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  UINT64_C(1) << GPU_FEATURE_COMPUTE,
                  70u);

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);

  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX atomic32 metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);

  if (!valid) {
    fprintf(stderr, "CUDA PTX atomic32 contract mismatch\n");
  }

  GPUDestroyShaderLibrary(library);
  return valid;
}
