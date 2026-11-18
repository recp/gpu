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

/*
 * a device's native 64-bit types reach USL (device->uslWide64,
 * uslPortabilitySubset): the SPIR-V GPU-C hands its Vulkan backend declares
 * Int64 / Float64 exactly when the device has them (USL emulates the rest in
 * 32-bit words), and divides 64-bit integers natively unless the device is
 * a portability subset; the HLSL it hands D3D12 names uint64_t / double the
 * same way
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L /* unsetenv */
#endif

#include "../../src/api/device_internal.h"
#include "../../src/backend/api/gpudef.h"

#include <spirv/unified1/spirv.h>
#include <us/compiler.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool     int64Capability;
static bool     float64Capability;
static uint32_t divide64;
static uint32_t calls;
static bool     valid;

static GPUShaderLibrary*
capture_binary(GPUDevice *device, const void *data, uint64_t size) {
  const uint32_t *words;
  uint8_t        *widths;
  size_t          count;
  size_t          at;
  uint32_t        length;
  uint32_t        op;

  (void)device;
  calls++;
  words = data;
  count = (size_t)(size / sizeof(*words));
  valid = count >= 5u && size % sizeof(*words) == 0u
          && words[0] == SpvMagicNumber && words[3] > 0u && words[3] <= 65536u;
  widths = valid ? calloc(words[3], 1u) : NULL; /* integer types' widths by id */
  valid  = valid && widths;

  for (at = 5u; valid && at < count; at += length) {
    length = words[at] >> 16u;
    op     = words[at] & 0xffffu;

    if (length == 0u || length > count - at) {
      valid = false;
      break;
    }

    if (op == SpvOpCapability && length == 2u) {
      int64Capability   = int64Capability || words[at + 1u] == SpvCapabilityInt64;
      float64Capability = float64Capability || words[at + 1u] == SpvCapabilityFloat64;
    } else if (op == SpvOpTypeInt && length == 4u && words[at + 1u] < words[3]) {
      widths[words[at + 1u]] = (uint8_t)words[at + 2u];
    } else if (op == SpvOpTypeVector && length == 4u
               && words[at + 1u] < words[3] && words[at + 2u] < words[3]) {
      widths[words[at + 1u]] = widths[words[at + 2u]];
    } else if ((op == SpvOpUDiv || op == SpvOpSDiv || op == SpvOpUMod || op == SpvOpSRem || op == SpvOpSMod)
               && length == 5u && words[at + 1u] < words[3] && widths[words[at + 1u]] == 64u) {
      divide64++;
    }
  }

  free(widths);

  /* the payload only: no native library */
  return NULL;
}

/* how often a word stands alone in HLSL text */
static uint32_t
hlslWords(const char *text, size_t size, const char *word) {
  const char *at;
  const char *end;
  size_t      length;
  uint32_t    found;

  length = strlen(word);
  end    = text + size;
  found  = 0u;

  for (at = text; (size_t)(end - at) >= length; at++) {
    if (memcmp(at, word, length) == 0
        && (at == text || !(isalnum((unsigned char)at[-1]) || at[-1] == '_'))
        && (at + length == end || !(isalnum((unsigned char)at[length]) || at[length] == '_'))) {
      found++;
    }
  }

  return found;
}

static GPUShaderLibrary*
capture_source(GPUDevice *device, const char *source, uint64_t size, uint32_t flags) {
  (void)device;
  (void)flags;
  calls++;
  valid             = source && size > 0u;
  int64Capability   = valid && hlslWords(source, (size_t)size, "uint64_t") > 0u;
  float64Capability = valid && hlslWords(source, (size_t)size, "double") > 0u;
  return NULL;
}

int
main(int argc, char **argv) {
  GPUShaderLibraryCreateInfo info   = {0};
  GPUDevice                  device = {0};
  GPUApi                     api    = {0};
  GPUShaderLibrary          *library;
  FILE                      *file;
  void                      *artifact;
  long                       size;
  uint32_t                   wide;
  uint32_t                   portability;
  bool                       expectDivide;
  int                        ok = 1;

  if (argc != 2 || !(file = fopen(argv[1], "rb")))
    return 1;

  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET))
    return 1;
  artifact = malloc((size_t)size);

  if (!artifact || fread(artifact, 1u, (size_t)size, file) != (size_t)size)
    return 1;
  fclose(file);

  api.backend                      = GPU_BACKEND_VULKAN;
  api.library.newLibraryWithBinary = capture_binary;
  device._api                      = &api;
  device.uslTargetProfile          = USL_TARGET_PROFILE_VULKAN_1_3;
  info.chain.sType                 = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize            = sizeof(info);
  info.sourceKind                  = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.sourceData                  = artifact;
  info.sourceSize                  = (uint64_t)size;
  info.disableDiskCache            = true;

  for (wide = 0u; wide < 4u; wide++) {
    for (portability = 0u; portability < 2u; portability++) {
      device.uslWide64            = (uint8_t)wide;
      device.uslPortabilitySubset = portability != 0u;
      int64Capability             = float64Capability = false;
      divide64                    = calls = 0u;
      valid                       = false;
      library                     = NULL;
      expectDivide                = (wide & GPU_USL_WIDE_INT64) && !portability;

      if (GPUCreateShaderLibrary(&device, &info, &library) != GPU_ERROR_BACKEND_FAILURE || library
          || calls != 1u || !valid
          || int64Capability != ((wide & GPU_USL_WIDE_INT64) != 0u)
          || float64Capability != ((wide & GPU_USL_WIDE_F64) != 0u)
          || (divide64 != 0u) != expectDivide) {
        fprintf(stderr, "wide64: device bits %u, portability %u: calls %u, valid %d, Int64 %d, Float64 %d, "
                "64-bit divisions %u (expected %s)\n",
                wide, portability, calls, valid, int64Capability, float64Capability, divide64,
                expectDivide ? "some" : "none");
        ok = 0;
      }
    }
  }

  /* D3D12's default path: HLSL text for DXC */
#if defined(_WIN32) || defined(WIN32)
  _putenv("GPU_DX12_NATIVE_DXIL=");
#else
  unsetenv("GPU_DX12_NATIVE_DXIL");
#endif
  api.backend                      = GPU_BACKEND_DX12;
  api.library.newLibraryWithBinary = NULL;
  api.library.newLibraryWithSource = capture_source;
  device.uslTargetProfile          = USL_TARGET_PROFILE_HLSL_SM_6_6;
  device.uslPortabilitySubset      = false;

  for (wide = 0u; wide < 4u; wide++) {
    device.uslWide64  = (uint8_t)wide;
    int64Capability   = float64Capability = false;
    calls             = 0u;
    valid             = false;
    library           = NULL;

    if (GPUCreateShaderLibrary(&device, &info, &library) != GPU_ERROR_BACKEND_FAILURE || library
        || calls != 1u || !valid
        || int64Capability != ((wide & GPU_USL_WIDE_INT64) != 0u)
        || float64Capability != ((wide & GPU_USL_WIDE_F64) != 0u)) {
      fprintf(stderr, "wide64 hlsl: device bits %u: calls %u, valid %d, uint64_t %d, double %d\n",
              wide, calls, valid, int64Capability, float64Capability);
      ok = 0;
    }
  }

  free(artifact);
  return ok ? 0 : 1;
}
