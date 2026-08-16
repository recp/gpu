#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GPU_ASYNC_COPY_BACKEND
#  error "GPU_ASYNC_COPY_BACKEND is required"
#endif

enum {
  ValueCount = 64u
};

typedef struct Vec4 {
  float x, y, z, w;
} Vec4;

_Static_assert(sizeof(Vec4) == 16u, "Vec4 ABI drift");

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
values_match(const Vec4 input[ValueCount], const Vec4 output[ValueCount]) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const Vec4 *expected = &input[ValueCount - 1u - i];

    if (output[i].x != expected->x || output[i].y != expected->y ||
        output[i].z != expected->z || output[i].w != expected->w) {
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
  GPUBindGroup          *group;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            entries[2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  Vec4                         input[ValueCount];
  Vec4                         output[ValueCount];
  uint64_t                     artifactSize;
  uint32_t                     adapterCount;
  GPUResult                    result;
  int                          status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-async-copy-usl artifact.us\n");
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
  group        = NULL;
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
  instanceInfo.preferredBackend = GPU_ASYNC_COPY_BACKEND;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    puts("async-copy backend unavailable");
    status = 77;
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    puts("async-copy adapter unavailable");
    status = 77;
    goto cleanup;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "async-copy compute device creation failed\n");
    goto cleanup;
  }

  result = GPUCreateShaderLibraryFromUSL(device,
                                         artifact,
                                         artifactSize,
                                         &library);
  if (result == GPU_ERROR_UNSUPPORTED) {
    puts("async copy unavailable");
    status = 77;
    goto cleanup;
  }
  if (result != GPU_OK || !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "async-copy shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "async-copy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "async_copy_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "async-copy pipeline creation failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < ValueCount; i++) {
    input[i].x  = (float)(i * 4u);
    input[i].y  = input[i].x + 1.0f;
    input[i].z  = input[i].x + 2.0f;
    input[i].w  = input[i].x + 3.0f;
    output[i].x = 0.0f;
    output[i].y = 0.0f;
    output[i].z = 0.0f;
    output[i].w = 0.0f;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "async-copy-input";
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
    fprintf(stderr, "async-copy input creation failed\n");
    goto cleanup;
  }

  bufferInfo.label = "async-copy-output";
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "async-copy output creation failed\n");
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
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "async-copy-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "async-copy bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "async-copy", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "async-copy"))) {
    fprintf(stderr, "async-copy command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "async-copy command submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !values_match(input, output)) {
    fprintf(stderr, "async-copy readback validation failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) puts("GPU USL async-copy validation passed");
  return status;
}
