#include <gpu/gpu.h>

#include "../usl_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Double3Storage {
  double lane[4];
} Double3Storage;

typedef struct Double3x3Storage {
  double columns[3][4];
} Double3x3Storage;

static const Double3Storage kValues[2] = {
  {{1.0, 2.0, 3.0, 0.0}},
  {{-4.0, 5.5, 6.25, 0.0}}
};

static const Double3x3Storage kMatrix = {{
  {1.0, 0.0, 5.0, 0.0},
  {2.0, 1.0, 6.0, 0.0},
  {3.0, 4.0, 0.0, 0.0}
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

int
main(int argc, char **argv) {
  GPUInstance           *instance = NULL;
  GPUAdapter            *adapter = NULL;
  GPUDevice             *device = NULL;
  GPUQueue              *queue = NULL;
  GPUShaderLibrary      *library = NULL;
  GPUShaderLayout       *shaderLayout = NULL;
  GPUComputePipeline    *pipeline = NULL;
  GPUBuffer             *buffers[5] = {0};
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
  GPUBindGroupEntry            groupEntries[5] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  Double3Storage               valueResult = {0};
  Double3x3Storage             matrixResult = {0};
  double                       determinantResult = 0.0;
  const Double3Storage         zeroValue = {0};
  const Double3x3Storage       zeroMatrix = {0};
  const double                 zeroDeterminant = 0.0;
  const void                  *initialValues[5] = {
    kValues, &kMatrix, &zeroValue, &zeroMatrix, &zeroDeterminant
  };
  const uint64_t bufferSizes[5] = {
    sizeof(kValues), sizeof(kMatrix), sizeof(zeroValue), sizeof(zeroMatrix),
    sizeof(zeroDeterminant)
  };
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

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
  if (!layoutEntries || layoutEntryCount != 5u) {
    fprintf(stderr, "Unexpected F64 storage reflection layout\n");
    goto cleanup;
  }
  for (uint32_t binding = 0u; binding < 5u; binding++) {
    GPUBindingType expectedType = binding < 2u
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
  for (uint32_t binding = 0u; binding < 5u; binding++) {
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
    groupEntries[binding].bindingType   = binding < 2u
                                            ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                            : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = bufferSizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-f64-storage-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 5u;
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
      !values_match(&valueResult, &kValues[1]) ||
      !matrix_matches(&matrixResult, &kMatrix) ||
      fabs(determinantResult - 1.0) > 1e-12) {
    fprintf(stderr, "Direct3D 12 F64 storage readback validation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  for (uint32_t i = 0u; i < 5u; i++) GPUDestroyBuffer(buffers[i]);
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
