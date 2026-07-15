#include "test.h"

enum {
  GPU_SHADER_F16_TEST_VALUE_COUNT = 4
};

static int
gpu_shaderF16CreateBuffer(GPUDevice          *device,
                          GPUBufferUsageFlags usage,
                          GPUBuffer         **outBuffer) {
  GPUBufferCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = GPU_SHADER_F16_TEST_VALUE_COUNT * sizeof(uint32_t);
  info.usage            = usage;
  return GPUCreateBuffer(device, &info, outBuffer) == GPU_OK && *outBuffer;
}

int
gpu_test_shader_f16(GPUAdapter *adapter, const char *bytecodePath) {
  GPUFeature                    feature       = GPU_FEATURE_SHADER_F16;
  GPUDeviceCreateInfo           deviceInfo    = {0};
  GPUComputePipelineCreateInfo  pipelineInfo  = {0};
  GPUBindGroupCreateInfo        groupInfo     = {0};
  GPUQueueSubmitInfo            submitInfo    = {0};
  GPUBindGroupEntry             entries[2]    = {0};
  GPUCommandBuffer             *submitList[1] = {0};
  uint32_t                      input[GPU_SHADER_F16_TEST_VALUE_COUNT];
  uint32_t                      output[GPU_SHADER_F16_TEST_VALUE_COUNT];
  GPUDevice                    *disabledDevice  = NULL;
  GPUShaderLibrary             *disabledLibrary = NULL;
  GPUDevice                    *device       = NULL;
  GPUQueue                     *queue        = NULL;
  GPUShaderLibrary             *library      = NULL;
  GPUShaderLayout              *shaderLayout = NULL;
  GPUComputePipeline           *pipeline     = NULL;
  GPUBindGroup                 *group        = NULL;
  GPUBuffer                    *inputBuffer  = NULL;
  GPUBuffer                    *outputBuffer = NULL;
  GPUCommandBuffer             *cmdb         = NULL;
  GPUComputePassEncoder        *pass         = NULL;
  GPUFence                     *fence        = NULL;
  void                         *bytecode      = NULL;
  uint64_t                      bytecodeSize  = 0u;
  int                           ok            = 0;

  if (!GPUIsFeatureSupported(adapter, feature)) {
    return 1;
  }
  if (!bytecodePath) {
    fprintf(stderr, "shader f16 artifact path is missing\n");
    return 0;
  }
  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode) {
    fprintf(stderr, "shader f16 artifact load failed\n");
    goto cleanup;
  }

  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  if (GPUCreateDevice(adapter, &deviceInfo, &disabledDevice) != GPU_OK ||
      !disabledDevice || GPUIsFeatureEnabled(disabledDevice, feature) ||
      GPUCreateShaderLibraryFromUSL(disabledDevice,
                                    bytecode,
                                    bytecodeSize,
                                    &disabledLibrary) == GPU_OK ||
      disabledLibrary) {
    fprintf(stderr, "shader f16 was accepted without feature enablement\n");
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "shader f16 device feature enablement failed\n");
    goto cleanup;
  }

  queue    = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "shader f16 setup failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "shader_f16_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline ||
      !gpu_shaderF16CreateBuffer(device,
                                 GPU_BUFFER_USAGE_STORAGE |
                                   GPU_BUFFER_USAGE_COPY_DST,
                                 &inputBuffer) ||
      !gpu_shaderF16CreateBuffer(device,
                                 GPU_BUFFER_USAGE_STORAGE |
                                   GPU_BUFFER_USAGE_COPY_SRC |
                                   GPU_BUFFER_USAGE_COPY_DST,
                                 &outputBuffer)) {
    fprintf(stderr, "shader f16 pipeline or buffer setup failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < GPU_SHADER_F16_TEST_VALUE_COUNT; i++) {
    input[i]  = (i + 1u) * 2u;
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
    fprintf(stderr, "shader f16 buffer upload failed\n");
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
  groupInfo.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize  = sizeof(groupInfo);
  groupInfo.layout            = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount        = 2u;
  groupInfo.pEntries          = entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue, "shader-f16-test", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "shader-f16-test"))) {
    fprintf(stderr, "shader f16 command setup failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "shader f16 fence create failed\n");
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
    fprintf(stderr, "shader f16 submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK) {
    fprintf(stderr, "shader f16 readback failed\n");
    goto cleanup;
  }
  for (uint32_t i = 0u; i < GPU_SHADER_F16_TEST_VALUE_COUNT; i++) {
    if (output[i] != i + 1u) {
      fprintf(stderr, "shader f16 mismatch at %u: %u\n", i, output[i]);
      goto cleanup;
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
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}
