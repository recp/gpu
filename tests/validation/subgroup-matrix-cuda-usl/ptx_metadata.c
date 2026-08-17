#include "../cuda-ptx-metadata/test.h"

#include <stdio.h>

static GPUResult
get_matrix_properties(const GPUAdapter               *adapter,
                      uint32_t                       *inoutPropertyCount,
                      GPUSubgroupMatrixPropertiesEXT *outProperties) {
  uint32_t capacity;

  (void)adapter;
  if (!inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity            = *inoutPropertyCount;
  *inoutPropertyCount = 1u;
  if (!outProperties) {
    return GPU_OK;
  }
  if (capacity == 0u) {
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  memset(outProperties, 0, sizeof(*outProperties));
  outProperties->m          = 16u;
  outProperties->n          = 16u;
  outProperties->k          = 16u;
  outProperties->aType      = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
  outProperties->bType      = GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT;
  outProperties->cType      = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
  outProperties->resultType = GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT;
  outProperties->stages     = GPU_SHADER_STAGE_COMPUTE_BIT;
  outProperties->scope      = GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
  return GPU_OK;
}

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
                                    "lhs",
                                    0u,
                                    0u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    2u,
                                    2u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "rhs",
                                    0u,
                                    1u,
                                    GPU_BINDING_STORAGE_BUFFER,
                                    2u,
                                    2u) ||
      !ptx_validate_buffer_resource(&library->_reflection,
                                    "output",
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
                                 GPU_BINDING_STORAGE_BUFFER,
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
  return ptx && strstr(ptx, ".version 6.3") &&
         strstr(ptx, ".target sm_70") &&
         strstr(ptx, ".visible .entry subgroup_matrix_cs(") &&
         strstr(ptx, ".reqntid 32, 1, 1") &&
         strstr(ptx, "wmma.load.a.sync.aligned.m16n16k16.global.row.f16") &&
         strstr(ptx, "wmma.load.b.sync.aligned.m16n16k16.global.row.f16") &&
         strstr(ptx, "wmma.mma.sync.aligned.m16n16k16.row.row.f32.f32") &&
         strstr(ptx, "wmma.store.d.sync.aligned.m16n16k16.global.row.f32");
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
                (UINT64_C(1) << GPU_FEATURE_SUBGROUPS) |
                (UINT64_C(1) << GPU_FEATURE_SHADER_F16) |
                (UINT64_C(1) << GPU_FEATURE_SUBGROUP_MATRIX);
  ptx_init_device(&device,
                  &adapter,
                  &instance,
                  &api,
                  featureMask,
                  70u);
  api.device.getSubgroupMatrixProperties = get_matrix_properties;
  api.device.supportsSubgroupOperations  = supports_subgroups;

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX subgroup-matrix metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX subgroup-matrix contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
