#include <gpu/gpu.h>

#include "../usl_test.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Double4 {
  double lane[4];
} Double4;

typedef struct Float2 {
  float lane[2];
} Float2;

typedef struct Int2 {
  int32_t lane[2];
} Int2;

static const Double4 kInput = {{-2.0, 3.0, 4.0, -5.0}};
static const Double4 kExpectedOutput[14] = {
  {{-1.5, 4.0, 5.5, -3.0}},
  {{-2.75, 3.0, -5.0, 8.5}},
  {{-2.0, 0.5, 0.5, -5.0}},
  {{-0.5, 3.0, 4.0, -0.5}},
  {{-1.0, 1.0, 1.0, -1.0}},
  {{0.0, 1.0, 1.0, 0.0}},
  {{-1.0, 1.0, 1.0, -1.0}},
  {{2.0, 3.0, 4.0, 5.0}},
  {{-4.0, 4.0, 2.75, 1.0}},
  {{10.0, 6.0, 0.5, 1.0}},
  {{-4.75, 8.5, -7.0, -6.375}},
  {{2.0, -3.0, -4.0, 5.0}},
  {{0.25882352941176473,
    -0.51764705882352946,
    1.0352941176470589,
    0.12941176470588237}},
  {{-2.2588235294117647,
    3.5176470588235293,
    2.9647058823529413,
    -5.1294117647058828}}
};
static const Double4 kSqrtInput[3] = {
  {{0.0, -0.0, DBL_TRUE_MIN, DBL_MIN}},
  {{1.0, 2.0, 4.0, 9.0}},
  {{DBL_MAX, INFINITY, -1.0, NAN}}
};
static const Float2 kPacked = {{1.5f, -2.25f}};
static const Float2 kExpectedPacked = {{4.0f, -5.0f}};
static const Int2   kInteger = {{7, -8}};
static const Int2   kExpectedInteger = {{-2, 3}};

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
double4_matches(const Double4 *actual,
                const Double4 *expected,
                uint32_t       element) {
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    if (fabs(actual->lane[lane] - expected->lane[lane]) > 1e-12) {
      fprintf(stderr,
              "F64 output mismatch at element %u lane %u: "
              "expected %.17g, got %.17g\n",
              element,
              lane,
              expected->lane[lane],
              actual->lane[lane]);
      return 0;
    }
  }
  return 1;
}

static uint64_t
double_bits(double value) {
  uint64_t bits;

  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static int
sqrt4_matches(const Double4 *actual,
              const Double4 *input,
              uint32_t       element) {
  for (uint32_t lane = 0u; lane < 4u; lane++) {
    double expected = sqrt(input->lane[lane]);
    uint64_t actualBits;
    uint64_t expectedBits;
    uint64_t distance;

    if (isnan(expected)) {
      if (isnan(actual->lane[lane])) continue;
    } else {
      actualBits   = double_bits(actual->lane[lane]);
      expectedBits = double_bits(expected);
      distance     = actualBits > expectedBits
                       ? actualBits - expectedBits
                       : expectedBits - actualBits;
      if (distance <= 1u) continue;
    }
    fprintf(stderr,
            "F64 sqrt mismatch at element %u lane %u: "
            "input %.17g, expected %.17g, got %.17g\n",
            element,
            lane,
            input->lane[lane],
            expected,
            actual->lane[lane]);
    return 0;
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
  Double4                      output[17] = {0};
  Float2                       packed = {0};
  Int2                         integer = {0};
  const Double4                zeroOutput[17] = {0};
  const void                  *initialValues[5] = {
    &kInput, zeroOutput, &kPacked, &kInteger, kSqrtInput
  };
  const uint64_t bufferSizes[5] = {
    sizeof(kInput), sizeof(zeroOutput), sizeof(kPacked), sizeof(kInteger),
    sizeof(kSqrtInput)
  };
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize = 0u;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok = 0;

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-f64-core-dx12-usl [artifact.us]\n");
    return 1;
  }
  artifactPath = argc == 2 ? argv[1] : "f64_core.us";
  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "F64 core USL artifact read failed\n");
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
    fprintf(stderr, "Direct3D 12 F64 shader creation failed\n");
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 5u) {
    fprintf(stderr, "Unexpected F64 reflection layout\n");
    goto cleanup;
  }
  for (uint32_t binding = 0u; binding < 5u; binding++) {
    GPUBindingType expectedType = binding == 0u || binding == 4u
                                    ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                    : GPU_BINDING_STORAGE_BUFFER;

    if (layoutEntries[binding].binding != binding ||
        layoutEntries[binding].bindingType != expectedType ||
        layoutEntries[binding].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        layoutEntries[binding].arrayCount != 1u ||
        layoutEntries[binding].hasDynamicOffset) {
      fprintf(stderr, "Unexpected F64 binding %u\n", binding);
      goto cleanup;
    }
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-native-f64-core";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "f64_core";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Direct3D 12 F64 pipeline creation failed\n");
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
      fprintf(stderr, "Direct3D 12 F64 buffer %u creation failed\n", binding);
      goto cleanup;
    }
    groupEntries[binding].binding       = binding;
    groupEntries[binding].bindingType   = binding == 0u || binding == 4u
                                            ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                            : GPU_BINDING_STORAGE_BUFFER;
    groupEntries[binding].buffer.buffer = buffers[binding];
    groupEntries[binding].buffer.size   = bufferSizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-native-f64-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 5u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup ||
      GPUAcquireCommandBuffer(queue, "dx12-native-f64-core", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 F64 bind/command creation failed\n");
    goto cleanup;
  }

  pass = GPUBeginComputePass(cmdb, "f64-core");
  if (!pass) {
    fprintf(stderr, "Direct3D 12 F64 compute pass creation failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "Direct3D 12 F64 fence creation failed\n");
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
      GPUQueueReadBuffer(queue,
                         buffers[2],
                         0u,
                         &packed,
                         sizeof(packed)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffers[3],
                         0u,
                         &integer,
                         sizeof(integer)) != GPU_OK ||
      fabsf(packed.lane[0] - kExpectedPacked.lane[0]) > 1e-6f ||
      fabsf(packed.lane[1] - kExpectedPacked.lane[1]) > 1e-6f ||
      integer.lane[0] != kExpectedInteger.lane[0] ||
      integer.lane[1] != kExpectedInteger.lane[1]) {
    fprintf(stderr, "Direct3D 12 F64 readback validation failed\n");
    goto cleanup;
  }
  for (uint32_t element = 0u; element < 14u; element++) {
    if (!double4_matches(&output[element],
                         &kExpectedOutput[element],
                         element)) {
      fprintf(stderr, "Direct3D 12 F64 output validation failed\n");
      goto cleanup;
    }
  }
  for (uint32_t element = 0u; element < 3u; element++) {
    if (!sqrt4_matches(&output[14u + element],
                       &kSqrtInput[element],
                       element)) {
      fprintf(stderr, "Direct3D 12 F64 sqrt validation failed\n");
      goto cleanup;
    }
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
  puts("Direct3D 12 native DXIL F64 core validation passed");
  return 0;
}
