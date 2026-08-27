#include "test.h"

enum {
  GPU_SUBGROUP_TEST_VALUE_COUNT = 64
};

typedef enum GPUSubgroupRunResult {
  GPU_SUBGROUP_RUN_FAILED      = 0,
  GPU_SUBGROUP_RUN_PASSED      = 1,
  GPU_SUBGROUP_RUN_UNSUPPORTED = 2
} GPUSubgroupRunResult;

static int
gpu_subgroupCreateBuffer(GPUDevice           *device,
                         GPUBufferUsageFlags  usage,
                         GPUBuffer          **outBuffer) {
  GPUBufferCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = GPU_SUBGROUP_TEST_VALUE_COUNT * sizeof(uint32_t);
  info.usage            = usage;
  return GPUCreateBuffer(device, &info, outBuffer) == GPU_OK && *outBuffer;
}

static int
gpu_subgroupValidateRelative(const GPULimits *limits,
                             const uint32_t   input[],
                             const uint32_t   output[]) {
  uint32_t subgroupSize;

  subgroupSize = 0u;
  for (uint32_t i = 0u; i < GPU_SUBGROUP_TEST_VALUE_COUNT; i++) {
    if (output[i] == 0u) {
      uint32_t candidate = i + 1u;

      if (subgroupSize == 0u) {
        subgroupSize = candidate;
      }
      if ((candidate % subgroupSize) != 0u) {
        fprintf(stderr,
                "subgroup relative boundary mismatch at %u, width %u\n",
                i,
                subgroupSize);
        return 0;
      }
    } else if (i + 1u >= GPU_SUBGROUP_TEST_VALUE_COUNT ||
               output[i] != input[i + 1u]) {
      fprintf(stderr,
              "subgroup relative shuffle mismatch at %u: %u\n",
              i,
              output[i]);
      return 0;
    }
  }

  if (!limits || subgroupSize == 0u ||
      subgroupSize < limits->minSubgroupSize ||
      subgroupSize > limits->maxSubgroupSize ||
      (GPU_SUBGROUP_TEST_VALUE_COUNT % subgroupSize) != 0u) {
    fprintf(stderr, "subgroup relative size is invalid: %u\n", subgroupSize);
    return 0;
  }
  return 1;
}

