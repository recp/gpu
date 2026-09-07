#include "../../src/api/device_internal.h"
#include "../../src/backend/api/gpudef.h"

#include <spirv/unified1/spirv.h>
#include <us/compiler.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t observedMask;
static uint32_t observedModes;
static uint32_t observedFma;
static uint32_t observedFmaCount;
static uint32_t calls;
static bool     valid;

static GPUShaderLibrary *
capture_binary(GPUDevice *device, const void *data, uint64_t size) {
  const uint32_t *words = data;
  size_t         count = (size_t)(size / sizeof(*words));
  uint8_t       *widths;

  (void)device;
  calls++;
  valid = count >= 5u && size % sizeof(*words) == 0u &&
          words[0] == SpvMagicNumber && words[3] > 0u && words[3] <= 65536u;
  widths = valid ? calloc(words[3], 1u) : NULL;
  valid = valid && widths;
  for (size_t i = 5u; valid && i < count;) {
    uint32_t length = words[i] >> 16u;
    if (length == 0u || length > count - i) {
      valid = false;
      break;
    }
    if ((words[i] & 0xffffu) == SpvOpExecutionMode && length == 4u &&
        words[i + 2u] == SpvExecutionModeSignedZeroInfNanPreserve) {
      switch (words[i + 3u]) {
        case 16u: observedMask |= 1u; break;
        case 32u: observedMask |= 2u; break;
        case 64u: observedMask |= 4u; break;
        default: valid = false; break;
      }
      observedModes++;
    }
    if ((words[i] & 0xffffu) == SpvOpTypeFloat && length == 3u && words[i + 1u] < words[3]) {
      widths[words[i + 1u]] = (uint8_t)(words[i + 2u] / 16u);
    } else if ((words[i] & 0xffffu) == SpvOpTypeVector && length == 4u &&
               words[i + 1u] < words[3] && words[i + 2u] < words[3]) {
      widths[words[i + 1u]] = widths[words[i + 2u]];
    } else if ((words[i] & 0xffffu) == SpvOpFmaKHR && length == 6u && words[i + 1u] < words[3]) {
      observedFma |= widths[words[i + 1u]];
      observedFmaCount++;
    }
    i += length;
  }
  free(widths);
  /* Capture the real GPU->USL payload, but do not simulate native creation. */
  return NULL;
}

int
main(int argc, char **argv) {
  GPUShaderLibraryCreateInfo info = {0};
  GPUDevice                 device = {0};
  GPUApi                    api = {0};
  void                     *artifact;
  FILE                     *file;
  long                      size;
  int                       ok = 1;
  bool                      strict = argc == 2;

  if ((argc != 2 && argc != 3) || !(file = fopen(argv[1], "rb"))) return 1;
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET)) return 1;
  artifact = malloc((size_t)size);
  if (!artifact || fread(artifact, 1u, (size_t)size, file) != (size_t)size) return 1;
  fclose(file);
  api.backend                      = GPU_BACKEND_VULKAN;
  api.library.newLibraryWithBinary  = capture_binary;
  device._api                      = &api;
  device.uslTargetProfile           = USL_TARGET_PROFILE_VULKAN_1_3;
  device.enabledFeatureMask        = UINT64_C(1) << GPU_FEATURE_SHADER_F16;
  info.chain.sType                 = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  info.chain.structSize            = sizeof(info);
  info.sourceKind                  = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.sourceData                  = artifact;
  info.sourceSize                  = (uint64_t)size;
  info.disableDiskCache            = true;
  /* Repeat in one process: capability-sensitive cache keys must stay distinct. */
  for (uint32_t round = 0u; round < 2u; ++round) {
    for (uint32_t mask = 0u; mask < 8u; ++mask) {
      for (uint32_t fmaMask = 0u; fmaMask < 8u; ++fmaMask) {
        GPUShaderLibrary *library = NULL;
        uint32_t expectedMask  = strict ? mask : 0u;
        uint32_t expectedModes = (expectedMask & 1u) + ((expectedMask >> 1u) & 1u) + ((expectedMask >> 2u) & 1u);
        uint32_t expectedFma   = (fmaMask & 1u) + ((fmaMask >> 1u) & 1u) + ((fmaMask >> 2u) & 1u);

        device.uslFloatPreserve = (uint8_t)mask;
        device.uslFma           = (uint8_t)fmaMask;
        observedMask = observedModes = calls = 0u;
        observedFma = observedFmaCount = 0u;
        valid = false;
        GPUResult result = GPUCreateShaderLibrary(&device, &info, &library);
        if (result != GPU_ERROR_BACKEND_FAILURE || library || calls != 1u ||
            !valid || observedMask != expectedMask || observedModes != expectedModes ||
            observedFma != fmaMask || observedFmaCount != expectedFma) {
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
