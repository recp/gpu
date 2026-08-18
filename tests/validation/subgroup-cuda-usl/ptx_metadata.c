#include "../cuda-ptx-metadata/test.h"

#include <stdio.h>

static bool
supports_subgroups(const GPUAdapter                 *adapter,
                   GPUShaderStageFlags               stage,
                   GPUBackendSubgroupOperationFlags  operations) {
  (void)adapter;
  (void)operations;
  return (stage & GPU_SHADER_STAGE_COMPUTE_BIT) != 0u;
}

static int
validate_contract(const GPUShaderLibrary *library) {
  const GPUShaderPTXInfo *info;
  const char             *ptx;

  if (!library || library->_reflection.resourceCount != 3u ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "values",
                                    0u,
                                    0u,
                                    GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                                    4u,
                                    4u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "shuffled",
                                    0u,
                                    1u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    4u,
                                    4u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "relative",
                                    0u,
                                    2u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    4u,
                                    4u)) {
    return 0;
  }

  info = library->_ptxInfo;
  if (!info || info->entryCount != 1u || info->paramCount != 3u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 3u ||
      info->entries[0].paramDataSize != 24u ||
      !ptx_validate_buffer_param(&info->params[0],
                                 0u,
                                 0u,
                                 GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                                 0u) ||
      !ptx_validate_buffer_param(&info->params[1],
                                 0u,
                                 1u,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 8u) ||
      !ptx_validate_buffer_param(&info->params[2],
                                 0u,
                                 2u,
                                 GPU_BINDING_STORAGE_BUFFER,
                                 16u)) {
    return 0;
  }

  ptx = ptx_source(library);
  /* Output buffers may alias values, so the intervening store requires a reload. */
  return ptx && strstr(ptx, ".version 6.2") &&
         strstr(ptx, ".target sm_70") &&
         strstr(ptx, ".visible .entry subgroup_cs(") &&
         strstr(ptx, ".reqntid 64, 1, 1") &&
         ptx_count(ptx, "activemask.b32") == 1u &&
         ptx_count(ptx, "shfl.sync.bfly.b32") == 1u &&
         ptx_count(ptx, "shfl.sync.down.b32") == 1u &&
         ptx_count(ptx, "selp.u32") == 1u &&
         ptx_count(ptx, "ld.global.u32") == 2u &&
         ptx_count(ptx, "st.global.u32") == 2u;
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
                (UINT64_C(1) << GPU_FEATURE_SUBGROUPS);
  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  featureMask,
                  70u);
  api.device.supportsSubgroupOperations = supports_subgroups;

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX subgroup metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX subgroup contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
