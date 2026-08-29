#include <gpu/gpu.h>

#include "../usl_test.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  F64_EXPONENTIAL_INPUT_ROWS  = 10u,
  F64_EXPONENTIAL_OUTPUT_ROWS = 12u
};

static const double kInputs[F64_EXPONENTIAL_INPUT_ROWS][4] = {
  {-2.75, -2.5, 2.5, 2.75},
  {-0.0, 0.0, -0.25, 0.25},
  {DBL_TRUE_MIN, DBL_MIN, 1.0, DBL_MAX},
  {-2.0, -2.0, -0.0, 0.0},
  {3.0, 0.5, -2.0, 4.0},
  {1.0000000000000002, 0.99999999999999989, DBL_TRUE_MIN, 2.0},
  {1099511627776.0, -1099511627776.0, 0.5, -1075.0},
  {1024.0, -1075.0, INFINITY, NAN},
  {-1074.0, -1073.5, 1023.0, 1023.5},
  {-745.0, -744.0, 709.0, 709.7}
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

static uint64_t
double_bits(double value) {
  uint64_t bits;

  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static uint64_t
ordered_double_bits(double value) {
  uint64_t bits = double_bits(value);

  return (bits & UINT64_C(0x8000000000000000)) != 0u
           ? ~bits
           : bits | UINT64_C(0x8000000000000000);
}

static uint64_t
ulp_distance(double left, double right) {
  uint64_t leftBits  = ordered_double_bits(left);
  uint64_t rightBits = ordered_double_bits(right);

  return leftBits > rightBits ? leftBits - rightBits : rightBits - leftBits;
}

static int
value_matches(const char *name,
              uint32_t    row,
              uint32_t    lane,
              double      actual,
              double      expected,
              uint64_t    limit) {
  uint64_t ulp;

  if (isnan(expected)) {
    if (isnan(actual)) return 1;
  } else if (actual == expected) {
    if (expected != 0.0 || double_bits(actual) == double_bits(expected))
      return 1;
  } else if (!isnan(actual) && !isinf(expected) && !isinf(actual)) {
    ulp = ulp_distance(actual, expected);
    if (ulp <= limit) return 1;
  }
  ulp = isnan(actual) || isnan(expected) ? UINT64_MAX
                                         : ulp_distance(actual, expected);
  fprintf(stderr,
          "HLSL F64 %s mismatch at row %u lane %u: expected %.17g, "
          "got %.17g (%llu ULP, limit %llu)\n",
          name,
          row,
          lane,
          expected,
          actual,
          (unsigned long long)ulp,
          (unsigned long long)limit);
  return 0;
}

static int
validate_results(const double output[F64_EXPONENTIAL_OUTPUT_ROWS][4]) {
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (!value_matches("exp2", 0u, lane, output[0][lane],
                       exp2(kInputs[0][lane]), 1u) ||
        !value_matches("exp", 0u, lane, output[1][lane],
                       exp(kInputs[0][lane]), 1u) ||
        !value_matches("log", 0u, lane, output[2][lane],
                       log(kInputs[1][lane]), 1u) ||
        !value_matches("log2", 0u, lane, output[3][lane],
                       log2(kInputs[1][lane]), 1u) ||
        !value_matches("log", 1u, lane, output[4][lane],
                       log(kInputs[2][lane]), 1u) ||
        !value_matches("log2", 1u, lane, output[5][lane],
                       log2(kInputs[2][lane]), 1u) ||
        !value_matches("pow", 0u, lane, output[6][lane],
                       pow(kInputs[3][lane], kInputs[4][lane]), 1u) ||
        !value_matches("pow", 1u, lane, output[7][lane],
                       pow(kInputs[5][lane], kInputs[6][lane]), 1u) ||
        !value_matches("exp2-special", 0u, lane, output[8][lane],
                       exp2(kInputs[7][lane]), 1u) ||
        !value_matches("exp-special", 0u, lane, output[9][lane],
                       exp(kInputs[7][lane]), 1u) ||
        !value_matches("exp2-boundary", 0u, lane, output[10][lane],
                       exp2(kInputs[8][lane]), 1u) ||
        !value_matches("exp-boundary", 0u, lane, output[11][lane],
                       exp(kInputs[9][lane]), 1u)) {
      return 0;
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance = NULL;
  GPUAdapter            *adapter = NULL;
  GPUDevice             *device = NULL;
  GPUQueue              *queue = NULL;
  GPUShaderLibrary      *library = NULL;
  GPUShaderLayout       *shaderLayout = NULL;
  GPUComputePipeline    *pipeline = NULL;
  GPUBuffer             *buffers[2] = {0};
  GPUBindGroup          *bindGroup = NULL;
  GPUCommandBuffer      *cmdb = NULL;
  GPUComputePassEncoder *pass = NULL;
  GPUFence              *fence = NULL;
  void                  *artifact = NULL;
  const char            *artifactPath;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPURuntimeConfig             runtimeConfig = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntries[2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  double output[F64_EXPONENTIAL_OUTPUT_ROWS][4] = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint64_t                       bufferSizes[2];
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr,
            "usage: gpu-f64-exponential-dx12-usl [f64_exponential.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f64_exponential.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "HLSL F64 exponential USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  result = GPUCreateInstance(&instanceInfo, &instance);
  if (result != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 HLSL F64 instance creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "Direct3D 12 HLSL F64 adapter enumeration failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "Direct3D 12 HLSL F64 device creation failed\n");
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  result = GPUConfigureRuntime(device, &runtimeConfig);
  if (result != GPU_OK) {
    fprintf(stderr, "Direct3D 12 HLSL F64 configuration failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  result = gpu_test_create_shader_library_from_usl(device,
                                                    artifact,
                                                    artifactSize,
                                                    &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "Direct3D 12 HLSL F64 library creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  result = GPUCreateShaderLayout(device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Direct3D 12 HLSL F64 layout creation failed (%d)\n",
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
    fprintf(stderr, "Unexpected HLSL F64 exponential reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-hlsl-f64-exponential";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f64_exponential";
  result = GPUCreateComputePipeline(device, &pipelineInfo, &pipeline);
  if (result != GPU_OK || !pipeline) {
    fprintf(stderr, "Direct3D 12 HLSL F64 pipeline creation failed (%d)\n",
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
      fprintf(stderr, "Direct3D 12 HLSL F64 buffer %u failed (%d)\n",
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
  groupInfo.label            = "dx12-hlsl-f64-exponential-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  result = GPUCreateBindGroup(device, &groupInfo, &bindGroup);
  if (result != GPU_OK || !bindGroup ||
      GPUAcquireCommandBuffer(queue,
                              "dx12-hlsl-f64-exponential",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 HLSL F64 bind/command failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f64-exponential");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 HLSL F64 compute pass failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  result = GPUCreateFence(device, NULL, &fence);
  if (result != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 HLSL F64 fence creation failed (%d)\n",
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
    fprintf(stderr, "Direct3D 12 HLSL F64 readback validation failed\n");
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
  puts("Direct3D 12 HLSL F64 exponential validation passed");
  return 0;
}
