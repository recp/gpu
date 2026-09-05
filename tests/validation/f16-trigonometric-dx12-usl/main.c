#include <gpu/gpu.h>

#include "../usl_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  F16_TRIG_CASES            = 2u,
  F16_TRIG_INPUT_ROWS       = F16_TRIG_CASES * 2u,
  F16_TRIG_OUTPUTS_PER_CASE = 12u,
  F16_TRIG_OUTPUT_ROWS      = F16_TRIG_CASES * F16_TRIG_OUTPUTS_PER_CASE
};

static const float kInputs[F16_TRIG_INPUT_ROWS][4] = {
  {0.0f, -0.0f, 0.0f, -0.0f},
  {0.0f, 0.0f, -0.0f, -0.0f},
  {0.5f, -0.5f, 1.0f, 1.5f},
  {0.75f, -0.75f, -1.0f, 2.0f}
};

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
    return NULL;
  }
  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static uint32_t
float_bits(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static int
value_matches(const char *name,
              uint32_t    lane,
              float       actual,
              float       expected) {
  float tolerance;

  if (isnan(expected)) {
    if (isnan(actual)) return 1;
  } else if (actual == expected) {
    if (expected != 0.0f || float_bits(actual) == float_bits(expected))
      return 1;
  } else if (!isnan(actual) && !isinf(actual) && !isinf(expected)) {
    tolerance = 0.0025f + 0.002f * fabsf(expected);
    if (fabsf(actual - expected) <= tolerance) return 1;
  }
  fprintf(stderr,
          "Direct DXIL F16 %s mismatch at lane %u: expected %.9g, got %.9g\n",
          name,
          lane,
          expected,
          actual);
  return 0;
}

static int
validate_results(const float output[F16_TRIG_OUTPUT_ROWS][4]) {
  static const char *names[F16_TRIG_OUTPUTS_PER_CASE] = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "sinh", "cosh", "tanh", "asinh+acosh", "atanh"
  };

  for (uint32_t testCase = 0u; testCase < F16_TRIG_CASES; testCase++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      float a = kInputs[testCase * 2u][lane];
      float b = kInputs[testCase * 2u + 1u][lane];
      float expected[F16_TRIG_OUTPUTS_PER_CASE] = {
        sinf(a),
        cosf(a),
        tanf(a),
        asinf(a),
        acosf(a),
        atanf(a),
        atan2f(a, b),
        sinhf(a),
        coshf(a),
        tanhf(a),
        asinhf(a) + acoshf(a),
        atanhf(a)
      };

      for (uint32_t row = 0u; row < F16_TRIG_OUTPUTS_PER_CASE; row++) {
        if (!value_matches(names[row],
                           lane,
                           output[testCase * F16_TRIG_OUTPUTS_PER_CASE + row]
                                 [lane],
                           expected[row])) {
          return 0;
        }
      }
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUFeature                    feature = GPU_FEATURE_SHADER_F16;
  GPUInstance                  *instance = NULL;
  GPUAdapter                   *adapter = NULL;
  GPUDevice                    *device = NULL;
  GPUQueue                     *queue = NULL;
  GPUShaderLibrary             *library = NULL;
  GPUShaderLayout              *shaderLayout = NULL;
  GPUComputePipeline           *pipeline = NULL;
  GPUBuffer                    *buffers[2] = {0};
  GPUBindGroup                 *bindGroup = NULL;
  GPUCommandBuffer             *cmdb = NULL;
  GPUComputePassEncoder        *pass = NULL;
  GPUFence                     *fence = NULL;
  void                         *artifact = NULL;
  GPUInstanceCreateInfo         instanceInfo = {0};
  GPUDeviceCreateInfo           deviceInfo = {0};
  GPURuntimeConfig              runtimeConfig = {0};
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBindGroupEntry             groupEntries[2] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  GPUQueueSubmitInfo            submitInfo = {0};
  float output[F16_TRIG_CASES * F16_TRIG_OUTPUTS_PER_CASE][4] = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  const char                    *artifactPath;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint64_t                       bufferSizes[2];
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr,
            "usage: gpu-f16-trigonometric-dx12-usl [f16_trigonometric.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f16_trigonometric.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "Direct3D 12 F16 trigonometric artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  result = GPUCreateInstance(&instanceInfo, &instance);
  if (result != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 F16 instance creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter || !GPUIsFeatureSupported(adapter, feature)) {
    fprintf(stderr, "Direct3D 12 F16 adapter is unavailable\n");
    goto cleanup;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  result = GPUCreateDevice(adapter, &deviceInfo, &device);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (result != GPU_OK || !device || !queue ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "Direct3D 12 F16 device creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK) {
    fprintf(stderr, "Direct3D 12 F16 runtime configuration failed\n");
    goto cleanup;
  }
  result = gpu_test_create_shader_library_from_usl(device,
                                                    artifact,
                                                    artifactSize,
                                                    &library);
  if (result != GPU_OK || !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Direct3D 12 F16 shader setup failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 2u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
      layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_STORAGE_BUFFER) {
    fprintf(stderr, "Unexpected Direct3D 12 F16 reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-f16-trigonometric";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f16_trigonometric";
  result = GPUCreateComputePipeline(device, &pipelineInfo, &pipeline);
  if (result != GPU_OK || !pipeline) {
    fprintf(stderr, "Direct3D 12 F16 pipeline creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  bufferSizes[0] = sizeof(kInputs);
  bufferSizes[1] = sizeof(output);
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t binding = 0u; binding < 2u; binding++) {
    bufferInfo.sizeBytes = bufferSizes[binding];
    result = GPUCreateBuffer(device, &bufferInfo, &buffers[binding]);
    if (result != GPU_OK || !buffers[binding] ||
        GPUQueueWriteBuffer(queue,
                            buffers[binding],
                            0u,
                            binding == 0u ? (const void *)kInputs
                                          : (const void *)output,
                            bufferSizes[binding]) != GPU_OK) {
      fprintf(stderr, "Direct3D 12 F16 buffer %u failed (%d)\n",
              binding,
              (int)result);
      goto cleanup;
    }
    groupEntries[binding].binding       = binding;
    groupEntries[binding].bindingType   = binding == 0u
                                            ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                            : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = bufferSizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-f16-trigonometric-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  result = GPUCreateBindGroup(device, &groupInfo, &bindGroup);
  if (result != GPU_OK || !bindGroup ||
      GPUAcquireCommandBuffer(queue,
                              "dx12-native-f16-trigonometric",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 F16 bind/command failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f16-trigonometric");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 F16 compute pass failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, F16_TRIG_CASES, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  result = GPUCreateFence(device, NULL, &fence);
  if (result != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 F16 fence creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[1],
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !validate_results(output)) {
    fprintf(stderr, "Direct3D 12 F16 readback validation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(buffers[0]);
  GPUDestroyBuffer(buffers[1]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  if (!ok) return 1;
  puts("Direct3D 12 native F16 trigonometric validation passed");
  return 0;
}
