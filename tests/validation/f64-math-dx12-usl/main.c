#include <gpu/gpu.h>

#include "../usl_test.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  F64_MATH_INPUT_ROWS  = 21u,
  F64_MATH_OUTPUT_ROWS = 42u
};

static const double kInputs[F64_MATH_INPUT_ROWS][4] = {
  {-2.75, -2.5, 2.5, 2.75},
  {-0.0, 0.0, -0.25, 0.25},
  {-INFINITY, INFINITY, NAN, 4503599627370496.0},
  {-2.0, -2.0, -0.0, 0.0},
  {3.0, 0.5, -3.0, -2.0},
  {1.0000000000000002, 0.99999999999999989, DBL_TRUE_MIN, 2.0},
  {1099511627776.0, -1099511627776.0, 0.5, -1075.0},
  {0.25, 1647099.3291652855, 0x1.21ad991898af8p+132, DBL_MAX},
  {-1.0, -0.75, -0.0, 0.5},
  {1.0, 0.99999999999999989, 1.0000000000000002, NAN},
  {-INFINITY, -2.4375, -0.0, DBL_MAX},
  {0.0, -0.0, INFINITY, -INFINITY},
  {-1.0, -1.0, INFINITY, -INFINITY},
  {1.0, -1.0, DBL_MIN, DBL_MAX},
  {1.0, -1.0, -DBL_MAX, DBL_MIN},
  {-INFINITY, -0.0, 0x1p-28, DBL_MAX},
  {0.5, 1.0, 1.0000000000000002, DBL_MAX},
  {-1.0000000000000002, -1.0, -0.0, 0.99999999999999989},
  {-2.0, -0.5, 0.5, 2.0},
  {1.5, 2.0, 3.0, 268435456.0},
  {-0.75, -0.25, 0.25, 0.75}
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
positive_ulp_distance(double left, double right) {
  uint64_t leftBits, rightBits;

  memcpy(&leftBits, &left, sizeof(leftBits));
  memcpy(&rightBits, &right, sizeof(rightBits));
  return leftBits > rightBits ? leftBits - rightBits : rightBits - leftBits;
}

static int
exact_rows_match(const char   *name,
                 const double  actual[3][4],
                 const double  expected[3][4]) {
  for (uint32_t row = 0u; row < 3u; row++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      uint64_t actualBits, expectedBits;

      if (isnan(expected[row][lane])) {
        if (isnan(actual[row][lane])) continue;
      } else if (memcmp(&actual[row][lane],
                        &expected[row][lane],
                        sizeof(actual[row][lane])) == 0) {
        continue;
      }
      memcpy(&actualBits, &actual[row][lane], sizeof(actualBits));
      memcpy(&expectedBits, &expected[row][lane], sizeof(expectedBits));
      fprintf(stderr,
              "F64 %s mismatch at row %u lane %u: expected 0x%016llx, "
              "got 0x%016llx\n",
              name,
              row,
              lane,
              (unsigned long long)expectedBits,
              (unsigned long long)actualBits);
      return 0;
    }
  }
  return 1;
}

