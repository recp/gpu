#include "../cuda-ptx-metadata/test.h"

#include <stdio.h>

static int
validate_contract(const GPUShaderLibrary *library) {
  const GPUShaderPTXInfo *info;
  const char             *ptx;

  if (!library || library->_reflection.resourceCount != 2u ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "unsigned_values",
                                    0u,
                                    0u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    8u,
                                    8u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "signed_values",
                                    0u,
                                    1u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    8u,
                                    8u)) {
    return 0;
  }

  info = library->_ptxInfo;
  if (!info || info->entryCount != 1u || info->paramCount != 2u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 2u ||
      info->entries[0].paramDataSize != 16u ||
      !ptx_validate_buffer_param(&info->params[0],
                                 0u,
                                 0u,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 0u) ||
      !ptx_validate_buffer_param(&info->params[1],
                                 0u,
                                 1u,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 8u)) {
    return 0;
  }

  ptx = ptx_source(library);
  return ptx && strstr(ptx, ".version 4.0") &&
         strstr(ptx, ".target sm_50") &&
         strstr(ptx, ".visible .entry atomic64_cs(") &&
         strstr(ptx, ".reqntid 1, 1, 1") &&
         strstr(ptx, "atom.global.add.u64") &&
         strstr(ptx, "atom.global.min.u64") &&
         strstr(ptx, "atom.global.max.u64") &&
         strstr(ptx, "atom.global.min.s64") &&
         strstr(ptx, "atom.global.max.s64") &&
         strstr(ptx, "st.global.u64") && strstr(ptx, "st.global.s64");
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUShaderLibrary *library;
  GPUDevice          device;
  GPUAdapter         adapter;
  GPUInstance        instance;
  GPUApi             api;
  GPUResult          result;
  uint64_t           featureMask;
  int                valid;

  featureMask = (UINT64_C(1) << GPU_FEATURE_COMPUTE) |
                (UINT64_C(1) << GPU_FEATURE_ATOMIC64);
  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  featureMask,
                  50u);

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX atomic64 metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX atomic64 contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
