#include "test.h"

enum {
  GPU_DX12_BINDING_UNIFORM_BYTES  = 512u,
  GPU_DX12_BINDING_DYNAMIC_OFFSET = 256u,
  GPU_DX12_BINDING_ENTRY_COUNT    = 5u,
  GPU_DX12_BINDING_GROUP_COUNT    = 2u
};

static int
create_binding_texture(GPUDevice        *device,
                       GPUQueue         *queue,
                       const char       *label,
                       const uint8_t     color[4],
                       GPUTexture      **outTexture,
                       GPUTextureView  **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    writeRegion = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK ||
      !*outTexture) {
    return 0;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 4u;
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(queue,
                           *outTexture,
                           &writeRegion,
                           color,
                           4u) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK &&
         *outView;
}

static int
near_value(float value, float expected) {
  float difference;

  difference = value - expected;
  return difference > -0.001f && difference < 0.001f;
}

int
gpu_test_dx12_binding_plan(GPUDevice *device, const char *bytecodePath) {
  static const uint8_t textureColors[GPU_DX12_BINDING_GROUP_COUNT][4] = {
    {255u, 0u, 0u, 255u},
    {0u, 255u, 0u, 255u}
  };
  static const float constants[GPU_DX12_BINDING_GROUP_COUNT][4] = {
    {2.0f, 2.0f, 2.0f, 2.0f},
    {0.5f, 0.5f, 0.5f, 0.5f}
  };
  static const float inputs[GPU_DX12_BINDING_GROUP_COUNT][4] = {
    {1.0f, 2.0f, 3.0f, 4.0f},
    {5.0f, 6.0f, 7.0f, 8.0f}
  };
  static const float expected[GPU_DX12_BINDING_GROUP_COUNT][4] = {
    {3.0f, 4.0f, 6.0f, 9.0f},
    {2.5f, 4.0f, 3.5f, 5.0f}
  };
  GPUQueue                    *queue;
  GPUShaderLibrary            *library;
  GPUShaderLayout             *shaderLayout;
  GPUComputePipeline          *pipeline;
  GPUBindGroup                *groups[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUBuffer                   *uniforms[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUBuffer                   *inputBuffers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUBuffer                   *outputBuffers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUTexture                  *textures[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUTextureView              *views[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUSampler                  *samplers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUCommandBuffer            *cmdb;
  GPUComputePassEncoder       *pass;
  GPUFence                    *fence;
  void                        *bytecode;
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUSamplerCreateInfo         samplerInfo   = {0};
  GPUBindGroupEntry            entries[GPU_DX12_BINDING_ENTRY_COUNT] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUBufferBarrier             bufferBarriers[GPU_DX12_BINDING_GROUP_COUNT] = {0};
  GPUBarrierBatch              barrierBatch = {0};
  GPUCommandBuffer            *submitBuffers[1];
  GPUQueueSubmitInfo           submitInfo = {0};
  float                        output[GPU_DX12_BINDING_GROUP_COUNT][4] = {0};
  uint32_t                     dynamicOffset;
  uint64_t                     bytecodeSize;
  int                          ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  cmdb         = NULL;
  pass         = NULL;
  fence        = NULL;
  bytecodeSize = 0u;
  bytecode     = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok           = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "DX12 binding-plan fixture setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout ||
      shaderLayout->bindGroupLayoutCount != GPU_DX12_BINDING_GROUP_COUNT ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "DX12 binding-plan shader layout failed\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-dx12-binding-plan";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "dx12_binding_plan_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "DX12 binding-plan pipeline failed\n");
    ok = 0;
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "api-dx12-binding-plan";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  for (uint32_t i = 0u; i < GPU_DX12_BINDING_GROUP_COUNT; i++) {
    bufferInfo.label     = "api-dx12-binding-plan-uniform";
    bufferInfo.sizeBytes = GPU_DX12_BINDING_UNIFORM_BYTES;
    bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                           GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &uniforms[i]) != GPU_OK ||
        !uniforms[i] ||
        GPUQueueWriteBuffer(queue,
                            uniforms[i],
                            GPU_DX12_BINDING_DYNAMIC_OFFSET,
                            constants[i],
                            sizeof(constants[i])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan uniform setup failed\n");
      ok = 0;
      goto cleanup;
    }

    bufferInfo.label     = "api-dx12-binding-plan-input";
    bufferInfo.sizeBytes = sizeof(inputs[i]);
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &inputBuffers[i]) != GPU_OK ||
        !inputBuffers[i] ||
        GPUQueueWriteBuffer(queue,
                            inputBuffers[i],
                            0u,
                            inputs[i],
                            sizeof(inputs[i])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan input setup failed\n");
      ok = 0;
      goto cleanup;
    }

    bufferInfo.label     = "api-dx12-binding-plan-output";
    bufferInfo.sizeBytes = sizeof(output[i]);
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                           GPU_BUFFER_USAGE_COPY_SRC |
                           GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &outputBuffers[i]) != GPU_OK ||
        !outputBuffers[i] ||
        GPUQueueWriteBuffer(queue,
                            outputBuffers[i],
                            0u,
                            output[i],
                            sizeof(output[i])) != GPU_OK ||
        !create_binding_texture(device,
                                queue,
                                "api-dx12-binding-plan-texture",
                                textureColors[i],
                                &textures[i],
                                &views[i]) ||
        GPUCreateSampler(device,
                         &samplerInfo,
                         false,
                         &samplers[i]) != GPU_OK ||
        !samplers[i]) {
      fprintf(stderr, "DX12 binding-plan resource setup failed\n");
      ok = 0;
      goto cleanup;
    }

    memset(entries, 0, sizeof(entries));
    entries[0].binding       = 0u;
    entries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
    entries[0].buffer.buffer = uniforms[i];
    entries[0].buffer.size   = sizeof(constants[i]);
    entries[1].binding       = 1u;
    entries[1].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[1].buffer.buffer = inputBuffers[i];
    entries[1].buffer.size   = sizeof(inputs[i]);
    entries[2].binding       = 2u;
    entries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[2].buffer.buffer = outputBuffers[i];
    entries[2].buffer.size   = sizeof(output[i]);
    entries[3].binding       = 3u;
    entries[3].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
    entries[3].textureView   = views[i];
    entries[4].binding       = 4u;
    entries[4].bindingType   = GPU_BINDING_SAMPLER;
    entries[4].sampler       = samplers[i];

    groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
    groupInfo.chain.structSize = sizeof(groupInfo);
    groupInfo.label            = "api-dx12-binding-plan";
    groupInfo.layout           = shaderLayout->bindGroupLayouts[i];
    groupInfo.entryCount       = GPU_DX12_BINDING_ENTRY_COUNT;
    groupInfo.pEntries         = entries;
    if (GPUCreateBindGroup(device, &groupInfo, &groups[i]) != GPU_OK ||
        !groups[i]) {
      fprintf(stderr, "DX12 binding-plan group setup failed\n");
      ok = 0;
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue,
                              "api-dx12-binding-plan",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(pass = GPUBeginComputePass(cmdb, "api-dx12-binding-plan"))) {
    fprintf(stderr, "DX12 binding-plan command setup failed\n");
    ok = 0;
    goto cleanup;
  }

  dynamicOffset = GPU_DX12_BINDING_DYNAMIC_OFFSET;
  GPUBindComputePipeline(pass, pipeline);
  for (uint32_t i = 0u; i < GPU_DX12_BINDING_GROUP_COUNT; i++) {
    GPUBindComputeGroup(pass, i, groups[i], 1u, &dynamicOffset);
  }
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  for (uint32_t i = 0u; i < GPU_DX12_BINDING_GROUP_COUNT; i++) {
    bufferBarriers[i].buffer    = outputBuffers[i];
    bufferBarriers[i].srcAccess = GPU_ACCESS_SHADER_WRITE;
    bufferBarriers[i].dstAccess = GPU_ACCESS_TRANSFER_READ;
    bufferBarriers[i].sizeBytes = sizeof(output[i]);
  }
  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = GPU_DX12_BINDING_GROUP_COUNT;
  barrierBatch.pBufferBarriers    = bufferBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 binding-plan fence failed\n");
    ok = 0;
    goto cleanup;
  }

  submitBuffers[0]              = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitBuffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "DX12 binding-plan submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  for (uint32_t i = 0u; i < GPU_DX12_BINDING_GROUP_COUNT; i++) {
    if (GPUQueueReadBuffer(queue,
                           outputBuffers[i],
                           0u,
                           output[i],
                           sizeof(output[i])) != GPU_OK) {
      fprintf(stderr, "DX12 binding-plan readback failed\n");
      ok = 0;
      goto cleanup;
    }
    for (uint32_t component = 0u; component < 4u; component++) {
      if (!near_value(output[i][component], expected[i][component])) {
        fprintf(stderr,
                "DX12 binding-plan mismatch at %u,%u: %.3f != %.3f\n",
                i,
                component,
                output[i][component],
                expected[i][component]);
        ok = 0;
        goto cleanup;
      }
    }
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  for (uint32_t i = 0u; i < GPU_DX12_BINDING_GROUP_COUNT; i++) {
    GPUDestroyBindGroup(groups[i]);
    GPUDestroySampler(samplers[i]);
    GPUDestroyTextureView(views[i]);
    GPUDestroyTexture(textures[i]);
    GPUDestroyBuffer(outputBuffers[i]);
    GPUDestroyBuffer(inputBuffers[i]);
    GPUDestroyBuffer(uniforms[i]);
  }
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
