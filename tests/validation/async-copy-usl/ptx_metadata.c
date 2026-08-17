#include "../cuda-ptx-metadata/test.h"

#include <stdio.h>

static int
validate_contract(const GPUShaderLibrary *library) {
  const GPUShaderPTXInfo *info;
  const char             *ptx;

  if (!library || library->_reflection.resourceCount != 2u ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "source",
                                    0u,
                                    0u,
                                    GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                                    16u,
                                    16u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "output",
                                    0u,
                                    1u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    16u,
                                    16u)) {
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
                                 GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                                 0u) ||
      !ptx_validate_buffer_param(&info->params[1],
                                 0u,
                                 1u,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 8u)) {
    return 0;
  }

  ptx = ptx_source(library);
  return ptx && strstr(ptx, ".version 7.0") &&
         strstr(ptx, ".target sm_80") &&
         strstr(ptx, ".shared .align 16 .b8 wg0[1024];") &&
         strstr(ptx, ".visible .entry async_copy_cs(") &&
         strstr(ptx, ".reqntid 64, 1, 1") &&
         strstr(ptx, "cp.async.ca.shared.global") &&
         strstr(ptx, "cp.async.commit_group;") &&
         strstr(ptx, "cp.async.wait_group 0;") &&
         strstr(ptx, "bar.sync 0;") &&
         strstr(ptx, "ld.shared.v4.f32") &&
         strstr(ptx, "st.global.v4.f32") &&
         !strstr(ptx, "ld.global.v4.f32");
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUShaderLibrary *library;
  GPUDevice          device;
  GPUAdapter         adapter;
  GPUInstance        instance;
  GPUApi             api;
  GPUResult          result;
  int                valid;

  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  UINT64_C(1) << GPU_FEATURE_COMPUTE,
                  80u);

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX async-copy metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX async-copy contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
