#include "test.h"

typedef enum GPUClockDerivativeTestKind {
  GPU_CLOCK_DERIVATIVE_TEST_CLOCK,
  GPU_CLOCK_DERIVATIVE_TEST_QUADS,
  GPU_CLOCK_DERIVATIVE_TEST_LINEAR
} GPUClockDerivativeTestKind;

typedef struct GPUClockDerivativeTestCase {
  const char                 *path;
  const char                 *entryPoint;
  const char                 *label;
  GPUFeature                 feature;
  GPUClockDerivativeTestKind kind;
} GPUClockDerivativeTestCase;

typedef union GPUClockDerivativeOutput {
  uint64_t clock[2];
  float    derivative[4];
} GPUClockDerivativeOutput;

static int
gpu_test_clockDerivativeCase(GPUAdapter                       *adapter,
                             const GPUClockDerivativeTestCase *test) {
  GPUDeviceCreateInfo          deviceInfo    = {0};
  GPUComputePipelineCreateInfo pipelineInfo  = {0};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUBindGroupCreateInfo       groupInfo     = {0};
  GPUQueueSubmitInfo           submitInfo    = {0};
  GPUBindGroupEntry            entry         = {0};
  GPUCommandBuffer            *submitList[1] = {0};
  GPUClockDerivativeOutput     output;
  GPUDevice                   *disabledDevice = NULL;
  GPUDevice                   *device         = NULL;
  GPUQueue                    *queue          = NULL;
  GPUShaderLibrary            *disabledLibrary = NULL;
  GPUShaderLibrary            *library        = NULL;
  GPUShaderLayout             *shaderLayout   = NULL;
  GPUComputePipeline          *pipeline       = NULL;
  GPUBuffer                   *buffer         = NULL;
  GPUBindGroup                *group          = NULL;
  GPUCommandBuffer            *cmdb           = NULL;
  GPUComputePassEncoder       *pass           = NULL;
  GPUFence                    *fence          = NULL;
  void                        *bytecode       = NULL;
  uint64_t                     bytecodeSize   = 0u;
  int                          ok             = 0;

  if (!GPUIsFeatureSupported(adapter, test->feature)) {
    printf("%s execution skipped: unsupported adapter\n", test->label);
    return 1;
  }
  if (!test->path ||
      !(bytecode = gpu_test_read_file(test->path, &bytecodeSize))) {
    fprintf(stderr, "%s fixture is unavailable\n", test->label);
    goto cleanup;
  }

  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  if (GPUCreateDevice(adapter, &deviceInfo, &disabledDevice) != GPU_OK ||
      !disabledDevice) {
    fprintf(stderr, "%s disabled-device setup failed\n", test->label);
    goto cleanup;
  }
  if (GPUCreateShaderLibraryFromUSL(disabledDevice,
                                    bytecode,
                                    bytecodeSize,
                                    &disabledLibrary) == GPU_OK ||
      disabledLibrary) {
    fprintf(stderr, "%s was accepted without feature enablement\n",
            test->label);
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.pFeatures    = &test->feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, test->feature)) {
    fprintf(stderr, "%s device feature enablement failed\n", test->label);
    goto cleanup;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "%s shader setup failed\n", test->label);
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = test->entryPoint;
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "%s pipeline creation failed\n", test->label);
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(output);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "%s output buffer creation failed\n", test->label);
    goto cleanup;
  }

  memset(&output, 0xff, sizeof(output));
  if (test->kind != GPU_CLOCK_DERIVATIVE_TEST_CLOCK) {
    for (uint32_t i = 0u; i < GPU_ARRAY_LEN(output.derivative); i++) {
      output.derivative[i] = -12345.0f;
    }
  }
  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          &output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "%s output initialization failed\n", test->label);
    goto cleanup;
  }

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entry.buffer.buffer = buffer;
  entry.buffer.size   = sizeof(output);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue, test->label, &cmdb) != GPU_OK || !cmdb ||
      !(pass = GPUBeginComputePass(cmdb, test->label))) {
    fprintf(stderr, "%s command setup failed\n", test->label);
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "%s fence creation failed\n", test->label);
    goto cleanup;
  }
  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "%s submit failed\n", test->label);
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         &output,
                         sizeof(output)) != GPU_OK) {
    fprintf(stderr, "%s readback failed\n", test->label);
    goto cleanup;
  }
  if (test->kind == GPU_CLOCK_DERIVATIVE_TEST_CLOCK) {
    if (output.clock[0] == UINT64_MAX || output.clock[1] != 123456789u) {
      fprintf(stderr,
              "%s output mismatch: %llu, %llu\n",
              test->label,
              (unsigned long long)output.clock[0],
              (unsigned long long)output.clock[1]);
      goto cleanup;
    }
  } else {
    float expected = test->kind == GPU_CLOCK_DERIVATIVE_TEST_QUADS
                   ? 2.0f
                   : 3.0f;

    for (uint32_t i = 0u; i < GPU_ARRAY_LEN(output.derivative); i++) {
      if (output.derivative[i] != expected) {
        fprintf(stderr,
                "%s output mismatch at %u: %.9g, expected %.9g\n",
                test->label,
                i,
                output.derivative[i],
                expected);
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
  GPUDestroyShaderLibrary(disabledLibrary);
  GPUDestroyDevice(disabledDevice);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(buffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}

int
gpu_test_clock_derivatives(GPUAdapter *adapter,
                           const char *subgroupClockPath,
                           const char *deviceClockPath,
                           const char *derivativeQuadsPath,
                           const char *derivativeLinearPath) {
  GPUClockDerivativeTestCase tests[] = {
    {
      subgroupClockPath,
      "shader_subgroup_clock_cs",
      "shader-subgroup-clock",
      GPU_FEATURE_SHADER_SUBGROUP_CLOCK,
      GPU_CLOCK_DERIVATIVE_TEST_CLOCK
    },
    {
      deviceClockPath,
      "shader_device_clock_cs",
      "shader-device-clock",
      GPU_FEATURE_SHADER_DEVICE_CLOCK,
      GPU_CLOCK_DERIVATIVE_TEST_CLOCK
    },
    {
      derivativeQuadsPath,
      "compute_derivatives_quads_cs",
      "compute-derivatives-quads",
      GPU_FEATURE_COMPUTE_DERIVATIVES_QUADS,
      GPU_CLOCK_DERIVATIVE_TEST_QUADS
    },
    {
      derivativeLinearPath,
      "compute_derivatives_linear_cs",
      "compute-derivatives-linear",
      GPU_FEATURE_COMPUTE_DERIVATIVES_LINEAR,
      GPU_CLOCK_DERIVATIVE_TEST_LINEAR
    }
  };

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(tests); i++) {
    if (!gpu_test_clockDerivativeCase(adapter, &tests[i])) {
      return 0;
    }
  }
  return 1;
}
