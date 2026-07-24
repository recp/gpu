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
  GPUAccelerationStructureGeometryEXT         geometry        = {0};
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
  bool                                     submitAttempted;
  int                                      ok;

  if (!adapter) {
    return 0;
  }
  if (!ray_dispatch_limits()) {
    fprintf(stderr, "ray dispatch limit validation failed\n");
    return 0;
  }

  disabled        = NULL;
  enabled         = NULL;
  queue           = NULL;
  library         = NULL;
  groupLayout     = NULL;
  layout          = NULL;
  vertexBuffer    = NULL;
  scratchBuffer   = NULL;
  outputBuffer    = NULL;
  blas            = NULL;
  tlas            = NULL;
  group           = NULL;
  pipeline        = NULL;
  table           = NULL;
  cmdb            = NULL;
  buildPass       = NULL;
  rayPass         = NULL;
  fence           = NULL;
  bytecode        = NULL;
  bytecodeSize    = 0u;
  scratchSize     = 0u;
  layoutCount     = 0u;
  sawScene        = false;
  sawOutput       = false;
  submitAttempted = false;
  ok              = 0;
  feature         = GPU_FEATURE_RAY_TRACING_PIPELINE;

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

  geometry.type                   =
    GPU_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_EXT;
  geometry.triangles.vertexBuffer = vertexBuffer;
  geometry.triangles.vertexCount  = 3u;
  geometry.triangles.vertexStride = sizeof(float) * 3u;
  geometry.triangles.vertexFormat = GPU_VERTEX_FORMAT_FLOAT32X3;
  geometry.triangles.flags        =
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
  submitAttempted                    = true;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "ray pipeline submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  GPUResetStats(enabled);
  for (uint32_t i = 0u; i < GPU_RAY_PIPELINE_WARM_ITERATIONS; i++) {
    submitAttempted = false;
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

    submitAttempted = true;
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
  if (cmdb && !submitAttempted) GPUDiscardCommandBuffer(cmdb);
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
gpu_test_intersection_function_table(GPUAdapter *adapter,
                                     const char *bytecodePath) {
  static const char * const entries[] = {
    "GPUCreateIntersectionFunctionTableEXT",
    "GPUDestroyIntersectionFunctionTableEXT",
    "GPUSetIntersectionFunctionTableBufferEXT",
    "GPUBindComputeIntersectionFunctionTableEXT",
    "GPUBindRenderIntersectionFunctionTableEXT"
  };
  GPUDeviceCreateInfo                         deviceInfo          = {0};
  GPUIntersectionFunctionEXT                 computeFunction     = {0};
  GPUIntersectionFunctionEXT                 renderFunction      = {0};
  GPUIntersectionFunctionPipelineEXT         computeFunctionInfo = {0};
  GPUIntersectionFunctionPipelineEXT         renderFunctionInfo  = {0};
  GPUComputePipelineCreateInfo                computePipelineInfo = {0};
  GPURenderPipelineCreateInfo                 renderPipelineInfo  = {0};
  GPUIntersectionFunctionTableCreateInfoEXT  computeTableInfo    = {0};
  GPUIntersectionFunctionTableCreateInfoEXT  renderTableInfo     = {0};
  GPUColorTargetState                         colorTarget         = {0};
  GPUBufferCreateInfo                         bufferInfo          = {0};
  GPUTextureCreateInfo                        textureInfo         = {0};
  GPUTextureViewCreateInfo                    viewInfo            = {0};
  GPURenderPassColorAttachment                colorAttachment     = {0};
  GPURenderPassCreateInfo                     renderPassInfo      = {0};
  GPUViewport                                 viewport            = {0};
  GPUScissorRect                              scissor             = {0};
  GPUQueueSubmitInfo                          submitInfo          = {0};
  GPUFeature                                  feature;
  GPUDevice                                  *device;
  GPUQueue                                   *computeQueue;
  GPUQueue                                   *graphicsQueue;
  GPUShaderLibrary                           *library;
  GPUShaderLayout                            *shaderLayout;
  GPUComputePipeline                         *computePipeline;
  GPURenderPipeline                          *renderPipeline;
  GPUIntersectionFunctionTableEXT            *computeTable;
  GPUIntersectionFunctionTableEXT            *renderTable;
  GPUBuffer                                  *buffer;
  GPUTexture                                 *target;
  GPUTextureView                             *targetView;
  GPUCommandBuffer                           *computeCmdb;
  GPUCommandBuffer                           *renderCmdb;
  GPUComputePassEncoder                      *computePass;
  GPURenderPassEncoder                       *renderPass;
  GPUFence                                   *computeFence;
  GPUFence                                   *renderFence;
  void                                       *bytecode;
  uint64_t                                    bytecodeSize;
  bool                                        computeSubmitted;
  bool                                        renderSubmitted;
  int                                         ok;

  if (!adapter) {
    return 0;
  }
  if (!GPUIsFeatureSupported(
        adapter,
        GPU_FEATURE_INTERSECTION_FUNCTION_TABLE
      )) {
    puts("intersection-function-table execution skipped: unsupported adapter");
    return 1;
  }
  if (!bytecodePath) {
    puts("intersection-function-table execution skipped: fixture unavailable");
    return 1;
  }

  feature          = GPU_FEATURE_INTERSECTION_FUNCTION_TABLE;
  device           = NULL;
  computeQueue     = NULL;
  graphicsQueue    = NULL;
  library          = NULL;
  shaderLayout     = NULL;
  computePipeline  = NULL;
  renderPipeline   = NULL;
  computeTable     = NULL;
  renderTable      = NULL;
  buffer           = NULL;
  target           = NULL;
  targetView       = NULL;
  computeCmdb      = NULL;
  renderCmdb       = NULL;
  computePass      = NULL;
  renderPass       = NULL;
  computeFence     = NULL;
  renderFence      = NULL;
  bytecode         = NULL;
  bytecodeSize     = 0u;
  computeSubmitted = false;
  renderSubmitted  = false;
  ok               = 0;

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY)) {
    fprintf(stderr, "intersection-function-table feature enablement failed\n");
    goto cleanup;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (!GPUGetProcAddr(device, entries[i])) {
      fprintf(stderr,
              "intersection-function-table entry unavailable: %s\n",
              entries[i]);
      goto cleanup;
    }
  }
  computeQueue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  graphicsQueue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!computeQueue || !graphicsQueue) {
    fprintf(stderr, "intersection-function-table queues unavailable\n");
    goto cleanup;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "intersection-function-table shader setup failed\n");
    goto cleanup;
  }

  computeFunction.entryPoint      = "constant_hit";
  computeFunction.stage           = GPU_SHADER_STAGE_COMPUTE_BIT;
  computeFunctionInfo.chain.sType =
    GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT;
  computeFunctionInfo.chain.structSize = sizeof(computeFunctionInfo);
  computeFunctionInfo.pFunctions       = &computeFunction;
  computeFunctionInfo.functionCount    = 1u;
  computePipelineInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computePipelineInfo.chain.structSize = sizeof(computePipelineInfo);
  computePipelineInfo.chain.pNext      = &computeFunctionInfo;
  computePipelineInfo.label            = "intersection-function-table-compute";
  computePipelineInfo.layout           = shaderLayout->pipelineLayout;
  computePipelineInfo.library          = library;
  computePipelineInfo.entryPoint       = "intersection_table_cs";
  if (GPUCreateComputePipeline(device,
                               &computePipelineInfo,
                               &computePipeline) != GPU_OK ||
      !computePipeline) {
    fprintf(stderr, "intersection-function-table compute pipeline failed\n");
    goto cleanup;
  }

  renderFunction.entryPoint      = "constant_hit";
  renderFunction.stage           = GPU_SHADER_STAGE_FRAGMENT_BIT;
  renderFunctionInfo.chain.sType =
    GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT;
  renderFunctionInfo.chain.structSize = sizeof(renderFunctionInfo);
  renderFunctionInfo.pFunctions       = &renderFunction;
  renderFunctionInfo.functionCount    = 1u;
  colorTarget.format                  = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask         = GPU_COLOR_WRITE_ALL;
  renderPipelineInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderPipelineInfo.chain.structSize = sizeof(renderPipelineInfo);
  renderPipelineInfo.chain.pNext      = &renderFunctionInfo;
  renderPipelineInfo.label            = "intersection-function-table-render";
  renderPipelineInfo.layout           = shaderLayout->pipelineLayout;
  renderPipelineInfo.library          = library;
  renderPipelineInfo.vertexEntry      = "intersection_table_vs";
  renderPipelineInfo.fragmentEntry    = "intersection_table_fs";
  renderPipelineInfo.pColorTargets    = &colorTarget;
  renderPipelineInfo.colorTargetCount = 1u;
  renderPipelineInfo.primitiveTopology =
    GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderPipelineInfo.cullMode                = GPU_CULL_MODE_NONE;
  renderPipelineInfo.frontFace               = GPU_FRONT_FACE_CCW;
  renderPipelineInfo.multisample.sampleCount = 1u;
  renderPipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device,
                              &renderPipelineInfo,
                              &renderPipeline) != GPU_OK ||
      !renderPipeline) {
    fprintf(stderr, "intersection-function-table render pipeline failed\n");
    goto cleanup;
  }

  computeTableInfo.chain.sType =
    GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_TABLE_CREATE_INFO_EXT;
  computeTableInfo.chain.structSize = sizeof(computeTableInfo);
  computeTableInfo.label            = "intersection-function-table-compute";
  computeTableInfo.computePipeline  = computePipeline;
  computeTableInfo.stage            = GPU_SHADER_STAGE_COMPUTE_BIT;
  if (GPUCreateIntersectionFunctionTableEXT(device,
                                            &computeTableInfo,
                                            &computeTable) !=
        GPU_OK ||
      !computeTable) {
    fprintf(stderr, "intersection-function-table compute table failed\n");
    goto cleanup;
  }

  renderTableInfo.chain.sType =
    GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_TABLE_CREATE_INFO_EXT;
  renderTableInfo.chain.structSize = sizeof(renderTableInfo);
  renderTableInfo.label            = "intersection-function-table-render";
  renderTableInfo.renderPipeline   = renderPipeline;
  renderTableInfo.stage            = GPU_SHADER_STAGE_FRAGMENT_BIT;
  if (GPUCreateIntersectionFunctionTableEXT(device,
                                            &renderTableInfo,
                                            &renderTable) != GPU_OK ||
      !renderTable) {
    fprintf(stderr, "intersection-function-table render table failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "intersection-function-table-buffer";
  bufferInfo.sizeBytes        = 256u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUSetIntersectionFunctionTableBufferEXT(computeTable,
                                               1u,
                                               buffer,
                                               0u) != GPU_OK ||
      GPUSetIntersectionFunctionTableBufferEXT(renderTable,
                                               2u,
                                               buffer,
                                               0u) != GPU_OK) {
    fprintf(stderr, "intersection-function-table buffer setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(computeQueue,
                              "intersection-function-table",
                              &computeCmdb) != GPU_OK ||
      !computeCmdb ||
      !(computePass = GPUBeginComputePass(computeCmdb,
                                          "intersection-function-table"))) {
    fprintf(stderr, "intersection-function-table compute pass failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, computePipeline);
  GPUBindComputeIntersectionFunctionTableEXT(computePass, 1u, computeTable);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  if (GPUCreateFence(device, NULL, &computeFence) != GPU_OK || !computeFence) {
    fprintf(stderr, "intersection-function-table compute fence failed\n");
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &computeCmdb;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = computeFence;
  computeSubmitted = true;
  if (GPUQueueSubmit(computeQueue, &submitInfo) != GPU_OK ||
      GPUWaitFence(computeFence, UINT64_MAX) != GPU_OK) {
    computeCmdb = NULL;
    fprintf(stderr, "intersection-function-table compute submit failed\n");
    goto cleanup;
  }
  computeCmdb = NULL;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "intersection-function-table-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target) {
    fprintf(stderr, "intersection-function-table render target failed\n");
    goto cleanup;
  }
  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "intersection-function-table-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "intersection-function-table target view failed\n");
    goto cleanup;
  }

  colorAttachment.view          = targetView;
  colorAttachment.loadOp        = GPU_LOAD_OP_CLEAR;
  colorAttachment.storeOp       = GPU_STORE_OP_STORE;
  colorAttachment.clearColor.float32[3] = 1.0f;
  renderPassInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.chain.structSize = sizeof(renderPassInfo);
  renderPassInfo.label            = "intersection-function-table-render";
  renderPassInfo.pColorAttachments = &colorAttachment;
  renderPassInfo.colorAttachmentCount = 1u;
  viewport.width    = 1.0f;
  viewport.height   = 1.0f;
  viewport.maxDepth = 1.0f;
  scissor.width     = 1u;
  scissor.height    = 1u;
  if (GPUAcquireCommandBuffer(graphicsQueue,
                              "intersection-function-table-render",
                              &renderCmdb) != GPU_OK ||
      !renderCmdb ||
      !(renderPass = GPUBeginRenderPass(renderCmdb, &renderPassInfo))) {
    fprintf(stderr, "intersection-function-table render pass failed\n");
    goto cleanup;
  }
  GPUBindRenderPipeline(renderPass, renderPipeline);
  GPUBindRenderIntersectionFunctionTableEXT(renderPass, 2u, renderTable);
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  if (GPUCreateFence(device, NULL, &renderFence) != GPU_OK || !renderFence) {
    fprintf(stderr, "intersection-function-table render fence failed\n");
    goto cleanup;
  }
  submitInfo.ppCommandBuffers = &renderCmdb;
  submitInfo.fence            = renderFence;
  renderSubmitted = true;
  if (GPUQueueSubmit(graphicsQueue, &submitInfo) != GPU_OK ||
      GPUWaitFence(renderFence, UINT64_MAX) != GPU_OK) {
    renderCmdb = NULL;
    fprintf(stderr, "intersection-function-table render submit failed\n");
    goto cleanup;
  }
  renderCmdb = NULL;
  ok         = 1;

cleanup:
  if (renderPass) GPUEndRenderPass(renderPass);
  if (computePass) GPUEndComputePass(computePass);
  if (renderCmdb && !renderSubmitted) GPUDiscardCommandBuffer(renderCmdb);
  if (computeCmdb && !computeSubmitted) GPUDiscardCommandBuffer(computeCmdb);
  GPUDestroyFence(renderFence);
  GPUDestroyFence(computeFence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBuffer(buffer);
  GPUDestroyIntersectionFunctionTableEXT(renderTable);
  GPUDestroyIntersectionFunctionTableEXT(computeTable);
  GPUDestroyRenderPipeline(renderPipeline);
  GPUDestroyComputePipeline(computePipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  GPUDestroyDevice(device);
  return ok;
}

int
gpu_test_ray_query(GPUAdapter *adapter, const char *bytecodePath) {
  static const float aabbs[] = {
    -0.5f, -0.5f, -0.1f,
     0.5f,  0.5f,  0.1f
  };
  static const float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.5f,  0.5f, 0.0f,
    -0.5f,  0.5f, 0.0f
  };
  static const uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
  static const float identity[3][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f}
  };
  GPUDeviceCreateInfo                          deviceInfo       = {0};
  GPUComputePipelineCreateInfo                 pipelineInfo     = {0};
  GPUAccelerationStructureGeometryEXT         geometry         = {0};
  GPUAccelerationStructureGeometryEXT         aabbGeometry     = {0};
  GPUAccelerationStructureBuildInfoEXT         blasBuild        = {0};
  GPUAccelerationStructureBuildInfoEXT         aabbBuild        = {0};
  GPUAccelerationStructureBuildInfoEXT         tlasBuild        = {0};
  GPUAccelerationStructureSizesEXT             blasSizes        = {0};
  GPUAccelerationStructureSizesEXT             aabbSizes        = {0};
  GPUAccelerationStructureSizesEXT             tlasSizes        = {0};
  GPUAccelerationStructureCreateInfoEXT        structureInfo    = {0};
  GPUAccelerationStructureInstanceEXT          instance         = {0};
  GPUBindGroupEntry                            groupEntries[3]   = {0};
  GPUBindGroupCreateInfo                       groupInfo         = {0};
  GPUBindGroupLayoutCreateInfo                 manualGroupInfo   = {0};
  GPUPipelineLayoutCreateInfo                  manualLayoutInfo  = {0};
  GPUQueueSubmitInfo                           submitInfo        = {0};
  GPUShaderReflection                          reflection        = {0};
  GPUFeature                                   feature;
  GPUDevice                                   *device;
  GPUQueue                                    *queue;
  GPUShaderLibrary                            *library;
  GPUBindGroupLayout                          *groupLayout;
  GPUBindGroupLayout                          *manualGroupLayout;
  GPUPipelineLayout                           *pipelineLayout;
  GPUPipelineLayout                           *manualPipelineLayout;
  GPUComputePipeline                          *pipeline;
  GPUComputePipeline                          *manualPipeline;
  GPUBuffer                                   *vertexBuffer;
  GPUBuffer                                   *indexBuffer;
  GPUBuffer                                   *aabbBuffer;
  GPUBuffer                                   *scratchBuffer;
  GPUBuffer                                   *inputBuffer;
  GPUBuffer                                   *outputBuffer;
  GPUAccelerationStructureEXT                 *blas;
  GPUAccelerationStructureEXT                 *aabbBlas;
  GPUAccelerationStructureEXT                 *tlas;
  GPUBindGroup                                *group;
  GPUCommandBuffer                            *cmdb;
  GPUAccelerationStructurePassEncoderEXT      *buildPass;
  GPUComputePassEncoder                       *computePass;
  GPUFence                                    *fence;
  const GPUBindGroupLayoutEntry               *layoutEntries;
  void                                        *bytecode;
  uint64_t                                     bytecodeSize;
  uint64_t                                     scratchSize;
  uint32_t                                     layoutEntryCount;
  uint32_t                                     layoutCount;
  uint32_t                                     inputValue;
  uint32_t                                     resultValue;
  bool                                         sawScene;
  bool                                         sawInput;
  bool                                         sawResult;
  bool                                         submitAttempted;
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

  feature              = GPU_FEATURE_RAY_QUERY;
  device               = NULL;
  queue                = NULL;
  library              = NULL;
  groupLayout          = NULL;
  manualGroupLayout    = NULL;
  pipelineLayout       = NULL;
  manualPipelineLayout = NULL;
  pipeline             = NULL;
  manualPipeline       = NULL;
  vertexBuffer         = NULL;
  indexBuffer          = NULL;
  aabbBuffer           = NULL;
  scratchBuffer        = NULL;
  inputBuffer          = NULL;
  outputBuffer         = NULL;
  blas                 = NULL;
  aabbBlas             = NULL;
  tlas                 = NULL;
  group                = NULL;
  cmdb                 = NULL;
  buildPass            = NULL;
  computePass          = NULL;
  fence                = NULL;
  bytecode             = NULL;
  bytecodeSize         = 0u;
  layoutEntryCount     = 0u;
  inputValue           = 0u;
  resultValue          = 0u;
  sawScene             = false;
  sawInput             = false;
  sawResult            = false;
  submitAttempted      = false;
  ok                   = 0;

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
                          sizeof(vertices)) != GPU_OK ||
      !ray_create_buffer(
        device,
        "ray-query-indices",
        sizeof(indices),
        GPU_BUFFER_USAGE_COPY_DST |
          GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT,
        &indexBuffer) ||
      GPUQueueWriteBuffer(queue,
                          indexBuffer,
                          0u,
                          indices,
                          sizeof(indices)) != GPU_OK ||
      !ray_create_buffer(
        device,
        "ray-query-aabbs",
        sizeof(aabbs),
        GPU_BUFFER_USAGE_COPY_DST |
          GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE_INPUT_EXT,
        &aabbBuffer) ||
      GPUQueueWriteBuffer(queue,
                          aabbBuffer,
                          0u,
                          aabbs,
                          sizeof(aabbs)) != GPU_OK) {
    fprintf(stderr, "ray-query geometry buffer setup failed\n");
    goto cleanup;
  }

  geometry.type                   =
    GPU_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_EXT;
  geometry.triangles.vertexBuffer = vertexBuffer;
  geometry.triangles.indexBuffer  = indexBuffer;
  geometry.triangles.vertexCount  = 4u;
  geometry.triangles.vertexStride = sizeof(float) * 3u;
  geometry.triangles.vertexFormat = GPU_VERTEX_FORMAT_FLOAT32X3;
  geometry.triangles.indexCount   = GPU_ARRAY_LEN(indices);
  geometry.triangles.indexType    = GPU_INDEX_TYPE_UINT16;
  blasBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  blasBuild.chain.structSize            = sizeof(blasBuild);
  blasBuild.label                       = "ray-query-blas";
  blasBuild.type                        =
    GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  blasBuild.mode                        = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  blasBuild.bottomLevel.pGeometries     = &geometry;
  blasBuild.bottomLevel.geometryCount   = 1u;
  geometry.triangles.indexBuffer = NULL;
  geometry.triangles.indexCount  = 0u;
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &blasBuild,
                                          &blasSizes) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "ray-query accepted incomplete non-indexed geometry\n");
    goto cleanup;
  }
  geometry.triangles.indexBuffer = indexBuffer;
  geometry.triangles.indexCount  = GPU_ARRAY_LEN(indices);
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

  aabbGeometry.type         = GPU_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_EXT;
  aabbGeometry.aabbs.buffer = aabbBuffer;
  aabbGeometry.aabbs.count  = 1u;
  aabbGeometry.aabbs.stride = sizeof(aabbs);
  aabbBuild.chain.sType      =
    GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT;
  aabbBuild.chain.structSize          = sizeof(aabbBuild);
  aabbBuild.label                     = "ray-query-aabb-blas";
  aabbBuild.type                      =
    GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT;
  aabbBuild.mode                      = GPU_ACCELERATION_STRUCTURE_BUILD_EXT;
  aabbBuild.bottomLevel.pGeometries   = &aabbGeometry;
  aabbBuild.bottomLevel.geometryCount = 1u;
  aabbGeometry.aabbs.stride           = sizeof(aabbs) + sizeof(float);
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &aabbBuild,
                                          &aabbSizes) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "ray-query accepted undersized AABB stride\n");
    goto cleanup;
  }
  aabbGeometry.aabbs.stride = sizeof(aabbs);
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &aabbBuild,
                                          &aabbSizes) != GPU_OK) {
    fprintf(stderr, "ray-query AABB BLAS size query failed\n");
    goto cleanup;
  }
  structureInfo.label     = "ray-query-aabb-blas";
  structureInfo.sizeBytes = aabbSizes.accelerationStructureSize;
  if (GPUCreateAccelerationStructureEXT(device,
                                        &structureInfo,
                                        &aabbBlas) != GPU_OK ||
      !aabbBlas) {
    fprintf(stderr, "ray-query AABB BLAS create failed\n");
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
  instance.hitGroupOffset             = 0x01000000u;
  if (GPUGetAccelerationStructureSizesEXT(device,
                                          &tlasBuild,
                                          &tlasSizes) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "ray-query accepted oversized hit-group offset\n");
    goto cleanup;
  }
  instance.hitGroupOffset = 0u;
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

  scratchSize = ray_max_u64(
    ray_max_u64(blasSizes.buildScratchSize, aabbSizes.buildScratchSize),
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
      !ray_create_buffer(device,
                         "ray-query-input",
                         sizeof(inputValue),
                         GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_DST,
                         &inputBuffer) ||
      GPUQueueWriteBuffer(queue,
                          inputBuffer,
                          0u,
                          &inputValue,
                          sizeof(inputValue)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          &resultValue,
                          sizeof(resultValue)) != GPU_OK) {
    fprintf(stderr, "ray-query scratch/input/output setup failed\n");
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
  if (reflection.resourceCount != 3u) {
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
               resource->bindingType ==
                 GPU_BINDING_READ_ONLY_STORAGE_BUFFER) {
      sawInput = true;
    } else if (resource->binding == 2u &&
               resource->bindingType == GPU_BINDING_STORAGE_BUFFER) {
      sawResult = true;
    }
  }
  if (!sawScene || !sawInput || !sawResult) {
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

  layoutEntries = GPUGetBindGroupLayoutEntries(groupLayout,
                                                &layoutEntryCount);
  manualGroupInfo.chain.sType =
    GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  manualGroupInfo.chain.structSize = sizeof(manualGroupInfo);
  manualGroupInfo.label            = "ray-query-manual-group";
  manualGroupInfo.entryCount       = layoutEntryCount;
  manualGroupInfo.pEntries         = layoutEntries;
  if (!layoutEntries || layoutEntryCount != 3u ||
      GPUCreateBindGroupLayout(device,
                               &manualGroupInfo,
                               &manualGroupLayout) != GPU_OK ||
      !manualGroupLayout) {
    fprintf(stderr, "ray-query manual layout creation failed\n");
    goto cleanup;
  }

  manualLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  manualLayoutInfo.chain.structSize     = sizeof(manualLayoutInfo);
  manualLayoutInfo.label                = "ray-query-manual-pipeline";
  manualLayoutInfo.bindGroupLayoutCount = 1u;
  manualLayoutInfo.ppBindGroupLayouts   = &manualGroupLayout;
  if (GPUCreatePipelineLayout(device,
                              &manualLayoutInfo,
                              &manualPipelineLayout) != GPU_OK ||
      !manualPipelineLayout) {
    fprintf(stderr, "ray-query manual pipeline layout failed\n");
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
  pipelineInfo.label  = "ray-query-manual-pipeline";
  pipelineInfo.layout = manualPipelineLayout;
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &manualPipeline) != GPU_OK ||
      !manualPipeline) {
    fprintf(stderr, "ray-query manual binding plan mismatch\n");
    goto cleanup;
  }

  groupEntries[0].binding               = 0u;
  groupEntries[0].bindingType           = GPU_BINDING_ACCELERATION_STRUCTURE;
  groupEntries[0].accelerationStructure = tlas;
  groupEntries[1].binding               = 1u;
  groupEntries[1].bindingType           = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  groupEntries[1].buffer.buffer         = inputBuffer;
  groupEntries[1].buffer.size           = sizeof(inputValue);
  groupEntries[2].binding               = 2u;
  groupEntries[2].bindingType           = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[2].buffer.buffer         = outputBuffer;
  groupEntries[2].buffer.size           = sizeof(resultValue);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "ray-query-group";
  groupInfo.layout           = groupLayout;
  groupInfo.entryCount       = GPU_ARRAY_LEN(groupEntries);
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
                                       aabbBlas,
                                       &aabbBuild,
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
  submitAttempted               = true;
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
  if (cmdb && !submitAttempted) GPUDiscardCommandBuffer(cmdb);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyComputePipeline(manualPipeline);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineLayout(manualPipelineLayout);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyBindGroupLayout(manualGroupLayout);
  GPUDestroyBindGroupLayout(groupLayout);
  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  GPUDestroyAccelerationStructureEXT(tlas);
  GPUDestroyAccelerationStructureEXT(aabbBlas);
  GPUDestroyAccelerationStructureEXT(blas);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyBuffer(scratchBuffer);
  GPUDestroyBuffer(aabbBuffer);
  GPUDestroyBuffer(indexBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyDevice(device);
  free(bytecode);
  return ok;
}
