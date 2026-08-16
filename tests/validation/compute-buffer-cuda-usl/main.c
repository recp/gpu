#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  ValueCount = 512u
};

typedef struct Params {
  float scale;
  float bias;
} Params;

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
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
values_match(const float values[ValueCount], const Params *params) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * params->scale + params->bias;

    if (fabsf(values[i] - expected) > 0.0001f) {
      return 0;
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *pipeline;
  GPUBuffer             *inputBuffer;
  GPUBuffer             *outputBuffer;
  GPUBuffer             *paramsBuffer;
  GPUBindGroup          *dataGroup;
  GPUBindGroup          *paramsGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            dataEntries[2] = {0};
  GPUBindGroupEntry            paramsEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  Params                       params[2] = {{-3.0f, 7.0f}, {2.0f, 1.0f}};
  float                        input[ValueCount];
  float                        output[ValueCount];
  uint64_t                     artifactSize;
  uint32_t                     adapterCount;
  uint32_t                     dynamicOffset;
  GPUResult                    result;
  int                          status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-compute-buffer-cuda-usl artifact.us\n");
    return 1;
  }

  instance     = NULL;
  adapter      = NULL;
  device       = NULL;
  queue        = NULL;
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  inputBuffer  = NULL;
  outputBuffer = NULL;
  paramsBuffer = NULL;
  dataGroup    = NULL;
  paramsGroup  = NULL;
  cmdb         = NULL;
  pass         = NULL;
  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  status       = 1;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    puts("CUDA Driver backend unavailable");
    status = 77;
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    puts("CUDA adapter unavailable");
    status = 77;
    goto cleanup;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "CUDA compute device creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA USL shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "cuda-usl-saxpy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "saxpy";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "CUDA compute pipeline creation failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < ValueCount; i++) {
    input[i]  = (float)i;
    output[i] = 0.0f;
  }
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-saxpy-input";
  bufferInfo.sizeBytes        = sizeof(input);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &inputBuffer) != GPU_OK ||
      !inputBuffer ||
      GPUQueueWriteBuffer(queue,
                          inputBuffer,
                          0u,
                          input,
                          sizeof(input)) != GPU_OK) {
    fprintf(stderr, "CUDA input buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "cuda-saxpy-output";
  bufferInfo.sizeBytes = sizeof(output);
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "CUDA output buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "cuda-saxpy-params";
  bufferInfo.sizeBytes = sizeof(params);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &paramsBuffer) != GPU_OK ||
      !paramsBuffer ||
      GPUQueueWriteBuffer(queue,
                          paramsBuffer,
                          0u,
                          params,
                          sizeof(params)) != GPU_OK) {
    fprintf(stderr, "CUDA uniform buffer creation failed\n");
    goto cleanup;
  }

  paramsEntry.binding       = 0u;
  paramsEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  paramsEntry.buffer.buffer = paramsBuffer;
  paramsEntry.buffer.size   = sizeof(params[0]);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-saxpy-params-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &paramsEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &paramsGroup) != GPU_OK ||
      !paramsGroup) {
    fprintf(stderr, "CUDA params bind group creation failed\n");
    goto cleanup;
  }

  dataEntries[0].binding       = 0u;
  dataEntries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  dataEntries[0].buffer.buffer = inputBuffer;
  dataEntries[0].buffer.size   = sizeof(input);
  dataEntries[1].binding       = 1u;
  dataEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  dataEntries[1].buffer.buffer = outputBuffer;
  dataEntries[1].buffer.size   = sizeof(output);
  groupInfo.label              = "cuda-saxpy-data-group";
  groupInfo.layout             = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount         = 2u;
  groupInfo.pEntries           = dataEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &dataGroup) != GPU_OK ||
      !dataGroup) {
    fprintf(stderr, "CUDA data bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-saxpy", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "saxpy"))) {
    fprintf(stderr, "CUDA command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  dynamicOffset = sizeof(params[0]);
  GPUBindComputeGroup(pass, 0u, paramsGroup, 1u, &dynamicOffset);
  GPUBindComputeGroup(pass, 1u, dataGroup, 0u, NULL);
  GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "CUDA command submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !values_match(output, &params[1])) {
    fprintf(stderr, "CUDA compute readback validation failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyBindGroup(dataGroup);
  GPUDestroyBindGroup(paramsGroup);
  GPUDestroyBuffer(paramsBuffer);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL compute buffer validation passed");
  }
  return status;
}
