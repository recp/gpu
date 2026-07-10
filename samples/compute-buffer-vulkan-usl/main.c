#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

static const GeneratedVertex kExpectedVertices[] = {
  {{-0.6f, -0.6f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.1f, 1.0f}},
  {{ 0.6f, -0.6f, 0.0f, 1.0f}, {0.1f, 1.0f, 0.3f, 1.0f}},
  {{ 0.0f,  0.6f, 0.0f, 1.0f}, {0.2f, 0.4f, 1.0f, 1.0f}}
};

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = fopen(path, "rb");
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
vertices_match(const GeneratedVertex *actual) {
  for (size_t i = 0u;
       i < sizeof(kExpectedVertices) / sizeof(kExpectedVertices[0]);
       i++) {
    for (size_t j = 0u; j < 4u; j++) {
      if (fabsf(actual[i].position[j] - kExpectedVertices[i].position[j]) >
            0.0001f ||
          fabsf(actual[i].color[j] - kExpectedVertices[i].color[j]) >
            0.0001f) {
        return 0;
      }
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUCommandQueue       *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *pipeline;
  GPUBuffer             *buffer;
  GPUBindGroup          *bindGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUFence              *fence;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPURuntimeConfig             runtimeConfig = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GeneratedVertex              vertices[3] = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  uint64_t               artifactSize;
  uint32_t               adapterCount;
  uint32_t               layoutEntryCount;
  int                    ok;

  if (argc != 2) {
    fprintf(stderr, "usage: hello-compute-buffer-vulkan-usl artifact.us\n");
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
  fence        = NULL;
  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  ok           = 0;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "Vulkan instance creation failed\n");
    goto cleanup;
  }

  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "Vulkan adapter enumeration failed\n");
    goto cleanup;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "Vulkan compute queue creation failed\n");
    goto cleanup;
  }

  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK) {
    fprintf(stderr, "GPU runtime configuration failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts[1] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "Vulkan USL shader layout creation failed\n");
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[1],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      layoutEntries[0].arrayCount != 1u ||
      layoutEntries[0].hasDynamicOffset) {
    fprintf(stderr, "Unexpected Vulkan compute reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "vulkan-usl-fill-vertices";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "fill_vertices";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Vulkan compute pipeline creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-compute-vertices";
  bufferInfo.sizeBytes        = sizeof(vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          vertices,
                          sizeof(vertices)) != GPU_OK) {
    fprintf(stderr, "Vulkan storage buffer creation failed\n");
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = buffer;
  groupEntry.buffer.size   = sizeof(vertices);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "vulkan-compute-group1";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "Vulkan compute bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "vulkan-compute", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "fill-vertices"))) {
    fprintf(stderr, "Vulkan compute command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 1u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 3u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "Vulkan compute fence creation failed\n");
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
                         buffer,
                         0u,
                         vertices,
                         sizeof(vertices)) != GPU_OK ||
      !vertices_match(vertices)) {
    fprintf(stderr, "Vulkan compute readback validation failed\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(buffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    return 1;
  }
  puts("Vulkan USL compute buffer validation passed");
  return 0;
}
