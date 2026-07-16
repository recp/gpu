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
  GPUSubgroupMatrixPropertiesEXT *properties;
  GPUDevice                       *device;
  GPUQueue                        *queue;
  GPUShaderLibrary                *library;
  GPUShaderLayout                 *shaderLayout;
  GPUComputePipeline              *pipeline;
  GPUBuffer                       *lhsBuffer;
  GPUBuffer                       *rhsBuffer;
  GPUBuffer                       *outputBuffer;
  GPUBindGroup                    *group;
  GPUCommandBuffer                *cmdb;
  GPUComputePassEncoder           *pass;
  GPUFence                        *fence;
  void                            *bytecode;
  GPUCommandBuffer                *submitList[1];
  GPUDeviceCreateInfo              deviceInfo;
  GPUComputePipelineCreateInfo     pipelineInfo;
  GPUBufferCreateInfo              bufferInfo;
  GPUBindGroupCreateInfo           groupInfo;
  GPUQueueSubmitInfo               submitInfo;
  GPUSubgroupMatrixPropertiesEXT   dummy;
  GPUBindGroupEntry                entries[3];
  uint64_t                         bytecodeSize;
  uint32_t                         propertyCount;
  uint32_t                         capacity;
  GPUFeature                       feature;
  GPUResult                        result;
  int                              supported;
  int                              ok;
  uint16_t                         lhsValues[64];
  uint16_t                         rhsValues[64];
  float                            outputValues[64];

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
    if (result != GPU_ERROR_UNSUPPORTED || propertyCount != 0u) {
      return 0;
    }
    puts("subgroup matrix execution skipped: unsupported adapter");
    return 1;
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
  feature                           = GPU_FEATURE_SUBGROUP_MATRIX;
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  queue        = NULL;
  device       = NULL;
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  lhsBuffer    = NULL;
  rhsBuffer    = NULL;
  outputBuffer = NULL;
  group        = NULL;
  cmdb         = NULL;
  pass         = NULL;
  fence        = NULL;
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
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "subgroup matrix queue unavailable\n");
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
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
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

  memset(lhsValues, 0, sizeof(lhsValues));
  memset(rhsValues, 0, sizeof(rhsValues));
  memset(outputValues, 0, sizeof(outputValues));
  /* IEEE-754 half 1.0 builds deterministic identity inputs. */
  for (uint32_t i = 0u; i < 8u; i++) {
    lhsValues[i * 8u + i] = 0x3c00u;
    rhsValues[i * 8u + i] = 0x3c00u;
  }

  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(lhsValues);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &lhsBuffer) != GPU_OK ||
      !lhsBuffer ||
      GPUCreateBuffer(device, &bufferInfo, &rhsBuffer) != GPU_OK ||
      !rhsBuffer ||
      GPUQueueWriteBuffer(queue,
                          lhsBuffer,
                          0u,
                          lhsValues,
                          sizeof(lhsValues)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          rhsBuffer,
                          0u,
                          rhsValues,
                          sizeof(rhsValues)) != GPU_OK) {
    fprintf(stderr, "subgroup matrix input buffer setup failed\n");
    goto cleanup;
  }

  bufferInfo.sizeBytes = sizeof(outputValues);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          outputValues,
                          sizeof(outputValues)) != GPU_OK) {
    fprintf(stderr, "subgroup matrix output buffer setup failed\n");
    goto cleanup;
  }

  memset(entries, 0, sizeof(entries));
  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[0].buffer.buffer = lhsBuffer;
  entries[0].buffer.size   = sizeof(lhsValues);
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = rhsBuffer;
  entries[1].buffer.size   = sizeof(rhsValues);
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[2].buffer.buffer = outputBuffer;
  entries[2].buffer.size   = sizeof(outputValues);
  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 3u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue,
                              "subgroup-matrix-test",
                              &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb,
                                            "subgroup-matrix-test"))) {
    fprintf(stderr, "subgroup matrix command setup failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "subgroup matrix fence creation failed\n");
    goto cleanup;
  }
  memset(&submitInfo, 0, sizeof(submitInfo));
  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "subgroup matrix submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         outputValues,
                         sizeof(outputValues)) != GPU_OK) {
    fprintf(stderr, "subgroup matrix readback failed\n");
    goto cleanup;
  }
  for (uint32_t row = 0u; row < 8u; row++) {
    for (uint32_t column = 0u; column < 8u; column++) {
      float expected = row == column ? 1.0f : 0.0f;
      float actual   = outputValues[row * 8u + column];

      if (actual != expected) {
        fprintf(stderr,
                "subgroup matrix result mismatch at %u,%u: %f\n",
                row,
                column,
                actual);
        goto cleanup;
      }
    }
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  free(bytecode);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(rhsBuffer);
  GPUDestroyBuffer(lhsBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}
