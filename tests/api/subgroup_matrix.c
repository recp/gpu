#include "test.h"

static int
gpu_subgroupMatrixComponentValid(GPUSubgroupMatrixComponentTypeEXT type) {
  return type > GPU_SUBGROUP_MATRIX_COMPONENT_UNKNOWN_EXT &&
         type <= GPU_SUBGROUP_MATRIX_COMPONENT_BF16_EXT;
}

static int
gpu_subgroupMatrixPropertyValid(
  const GPUSubgroupMatrixPropertiesEXT *property) {
  return property && property->m > 0u && property->n > 0u &&
         property->k > 0u && property->stages != 0u &&
         gpu_subgroupMatrixComponentValid(property->aType) &&
         gpu_subgroupMatrixComponentValid(property->bType) &&
         gpu_subgroupMatrixComponentValid(property->cType) &&
         gpu_subgroupMatrixComponentValid(property->resultType) &&
         property->scope == GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
}

int
gpu_test_subgroup_matrix(GPUAdapter *adapter, const char *bytecodePath) {
  GPUFeature                         feature;
  GPUDeviceCreateInfo                deviceInfo;
  GPUComputePipelineCreateInfo       pipelineInfo;
  GPUSubgroupMatrixPropertiesEXT    *properties;
  GPUSubgroupMatrixPropertiesEXT     dummy;
  GPUDevice                         *device;
  GPUShaderLibrary                  *library;
  GPUShaderLayout                   *shaderLayout;
  GPUComputePipeline                *pipeline;
  void                              *bytecode;
  uint64_t                           bytecodeSize;
  uint32_t                           propertyCount;
  uint32_t                           capacity;
  GPUResult                          result;
  int                                supported;
  int                                ok;

  if (!adapter ||
      GPUGetSubgroupMatrixPropertiesEXT(NULL, &propertyCount, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetSubgroupMatrixPropertiesEXT(adapter, NULL, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    return 0;
  }

  supported     = GPUIsFeatureSupported(adapter,
                                        GPU_FEATURE_SUBGROUP_MATRIX);
  propertyCount = 0u;
  result = GPUGetSubgroupMatrixPropertiesEXT(adapter, &propertyCount, NULL);
  if (!supported) {
    return result == GPU_ERROR_UNSUPPORTED && propertyCount == 0u;
  }
  if (result != GPU_OK || propertyCount == 0u || !bytecodePath) {
    fprintf(stderr, "subgroup matrix capabilities are invalid\n");
    return 0;
  }

  capacity = 0u;
  if (GPUGetSubgroupMatrixPropertiesEXT(adapter, &capacity, &dummy) !=
        GPU_ERROR_INSUFFICIENT_CAPACITY ||
      capacity != propertyCount) {
    fprintf(stderr, "subgroup matrix capacity query is invalid\n");
    return 0;
  }

  properties = calloc(propertyCount, sizeof(*properties));
  if (!properties) {
    return 0;
  }
  capacity = propertyCount;
  result = GPUGetSubgroupMatrixPropertiesEXT(adapter,
                                             &capacity,
                                             properties);
  if (result != GPU_OK || capacity != propertyCount) {
    free(properties);
    return 0;
  }
  for (uint32_t i = 0u; i < propertyCount; i++) {
    if (!gpu_subgroupMatrixPropertyValid(&properties[i])) {
      free(properties);
      return 0;
    }
  }
  free(properties);

  memset(&deviceInfo, 0, sizeof(deviceInfo));
  feature                          = GPU_FEATURE_SUBGROUP_MATRIX;
  deviceInfo.chain.sType          = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize     = sizeof(deviceInfo);
  deviceInfo.required.pFeatures   = &feature;
  deviceInfo.required.featureCount = 1u;
  device       = NULL;
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  bytecode     = NULL;
  bytecodeSize = 0u;
  ok           = 0;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS) ||
      !GPUGetProcAddr(device, "GPUGetSubgroupMatrixPropertiesEXT")) {
    fprintf(stderr, "subgroup matrix feature enablement failed\n");
    goto cleanup;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "subgroup matrix shader setup failed\n");
    goto cleanup;
  }

  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "subgroup_matrix_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "subgroup matrix pipeline creation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  free(bytecode);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}
