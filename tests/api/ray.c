#include "test.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/ray_internal.h"

enum {
  GPU_RAY_PIPELINE_WARM_ITERATIONS = 8u
};

static uint64_t
ray_max_u64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

static int
ray_dispatch_limits(void) {
  static const uint32_t maxSize[3] = {1024u, 1024u, 64u};

  return gpuRayDispatchFits(1024u, 1024u, 1u, maxSize, 1u << 20u) &&
         !gpuRayDispatchFits(1025u, 1u, 1u, maxSize, 1u << 20u) &&
         !gpuRayDispatchFits(1024u, 1024u, 2u, maxSize, 1u << 20u) &&
         !gpuRayDispatchFits(0u, 1u, 1u, maxSize, 1u << 20u) &&
         gpuRayDispatchFits(1024u, 1024u, 1024u, NULL, 1ull << 30u) &&
         !gpuRayDispatchFits(1024u, 1024u, 1025u, NULL, 1ull << 30u);
}

static int
ray_create_buffer(GPUDevice           *device,
                  const char          *label,
                  uint64_t             sizeBytes,
                  GPUBufferUsageFlags  usage,
                  GPUBuffer          **outBuffer) {
  GPUBufferCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.sizeBytes        = sizeBytes;
  info.usage            = usage;
  return GPUCreateBuffer(device, &info, outBuffer) == GPU_OK && *outBuffer;
}

