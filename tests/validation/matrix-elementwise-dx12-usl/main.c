#include <gpu/gpu.h>

#include "../usl_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Matrix3x3Storage {
  float columns[3][4];
} Matrix3x3Storage;

static const Matrix3x3Storage kLeft = {{
  {1.0f, 2.0f, 3.0f, 0.0f},
  {4.0f, 5.0f, 6.0f, 0.0f},
  {7.0f, 8.0f, 9.0f, 0.0f}
}};

static const Matrix3x3Storage kRight = {{
  {9.0f, 8.0f, 7.0f, 0.0f},
  {6.0f, 5.0f, 4.0f, 0.0f},
  {3.0f, 2.0f, 1.0f, 0.0f}
}};

static const Matrix3x3Storage kExpectedAdd = {{
  {10.0f, 10.0f, 10.0f, 0.0f},
  {10.0f, 10.0f, 10.0f, 0.0f},
  {10.0f, 10.0f, 10.0f, 0.0f}
}};

static const Matrix3x3Storage kExpectedSub = {{
  {-8.0f, -6.0f, -4.0f, 0.0f},
  {-2.0f,  0.0f,  2.0f, 0.0f},
  { 4.0f,  6.0f,  8.0f, 0.0f}
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
matrix_matches(const Matrix3x3Storage *actual,
               const Matrix3x3Storage *expected,
               const char             *label) {
  for (uint32_t column = 0u; column < 3u; column++) {
    for (uint32_t lane = 0u; lane < 4u; lane++) {
      float difference = fabsf(actual->columns[column][lane] -
                               expected->columns[column][lane]);

      if (difference > 0.0001f) {
        fprintf(stderr,
                "%s mismatch at column %u lane %u: expected %.6f, got %.6f\n",
                label,
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
  GPUBuffer             *buffers[4] = {0};
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
  GPUBindGroupEntry            groupEntries[4] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  Matrix3x3Storage             addResult = {0};
  Matrix3x3Storage             subResult = {0};
  const Matrix3x3Storage       zero = {0};
  const Matrix3x3Storage      *initialValues[4] = {
    &kLeft, &kRight, &zero, &zero
  };
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-matrix-elementwise-dx12-usl [artifact.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "matrix_elementwise.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "matrix element-wise USL artifact read failed\n");
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
    fprintf(stderr, "Direct3D 12 matrix shader creation failed\n");
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 4u) {
    fprintf(stderr, "Unexpected matrix reflection layout\n");
    goto cleanup;
  }
  for (uint32_t binding = 0u; binding < 4u; binding++) {
    GPUBindingType expectedType = binding < 2u
                                    ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                    : GPU_BINDING_STORAGE_BUFFER;

    if (layoutEntries[binding].binding != binding ||
        layoutEntries[binding].bindingType != expectedType ||
        layoutEntries[binding].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        layoutEntries[binding].arrayCount != 1u ||
        layoutEntries[binding].hasDynamicOffset) {
      fprintf(stderr, "Unexpected matrix binding %u\n", binding);
      goto cleanup;
    }
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-matrix-elementwise";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "dxil_matrix_elementwise";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Direct3D 12 matrix pipeline creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(Matrix3x3Storage);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t binding = 0u; binding < 4u; binding++) {
    if (GPUCreateBuffer(device, &bufferInfo, &buffers[binding]) != GPU_OK ||
        !buffers[binding] ||
        GPUQueueWriteBuffer(queue,
                            buffers[binding],
                            0u,
                            initialValues[binding],
                            sizeof(Matrix3x3Storage)) != GPU_OK) {
      fprintf(stderr, "Direct3D 12 matrix buffer %u creation failed\n", binding);
      goto cleanup;
    }
    groupEntries[binding].binding     = binding;
    groupEntries[binding].bindingType = binding < 2u
                                          ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                          : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = sizeof(Matrix3x3Storage);
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-matrix-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 4u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup ||
      GPUAcquireCommandBuffer(queue,
                              "dx12-native-matrix-elementwise",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 matrix bind/command creation failed\n");
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "matrix-elementwise");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 matrix compute pass creation failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 matrix fence creation failed\n");
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
                         &addResult,
                         sizeof(addResult)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[3],
                         0u,
                         &subResult,
                         sizeof(subResult)) != GPU_OK ||
      !matrix_matches(&addResult, &kExpectedAdd, "matrix add") ||
      !matrix_matches(&subResult, &kExpectedSub, "matrix sub")) {
    fprintf(stderr, "Direct3D 12 matrix readback validation failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) GPUEndComputePass(pass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  for (uint32_t i = 0u; i < 4u; i++) GPUDestroyBuffer(buffers[i]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);
  if (!ok) return 1;
  puts("Direct3D 12 native DXIL matrix element-wise validation passed");
  return 0;
}
