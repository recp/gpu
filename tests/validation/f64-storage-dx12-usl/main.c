#include <gpu/gpu.h>

#include "../usl_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Double3Storage {
  double lane[4];
} Double3Storage;

typedef struct Double3x3Storage {
  double columns[3][4];
} Double3x3Storage;

typedef struct Double2x2Storage {
  double columns[2][2];
} Double2x2Storage;

typedef struct Double2x3Storage {
  double columns[3][2];
} Double2x3Storage;

typedef struct Double3x2Storage {
  double columns[2][4];
} Double3x2Storage;

typedef struct Double4x4Storage {
  double columns[4][4];
} Double4x4Storage;

static const Double3Storage kValues[2] = {
  {{1.0, 2.0, 3.0, 0.0}},
  {{-4.0, 5.5, 6.25, 0.0}}
};

static const Double3x3Storage kMatrix = {{
  {1.0, 0.0, 5.0, 0.0},
  {2.0, 1.0, 6.0, 0.0},
  {3.0, 4.0, 0.0, 0.0}
}};

static const Double3x3Storage kMatrixTranspose = {{
  {1.0, 2.0, 3.0, 0.0},
  {0.0, 1.0, 4.0, 0.0},
  {5.0, 6.0, 0.0, 0.0}
}};

static const Double3x3Storage kMatrixRight = {{
  {0.5, -1.0, 2.0, 0.0},
  {3.0, 0.25, -2.0, 0.0},
  {-4.0, 1.5, 0.75, 0.0}
}};

static const double kMatrixScale = 2.0;

static const double kLerpValues[5][4] = {
  {1.0, -2.0, 10.0, 0.0},
  {5.0, 6.0, -2.0, 100.0},
  {0.25, 0.5, 1.5, -0.5},
  {10.0, 20.0, 30.0, 40.0},
  {18.0, 4.0, 6.0, 100.0}
};

static const Double3Storage kProductVector = {{2.0, -1.0, 0.5, 0.0}};

static const Double2x3Storage kMixedLeft = {{
  {1.0, 2.0},
  {-1.0, 0.5},
  {3.0, -2.0}
}};

static const Double3x2Storage kMixedRight = {{
  {0.25, 2.0, -1.0, 0.0},
  {4.0, -0.5, 1.5, 0.0}
}};

static const Double2x2Storage kMatrix2 = {{
  {1.0, 3.0},
  {2.0, 4.0}
}};

static const Double2x2Storage kMatrix2Inverse = {{
  {-2.0, 1.5},
  {1.0, -0.5}
}};

static const Double3x3Storage kMatrixInverse = {{
  {-24.0, 20.0, -5.0, 0.0},
  {18.0, -15.0, 4.0, 0.0},
  {5.0, -4.0, 1.0, 0.0}
}};

static const Double4x4Storage kMatrix4 = {{
  {1.0, 0.0, 0.0, 0.0},
  {0.0, 1.0, 0.0, 0.0},
  {0.0, 0.0, 1.0, 0.0},
  {0.0, 0.0, 0.0, 1.0}
}};

static const Double4x4Storage kMatrix4Inverse = {{
  {1.0, 0.0, 0.0, 0.0},
  {0.0, 1.0, 0.0, 0.0},
  {0.0, 0.0, 1.0, 0.0},
  {0.0, 0.0, 0.0, 1.0}
}};

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

static int
values_match(const Double3Storage *actual, const Double3Storage *expected) {
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (fabs(actual->lane[lane] - expected->lane[lane]) > 1e-12) {
      fprintf(stderr,
              "F64 double3 mismatch at lane %u: expected %.17g, got %.17g\n",
              lane,
              expected->lane[lane],
              actual->lane[lane]);
      return 0;
    }
  }
  return 1;
}

