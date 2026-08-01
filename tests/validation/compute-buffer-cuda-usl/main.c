#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  ValueCount = 512u
};

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
values_match(const float values[ValueCount]) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * 2.0f + 1.0f;

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
  GPUBuffer             *buffer;
  GPUBindGroup          *bindGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  float                        values[ValueCount];
  uint64_t                     artifactSize;
  uint32_t                     adapterCount;
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
  buffer       = NULL;
  bindGroup    = NULL;
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
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
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
    values[i] = (float)i;
  }
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-saxpy-values";
  bufferInfo.sizeBytes        = sizeof(values);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          values,
                          sizeof(values)) != GPU_OK) {
    fprintf(stderr, "CUDA storage buffer creation failed\n");
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = buffer;
  groupEntry.buffer.size   = sizeof(values);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-saxpy-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "CUDA bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-saxpy", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "saxpy"))) {
    fprintf(stderr, "CUDA command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
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
                         buffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK ||
      !values_match(values)) {
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
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(buffer);
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