static int
ulp_value_matches(const char *name,
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
  } else if (!isnan(actual)) {
    ulp = positive_ulp_distance(actual, expected);
    if (ulp <= limit) return 1;
  }
  ulp = positive_ulp_distance(actual, expected);
  fprintf(stderr,
          "F64 %s mismatch at row %u lane %u: expected %.17g, got %.17g "
          "(%llu ULP, limit %llu)\n",
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
validate_results(const double output[F64_MATH_OUTPUT_ROWS][4]) {
  static const double floorExpected[3][4] = {
    {-3.0, -3.0, 2.0, 2.0},
    {-0.0, 0.0, -1.0, 0.0},
    {-INFINITY, INFINITY, NAN, 4503599627370496.0}
  };
  static const double ceilExpected[3][4] = {
    {-2.0, -2.0, 3.0, 3.0},
    {-0.0, 0.0, -0.0, 1.0},
    {-INFINITY, INFINITY, NAN, 4503599627370496.0}
  };
  static const double truncExpected[3][4] = {
    {-2.0, -2.0, 2.0, 2.0},
    {-0.0, 0.0, -0.0, 0.0},
    {-INFINITY, INFINITY, NAN, 4503599627370496.0}
  };
  static const double roundExpected[3][4] = {
    {-3.0, -2.0, 2.0, 3.0},
    {-0.0, 0.0, -0.0, 0.0},
    {-INFINITY, INFINITY, NAN, 4503599627370496.0}
  };
  static const double fractExpected[3][4] = {
    {0.25, 0.5, 0.5, 0.75},
    {0.0, 0.0, 0.75, 0.25},
    {NAN, NAN, NAN, 0.0}
  };
  double trigExpected[2][4];

  if (!exact_rows_match("floor", &output[0], floorExpected) ||
      !exact_rows_match("ceil", &output[3], ceilExpected) ||
      !exact_rows_match("trunc", &output[6], truncExpected) ||
      !exact_rows_match("round", &output[9], roundExpected) ||
      !exact_rows_match("fract", &output[12], fractExpected)) {
    return 0;
  }
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (!ulp_value_matches("exp2", 0u, lane, output[15][lane],
                           exp2(kInputs[0][lane]), 1u) ||
        !ulp_value_matches("exp", 0u, lane, output[16][lane],
                           exp(kInputs[0][lane]), 1u) ||
        !ulp_value_matches("log", 0u, lane, output[17][lane],
                           log(kInputs[1][lane]), 1u) ||
        !ulp_value_matches("log2", 0u, lane, output[18][lane],
                           log2(kInputs[1][lane]), 1u) ||
        !ulp_value_matches("sinh", 0u, lane, output[19][lane],
                           sinh(kInputs[0][lane]), 2u) ||
        !ulp_value_matches("cosh", 0u, lane, output[20][lane],
                           cosh(kInputs[0][lane]), 2u) ||
        !ulp_value_matches("tanh", 0u, lane, output[21][lane],
                           tanh(kInputs[0][lane]), 2u) ||
        !ulp_value_matches("pow", 0u, lane, output[22][lane],
                           pow(kInputs[3][lane], kInputs[4][lane]), 1u) ||
        !ulp_value_matches("pow", 1u, lane, output[23][lane],
                           pow(kInputs[5][lane], kInputs[6][lane]), 1u)) {
      return 0;
    }
  }
  trigExpected[0][0] = sin(kInputs[7][0]);
  trigExpected[0][1] = cos(kInputs[7][1]);
  trigExpected[0][2] = sin(kInputs[7][2]);
  trigExpected[0][3] = cos(kInputs[7][3]);
  trigExpected[1][0] = tan(kInputs[7][0]);
  trigExpected[1][1] = tan(kInputs[7][2]);
  trigExpected[1][2] = sin(kInputs[7][3]);
  trigExpected[1][3] = cos(kInputs[7][3]);
  for (uint32_t row = 0u; row < 2u; row++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      uint64_t limit = row == 1u && lane < 2u ? 5u : 3u;

      if (!ulp_value_matches("trig",
                             row,
                             lane,
                             output[24u + row][lane],
                             trigExpected[row][lane],
                             limit)) {
        return 0;
      }
    }
  }
  for (uint32_t lane = 0u; lane < 2u; lane++) {
    if (!ulp_value_matches("sin-vector2",
                           0u,
                           lane,
                           output[26][lane],
                           sin(kInputs[7][lane]),
                           3u)) {
      return 0;
    }
  }
  if (output[26][2] != 0.0 || output[26][3] != 0.0) {
    fprintf(stderr, "F64 sin-vector2 padding mismatch\n");
    return 0;
  }
  for (uint32_t lane = 0u; lane < 3u; lane++) {
    if (!ulp_value_matches("cos-vector3",
                           0u,
                           lane,
                           output[27][lane],
                           cos(kInputs[7][lane]),
                           3u)) {
      return 0;
    }
  }
  if (output[27][3] != 0.0) {
    fprintf(stderr, "F64 cos-vector3 padding mismatch\n");
    return 0;
  }
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (!ulp_value_matches("tan-vector4",
                           0u,
                           lane,
                           output[28][lane],
                           tan(kInputs[7][lane]),
                           5u)) {
      return 0;
    }
  }
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (!ulp_value_matches("asin",
                           0u,
                           lane,
                           output[29][lane],
                           asin(kInputs[8][lane]),
                           3u) ||
        !ulp_value_matches("acos",
                           0u,
                           lane,
                           output[30][lane],
                           acos(kInputs[8][lane]),
                           3u) ||
        !ulp_value_matches("asin-special",
                           1u,
                           lane,
                           output[31][lane],
                           asin(kInputs[9][lane]),
                           3u) ||
        !ulp_value_matches("acos-special",
                           1u,
                           lane,
                           output[32][lane],
                           acos(kInputs[9][lane]),
                           3u) ||
        !ulp_value_matches("atan",
                           0u,
                           lane,
                           output[33][lane],
                           atan(kInputs[10][lane]),
                           2u) ||
        !ulp_value_matches("atan2-special",
                           0u,
                           lane,
                           output[34][lane],
                           atan2(kInputs[11][lane], kInputs[12][lane]),
                           2u) ||
        !ulp_value_matches("atan2",
                           1u,
                           lane,
                           output[35][lane],
                           atan2(kInputs[13][lane], kInputs[14][lane]),
                           2u)) {
      return 0;
    }
  }
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (!ulp_value_matches("asinh-special",
                           0u,
                           lane,
                           output[36][lane],
                           asinh(kInputs[15][lane]),
                           4u) ||
        !ulp_value_matches("asinh",
                           1u,
                           lane,
                           output[37][lane],
                           asinh(kInputs[18][lane]),
                           4u) ||
        !ulp_value_matches("acosh-special",
                           0u,
                           lane,
                           output[38][lane],
                           acosh(kInputs[16][lane]),
                           4u) ||
        !ulp_value_matches("acosh",
                           1u,
                           lane,
                           output[39][lane],
                           acosh(kInputs[19][lane]),
                           4u) ||
        !ulp_value_matches("atanh-special",
                           0u,
                           lane,
                           output[40][lane],
                           atanh(kInputs[17][lane]),
                           4u) ||
        !ulp_value_matches("atanh",
                           1u,
                           lane,
                           output[41][lane],
                           atanh(kInputs[20][lane]),
                           4u)) {
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
  double                       output[F64_MATH_OUTPUT_ROWS][4] = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint64_t                       bufferSizes[2];
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-f64-math-dx12-usl [artifact.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f64_math.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "F64 math USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  result = GPUCreateInstance(&instanceInfo, &instance);
  if (result != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 F64 math instance creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "Direct3D 12 F64 math adapter enumeration failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "Direct3D 12 F64 math device creation failed\n");
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  result = GPUConfigureRuntime(device, &runtimeConfig);
  if (result != GPU_OK) {
    fprintf(stderr, "Direct3D 12 F64 math configuration failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  result = gpu_test_create_shader_library_from_usl(device,
                                                    artifact,
                                                    artifactSize,
                                                    &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "Direct3D 12 F64 math library creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }
  result = GPUCreateShaderLayout(device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Direct3D 12 F64 math layout creation failed (%d)\n",
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
    fprintf(stderr, "Unexpected F64 math reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-f64-math";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f64_math";
  result = GPUCreateComputePipeline(device, &pipelineInfo, &pipeline);
  if (result != GPU_OK || !pipeline) {
    fprintf(stderr, "Direct3D 12 F64 math pipeline creation failed (%d)\n",
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
      fprintf(stderr, "Direct3D 12 F64 math buffer %u creation failed (%d)\n",
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
  groupInfo.label            = "dx12-native-f64-math-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  result = GPUCreateBindGroup(device, &groupInfo, &bindGroup);
  if (result != GPU_OK || !bindGroup ||
      GPUAcquireCommandBuffer(queue, "dx12-native-f64-math", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 F64 math bind/command creation failed (%d)\n",
            (int)result);
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f64-math");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 F64 math compute pass creation failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  result = GPUCreateFence(device, NULL, &fence);
  if (result != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 F64 math fence creation failed (%d)\n",
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
    fprintf(stderr, "Direct3D 12 F64 math readback validation failed\n");
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
  puts("Direct3D 12 native DXIL F64 math validation passed");
  return 0;
}
