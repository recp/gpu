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

typedef struct ComputeConstants {
  float tint[4];
} ComputeConstants;

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
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *pipeline;
  GPUBuffer             *buffer;
  GPUBuffer             *indirectBuffer;
  GPUBuffer             *dispatchBuffer;
  GPUBindGroup          *bindGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUFence              *fence;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPURuntimeConfig             runtimeConfig = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntries[2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUBufferBarrier             barriers[2] = {0};
  GPUBarrierBatch              barrierBatch = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GeneratedVertex              vertices[3] = {0};
  uint32_t                     drawArgs[5] = {0};
  const uint32_t               expectedDrawArgs[5] = {3u, 1u, 0u, 0u, 0u};
  const uint32_t               dispatchArgs[3] = {3u, 1u, 1u};
  const ComputeConstants       constants = {{1.0f, 1.0f, 1.0f, 1.0f}};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok;

  if (argc != 2) {
    fprintf(stderr, "usage: hello-compute-buffer-vulkan-usl artifact.us\n");
    return 1;
  }

  instance       = NULL;
  adapter        = NULL;
  device         = NULL;
  queue          = NULL;
  library        = NULL;
  shaderLayout   = NULL;
  pipeline       = NULL;
  buffer         = NULL;
  indirectBuffer = NULL;
  dispatchBuffer = NULL;
  bindGroup      = NULL;
  cmdb           = NULL;
  pass           = NULL;
  fence          = NULL;
  artifactSize   = 0u;
  artifact       = read_file(argv[1], &artifactSize);
  ok             = 0;
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
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
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
  if (!layoutEntries || layoutEntryCount != 2u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      layoutEntries[0].arrayCount != 1u ||
      layoutEntries[0].hasDynamicOffset ||
      layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[1].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      layoutEntries[1].arrayCount != 1u ||
      layoutEntries[1].hasDynamicOffset) {
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

  bufferInfo.label     = "vulkan-compute-draw-args";
  bufferInfo.sizeBytes = sizeof(drawArgs);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_INDIRECT |
                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &indirectBuffer) != GPU_OK ||
      !indirectBuffer) {
    fprintf(stderr, "Vulkan indirect buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "vulkan-compute-dispatch-args";
  bufferInfo.sizeBytes = sizeof(dispatchArgs);
  bufferInfo.usage     = GPU_BUFFER_USAGE_INDIRECT |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &dispatchBuffer) != GPU_OK ||
      !dispatchBuffer ||
      GPUQueueWriteBuffer(queue,
                          dispatchBuffer,
                          0u,
                          dispatchArgs,
                          sizeof(dispatchArgs)) != GPU_OK) {
    fprintf(stderr, "Vulkan dispatch buffer creation failed\n");
    goto cleanup;
  }

  groupEntries[0].binding       = 0u;
  groupEntries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[0].buffer.buffer = buffer;
  groupEntries[0].buffer.size   = sizeof(vertices);
  groupEntries[1].binding       = 1u;
  groupEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[1].buffer.buffer = indirectBuffer;
  groupEntries[1].buffer.size   = sizeof(drawArgs);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "vulkan-compute-group1";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
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
  GPUSetComputePushConstants(pass,
                             0u,
                             (uint32_t)sizeof(constants),
                             &constants);
  GPUDispatchIndirect(pass, dispatchBuffer, 0u);
  GPUEndComputePass(pass);
  pass = NULL;

  barriers[0].buffer              = buffer;
  barriers[0].srcAccess           = GPU_ACCESS_SHADER_WRITE;
  barriers[0].dstAccess           = GPU_ACCESS_SHADER_READ;
  barriers[0].sizeBytes           = sizeof(vertices);
  barriers[1].buffer              = indirectBuffer;
  barriers[1].srcAccess           = GPU_ACCESS_SHADER_WRITE;
  barriers[1].dstAccess           = GPU_ACCESS_INDIRECT_READ;
  barriers[1].sizeBytes           = sizeof(drawArgs);
  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_VERTEX;
  barrierBatch.bufferBarrierCount = 2u;
  barrierBatch.pBufferBarriers     = barriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

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
      GPUQueueReadBuffer(queue,
                         indirectBuffer,
                         0u,
                         drawArgs,
                         sizeof(drawArgs)) != GPU_OK ||
      !vertices_match(vertices) ||
      memcmp(drawArgs, expectedDrawArgs, sizeof(drawArgs)) != 0) {
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
  GPUDestroyBuffer(dispatchBuffer);
  GPUDestroyBuffer(indirectBuffer);
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