static GPUSubgroupRunResult
gpu_subgroupRun(GPUDevice       *device,
                GPUQueue        *queue,
                const GPULimits *limits,
                const char      *bytecodePath,
                const char      *entryPoint,
                bool             relative) {
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GPUBindGroupEntry            entries[2] = {0};
  GPUCommandBuffer            *submitList[1] = {0};
  uint32_t                     input[GPU_SUBGROUP_TEST_VALUE_COUNT];
  uint32_t                     output[GPU_SUBGROUP_TEST_VALUE_COUNT];
  GPUShaderLibrary            *library      = NULL;
  GPUShaderLayout             *shaderLayout = NULL;
  GPUComputePipeline          *pipeline     = NULL;
  GPUBindGroup                *group        = NULL;
  GPUBuffer                   *inputBuffer  = NULL;
  GPUBuffer                   *outputBuffer = NULL;
  GPUCommandBuffer            *cmdb         = NULL;
  GPUComputePassEncoder       *pass         = NULL;
  GPUFence                    *fence        = NULL;
  void                        *bytecode      = NULL;
  uint64_t                     bytecodeSize  = 0u;
  GPUResult                    libraryResult;
  GPUSubgroupRunResult         result;

  if (!device || !queue || !limits || !bytecodePath || !entryPoint) {
    return GPU_SUBGROUP_RUN_FAILED;
  }
  result   = GPU_SUBGROUP_RUN_FAILED;
  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "subgroup artifact read failed: %s\n", entryPoint);
    goto cleanup;
  }

  libraryResult = GPUCreateShaderLibraryFromUSL(device,
                                                bytecode,
                                                bytecodeSize,
                                                &library);
  if (relative && libraryResult == GPU_ERROR_UNSUPPORTED && !library) {
    result = GPU_SUBGROUP_RUN_UNSUPPORTED;
    goto cleanup;
  }
  if (libraryResult != GPU_OK || !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "subgroup shader setup failed: %s\n", entryPoint);
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = entryPoint;
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline ||
      !gpu_subgroupCreateBuffer(device,
                                GPU_BUFFER_USAGE_STORAGE |
                                  GPU_BUFFER_USAGE_COPY_DST,
                                &inputBuffer) ||
      !gpu_subgroupCreateBuffer(device,
                                GPU_BUFFER_USAGE_STORAGE |
                                  GPU_BUFFER_USAGE_COPY_SRC |
                                  GPU_BUFFER_USAGE_COPY_DST,
                                &outputBuffer)) {
    fprintf(stderr, "subgroup pipeline or buffer setup failed: %s\n",
            entryPoint);
    goto cleanup;
  }

  for (uint32_t i = 0u; i < GPU_SUBGROUP_TEST_VALUE_COUNT; i++) {
    input[i]  = i + 100u;
    output[i] = UINT32_MAX;
  }
  if (GPUQueueWriteBuffer(queue,
                          inputBuffer,
                          0u,
                          input,
                          sizeof(input)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "subgroup buffer upload failed: %s\n", entryPoint);
    goto cleanup;
  }

  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  entries[0].buffer.buffer = inputBuffer;
  entries[0].buffer.size   = sizeof(input);
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = outputBuffer;
  entries[1].buffer.size   = sizeof(output);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue, entryPoint, &cmdb) != GPU_OK || !cmdb ||
      !(pass = GPUBeginComputePass(cmdb, entryPoint))) {
    fprintf(stderr, "subgroup command setup failed: %s\n", entryPoint);
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "subgroup fence creation failed: %s\n", entryPoint);
    goto cleanup;
  }
  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "subgroup submit failed: %s\n", entryPoint);
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK) {
    fprintf(stderr, "subgroup readback failed: %s\n", entryPoint);
    goto cleanup;
  }
  if (relative) {
    if (!gpu_subgroupValidateRelative(limits, input, output)) {
      goto cleanup;
    }
  } else {
    for (uint32_t i = 0u; i < GPU_SUBGROUP_TEST_VALUE_COUNT; i++) {
      if (output[i] != input[i ^ 1u]) {
        fprintf(stderr, "subgroup shuffle mismatch at %u: %u\n", i, output[i]);
        goto cleanup;
      }
    }
  }
  result = GPU_SUBGROUP_RUN_PASSED;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  free(bytecode);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  return result;
}

int
gpu_test_subgroup(GPUAdapter *adapter,
                  const char *bytecodePath,
                  const char *relativeBytecodePath) {
  GPUFeature             feature = GPU_FEATURE_SUBGROUPS;
  GPUDeviceCreateInfo    deviceInfo = {0};
  GPUAdapterCapabilities adapterCaps;
  GPUDeviceCapabilities  deviceCaps;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUSubgroupRunResult   result;
  int                    ok;

  if (!GPUIsFeatureSupported(adapter, feature)) {
    return 1;
  }
  if (!bytecodePath || !relativeBytecodePath ||
      GPUGetAdapterCapabilities(adapter, &adapterCaps) != GPU_OK ||
      adapterCaps.limits.minSubgroupSize == 0u ||
      adapterCaps.limits.maxSubgroupSize <
        adapterCaps.limits.minSubgroupSize) {
    fprintf(stderr, "subgroup adapter capabilities are invalid\n");
    return 0;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  device                           = NULL;
  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK ||
      !device || !GPUIsFeatureEnabled(device, feature) ||
      GPUGetDeviceCapabilities(device, &deviceCaps) != GPU_OK ||
      deviceCaps.limits.minSubgroupSize != adapterCaps.limits.minSubgroupSize) {
    fprintf(stderr, "subgroup device feature enablement failed\n");
    GPUDestroyDevice(device);
    return 0;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  ok = queue &&
       gpu_subgroupRun(device,
                       queue,
                       &deviceCaps.limits,
                       bytecodePath,
                       "subgroup_cs",
                       false) == GPU_SUBGROUP_RUN_PASSED;
  if (ok) {
    result = gpu_subgroupRun(device,
                             queue,
                             &deviceCaps.limits,
                             relativeBytecodePath,
                             "subgroup_relative_cs",
                             true);
    if (result == GPU_SUBGROUP_RUN_UNSUPPORTED) {
      puts("subgroup relative shuffle skipped: unsupported adapter");
    } else {
      ok = result == GPU_SUBGROUP_RUN_PASSED;
    }
  }

  GPUDestroyDevice(device);
  return ok;
}
