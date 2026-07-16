#include "test.h"

static uint64_t
ray_max_u64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
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