int
gpu_test_ray_pipeline_feature(GPUAdapter *adapter,
                              const char *bytecodePath) {
  static const float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.0f,  0.5f, 0.0f
  };
  static const float identity[3][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}
  };
  static const char * const entries[] = {
    "GPUCreateRayTracingPipelineEXT",
    "GPUDestroyRayTracingPipelineEXT",
    "GPUCreateShaderTableEXT",
    "GPUDestroyShaderTableEXT",
    "GPUBeginRayTracingPassEXT",
    "GPUBindRayTracingPipelineEXT",
    "GPUBindRayTracingGroupEXT",
    "GPUDispatchRaysEXT",
    "GPUEndRayTracingPassEXT"
  };
  GPUDeviceCreateInfo                          deviceInfo      = {0};
  GPUAccelerationStructureTriangleGeometryEXT geometry        = {0};
  GPUAccelerationStructureBuildInfoEXT         blasBuild       = {0};
  GPUAccelerationStructureBuildInfoEXT         tlasBuild       = {0};
  GPUAccelerationStructureSizesEXT             blasSizes       = {0};
  GPUAccelerationStructureSizesEXT             tlasSizes       = {0};
  GPUAccelerationStructureCreateInfoEXT        structureInfo   = {0};
  GPUAccelerationStructureInstanceEXT          instance        = {0};
  GPUBindGroupEntry                            groupEntries[2]  = {0};
  GPUBindGroupCreateInfo                       groupInfo        = {0};
  GPUQueueSubmitInfo                           submitInfo       = {0};
  GPUShaderReflection                          reflection       = {0};
  GPURayTracingShaderGroupEXT                  groups[7]        = {0};
  GPURayTracingPipelineCreateInfoEXT           pipelineInfo     = {0};
  GPUShaderTableRecordEXT                      raygenRecord     = {0};
  GPUShaderTableRecordEXT                      missRecord       = {0};
  GPUShaderTableRecordEXT                      hitRecords[2]    = {0};
  GPUShaderTableRecordEXT                      callableRecord   = {0};
  GPUShaderTableCreateInfoEXT                  tableInfo        = {0};
  GPUDevice                               *disabled;
  GPUDevice                               *enabled;
  GPUQueue                                *queue;
  GPUShaderLibrary                        *library;
  GPUBindGroupLayout                      *groupLayout;
  GPUPipelineLayout                       *layout;
  GPUBuffer                               *vertexBuffer;
  GPUBuffer                               *scratchBuffer;
  GPUBuffer                               *outputBuffer;
  GPUAccelerationStructureEXT             *blas;
  GPUAccelerationStructureEXT             *tlas;
  GPUBindGroup                            *group;
  GPURayTracingPipelineEXT                *pipeline;
  GPUShaderTableEXT                       *table;
  GPUCommandBuffer                        *cmdb;
  GPUAccelerationStructurePassEncoderEXT *buildPass;
  GPURayTracingPassEncoderEXT             *rayPass;
  GPUFence                                *fence;
  void                                    *bytecode;
  uint64_t                                 bytecodeSize;
  uint64_t                                 scratchSize;
  uint32_t                                 layoutCount;
  uint32_t                                 resultValues[2];
  GPUFeature                               feature;
  GPUResult                                result;
  bool                                     sawScene;
  bool                                     sawOutput;
  int                                      ok;

  if (!adapter) {
    return 0;
  }
  if (!ray_dispatch_limits()) {
    fprintf(stderr, "ray dispatch limit validation failed\n");
    return 0;
  }

  disabled      = NULL;
  enabled       = NULL;
  queue         = NULL;
  library       = NULL;
  groupLayout   = NULL;
  layout        = NULL;
  vertexBuffer  = NULL;
  scratchBuffer = NULL;
  outputBuffer  = NULL;
  blas          = NULL;
  tlas          = NULL;
  group         = NULL;
  pipeline      = NULL;
  table         = NULL;
  cmdb          = NULL;
  buildPass     = NULL;
  rayPass       = NULL;
  fence         = NULL;
  bytecode      = NULL;
  bytecodeSize  = 0u;
  scratchSize   = 0u;
  layoutCount   = 0u;
  sawScene      = false;
  sawOutput     = false;
  ok            = 0;
  feature       = GPU_FEATURE_RAY_TRACING_PIPELINE;

  resultValues[0] = 0u;
  resultValues[1] = 0u;
  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  if (!GPUIsFeatureSupported(adapter, feature)) {
    deviceInfo.required.featureCount = 1u;
    deviceInfo.required.pFeatures    = &feature;
    result = GPUCreateDevice(adapter, &deviceInfo, &enabled);
    if (result != GPU_ERROR_UNSUPPORTED || enabled) {
      fprintf(stderr, "unsupported ray pipeline feature was enabled\n");
      GPUDestroyDevice(enabled);
      return 0;
    }
    puts("ray-pipeline execution skipped: unsupported adapter");
    return 1;
  }

  if (GPUCreateDevice(adapter, &deviceInfo, &disabled) != GPU_OK ||
      !disabled) {
    fprintf(stderr, "ray pipeline disabled-device creation failed\n");
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (GPUGetProcAddr(disabled, entries[i])) {
      fprintf(stderr,
              "ray pipeline entry enabled by default: %s\n",
              entries[i]);
      GPUDestroyDevice(disabled);
      return 0;
    }
  }
  GPUDestroyDevice(disabled);

  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(adapter, &deviceInfo, &enabled) != GPU_OK || !enabled ||
      !GPUIsFeatureEnabled(enabled, feature) ||
      !GPUIsFeatureEnabled(enabled, GPU_FEATURE_RAY_QUERY)) {
    fprintf(stderr, "ray pipeline feature enablement failed\n");
    GPUDestroyDevice(enabled);
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (!GPUGetProcAddr(enabled, entries[i])) {
      fprintf(stderr, "ray pipeline entry unavailable: %s\n", entries[i]);
      GPUDestroyDevice(enabled);
      return 0;
    }
  }
  if (!GPUGetProcAddr(enabled, "GPUBuildAccelerationStructureEXT")) {
    fprintf(stderr, "ray pipeline did not enable ray-query dependency\n");
    goto cleanup;
  }
  enabled->runtimeConfig.enableStats = true;
  queue = GPUGetQueue(enabled, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "ray pipeline queue unavailable\n");
    goto cleanup;
  }
  if (!bytecodePath) {
    puts("ray-pipeline execution skipped: fixture unavailable");
    ok = 1;
    goto cleanup;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(enabled,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUGetShaderReflection(library, &reflection) != GPU_OK) {
    fprintf(stderr, "ray pipeline USL compile/reflection failed\n");
    goto cleanup;
  }
  if (reflection.resourceCount != 2u) {
    fprintf(stderr,
            "ray pipeline reflection resource mismatch: %u\n",
            reflection.resourceCount);
    goto cleanup;
  }
  for (uint32_t i = 0u; i < reflection.resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection.pResources[i];
    if (resource->groupIndex != 0u ||
        resource->visibility != GPU_SHADER_STAGE_RAY_GENERATION_BIT) {
      continue;
    }
    if (resource->binding == 0u &&
        resource->bindingType == GPU_BINDING_ACCELERATION_STRUCTURE) {
      sawScene = true;
    } else if (resource->binding == 1u &&
               resource->bindingType == GPU_BINDING_STORAGE_BUFFER) {
      sawOutput = true;
    }
  }
  if (!sawScene || !sawOutput) {
    fprintf(stderr, "ray pipeline reflection binding mismatch\n");
    goto cleanup;
  }
  layoutCount = 1u;
  if (GPUCreateBindGroupLayoutsFromReflection(enabled,
                                               library,
                                               &layoutCount,
                                               &groupLayout) != GPU_OK ||
      layoutCount != 1u || !groupLayout ||
      GPUCreatePipelineLayoutFromReflection(enabled,
                                            library,
                                            layoutCount,
                                            &groupLayout,
                                            &layout) != GPU_OK ||
      !layout) {
    fprintf(stderr, "ray pipeline reflected layout creation failed\n");
    goto cleanup;
  }
  if (!ray_create_buffer(
        enabled,
        "ray-pipeline-vertices",
        sizeof(vertices),
        GPU_BUFFER_USAGE_COPY_DST |
          GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT,
        &vertexBuffer) ||
      GPUQueueWriteBuffer(queue,
                          vertexBuffer,
                          0u,
                          vertices,
                          sizeof(vertices)) != GPU_OK) {
    fprintf(stderr, "ray pipeline vertex setup failed\n");
    goto cleanup;
  }

  geometry.vertexBuffer = vertexBuffer;
  geometry.vertexCount  = 3u;
  geometry.vertexStride = sizeof(float) * 3u;
  geometry.vertexFormat = GPU_VERTEX_FORMAT_FLOAT32X3;
  geometry.flags        =
    GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT;
  blasBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  blasBuild.chain.structSize          = sizeof(blasBuild);
  blasBuild.label                     = "ray-pipeline-blas";
  blasBuild.type                      =
    GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  blasBuild.mode                      = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  blasBuild.bottomLevel.pGeometries   = &geometry;
  blasBuild.bottomLevel.geometryCount = 1u;
  if (GPUGetAccelerationStructureSizesEXT(enabled,
                                          &blasBuild,
                                          &blasSizes) != GPU_OK) {
    fprintf(stderr, "ray pipeline BLAS size query failed\n");
    goto cleanup;
  }

  structureInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_EXT;
  structureInfo.chain.structSize = sizeof(structureInfo);
  structureInfo.label            = "ray-pipeline-blas";
  structureInfo.type             =
    GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  structureInfo.sizeBytes        = blasSizes.accelerationStructureSize;
  if (GPUCreateAccelerationStructureEXT(enabled,
                                        &structureInfo,
                                        &blas) != GPU_OK || !blas) {
    fprintf(stderr, "ray pipeline BLAS create failed\n");
    goto cleanup;
  }

  instance.structure = blas;
  instance.flags     = GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT;
  memcpy(instance.transform, identity, sizeof(identity));
  tlasBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  tlasBuild.chain.structSize        = sizeof(tlasBuild);
  tlasBuild.label                   = "ray-pipeline-tlas";
  tlasBuild.type                    =
    GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT;
  tlasBuild.mode                    = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  tlasBuild.topLevel.pInstances     = &instance;
  tlasBuild.topLevel.instanceCount  = 1u;
  if (GPUGetAccelerationStructureSizesEXT(enabled,
                                          &tlasBuild,
                                          &tlasSizes) != GPU_OK) {
    fprintf(stderr, "ray pipeline TLAS size query failed\n");
    goto cleanup;
  }

  structureInfo.label     = "ray-pipeline-tlas";
  structureInfo.type      = GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT;
  structureInfo.sizeBytes = tlasSizes.accelerationStructureSize;
  if (GPUCreateAccelerationStructureEXT(enabled,
                                        &structureInfo,
                                        &tlas) != GPU_OK || !tlas) {
    fprintf(stderr, "ray pipeline TLAS create failed\n");
    goto cleanup;
  }

  scratchSize = ray_max_u64(blasSizes.buildScratchSize,
                            tlasSizes.buildScratchSize);
  if (!ray_create_buffer(
        enabled,
        "ray-pipeline-scratch",
        scratchSize,
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT,
        &scratchBuffer) ||
      !ray_create_buffer(enabled,
                         "ray-pipeline-output",
                         sizeof(resultValues),
                         GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_SRC |
                           GPU_BUFFER_USAGE_COPY_DST,
                         &outputBuffer) ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          resultValues,
                          sizeof(resultValues)) != GPU_OK) {
    fprintf(stderr, "ray pipeline scratch/output setup failed\n");
    goto cleanup;
  }

  groupEntries[0].binding               = 0u;
  groupEntries[0].bindingType           = GPU_BINDING_ACCELERATION_STRUCTURE;
  groupEntries[0].accelerationStructure = tlas;
  groupEntries[1].buffer.buffer         = outputBuffer;
  groupEntries[1].buffer.size           = sizeof(resultValues);
  groupEntries[1].binding               = 1u;
  groupEntries[1].bindingType           = GPU_BINDING_STORAGE_BUFFER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "ray-pipeline-group";
  groupInfo.layout           = groupLayout;
  groupInfo.pEntries         = groupEntries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(groupEntries);
  if (GPUCreateBindGroup(enabled, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "ray pipeline bind group failed\n");
    goto cleanup;
  }

  groups[0].generalEntry    = "raygen_main";
  groups[0].type            = GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT;
  groups[1].generalEntry    = "miss_main";
  groups[1].type            = GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT;
  groups[2].closestHitEntry = "closest_hit_main";
  groups[2].anyHitEntry     = "any_hit_main";
  groups[2].type            = GPU_RAY_TRACING_SHADER_GROUP_TRIANGLES_HIT_EXT;
  groups[3].intersectionEntry = "intersection_main";
  groups[3].type              = GPU_RAY_TRACING_SHADER_GROUP_PROCEDURAL_HIT_EXT;
  groups[4].generalEntry      = "callable_main";
  groups[4].type              = GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT;
  groups[5].generalEntry      = "raygen_large";
  groups[5].type              = GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT;
  groups[6].intersectionEntry = "intersection_large";
  groups[6].type              = GPU_RAY_TRACING_SHADER_GROUP_PROCEDURAL_HIT_EXT;

  pipelineInfo.chain.sType =
    GPU_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_EXT;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label             = "ray-pipeline-reflection-limits";
  pipelineInfo.library           = library;
  pipelineInfo.layout            = layout;
  pipelineInfo.pGroups           = groups;
  pipelineInfo.groupCount        = GPU_ARRAY_LEN(groups);
  pipelineInfo.maxRecursionDepth = 1u;

  pipelineInfo.maxPayloadSizeBytes      = 1u;
  pipelineInfo.maxHitAttributeSizeBytes = 1u;
  result = GPUCreateRayTracingPipelineEXT(enabled,
                                          &pipelineInfo,
                                          &pipeline);
  if (result != GPU_ERROR_INVALID_ARGUMENT || pipeline) {
    fprintf(stderr, "ray pipeline accepted undersized interfaces\n");
    goto cleanup;
  }

  pipelineInfo.maxPayloadSizeBytes      = sizeof(float);
  pipelineInfo.maxHitAttributeSizeBytes = sizeof(float) * 2u;
  result = GPUCreateRayTracingPipelineEXT(enabled,
                                          &pipelineInfo,
                                          &pipeline);
  if (result != GPU_ERROR_INVALID_ARGUMENT || pipeline) {
    fprintf(stderr, "ray pipeline ignored wider reflected interfaces\n");
    goto cleanup;
  }

  pipelineInfo.maxPayloadSizeBytes      = 0u;
  pipelineInfo.maxHitAttributeSizeBytes = 0u;
  if (GPUCreateRayTracingPipelineEXT(enabled,
                                     &pipelineInfo,
                                     &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "ray pipeline reflection-limit inference failed\n");
    goto cleanup;
  }
  if (pipeline->maxPayloadSizeBytes != sizeof(float) * 4u ||
      pipeline->maxHitAttributeSizeBytes != sizeof(float) * 4u) {
    fprintf(stderr,
            "ray pipeline reflection-limit mismatch: %u/%u\n",
            pipeline->maxPayloadSizeBytes,
            pipeline->maxHitAttributeSizeBytes);
    goto cleanup;
  }

  raygenRecord.groupIndex   = 0u;
  missRecord.groupIndex     = 1u;
  hitRecords[0].groupIndex  = 2u;
  hitRecords[1].groupIndex  = 3u;
  callableRecord.groupIndex = 4u;
  tableInfo.chain.sType           = GPU_STRUCTURE_TYPE_SHADER_TABLE_CREATE_INFO_EXT;
  tableInfo.chain.structSize      = sizeof(tableInfo);
  tableInfo.label                 = "ray-pipeline-table";
  tableInfo.pipeline              = pipeline;
  tableInfo.pRayGenerationRecord  = &raygenRecord;
  tableInfo.pMissRecords          = &missRecord;
  tableInfo.pHitGroupRecords      = hitRecords;
  tableInfo.pCallableRecords      = &callableRecord;
  tableInfo.missRecordCount       = 1u;
  tableInfo.hitGroupRecordCount   = GPU_ARRAY_LEN(hitRecords);
  tableInfo.callableRecordCount   = 1u;

  raygenRecord.groupIndex = 1u;
  result = GPUCreateShaderTableEXT(enabled, &tableInfo, &table);
  if (result != GPU_ERROR_INVALID_ARGUMENT || table) {
    fprintf(stderr, "ray table accepted a miss record as ray generation\n");
    goto cleanup;
  }
  raygenRecord.groupIndex = 0u;

  missRecord.groupIndex = 0u;
  result = GPUCreateShaderTableEXT(enabled, &tableInfo, &table);
  if (result != GPU_ERROR_INVALID_ARGUMENT || table) {
    fprintf(stderr, "ray table accepted a ray-generation record as miss\n");
    goto cleanup;
  }
  missRecord.groupIndex = 1u;

  hitRecords[0].groupIndex = 1u;
  result = GPUCreateShaderTableEXT(enabled, &tableInfo, &table);
  if (result != GPU_ERROR_INVALID_ARGUMENT || table) {
    fprintf(stderr, "ray table accepted a general group as hit group\n");
    goto cleanup;
  }
  hitRecords[0].groupIndex = 2u;

  callableRecord.groupIndex = 1u;
  result = GPUCreateShaderTableEXT(enabled, &tableInfo, &table);
  if (result != GPU_ERROR_INVALID_ARGUMENT || table) {
    fprintf(stderr, "ray table accepted a miss record as callable\n");
    goto cleanup;
  }
  callableRecord.groupIndex = 4u;

  if (GPUCreateShaderTableEXT(enabled, &tableInfo, &table) != GPU_OK ||
      !table) {
    fprintf(stderr, "ray pipeline shader table creation failed\n");
    goto cleanup;
  }

  result = GPUAcquireCommandBuffer(queue, "ray-pipeline", &cmdb);
  if (result != GPU_OK || !cmdb ||
      !(buildPass = GPUBeginAccelerationStructurePassEXT(
          cmdb,
          "ray-pipeline-build")) ||
      GPUBuildAccelerationStructureEXT(buildPass,
                                       blas,
                                       &blasBuild,
                                       scratchBuffer,
                                       0u) != GPU_OK ||
      GPUBuildAccelerationStructureEXT(buildPass,
                                       tlas,
                                       &tlasBuild,
                                       scratchBuffer,
                                       0u) != GPU_OK) {
    fprintf(stderr, "ray pipeline build encoding failed\n");
    goto cleanup;
  }
  GPUEndAccelerationStructurePassEXT(buildPass);
  buildPass = NULL;

  rayPass = GPUBeginRayTracingPassEXT(cmdb, "ray-pipeline-dispatch");
  if (!rayPass) {
    fprintf(stderr, "ray pipeline pass creation failed\n");
    goto cleanup;
  }
  GPUBindRayTracingPipelineEXT(rayPass, pipeline);
  GPUBindRayTracingGroupEXT(rayPass, 0u, group, 0u, NULL);
  GPUDispatchRaysEXT(rayPass, table, GPU_ARRAY_LEN(resultValues), 1u, 1u);
  GPUEndRayTracingPassEXT(rayPass);
  rayPass = NULL;

  if (GPUCreateFence(enabled, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "ray pipeline fence create failed\n");
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  submitInfo.commandBufferCount = 1u;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "ray pipeline submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  GPUResetStats(enabled);
  for (uint32_t i = 0u; i < GPU_RAY_PIPELINE_WARM_ITERATIONS; i++) {
    if (GPUAcquireCommandBuffer(queue, "ray-pipeline-warm", &cmdb) != GPU_OK ||
        !cmdb ||
        !(rayPass = GPUBeginRayTracingPassEXT(cmdb, "ray-pipeline-warm"))) {
      fprintf(stderr, "ray pipeline warm pass creation failed\n");
      goto cleanup;
    }
    GPUBindRayTracingPipelineEXT(rayPass, pipeline);
    GPUBindRayTracingPipelineEXT(rayPass, pipeline);
    GPUBindRayTracingGroupEXT(rayPass, 0u, group, 0u, NULL);
    GPUBindRayTracingGroupEXT(rayPass, 0u, group, 0u, NULL);
    GPUDispatchRaysEXT(rayPass,
                       table,
                       GPU_ARRAY_LEN(resultValues),
                       1u,
                       1u);
    GPUEndRayTracingPassEXT(rayPass);
    rayPass = NULL;

    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      cmdb = NULL;
      fprintf(stderr, "ray pipeline warm submission failed\n");
      goto cleanup;
    }
    cmdb = NULL;
  }
  if (enabled->currentFrameStats.hotPathAllocCount != 0u ||
      enabled->currentFrameStats.hotPathAllocBytes != 0u ||
      enabled->currentFrameStats.hotPathFreeCount != 0u ||
      enabled->currentFrameStats.hotPathFreeBytes != 0u ||
      enabled->currentFrameStats.requestedBindCalls !=
        GPU_RAY_PIPELINE_WARM_ITERATIONS * 4u ||
      enabled->currentFrameStats.emittedBindCalls !=
        GPU_RAY_PIPELINE_WARM_ITERATIONS * 2u) {
    fprintf(stderr,
            "ray pipeline warm path mismatch: %llu allocations, "
            "%llu frees, binds %u/%u\n",
            (unsigned long long)
              enabled->currentFrameStats.hotPathAllocCount,
            (unsigned long long)
              enabled->currentFrameStats.hotPathFreeCount,
            enabled->currentFrameStats.requestedBindCalls,
            enabled->currentFrameStats.emittedBindCalls);
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         resultValues,
                         sizeof(resultValues)) != GPU_OK ||
      resultValues[0] != 13u || resultValues[1] != 22u) {
    fprintf(stderr,
            "ray pipeline traversal mismatch: %u, %u\n",
            resultValues[0],
            resultValues[1]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (rayPass) GPUEndRayTracingPassEXT(rayPass);
  if (buildPass) GPUEndAccelerationStructurePassEXT(buildPass);
  GPUDestroyFence(fence);
  GPUDestroyShaderTableEXT(table);
  GPUDestroyRayTracingPipelineEXT(pipeline);
  GPUDestroyBindGroup(group);
  GPUDestroyAccelerationStructureEXT(tlas);
  GPUDestroyAccelerationStructureEXT(blas);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(scratchBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyPipelineLayout(layout);
  GPUDestroyBindGroupLayout(groupLayout);
  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  GPUDestroyDevice(enabled);
  return ok;
}

int
gpu_test_ray_query(GPUAdapter *adapter, const char *bytecodePath) {
  static const float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.0f,  0.5f, 0.0f
  };
  static const float identity[3][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}
  };
  GPUDeviceCreateInfo                          deviceInfo       = {0};
  GPUComputePipelineCreateInfo                 pipelineInfo     = {0};
  GPUAccelerationStructureTriangleGeometryEXT geometry         = {0};
  GPUAccelerationStructureBuildInfoEXT         blasBuild        = {0};
  GPUAccelerationStructureBuildInfoEXT         tlasBuild        = {0};
  GPUAccelerationStructureSizesEXT             blasSizes        = {0};
  GPUAccelerationStructureSizesEXT             tlasSizes        = {0};
  GPUAccelerationStructureCreateInfoEXT        structureInfo    = {0};
  GPUAccelerationStructureInstanceEXT          instance         = {0};
  GPUBindGroupEntry                            groupEntries[2]   = {0};
  GPUBindGroupCreateInfo                       groupInfo         = {0};
  GPUQueueSubmitInfo                           submitInfo        = {0};
  GPUShaderReflection                          reflection        = {0};
  GPUFeature                                   feature;
  GPUDevice                                   *device;
  GPUQueue                                    *queue;
  GPUShaderLibrary                            *library;
  GPUBindGroupLayout                          *groupLayout;
  GPUPipelineLayout                           *pipelineLayout;
  GPUComputePipeline                          *pipeline;
  GPUBuffer                                   *vertexBuffer;
  GPUBuffer                                   *scratchBuffer;
  GPUBuffer                                   *outputBuffer;
  GPUAccelerationStructureEXT                 *blas;
  GPUAccelerationStructureEXT                 *tlas;
  GPUBindGroup                                *group;
  GPUCommandBuffer                            *cmdb;
  GPUAccelerationStructurePassEncoderEXT      *buildPass;
  GPUComputePassEncoder                       *computePass;
  GPUFence                                    *fence;
  void                                        *bytecode;
  uint64_t                                     bytecodeSize;
  uint64_t                                     scratchSize;
  uint32_t                                     layoutCount;
  uint32_t                                     resultValue;
  bool                                         sawScene;
  bool                                         sawResult;
  int                                          ok;

  if (!adapter) {
    return 0;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_RAY_QUERY)) {
    puts("ray-query execution skipped: unsupported adapter");
    return 1;
  }
  if (!bytecodePath) {
    puts("ray-query execution skipped: fixture unavailable");
    return 1;
  }

  feature         = GPU_FEATURE_RAY_QUERY;
  device          = NULL;
  queue           = NULL;
  library         = NULL;
  groupLayout     = NULL;
  pipelineLayout  = NULL;
  pipeline        = NULL;
  vertexBuffer    = NULL;
  scratchBuffer   = NULL;
  outputBuffer    = NULL;
  blas            = NULL;
  tlas            = NULL;
  group           = NULL;
  cmdb            = NULL;
  buildPass       = NULL;
  computePass     = NULL;
  fence           = NULL;
  bytecode        = NULL;
  bytecodeSize    = 0u;
  resultValue     = 0u;
  sawScene        = false;
  sawResult       = false;
  ok              = 0;

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUGetProcAddr(device, "GPUBuildAccelerationStructureEXT")) {
    fprintf(stderr, "ray-query feature enablement failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "ray-query queue unavailable\n");
    goto cleanup;
  }

  if (!ray_create_buffer(
        device,
        "ray-query-vertices",
        sizeof(vertices),
        GPU_BUFFER_USAGE_COPY_DST |
          GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT,
        &vertexBuffer) ||
      GPUQueueWriteBuffer(queue,
                          vertexBuffer,
                          0u,
                          vertices,
                          sizeof(vertices)) != GPU_OK) {
    fprintf(stderr, "ray-query vertex buffer setup failed\n");
    goto cleanup;
  }

  geometry.vertexBuffer = vertexBuffer;
  geometry.vertexCount  = 3u;
  geometry.vertexStride = sizeof(float) * 3u;
  geometry.vertexFormat = GPU_VERTEX_FORMAT_FLOAT32X3;
  blasBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  blasBuild.chain.structSize            = sizeof(blasBuild);
  blasBuild.label                       = "ray-query-blas";
  blasBuild.type                        =
    GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  blasBuild.mode                        = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  blasBuild.bottomLevel.pGeometries     = &geometry;
  blasBuild.bottomLevel.geometryCount   = 1u;
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &blasBuild,
                                          &blasSizes) != GPU_OK) {
    fprintf(stderr, "ray-query BLAS size query failed\n");
    goto cleanup;
  }

  structureInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_EXT;
  structureInfo.chain.structSize = sizeof(structureInfo);
  structureInfo.label            = "ray-query-blas";
  structureInfo.type             = GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  structureInfo.sizeBytes        = blasSizes.accelerationStructureSize;
  if (GPUCreateAccelerationStructureEXT(device,
                                        &structureInfo,
                                        &blas) != GPU_OK || !blas) {
    fprintf(stderr, "ray-query BLAS create failed\n");
    goto cleanup;
  }

  instance.structure = blas;
  memcpy(instance.transform, identity, sizeof(identity));
  instance.flags = GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT;
  tlasBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  tlasBuild.chain.structSize          = sizeof(tlasBuild);
  tlasBuild.label                     = "ray-query-tlas";
  tlasBuild.type                      = GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT;
  tlasBuild.mode                      = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  tlasBuild.topLevel.pInstances       = &instance;
  tlasBuild.topLevel.instanceCount    = 1u;
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &tlasBuild,
                                          &tlasSizes) != GPU_OK) {
    fprintf(stderr, "ray-query TLAS size query failed\n");
    goto cleanup;
  }

  structureInfo.label     = "ray-query-tlas";
  structureInfo.type      = GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT;
  structureInfo.sizeBytes = tlasSizes.accelerationStructureSize;
  if (GPUCreateAccelerationStructureEXT(device,
                                        &structureInfo,
                                        &tlas) != GPU_OK || !tlas) {
    fprintf(stderr, "ray-query TLAS create failed\n");
    goto cleanup;
  }

  scratchSize = ray_max_u64(blasSizes.buildScratchSize,
                            tlasSizes.buildScratchSize);
  if (!ray_create_buffer(
        device,
        "ray-query-scratch",
        scratchSize,
        GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_SCRATCH_EXT,
        &scratchBuffer) ||
      !ray_create_buffer(device,
                         "ray-query-result",
                         sizeof(resultValue),
                         GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_SRC |
                           GPU_BUFFER_USAGE_COPY_DST,
                         &outputBuffer) ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          &resultValue,
                          sizeof(resultValue)) != GPU_OK) {
    fprintf(stderr, "ray-query scratch/output setup failed\n");
    goto cleanup;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUGetShaderReflection(library, &reflection) != GPU_OK) {
    fprintf(stderr, "ray-query USL compile/reflection failed\n");
    goto cleanup;
  }
  if (reflection.resourceCount != 2u) {
    fprintf(stderr,
            "ray-query reflection resource mismatch: %u\n",
            reflection.resourceCount);
    goto cleanup;
  }
  for (uint32_t i = 0u; i < reflection.resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection.pResources[i];
    if (resource->groupIndex != 0u ||
        resource->visibility != GPU_SHADER_STAGE_COMPUTE_BIT) {
      continue;
    }
    if (resource->binding == 0u &&
        resource->bindingType == GPU_BINDING_ACCELERATION_STRUCTURE) {
      sawScene = true;
    } else if (resource->binding == 1u &&
               resource->bindingType == GPU_BINDING_STORAGE_BUFFER) {
      sawResult = true;
    }
  }
  if (!sawScene || !sawResult) {
    fprintf(stderr, "ray-query reflection binding mismatch\n");
    goto cleanup;
  }
  layoutCount = 1u;
  if (GPUCreateBindGroupLayoutsFromReflection(device,
                                               library,
                                               &layoutCount,
                                               &groupLayout) != GPU_OK ||
      layoutCount != 1u || !groupLayout ||
      GPUCreatePipelineLayoutFromReflection(device,
                                            library,
                                            layoutCount,
                                            &groupLayout,
                                            &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "ray-query reflected layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "ray-query-pipeline";
  pipelineInfo.layout           = pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "ray_query_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "ray-query compute pipeline failed\n");
    goto cleanup;
  }

  groupEntries[0].binding               = 0u;
  groupEntries[0].bindingType           = GPU_BINDING_ACCELERATION_STRUCTURE;
  groupEntries[0].accelerationStructure = tlas;
  groupEntries[1].binding               = 1u;
  groupEntries[1].bindingType           = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[1].buffer.buffer         = outputBuffer;
  groupEntries[1].buffer.size           = sizeof(resultValue);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "ray-query-group";
  groupInfo.layout           = groupLayout;
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "ray-query bind group failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "ray-query", &cmdb) != GPU_OK ||
      !cmdb ||
      !(buildPass = GPUBeginAccelerationStructurePassEXT(cmdb,
                                                         "ray-query-build")) ||
      GPUBuildAccelerationStructureEXT(buildPass,
                                       blas,
                                       &blasBuild,
                                       scratchBuffer,
                                       0u) != GPU_OK ||
      GPUBuildAccelerationStructureEXT(buildPass,
                                       tlas,
                                       &tlasBuild,
                                       scratchBuffer,
                                       0u) != GPU_OK) {
    fprintf(stderr, "ray-query build encoding failed\n");
    goto cleanup;
  }
  GPUEndAccelerationStructurePassEXT(buildPass);
  buildPass = NULL;

  computePass = GPUBeginComputePass(cmdb, "ray-query-dispatch");
  if (!computePass) {
    fprintf(stderr, "ray-query compute pass failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "ray-query fence create failed\n");
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "ray-query submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         &resultValue,
                         sizeof(resultValue)) != GPU_OK ||
      resultValue != 1u) {
    fprintf(stderr, "ray-query hit mismatch: %u\n", resultValue);
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (computePass) GPUEndComputePass(computePass);
  if (buildPass) GPUEndAccelerationStructurePassEXT(buildPass);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(groupLayout);
  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  GPUDestroyAccelerationStructureEXT(tlas);
  GPUDestroyAccelerationStructureEXT(blas);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(scratchBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyDevice(device);
  free(bytecode);
  return ok;
}
