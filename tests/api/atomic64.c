#include "test.h"

int
gpu_test_atomic64(GPUAdapter *adapter, const char *bytecodePath) {
  GPUFeature                    feature       = GPU_FEATURE_ATOMIC64;
  GPUDeviceCreateInfo           deviceInfo    = {0};
  GPUComputePipelineCreateInfo  pipelineInfo  = {0};
  GPUBindGroupCreateInfo        groupInfo     = {0};
  GPUQueueSubmitInfo            submitInfo    = {0};
  GPUBindGroupEntry             entries[2]    = {0};
  GPUCommandBuffer             *submitList[1] = {0};
  uint64_t                      unsignedValues[2] = {10u, 0u};
  int64_t                       signedValues[2]   = {-10, 0};
  GPUDevice                    *disabledDevice  = NULL;
  GPUShaderLibrary             *disabledLibrary = NULL;
  GPUDevice                    *device       = NULL;
  GPUQueue                     *queue        = NULL;
  GPUShaderLibrary             *library      = NULL;
  GPUShaderLayout              *shaderLayout = NULL;
  GPUComputePipeline           *pipeline     = NULL;
  GPUBindGroup                 *group        = NULL;
  GPUBuffer                    *unsignedBuffer = NULL;
  GPUBuffer                    *signedBuffer   = NULL;
  GPUCommandBuffer             *cmdb         = NULL;
  GPUComputePassEncoder        *pass         = NULL;
  GPUFence                     *fence        = NULL;
  void                         *bytecode      = NULL;
  uint64_t                      bytecodeSize  = 0u;
  GPUResult                     result;
  int                           supported;
  int                           ok            = 0;

  if (!adapter || !bytecodePath) {
    return 0;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  supported = GPUIsFeatureSupported(adapter, feature);
  if (!supported) {
    result = GPUCreateDevice(adapter, &deviceInfo, &device);
    GPUDestroyDevice(device);
    return result == GPU_ERROR_UNSUPPORTED && !device;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "atomic64 artifact load failed\n");
    goto cleanup;
  }

  deviceInfo.required.pFeatures    = NULL;
  deviceInfo.required.featureCount = 0u;
  if (GPUCreateDevice(adapter, &deviceInfo, &disabledDevice) != GPU_OK ||
      !disabledDevice || GPUIsFeatureEnabled(disabledDevice, feature) ||
      GPUCreateShaderLibraryFromUSL(disabledDevice,
                                    bytecode,
                                    bytecodeSize,
                                    &disabledLibrary) == GPU_OK ||
      disabledLibrary) {
    fprintf(stderr, "atomic64 was accepted without feature enablement\n");
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "atomic64 device feature enablement failed\n");
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
    fprintf(stderr, "atomic64 shader setup failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "atomic64_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "atomic64 pipeline creation failed\n");
    goto cleanup;
  }

  {
    GPUBufferCreateInfo bufferInfo = {0};

    bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.chain.structSize = sizeof(bufferInfo);
    bufferInfo.sizeBytes        = sizeof(unsignedValues);
    bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                  GPU_BUFFER_USAGE_COPY_SRC |
                                  GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device,
                        &bufferInfo,
                        &unsignedBuffer) != GPU_OK ||
        !unsignedBuffer ||
        GPUQueueWriteBuffer(queue,
                            unsignedBuffer,
                            0u,
                            unsignedValues,
                            sizeof(unsignedValues)) != GPU_OK ||
        GPUCreateBuffer(device, &bufferInfo, &signedBuffer) != GPU_OK ||
        !signedBuffer ||
        GPUQueueWriteBuffer(queue,
                            signedBuffer,
                            0u,
                            signedValues,
                            sizeof(signedValues)) != GPU_OK) {
      fprintf(stderr, "atomic64 buffer setup failed\n");
      goto cleanup;
    }
  }

  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[0].buffer.buffer = unsignedBuffer;
  entries[0].buffer.size   = sizeof(unsignedValues);
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = signedBuffer;
  entries[1].buffer.size   = sizeof(signedValues);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 2u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue, "atomic64-test", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "atomic64-test"))) {
    fprintf(stderr, "atomic64 command setup failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "atomic64 fence creation failed\n");
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
    fprintf(stderr, "atomic64 submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         unsignedBuffer,
                         0u,
                         unsignedValues,
                         sizeof(unsignedValues)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         signedBuffer,
                         0u,
                         signedValues,
                         sizeof(signedValues)) != GPU_OK ||
      unsignedValues[0] != 3u || unsignedValues[1] != 23u ||
      signedValues[0] != -3 || signedValues[1] != -31) {
    fprintf(stderr,
            "atomic64 result mismatch: %llu, %llu; %lld, %lld\n",
            (unsigned long long)unsignedValues[0],
            (unsigned long long)unsignedValues[1],
            (long long)signedValues[0],
            (long long)signedValues[1]);
    goto cleanup;
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
  GPUDestroyBuffer(signedBuffer);
  GPUDestroyBuffer(unsignedBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}