static int
matrix_matches(const Double3x3Storage *actual,
               const Double3x3Storage *expected) {
  for (uint32_t column = 0u; column < 3u; column++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      if (fabs(actual->columns[column][lane] -
               expected->columns[column][lane]) > 1e-12) {
        fprintf(stderr,
                "F64 matrix mismatch at column %u lane %u: "
                "expected %.17g, got %.17g\n",
                column,
                lane,
                expected->columns[column][lane],
                actual->columns[column][lane]);
        return 0;
      }
    }
  }
  return 1;
}

static int
matrix2_matches(const Double2x2Storage *actual,
                const Double2x2Storage *expected) {
  for (uint32_t column = 0u; column < 2u; column++) {
    for (uint32_t lane = 0u; lane < 2u; lane++) {
      if (fabs(actual->columns[column][lane] -
               expected->columns[column][lane]) > 1e-12) {
        fprintf(stderr,
                "F64 matrix2 mismatch at column %u lane %u: "
                "expected %.17g, got %.17g\n",
                column,
                lane,
                expected->columns[column][lane],
                actual->columns[column][lane]);
        return 0;
      }
    }
  }
  return 1;
}

static int
matrix4_matches(const Double4x4Storage *actual,
                const Double4x4Storage *expected) {
  for (uint32_t column = 0u; column < 4u; column++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      if (fabs(actual->columns[column][lane] -
               expected->columns[column][lane]) > 1e-12) {
        fprintf(stderr,
                "F64 matrix4 mismatch at column %u lane %u: "
                "expected %.17g, got %.17g\n",
                column,
                lane,
                expected->columns[column][lane],
                actual->columns[column][lane]);
        return 0;
      }
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
  GPUBuffer             *buffers[31] = {0};
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
  GPUBindGroupEntry            groupEntries[31] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  Double3Storage               valueResult = {0};
  Double3x3Storage             matrixResult = {0};
  Double3x3Storage             elementwiseResult[4] = {0};
  Double3x3Storage             elementwiseExpected[4] = {0};
  Double3x3Storage             productResult = {0};
  Double3x3Storage             productExpected = {0};
  Double3Storage               matrixVectorResult = {0};
  Double3Storage               matrixVectorExpected = {0};
  Double3Storage               vectorMatrixResult = {0};
  Double3Storage               vectorMatrixExpected = {0};
  Double2x2Storage             mixedResult = {0};
  Double2x2Storage             mixedExpected = {0};
  Double2x2Storage             inverse2Result = {0};
  Double3x3Storage             inverse3Result = {0};
  Double4x4Storage             inverse4Result = {0};
  double                       lerpResult[4] = {0};
  double                       inverseLerpResult[4] = {0};
  double                       remapResult[4] = {0};
  double                       stepResult[4] = {0};
  double                       smoothstepResult[4] = {0};
  double                       copysignResult[4] = {0};
  double                       modResult[4] = {0};
  double                       determinantResult[4] = {0};
  const Double3Storage         zeroValue = {0};
  const Double3x3Storage       zeroMatrix = {0};
  const Double2x2Storage       zeroMatrix2 = {0};
  const Double4x4Storage       zeroMatrix4 = {0};
  const double                 zeroDouble4[4] = {0};
  const double                 zeroDeterminant[4] = {0};
  const void                  *initialValues[31] = {
    kValues, &kMatrix, &zeroValue, &zeroMatrix, zeroDeterminant,
    &kMatrix2, &kMatrix4, &kMatrixRight, &kMatrixScale,
    &zeroMatrix, &zeroMatrix, &zeroMatrix, &zeroMatrix,
    &kProductVector, &zeroMatrix, &zeroValue, &zeroValue,
    &kMixedLeft, &kMixedRight, &zeroMatrix2,
    &zeroMatrix2, &zeroMatrix, &zeroMatrix4,
    kLerpValues, zeroDouble4, zeroDouble4, zeroDouble4, zeroDouble4,
    zeroDouble4, zeroDouble4, zeroDouble4
  };
  const uint64_t bufferSizes[31] = {
    sizeof(kValues), sizeof(kMatrix), sizeof(zeroValue), sizeof(zeroMatrix),
    sizeof(zeroDeterminant), sizeof(kMatrix2), sizeof(kMatrix4),
    sizeof(kMatrixRight), sizeof(kMatrixScale), sizeof(zeroMatrix),
    sizeof(zeroMatrix), sizeof(zeroMatrix), sizeof(zeroMatrix),
    sizeof(kProductVector), sizeof(zeroMatrix), sizeof(zeroValue),
    sizeof(zeroValue), sizeof(kMixedLeft), sizeof(kMixedRight),
    sizeof(zeroMatrix2), sizeof(zeroMatrix2), sizeof(zeroMatrix),
    sizeof(zeroMatrix4), sizeof(kLerpValues), sizeof(zeroDouble4),
    sizeof(zeroDouble4), sizeof(zeroDouble4), sizeof(zeroDouble4),
    sizeof(zeroDouble4), sizeof(zeroDouble4), sizeof(zeroDouble4)
  };
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  for (uint32_t column = 0u; column < 3u; column++) {
    for (uint32_t lane = 0u; lane < 3u; lane++) {
      double left  = kMatrix.columns[column][lane];
      double right = kMatrixRight.columns[column][lane];

      elementwiseExpected[0].columns[column][lane] = left + right;
      elementwiseExpected[1].columns[column][lane] = left - right;
      elementwiseExpected[2].columns[column][lane] = kMatrixScale * left;
      elementwiseExpected[3].columns[column][lane] = left / kMatrixScale;
      for (uint32_t term = 0u; term < 3u; term++) {
        productExpected.columns[column][lane] +=
          kMatrix.columns[term][lane] *
          kMatrixRight.columns[column][term];
      }
    }
  }
  for (uint32_t lane = 0u; lane < 3u; lane++) {
    for (uint32_t term = 0u; term < 3u; term++) {
      matrixVectorExpected.lane[lane] +=
        kMatrix.columns[term][lane] * kProductVector.lane[term];
      vectorMatrixExpected.lane[lane] +=
        kProductVector.lane[term] * kMatrix.columns[lane][term];
    }
  }
  for (uint32_t column = 0u; column < 2u; column++) {
    for (uint32_t lane = 0u; lane < 2u; lane++) {
      for (uint32_t term = 0u; term < 3u; term++) {
        mixedExpected.columns[column][lane] +=
          kMixedLeft.columns[term][lane] *
          kMixedRight.columns[column][term];
      }
    }
  }

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-f64-storage-dx12-usl [artifact.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f64_storage.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "F64 storage USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 instance creation failed\n");
    goto cleanup;
  }
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "Direct3D 12 adapter enumeration failed\n");
    goto cleanup;
  }
  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "Direct3D 12 compute device creation failed\n");
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK ||
      gpu_test_create_shader_library_from_usl(device,
                                               artifact,
                                               artifactSize,
                                               &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Direct3D 12 F64 storage shader creation failed\n");
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 31u) {
    fprintf(stderr, "Unexpected F64 storage reflection layout\n");
    goto cleanup;
  }
  for (uint32_t binding = 0u; binding < 31u; binding++) {
    GPUBindingType expectedType = binding < 2u ||
                                  (binding >= 5u && binding <= 8u) ||
                                  binding == 13u ||
                                  (binding >= 17u && binding <= 18u) ||
                                  binding == 23u
                                    ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                    : GPU_BINDING_STORAGE_BUFFER;

    if (layoutEntries[binding].binding != binding ||
        layoutEntries[binding].bindingType != expectedType ||
        layoutEntries[binding].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        layoutEntries[binding].arrayCount != 1u ||
        layoutEntries[binding].hasDynamicOffset) {
      fprintf(stderr, "Unexpected F64 storage binding %u\n", binding);
      goto cleanup;
    }
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-f64-storage";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f64_storage";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Direct3D 12 F64 storage pipeline creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t binding = 0u; binding < 31u; binding++) {
    bufferInfo.sizeBytes = bufferSizes[binding];
    if (GPUCreateBuffer(device, &bufferInfo, &buffers[binding]) != GPU_OK ||
        !buffers[binding] ||
        GPUQueueWriteBuffer(queue,
                            buffers[binding],
                            0u,
                            initialValues[binding],
                            bufferSizes[binding]) != GPU_OK) {
      fprintf(stderr,
              "Direct3D 12 F64 storage buffer %u creation failed\n",
              binding);
      goto cleanup;
    }
    groupEntries[binding].binding       = binding;
    groupEntries[binding].bindingType   = binding < 2u ||
                                          (binding >= 5u && binding <= 8u) ||
                                          binding == 13u ||
                                          (binding >= 17u && binding <= 18u) ||
                                          binding == 23u
                                            ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                            : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = bufferSizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-f64-storage-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 31u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup ||
      GPUAcquireCommandBuffer(queue,
                              "dx12-native-f64-storage",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 F64 storage bind/command creation failed\n");
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f64-storage");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 F64 storage compute pass creation failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 F64 storage fence creation failed\n");
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
                         buffers[2],
                         0u,
                         &valueResult,
                         sizeof(valueResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[3],
                         0u,
                         &matrixResult,
                         sizeof(matrixResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[4],
                         0u,
                         &determinantResult,
                         sizeof(determinantResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[9],
                         0u,
                         &elementwiseResult[0],
                         sizeof(elementwiseResult[0])) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[10],
                         0u,
                         &elementwiseResult[1],
                         sizeof(elementwiseResult[1])) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[11],
                         0u,
                         &elementwiseResult[2],
                         sizeof(elementwiseResult[2])) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[12],
                         0u,
                         &elementwiseResult[3],
                         sizeof(elementwiseResult[3])) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[14],
                         0u,
                         &productResult,
                         sizeof(productResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[15],
                         0u,
                         &matrixVectorResult,
                         sizeof(matrixVectorResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[16],
                         0u,
                         &vectorMatrixResult,
                         sizeof(vectorMatrixResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[19],
                         0u,
                         &mixedResult,
                         sizeof(mixedResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[20],
                         0u,
                         &inverse2Result,
                         sizeof(inverse2Result)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[21],
                         0u,
                         &inverse3Result,
                         sizeof(inverse3Result)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[22],
                         0u,
                         &inverse4Result,
                         sizeof(inverse4Result)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[24],
                         0u,
                         lerpResult,
                         sizeof(lerpResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[25],
                         0u,
                         inverseLerpResult,
                         sizeof(inverseLerpResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[26],
                         0u,
                         remapResult,
                         sizeof(remapResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[27],
                         0u,
                         stepResult,
                         sizeof(stepResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[28],
                         0u,
                         smoothstepResult,
                         sizeof(smoothstepResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[29],
                         0u,
                         copysignResult,
                         sizeof(copysignResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[30],
                         0u,
                         modResult,
                         sizeof(modResult)) != GPU_OK ||
      !values_match(&valueResult, &kValues[1]) ||
      !matrix_matches(&matrixResult, &kMatrixTranspose) ||
      !matrix_matches(&elementwiseResult[0], &elementwiseExpected[0]) ||
      !matrix_matches(&elementwiseResult[1], &elementwiseExpected[1]) ||
      !matrix_matches(&elementwiseResult[2], &elementwiseExpected[2]) ||
      !matrix_matches(&elementwiseResult[3], &elementwiseExpected[3]) ||
      !matrix_matches(&productResult, &productExpected) ||
      !values_match(&matrixVectorResult, &matrixVectorExpected) ||
      !values_match(&vectorMatrixResult, &vectorMatrixExpected) ||
      !matrix2_matches(&mixedResult, &mixedExpected) ||
      !matrix2_matches(&inverse2Result, &kMatrix2Inverse) ||
      !matrix_matches(&inverse3Result, &kMatrixInverse) ||
      !matrix4_matches(&inverse4Result, &kMatrix4Inverse)) {
    fprintf(stderr, "Direct3D 12 F64 storage readback validation failed\n");
    goto cleanup;
  }
  {
    static const double expected[] = {-2.0, 1.0, 1.0, 1.0};
    static const double lerpExpected[] = {2.0, 2.0, -8.0, -50.0};
    static const double inverseLerpExpected[] = {
      -0.1875, 0.3125, 0.708333333333333333, -0.005
    };
    static const double remapExpected[] = {8.5, 15.0, 13.0, 39.7};
    static const double stepExpected[] = {1.0, 1.0, 0.0, 1.0};
    static const double smoothstepExpected[] = {
      0.0, 475.0 / 2048.0, 5491.0 / 6912.0, 0.0
    };
    static const double copysignExpected[] = {-1.0, 2.0, -10.0, 0.0};
    static const double modExpected[] = {0.25, 99.5, -0.5, 0.0};

    for (uint32_t lane = 0u; lane < 4u; lane++) {
      if (fabs(determinantResult[lane] - expected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 determinant mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                expected[lane],
                determinantResult[lane]);
        goto cleanup;
      }
      if (fabs(lerpResult[lane] - lerpExpected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 lerp mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                lerpExpected[lane],
                lerpResult[lane]);
        goto cleanup;
      }
      if (fabs(inverseLerpResult[lane] - inverseLerpExpected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 inverselerp mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                inverseLerpExpected[lane],
                inverseLerpResult[lane]);
        goto cleanup;
      }
      if (fabs(remapResult[lane] - remapExpected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 remap mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                remapExpected[lane],
                remapResult[lane]);
        goto cleanup;
      }
      if (fabs(stepResult[lane] - stepExpected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 step mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                stepExpected[lane],
                stepResult[lane]);
        goto cleanup;
      }
      if (fabs(smoothstepResult[lane] - smoothstepExpected[lane]) > 1e-12) {
        fprintf(stderr,
                "F64 smoothstep mismatch at lane %u: expected %.17g, got %.17g\n",
                lane,
                smoothstepExpected[lane],
                smoothstepResult[lane]);
        goto cleanup;
      }
      if (memcmp(&copysignResult[lane],
                 &copysignExpected[lane],
                 sizeof(copysignResult[lane])) != 0) {
        uint64_t actualBits, expectedBits;

        memcpy(&actualBits, &copysignResult[lane], sizeof(actualBits));
        memcpy(&expectedBits, &copysignExpected[lane], sizeof(expectedBits));
        fprintf(stderr,
                "F64 copysign mismatch at lane %u: expected 0x%016llx, "
                "got 0x%016llx\n",
                lane,
                (unsigned long long)expectedBits,
                (unsigned long long)actualBits);
        goto cleanup;
      }
      if (memcmp(&modResult[lane],
                 &modExpected[lane],
                 sizeof(modResult[lane])) != 0) {
        uint64_t actualBits, expectedBits;

        memcpy(&actualBits, &modResult[lane], sizeof(actualBits));
        memcpy(&expectedBits, &modExpected[lane], sizeof(expectedBits));
        fprintf(stderr,
                "F64 mod mismatch at lane %u: expected 0x%016llx, "
                "got 0x%016llx\n",
                lane,
                (unsigned long long)expectedBits,
                (unsigned long long)actualBits);
        goto cleanup;
      }
    }
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  for (uint32_t i = 0u; i < 31u; i++) GPUDestroyBuffer(buffers[i]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  if (!ok) return 1;
  puts("Direct3D 12 native DXIL F64 storage validation passed");
  return 0;
}
