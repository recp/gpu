#include "test.h"

enum {
  GPU_DESCRIPTOR_ARRAY_UNIFORM_BYTES  = 256u,
  GPU_DESCRIPTOR_ARRAY_READBACK_BYTES = 256u
};

static int
create_color_texture(GPUDevice       *device,
                     GPUCommandQueue *queue,
                     const char      *label,
                     const uint8_t    color[4],
                     GPUTexture     **outTexture,
                     GPUTextureView **outView) {
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
                                 GPU_TEXTURE_USAGE_STORAGE |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
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

int
gpu_test_descriptor_array(GPUDevice *device, const char *bytecodePath) {
  static const uint8_t red[4]   = {255u, 0u, 0u, 255u};
  static const uint8_t green[4] = {0u, 255u, 0u, 255u};
  static const uint8_t black[4] = {0u, 0u, 0u, 255u};
  GPUCommandQueue                  *queue;
  GPUShaderLibrary                 *library;
  GPUShaderLayout                  *shaderLayout;
  GPUComputePipeline               *pipeline;
  GPUBindGroup                     *group;
  GPUTexture                       *textures[2];
  GPUTextureView                   *views[2];
  GPUTexture                       *storageTextures[2];
  GPUTextureView                   *storageViews[2];
  GPUSampler                       *samplers[2];
  GPUBuffer                        *selectionBuffer;
  GPUBuffer                        *outputBuffer;
  GPUBuffer                        *textureReadback;
  GPUCommandBuffer                 *cmdb;
  GPUCommandBuffer                 *submitBuffers[1];
  GPUComputePassEncoder            *computePass;
  GPUCopyPassEncoder               *copyPass;
  GPUFence                         *fence;
  void                             *bytecode;
  const GPUBindGroupLayoutEntry    *layoutEntries;
  GPUComputePipelineCreateInfo      pipelineInfo   = {0};
  GPUSamplerCreateInfo              samplerInfo    = {0};
  GPUBufferCreateInfo               bufferInfo     = {0};
  GPUBindGroupEntry                 groupEntries[9] = {{0}};
  GPUBindGroupCreateInfo            groupInfo      = {0};
  GPUBufferBarrier                  outputBarrier  = {0};
  GPUTextureBarrier                 textureBarrier = {0};
  GPUBarrierBatch                   barrierBatch   = {0};
  GPUBufferTextureCopyRegion        copyRegion     = {0};
  GPUQueueSubmitInfo                submitInfo     = {0};
  uint32_t                          selection[64]   = {1u};
  float                             output[4]       = {0.0f};
  uint8_t                           pixels[GPU_DESCRIPTOR_ARRAY_READBACK_BYTES] = {0};
  uint64_t                          bytecodeSize;
  uint32_t                          layoutEntryCount;
  int                               ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue              = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library            = NULL;
  shaderLayout       = NULL;
  pipeline           = NULL;
  group              = NULL;
  textures[0]        = NULL;
  textures[1]        = NULL;
  views[0]           = NULL;
  views[1]           = NULL;
  storageTextures[0] = NULL;
  storageTextures[1] = NULL;
  storageViews[0]    = NULL;
  storageViews[1]    = NULL;
  samplers[0]        = NULL;
  samplers[1]        = NULL;
  selectionBuffer    = NULL;
  outputBuffer       = NULL;
  textureReadback    = NULL;
  cmdb               = NULL;
  computePass        = NULL;
  copyPass           = NULL;
  fence              = NULL;
  bytecodeSize       = 0u;
  bytecode           = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok                 = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "descriptor array fixture setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "descriptor array shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[1],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 6u) {
    fprintf(stderr, "descriptor array layout entry mismatch\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-descriptor-array";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "descriptor_array_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "descriptor array compute pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (!create_color_texture(device,
                            queue,
                            "api-descriptor-array-red",
                            red,
                            &textures[0],
                            &views[0]) ||
      !create_color_texture(device,
                            queue,
                            "api-descriptor-array-green",
                            green,
                            &textures[1],
                            &views[1]) ||
      !create_color_texture(device,
                            queue,
                            "api-descriptor-array-storage-0",
                            black,
                            &storageTextures[0],
                            &storageViews[0]) ||
      !create_color_texture(device,
                            queue,
                            "api-descriptor-array-storage-1",
                            black,
                            &storageTextures[1],
                            &storageViews[1])) {
    fprintf(stderr, "descriptor array texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "api-descriptor-array-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(device, &samplerInfo, false, &samplers[0]) != GPU_OK ||
      !samplers[0] ||
      GPUCreateSampler(device, &samplerInfo, false, &samplers[1]) != GPU_OK ||
      !samplers[1]) {
    fprintf(stderr, "descriptor array sampler creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-descriptor-array-selection";
  bufferInfo.sizeBytes        = GPU_DESCRIPTOR_ARRAY_UNIFORM_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &selectionBuffer) != GPU_OK ||
      !selectionBuffer ||
      GPUQueueWriteBuffer(queue,
                          selectionBuffer,
                          0u,
                          selection,
                          sizeof(selection)) != GPU_OK) {
    fprintf(stderr, "descriptor array selection buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.label     = "api-descriptor-array-output";
  bufferInfo.sizeBytes = sizeof(output);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "descriptor array output buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.label     = "api-descriptor-array-readback";
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST |
                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &textureReadback) != GPU_OK ||
      !textureReadback) {
    fprintf(stderr, "descriptor array readback buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  for (uint32_t i = 0u; i < 2u; i++) {
    groupEntries[i].binding     = 0u;
    groupEntries[i].arrayIndex  = i;
    groupEntries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    groupEntries[i].textureView = views[i];

    groupEntries[3u + i].binding     = 3u;
    groupEntries[3u + i].arrayIndex  = i;
    groupEntries[3u + i].bindingType = GPU_BINDING_SAMPLER;
    groupEntries[3u + i].sampler     = samplers[i];

    groupEntries[7u + i].binding     = 6u;
    groupEntries[7u + i].arrayIndex  = i;
    groupEntries[7u + i].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    groupEntries[7u + i].textureView = storageViews[i];
  }
  groupEntries[2].binding       = 1u;
  groupEntries[2].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[2].textureView   = views[0];
  groupEntries[5].binding       = 4u;
  groupEntries[5].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  groupEntries[5].buffer.buffer = selectionBuffer;
  groupEntries[5].buffer.size   = sizeof(selection);
  groupEntries[6].binding       = 5u;
  groupEntries[6].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[6].buffer.buffer = outputBuffer;
  groupEntries[6].buffer.size   = sizeof(output);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-descriptor-array";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount       = (uint32_t)GPU_ARRAY_LEN(groupEntries);
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "descriptor array bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-descriptor-array", &cmdb) != GPU_OK ||
      !cmdb ||
      !(computePass = GPUBeginComputePass(cmdb, "api-descriptor-array"))) {
    fprintf(stderr, "descriptor array compute pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 1u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  outputBarrier.buffer    = outputBuffer;
  outputBarrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
  outputBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
  outputBarrier.sizeBytes = sizeof(output);

  textureBarrier.texture    = storageTextures[1];
  textureBarrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;

  barrierBatch.srcStages           = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount  = 1u;
  barrierBatch.pBufferBarriers     = &outputBarrier;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-descriptor-array-readback");
  if (!copyPass) {
    fprintf(stderr, "descriptor array copy pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow         = GPU_DESCRIPTOR_ARRAY_READBACK_BYTES;
  copyRegion.rowsPerImage        = 1u;
  copyRegion.texture.width       = 1u;
  copyRegion.texture.height      = 1u;
  copyRegion.texture.depth       = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         storageTextures[1],
                         textureReadback,
                         &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "descriptor array fence creation failed\n");
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
    fprintf(stderr, "descriptor array submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      output[0] < -0.01f || output[0] > 0.01f ||
      output[1] < 0.99f || output[1] > 1.01f ||
      output[2] < -0.01f || output[2] > 0.01f ||
      output[3] < 0.99f || output[3] > 1.01f ||
      GPUQueueReadBuffer(queue,
                         textureReadback,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK ||
      pixels[0] != 0u || pixels[1] != 255u ||
      pixels[2] != 0u || pixels[3] != 255u) {
    fprintf(stderr,
            "descriptor array readback mismatch: %.3f %.3f %.3f %.3f\n",
            output[0],
            output[1],
            output[2],
            output[3]);
    ok = 0;
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(selectionBuffer);
  GPUDestroySampler(samplers[1]);
  GPUDestroySampler(samplers[0]);
  GPUDestroyTextureView(views[1]);
  GPUDestroyTextureView(views[0]);
  GPUDestroyTextureView(storageViews[1]);
  GPUDestroyTextureView(storageViews[0]);
  GPUDestroyTexture(textures[1]);
  GPUDestroyTexture(textures[0]);
  GPUDestroyTexture(storageTextures[1]);
  GPUDestroyTexture(storageTextures[0]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
