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

#include <spirv/unified1/spirv.h>
#include <us/compiler.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t observedMask;
static uint32_t observedModes;
static uint32_t observedHalfFence;
static uint32_t observedFma;
static uint32_t observedFmaCount;
static uint32_t observedDenorm;
static uint32_t observedRTE;
static uint32_t observedDefaults;
static uint32_t calls;
static bool     valid;

static GPUShaderLibrary*
capture_binary(GPUDevice *device, const void *data, uint64_t size) {
  const uint32_t *words;
  uint8_t        *widths;
  size_t          count;
  size_t          instructionIndex;
  size_t          defaultIndex;
  uint32_t        instructionLength;
  uint32_t        bit;
  uint32_t        defaultLength;

  words = data;
  count = (size_t)(size / sizeof(*words));

  (void)device;
  calls++;
  valid  = count >= 5u && size % sizeof(*words) == 0u
           && words[0] == SpvMagicNumber && words[3] > 0u && words[3] <= 65536u;
  widths = valid ? calloc(words[3], 1u) : NULL;
  valid  = valid && widths;

  for (instructionIndex = 5u; valid && instructionIndex < count;) {
    instructionLength = words[instructionIndex] >> 16u;

    if (instructionLength == 0u || instructionLength > count - instructionIndex) {
      valid = false;
      break;
    }

    if ((words[instructionIndex] & 0xffffu) == SpvOpExecutionMode && instructionLength == 4u) {
      bit = words[instructionIndex + 3u] / 16u;

      switch (words[instructionIndex + 2u]) {
        case SpvExecutionModeSignedZeroInfNanPreserve:
          observedMask |= bit;
          observedModes++;
          break;
        case SpvExecutionModeDenormPreserve:
          observedDenorm |= bit;
          break;
        case SpvExecutionModeRoundingModeRTE:
          observedRTE |= bit;
          break;
        default:
          break;
      }
    }

    if ((words[instructionIndex] & 0xffffu) == SpvOpTypeFloat && instructionLength == 3u
        && words[instructionIndex + 1u] < words[3]) {
      widths[words[instructionIndex + 1u]] = (uint8_t)(words[instructionIndex + 2u] / 16u);
    } else if ((words[instructionIndex] & 0xffffu) == SpvOpTypeVector && instructionLength == 4u
               && words[instructionIndex + 1u] < words[3] && words[instructionIndex + 2u] < words[3]) {
      widths[words[instructionIndex + 1u]] = widths[words[instructionIndex + 2u]];
    } else if ((words[instructionIndex] & 0xffffu) == SpvOpFmaKHR && instructionLength == 6u
               && words[instructionIndex + 1u] < words[3]) {
      observedFma |= widths[words[instructionIndex + 1u]];
      observedFmaCount++;
    }

    if ((words[instructionIndex] & 0xffffu) == SpvOpFMul && instructionLength == 5u
        && words[instructionIndex + 1u] < words[3] && widths[words[instructionIndex + 1u]] == 1u) {
      observedHalfFence++;
    }

    instructionIndex += instructionLength;
  }

  for (defaultIndex = 5u; valid && defaultIndex < count;) {
    defaultLength = words[defaultIndex] >> 16u;

    if ((words[defaultIndex] & 0xffffu) == SpvOpExecutionModeId && defaultLength == 5u
        && words[defaultIndex + 2u] == SpvExecutionModeFPFastMathDefault && words[defaultIndex + 3u] < words[3]) {
      valid = widths[words[defaultIndex + 3u]] != 0u;
      observedDefaults++;
    }

    defaultIndex += defaultLength;
  }

  free(widths);

  /* capture the real GPU->USL payload, but do not simulate native creation. */

  return NULL;
}

