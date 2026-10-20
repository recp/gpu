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

#include "../../src/api/device_internal.h"
#include "../../src/backend/api/gpudef.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char native_source[] = "kernel void probe() {}";

static uint32_t observedFlags;
static uint32_t calls;

static GPUShaderLibrary *
capture_source(GPUDevice  *device,
               const char *source,
               uint64_t    size,
               uint32_t    flags) {
  (void)device;

  if (source && size) {
    observedFlags = flags;
    calls++;
  }

  /* inspect the real compilation boundary without simulating a native library. */

  return NULL;
}

int
main(int argc, char **argv) {
  GPUShaderLibraryCreateInfo info   = {0};
  GPUDevice                  device = {0};
  GPUApi                     api    = {0};
  GPUShaderLibrary          *library;
  void                      *artifact;
  FILE                      *file;
  long                       size;
  uint32_t                   expected, round;
  GPUResult                  result;
  int                        ok = 1;

  if (argc != 3 || !(file = fopen(argv[1], "rb")))
    return 1;

  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return 1;
  }

  artifact = malloc((size_t)size);

  if (!artifact || fread(artifact, 1u, (size_t)size, file) != (size_t)size) {
    free(artifact);
    fclose(file);
    return 1;
  }

  fclose(file);

  expected = strcmp(argv[2], "relaxed") == 0
             ? GPU_SHADER_SOURCE_COMPILE_RELAXED_FP : GPU_SHADER_SOURCE_COMPILE_STRICT_IEEE;

  api.backend                      = GPU_BACKEND_METAL;
  api.library.newLibraryWithSource = capture_source;
  device._api                      = &api;
  device.uslTargetVersion          = 401u;
  info.chain.sType                 = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize            = sizeof(info);
  info.sourceKind                  = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.sourceData                  = artifact;
  info.sourceSize                  = (uint64_t)size;

  for (round = 0u; round < 3u; round++) {
    info.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
    info.sourceData       = artifact;
    info.sourceSize       = (uint64_t)size;
    info.disableDiskCache = round == 0u;
    library               = NULL;
    calls                 = 0u;
    observedFlags         = UINT32_MAX;
    result                = GPUCreateShaderLibrary(&device, &info, &library);

    if (result != GPU_ERROR_BACKEND_FAILURE || library || calls != 1u || observedFlags != expected) {
      fprintf(stderr, "math policy round %u: result=%d calls=%u flags=%u expected=%u\n",
              round, result, calls, observedFlags, expected);
      ok = 0;
    }

    info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
    info.sourceData = native_source;
    info.sourceSize = sizeof(native_source) - 1u;
    calls           = 0u;
    observedFlags   = UINT32_MAX;
    result          = GPUCreateShaderLibrary(&device, &info, &library);

    if (result != GPU_ERROR_BACKEND_FAILURE || library || calls != 1u
        || observedFlags != GPU_SHADER_SOURCE_COMPILE_NONE) {
      fprintf(stderr, "native math policy round %u: result=%d calls=%u flags=%u\n",
              round, result, calls, observedFlags);
      ok = 0;
    }
  }

  free(artifact);
  return ok ? 0 : 1;
}
