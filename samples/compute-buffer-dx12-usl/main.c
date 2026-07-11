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
  GPUBuffer             *indirectBuffer;
  GPUBuffer             *dispatchBuffer;
  GPUBuffer             *queryBuffer;
  GPUBindGroup          *bindGroup;
  GPUQuerySet           *querySet;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUFence              *fence;
  void                  *artifact;
  const char            *artifactPath;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUDeviceCreateInfo          deviceInfo = {0};
  GPURuntimeConfig             runtimeConfig = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntries[2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUBufferBarrier             barriers[2] = {0};
  GPUBarrierBatch              barrierBatch = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GPUQuerySetCreateInfo        queryInfo = {0};
  GPUPipelineStatisticsResult  pipelineStats = {0};
  GeneratedVertex              vertices[3] = {0};
  uint32_t                     drawArgs[5] = {0};
  const uint32_t               expectedDrawArgs[5] = {3u, 1u, 0u, 0u, 0u};
  const uint32_t               dispatchArgs[3] = {3u, 1u, 1u};
  const GPUFeature             requiredFeatures[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_INDIRECT_DRAW
  };
  const GPUFeature             optionalFeatures[] = {
    GPU_FEATURE_PIPELINE_STATISTICS
  };
  const GPUBindGroupLayoutEntry *layoutEntries;
  uint64_t               artifactSize;
  uint32_t               adapterCount;
  uint32_t               layoutEntryCount;
  bool                   pipelineStatsEnabled;
  int                    ok;

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-compute-buffer-dx12-usl [artifact.us]\n");
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
  queryBuffer    = NULL;
  bindGroup      = NULL;
  querySet       = NULL;
  cmdb           = NULL;
  pass           = NULL;
  fence          = NULL;
  artifactSize         = 0u;
  artifactPath         = argc == 2 ? argv[1] : "compute_buffer.us";
  artifact             = read_file(artifactPath, &artifactSize);
  pipelineStatsEnabled = false;
  ok                   = 0;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DIRECTX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "Direct3D 12 instance creation failed\n");
    goto cleanup;
  }

  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "Direct3D 12 adapter enumeration failed\n");
    goto cleanup;
  }

  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  deviceInfo.label            = "dx12-compute-device";
  deviceInfo.required.featureCount =
    (uint32_t)(sizeof(requiredFeatures) / sizeof(requiredFeatures[0]));
  deviceInfo.required.pFeatures    = requiredFeatures;
  deviceInfo.optional.featureCount =
    (uint32_t)(sizeof(optionalFeatures) / sizeof(optionalFeatures[0]));
  deviceInfo.optional.pFeatures    = optionalFeatures;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK) {
    device = NULL;
  }
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "Direct3D 12 compute queue creation failed\n");
    goto cleanup;
  }
  pipelineStatsEnabled = GPUIsFeatureEnabled(
    device,
    GPU_FEATURE_PIPELINE_STATISTICS
  );

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
    fprintf(stderr, "Direct3D 12 USL shader layout creation failed\n");
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
    fprintf(stderr, "Unexpected Direct3D 12 compute reflection layout\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-usl-fill-vertices";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "fill_vertices";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "Direct3D 12 compute pipeline creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "dx12-compute-vertices";
  bufferInfo.sizeBytes        = sizeof(vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          vertices,
                          sizeof(vertices)) != GPU_OK) {
    fprintf(stderr, "Direct3D 12 storage buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "dx12-compute-draw-args";
  bufferInfo.sizeBytes = sizeof(drawArgs);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_INDIRECT |
                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &indirectBuffer) != GPU_OK ||
      !indirectBuffer) {
    fprintf(stderr, "Direct3D 12 indirect buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "dx12-compute-dispatch-args";
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
    fprintf(stderr, "Direct3D 12 dispatch buffer creation failed\n");
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
  groupInfo.label            = "dx12-compute-group1";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "Direct3D 12 compute bind group creation failed\n");
    goto cleanup;
  }

  if (pipelineStatsEnabled) {
    queryInfo.chain.sType       = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
    queryInfo.chain.structSize  = sizeof(queryInfo);
    queryInfo.label             = "dx12-compute-pipeline-stats";
    queryInfo.type              = GPU_QUERY_PIPELINE_STATISTICS;
    queryInfo.count             = 1u;
    queryInfo.pipelineStatsMask = GPU_PIPESTAT_COMPUTE_SHADER_INVOCATIONS;
    if (GPUCreateQuerySet(device, &queryInfo, &querySet) != GPU_OK ||
        !querySet) {
      fprintf(stderr,
              "Direct3D 12 pipeline statistics query creation failed\n");
      goto cleanup;
    }

    bufferInfo.label     = "dx12-compute-pipeline-stats-result";
    bufferInfo.sizeBytes = sizeof(pipelineStats);
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_SRC |
                           GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &queryBuffer) != GPU_OK ||
        !queryBuffer) {
      fprintf(stderr,
              "Direct3D 12 pipeline statistics buffer creation failed\n");
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue, "dx12-compute", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "Direct3D 12 compute command encoding failed\n");
    goto cleanup;
  }
  if (pipelineStatsEnabled) {
    GPUBeginPipelineStatisticsQuery(cmdb, querySet, 0u);
  }
  pass = GPUBeginComputePass(cmdb, "fill-vertices");
  if (!pass) {
    if (pipelineStatsEnabled) {
      GPUEndPipelineStatisticsQuery(cmdb, querySet);
    }
    fprintf(stderr, "Direct3D 12 compute pass creation failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 1u, bindGroup, 0u, NULL);
  GPUDispatchIndirect(pass, dispatchBuffer, 0u);
  GPUEndComputePass(pass);
  pass = NULL;
  if (pipelineStatsEnabled) {
    GPUEndPipelineStatisticsQuery(cmdb, querySet);
    GPUResolveQuerySet(cmdb, querySet, 0u, 1u, queryBuffer, 0u);
  }

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
    fprintf(stderr, "Direct3D 12 compute fence creation failed\n");
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
    fprintf(stderr, "Direct3D 12 compute readback validation failed\n");
    goto cleanup;
  }
  if (pipelineStatsEnabled) {
    GPUResult queryReadResult;

    queryReadResult = GPUQueueReadBuffer(queue,
                                         queryBuffer,
                                         0u,
                                         &pipelineStats,
                                         sizeof(pipelineStats));
    if (queryReadResult != GPU_OK ||
        pipelineStats.computeShaderInvocations < 3u) {
      fprintf(stderr,
              "Direct3D 12 pipeline statistics validation failed "
              "(read=%d, CSInvocations=%llu)\n",
              (int)queryReadResult,
              (unsigned long long)pipelineStats.computeShaderInvocations);
      goto cleanup;
    }
  } else {
    puts("Direct3D 12 pipeline statistics unavailable; skipped");
  }

  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(queryBuffer);
  GPUDestroyQuerySet(querySet);
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
  puts("Direct3D 12 USL compute buffer validation passed");
  return 0;
}
