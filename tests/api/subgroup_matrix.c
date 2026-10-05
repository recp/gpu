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

#include "test.h"

enum {
  MATRIX_MAX_LHS_ELEMENTS    = 256u,
  MATRIX_MAX_RHS_ELEMENTS    = 256u,
  MATRIX_MAX_OUTPUT_ELEMENTS = 256u
};

static int
gpu_subgroupMatrixComponentValid(GPUSubgroupMatrixComponentTypeEXT type) {
  return type > GPU_SUBGROUP_MATRIX_COMPONENT_UNKNOWN_EXT
         && type <= GPU_SUBGROUP_MATRIX_COMPONENT_BF16_EXT;
}

static int
gpu_subgroupMatrixPropertyValid(const GPUSubgroupMatrixPropertiesEXT *property) {
  return property && property->m > 0u && property->n > 0u
         && property->k > 0u && property->stages != 0u
         && gpu_subgroupMatrixComponentValid(property->aType)
         && gpu_subgroupMatrixComponentValid(property->bType)
         && gpu_subgroupMatrixComponentValid(property->cType)
         && gpu_subgroupMatrixComponentValid(property->resultType)
         && property->scope == GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
}

int
gpu_test_subgroup_matrix(GPUAdapter *adapter, const char *bytecodePath) {
  GPUSubgroupMatrixPropertiesEXT *properties;
  GPUDevice                      *device;
  GPUQueue                       *queue;
  GPUShaderLibrary               *library;
  GPUShaderLayout                *shaderLayout;
  GPUComputePipeline             *pipeline;
  GPUBuffer                      *lhsBuffer;
  GPUBuffer                      *rhsBuffer;
  GPUBuffer                      *outputBuffer;
  GPUBindGroup                   *group;
  GPUCommandBuffer               *cmdb;
  GPUComputePassEncoder          *pass;
  GPUFence                       *fence;
  void                           *bytecode;
  const char                     *matrixProfile;
  GPUCommandBuffer               *submitList[1];
  GPUDeviceCreateInfo             deviceInfo;
  GPUComputePipelineCreateInfo    pipelineInfo;
  GPUBufferCreateInfo             bufferInfo;
  GPUBindGroupCreateInfo          groupInfo;
  GPUQueueSubmitInfo              submitInfo;
  GPUSubgroupMatrixPropertiesEXT  dummy;
  GPUBindGroupEntry               entries[3];
  uint64_t                        bytecodeSize;
  uint32_t                        propertyCount;
  uint32_t                        capacity;
  uint32_t                        matrixM;
  uint32_t                        matrixN;
  uint32_t                        matrixK;
  uint32_t                        lhsElementCount;
  uint32_t                        rhsElementCount;
  uint32_t                        outputElementCount;
  GPUFeature                      features[2];
  GPUResult                       result;
  int                             supported;
  int                             profileSupported;
  int                             ok;
  uint16_t                        lhsValues[MATRIX_MAX_LHS_ELEMENTS];
  uint16_t                        rhsValues[MATRIX_MAX_RHS_ELEMENTS];
  float                           outputValues[MATRIX_MAX_OUTPUT_ELEMENTS];
  float                           expected;
  float                           actual;

  matrixM       = 8u;
  matrixN       = 8u;
  matrixK       = 8u;
  matrixProfile = getenv("GPU_SUBGROUP_MATRIX_PROFILE");

  if (matrixProfile && strcmp(matrixProfile, "16x8x8") == 0) {
    matrixM = 16u;
  } else if (matrixProfile && strcmp(matrixProfile, "16x16x16") == 0) {
    matrixM = 16u;
    matrixN = 16u;
    matrixK = 16u;
  }

  lhsElementCount    = matrixM * matrixK;
  rhsElementCount    = matrixK * matrixN;
  outputElementCount = matrixM * matrixN;

  if (!adapter
      || GPUGetSubgroupMatrixPropertiesEXT(NULL, &propertyCount, NULL) != GPU_ERROR_INVALID_ARGUMENT
      || GPUGetSubgroupMatrixPropertiesEXT(adapter, NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    return 0;
  }

  supported     = GPUIsFeatureSupported(adapter,
                                        GPU_FEATURE_SUBGROUP_MATRIX);
  propertyCount = 0u;
  result        = GPUGetSubgroupMatrixPropertiesEXT(adapter, &propertyCount, NULL);

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

  if (GPUGetSubgroupMatrixPropertiesEXT(adapter, &capacity, &dummy) != GPU_ERROR_INSUFFICIENT_CAPACITY
      || capacity != propertyCount) {
    fprintf(stderr, "subgroup matrix capacity query is invalid\n");
    return 0;
  }

  if (!(properties = calloc(propertyCount, sizeof(*properties)))) {
    return 0;
  }

  capacity = propertyCount;
  result   = GPUGetSubgroupMatrixPropertiesEXT(adapter,
                                               &capacity,
                                               properties);

  if (result != GPU_OK || capacity != propertyCount) {
    free(properties);
    return 0;
  }

  profileSupported = 0;

  for (uint32_t propertyIndex = 0u; propertyIndex < propertyCount; propertyIndex++) {
    if (!gpu_subgroupMatrixPropertyValid(&properties[propertyIndex])) {
      free(properties);
      return 0;
    }

    if (properties[propertyIndex].m == matrixM
        && properties[propertyIndex].n == matrixN
        && properties[propertyIndex].k == matrixK
        && properties[propertyIndex].aType == GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT
        && properties[propertyIndex].bType == GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT
        && properties[propertyIndex].cType == GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT
        && properties[propertyIndex].resultType == GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT
        && (properties[propertyIndex].stages & GPU_SHADER_STAGE_COMPUTE_BIT) != 0u) {
      profileSupported = 1;
    }
  }

  free(properties);

  if (!profileSupported) {
    printf("subgroup matrix execution skipped: %ux%ux%u unsupported\n",
           matrixM,
           matrixN,
           matrixK);
    return 1;
  }

  memset(&deviceInfo, 0, sizeof(deviceInfo));
  features[0] = GPU_FEATURE_SUBGROUP_MATRIX;
  features[1] = GPU_FEATURE_SHADER_F16;
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = 2u;
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
  ok = 0;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK || !device
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX)
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUPS)
      || !GPUGetProcAddr(device, "GPUGetSubgroupMatrixPropertiesEXT")) {
    fprintf(stderr, "subgroup matrix feature enablement failed\n");
    goto cleanup;
  }

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u))) {
    queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  }

  if (!queue) {
    fprintf(stderr, "subgroup matrix queue unavailable\n");
    goto cleanup;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);

  if (!bytecode
      || GPUCreateShaderLibraryFromUSL(device,
                                       bytecode,
                                       bytecodeSize,
                                       &library) != GPU_OK
      || !library
      || GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK
      || !shaderLayout || !shaderLayout->pipelineLayout
      || shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "subgroup matrix shader setup failed\n");
    goto cleanup;
  }

  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "subgroup_matrix_cs";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK
      || !pipeline) {
    fprintf(stderr, "subgroup matrix pipeline creation failed\n");
    goto cleanup;
  }

  memset(lhsValues, 0, sizeof(lhsValues));
  memset(rhsValues, 0, sizeof(rhsValues));
  memset(outputValues, 0, sizeof(outputValues));

  /* IEEE-754 half 1.0 builds deterministic identity inputs. */

  for (uint32_t diagonalIndex = 0u; diagonalIndex < matrixK; diagonalIndex++) {
    lhsValues[diagonalIndex * matrixK + diagonalIndex] = 0x3c00u;
    rhsValues[diagonalIndex * matrixN + diagonalIndex] = 0x3c00u;
  }

  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = lhsElementCount * sizeof(lhsValues[0]);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &lhsBuffer) != GPU_OK
      || !lhsBuffer
      || GPUCreateBuffer(device, &bufferInfo, &rhsBuffer) != GPU_OK
      || !rhsBuffer
      || GPUQueueWriteBuffer(queue,
                             lhsBuffer,
                             0u,
                             lhsValues,
                             bufferInfo.sizeBytes) != GPU_OK
      || GPUQueueWriteBuffer(queue,
                             rhsBuffer,
                             0u,
                             rhsValues,
                             rhsElementCount * sizeof(rhsValues[0])) != GPU_OK) {
    fprintf(stderr, "subgroup matrix input buffer setup failed\n");
    goto cleanup;
  }

  bufferInfo.sizeBytes = outputElementCount * sizeof(outputValues[0]);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK
      || !outputBuffer
      || GPUQueueWriteBuffer(queue,
                             outputBuffer,
                             0u,
                             outputValues,
                             bufferInfo.sizeBytes) != GPU_OK) {
    fprintf(stderr, "subgroup matrix output buffer setup failed\n");
    goto cleanup;
  }

  memset(entries, 0, sizeof(entries));
  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[0].buffer.buffer = lhsBuffer;
  entries[0].buffer.size   = lhsElementCount * sizeof(lhsValues[0]);
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = rhsBuffer;
  entries[1].buffer.size   = rhsElementCount * sizeof(rhsValues[0]);
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[2].buffer.buffer = outputBuffer;
  entries[2].buffer.size   = outputElementCount * sizeof(outputValues[0]);
  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 3u;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group
      || GPUAcquireCommandBuffer(queue,
                                 "subgroup-matrix-test",
                                 &cmdb) != GPU_OK
      || !cmdb || !(pass = GPUBeginComputePass(cmdb,
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
  submitList[0]                 = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK
      || GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "subgroup matrix submit failed\n");
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         outputValues,
                         outputElementCount * sizeof(outputValues[0])) != GPU_OK) {
    fprintf(stderr, "subgroup matrix readback failed\n");
    goto cleanup;
  }

  for (uint32_t row = 0u; row < matrixM; row++) {
    for (uint32_t column = 0u; column < matrixN; column++) {
      expected = row < matrixK && row == column ? 1.0f : 0.0f;
      actual   = outputValues[row * matrixN + column];

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