int
main(int argc, char **argv) {
  GPUShaderLibraryCreateInfo info   = {0};
  GPUDevice                  device = {0};
  Api                        api    = {0};
  void                      *artifact;
  FILE                      *file;
  GPUShaderLibrary          *library;
  long                       size;
  int                        ok = 1;
  uint32_t                   round;
  uint32_t                   test;
  uint32_t                   controls2;
  uint32_t                   mask;
  uint32_t                   fmaMask;
  uint32_t                   denormMask;
  uint32_t                   rteMask;
  uint32_t                   expectedMask;
  uint32_t                   expectedModes;
  uint32_t                   expectedFma;
  GPUResult                  result;
  bool                       strict;
  bool                       modern;

  strict = argc == 2;

  if ((argc != 2 && argc != 3) || !(file = fopen(argv[1], "rb")))
    return 1;

  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0
      || fseek(file, 0, SEEK_SET))
    return 1;
  artifact = malloc((size_t)size);

  if (!artifact || fread(artifact, 1u, (size_t)size, file) != (size_t)size)
    return 1;
  fclose(file);
  api.backend                      = GPU_BACKEND_VULKAN;
  api.library.newLibraryWithBinary = capture_binary;
  device._api                      = &api;
  device.uslTargetProfile          = USL_TARGET_PROFILE_VULKAN_1_3;
  device.enabledFeatureMask        = UINT64_C(1) << GPU_FEATURE_SHADER_F16;
  info.chain.sType                 = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize            = sizeof(info);
  info.sourceKind                  = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.sourceData                  = artifact;
  info.sourceSize                  = (uint64_t)size;
  info.disableDiskCache            = true;

  /* repeat in one process: capability-sensitive cache keys must stay distinct. */

  for (round = 0u; round < 3u; ++round) {
    for (test = 0u; test < 128u; ++test) {
      for (controls2 = 0u; controls2 < 2u; ++controls2) {
        mask          = test < 64u ? test >> 3u : 7u;
        fmaMask       = test < 64u ? test & 7u : 7u;
        denormMask    = test < 64u ? 0u : (test - 64u) >> 3u;
        rteMask       = test < 64u ? 0u : test & 7u;
        modern        = strict && controls2 && mask == 7u;
        library       = NULL;
        expectedMask  = strict && !modern ? mask : 0u;
        expectedModes = (expectedMask & 1u) + ((expectedMask >> 1u) & 1u) + ((expectedMask >> 2u) & 1u);
        expectedFma   = (fmaMask & 1u) + ((fmaMask >> 1u) & 1u) + ((fmaMask >> 2u) & 1u);

        device.uslFloatPreserve  = (uint8_t)mask;
        device.uslFma            = (uint8_t)fmaMask;
        device.uslDenormPreserve = (uint8_t)denormMask;
        device.uslRoundingRTE    = (uint8_t)rteMask;
        device.uslFloatControls2 = controls2 != 0u;
        device.uslHalfRoundtrip  = round == 1u && (mask & denormMask & rteMask & 1u);
        observedHalfFence        = 0u;
        observedMask             = observedModes = calls = 0u;
        observedFma              = observedFmaCount = 0u;
        observedDenorm           = observedRTE = observedDefaults = 0u;
        valid                    = false;
        result                   = GPUCreateShaderLibrary(&device, &info, &library);

        if (result != GPU_ERROR_BACKEND_FAILURE || library || calls != 1u
            || !valid || observedMask != expectedMask || observedModes != expectedModes
            || observedFma != fmaMask || observedFmaCount != expectedFma
            || observedDenorm != (strict ? denormMask : 0u)
            || observedRTE != (strict ? rteMask : 0u)
            || observedHalfFence != (strict && device.uslHalfRoundtrip ? 1u : 0u)
            || observedDefaults != (modern ? 3u : 0u)) {
          fprintf(stderr, "float controls round %u mask %u fma %u: result=%d calls=%u mask=%u modes=%u fma=%u/%u\n",
                  round, mask, fmaMask, result, calls, observedMask, observedModes, observedFma, observedFmaCount);
          ok = 0;
        }
      }
    }
  }

  free(artifact);
  return ok ? 0 : 1;
}
