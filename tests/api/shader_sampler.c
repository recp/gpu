#include "test.h"
#include "../../src/api/device_internal.h"

enum {
  GPU_SOURCE_SAMPLER_WARM_ITERATIONS = 16u,
  GPU_SOURCE_SAMPLER_WARM_BIND_REQUESTS =
    GPU_SOURCE_SAMPLER_WARM_ITERATIONS * 4u,
  GPU_SOURCE_SAMPLER_WARM_BIND_EMISSIONS =
    GPU_SOURCE_SAMPLER_WARM_ITERATIONS * 2u,
  GPU_SOURCE_SAMPLER_WARM_STATE_REQUESTS =
    GPU_SOURCE_SAMPLER_WARM_ITERATIONS * 8u,
  GPU_SOURCE_SAMPLER_WARM_STATE_EMISSIONS =
    GPU_SOURCE_SAMPLER_WARM_ITERATIONS * 4u
};

static int
submit_source_sampler_draw(GPUCommandQueue            *queue,
                           GPURenderPipeline          *pipeline,
                           GPUBindGroup               *group,
                           GPURenderPassCreateInfo    *passInfo,
                           GPUFence                   *fence) {
  GPUCommandBuffer         *cmdb;
  GPUCommandBuffer         *submitBuffers[1];
  GPURenderPassEncoder     *renderPass;
  GPUQueueSubmitInfo        submitInfo   = {0};
  GPUDynamicStateApplyInfo  dynamicState = {0};

  cmdb       = NULL;
  renderPass = NULL;
  if (GPUAcquireCommandBuffer(queue,
                              "api-source-sampler-warm",
                              &cmdb) != GPU_OK ||
      !cmdb || !(renderPass = GPUBeginRenderPass(cmdb, passInfo))) {
    return 0;
  }

  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderGroup(renderPass, 0u, group, 0u, NULL);
  GPUBindRenderGroup(renderPass, 0u, group, 0u, NULL);
  dynamicState.chain.sType      = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  dynamicState.chain.structSize = sizeof(dynamicState);
  dynamicState.mask             = GPU_DYNAMIC_STATE_VIEWPORT_BIT |
                                  GPU_DYNAMIC_STATE_SCISSOR_BIT |
                                  GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT |
                                  GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  dynamicState.viewport.width   = 4.0;
  dynamicState.viewport.height  = 4.0;
  dynamicState.viewport.zfar    = 1.0;
  dynamicState.scissor.width    = 4u;
  dynamicState.scissor.height   = 4u;
  dynamicState.blendConstant[3] = 1.0f;
  GPUApplyDynamicState(renderPass, &dynamicState);
  GPUApplyDynamicState(renderPass, &dynamicState);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);

  submitBuffers[0]              = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitBuffers;
  submitInfo.fence              = fence;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
}

int
gpu_test_source_sampler_draw(GPUDevice *device, const char *bytecodePath) {
  static const uint8_t sourcePixel[] = {255u, 0u, 0u, 255u};
  const uint32_t width      = 4u;
  const uint32_t height     = 4u;
  const uint32_t rowPitch   = 256u;
  const uint64_t imageBytes = (uint64_t)rowPitch * height;
  GPUCommandQueue                  *queue;
  GPUShaderLibrary                 *library;
  GPUShaderLayout                  *shaderLayout;
  GPURenderPipeline                *pipeline;
  GPUBindGroup                     *group;
  GPUTexture                       *sampledTexture;
  GPUTexture                       *targetTexture;
  GPUTextureView                   *sampledView;
  GPUTextureView                   *targetView;
  GPUBuffer                        *readbackBuffer;
  GPUCommandBuffer                 *cmdb;
  GPUCommandBuffer                 *submitBuffers[1];
  GPURenderPassEncoder             *renderPass;
  GPUCopyPassEncoder               *copyPass;
  GPUFence                         *fence;
  void                             *bytecode;
  GPUColorTargetState               colorTarget = {0};
  GPURenderPipelineCreateInfo       pipelineInfo = {0};
  GPUTextureCreateInfo              textureInfo = {0};
  GPUTextureViewCreateInfo          viewInfo = {0};
  GPUTextureWriteRegion             writeRegion = {0};
  GPUBindGroupEntry                 groupEntry = {0};
  GPUBindGroupCreateInfo            groupInfo = {0};
  GPUBufferCreateInfo               bufferInfo = {0};
  GPURenderPassColorAttachment      color = {0};
  GPURenderPassCreateInfo           passInfo = {0};
  GPUTextureBarrier                 textureBarrier = {0};
  GPUBarrierBatch                   barrierBatch = {0};
  GPUBufferTextureCopyRegion        copyRegion = {0};
  GPUQueueSubmitInfo                submitInfo = {0};
  const GPUBindGroupLayoutEntry    *layoutEntries;
  uint8_t                           pixels[256u * 4u] = {0};
  uint64_t                          bytecodeSize;
  uint32_t                          layoutEntryCount;
  size_t                            centerOffset;
  int                               ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library        = NULL;
  shaderLayout   = NULL;
  pipeline       = NULL;
  group          = NULL;
  sampledTexture = NULL;
  targetTexture  = NULL;
  sampledView    = NULL;
  targetView     = NULL;
  readbackBuffer = NULL;
  cmdb           = NULL;
  renderPass     = NULL;
  copyPass       = NULL;
  fence          = NULL;
  bytecodeSize   = 0u;
  bytecode       = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok             = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "source sampler fixture setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "source sampler shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    fprintf(stderr, "source sampler leaked into public shader layout\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-source-sampler-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &sampledTexture) != GPU_OK ||
      !sampledTexture) {
    fprintf(stderr, "source sampler texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = (uint32_t)sizeof(sourcePixel);
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(queue,
                           sampledTexture,
                           &writeRegion,
                           sourcePixel,
                           sizeof(sourcePixel)) != GPU_OK) {
    fprintf(stderr, "source sampler texture upload failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-source-sampler-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(sampledTexture, &viewInfo, &sampledView) != GPU_OK ||
      !sampledView) {
    fprintf(stderr, "source sampler texture view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntry.textureView   = sampledView;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-source-sampler-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "source sampler bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  colorTarget.format          = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-source-sampler-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "source_sampler_vs";
  pipelineInfo.fragmentEntry    = "source_sampler_fs";
  pipelineInfo.colorTargetCount = 1u;
  pipelineInfo.pColorTargets    = &colorTarget;
  pipelineInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode                = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace               = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "source sampler render pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.label  = "api-source-sampler-target";
  textureInfo.width  = width;
  textureInfo.height = height;
  textureInfo.usage  = GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &targetTexture) != GPU_OK ||
      !targetTexture) {
    fprintf(stderr, "source sampler target creation failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.label = "api-source-sampler-target-view";
  if (GPUCreateTextureView(targetTexture, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "source sampler target view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-source-sampler-readback";
  bufferInfo.sizeBytes        = imageBytes;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "source sampler readback buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "api-source-sampler-draw",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "source sampler command buffer acquisition failed\n");
    ok = 0;
    goto cleanup;
  }

  color.view                    = targetView;
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  color.clearColor.float32[3]   = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "api-source-sampler-draw";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "source sampler render pass creation failed\n");
    ok = 0;
    goto cleanup;
  }

  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderGroup(renderPass, 0u, group, 0u, NULL);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture    = targetTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-source-sampler-readback");
  if (!copyPass) {
    fprintf(stderr, "source sampler copy pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = rowPitch;
  copyRegion.rowsPerImage       = height;
  copyRegion.texture.width      = width;
  copyRegion.texture.height     = height;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         targetTexture,
                         readbackBuffer,
                         &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "source sampler fence creation failed\n");
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
    fprintf(stderr, "source sampler draw submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         imageBytes) != GPU_OK) {
    fprintf(stderr, "source sampler readback failed\n");
    ok = 0;
    goto cleanup;
  }

  centerOffset = (size_t)2u * rowPitch + 2u * 4u;
  ok = pixels[centerOffset + 0u] >= 250u &&
       pixels[centerOffset + 1u] <= 2u &&
       pixels[centerOffset + 2u] <= 2u &&
       pixels[centerOffset + 3u] >= 250u;
  if (!ok) {
    fprintf(stderr,
            "source sampler draw pixel mismatch: %u %u %u %u\n",
            (unsigned)pixels[centerOffset + 0u],
            (unsigned)pixels[centerOffset + 1u],
            (unsigned)pixels[centerOffset + 2u],
            (unsigned)pixels[centerOffset + 3u]);
    goto cleanup;
  }

  GPUResetStats(device);
  for (uint32_t i = 0u; i < GPU_SOURCE_SAMPLER_WARM_ITERATIONS; i++) {
    if (!submit_source_sampler_draw(queue,
                                    pipeline,
                                    group,
                                    &passInfo,
                                    fence)) {
      fprintf(stderr, "source sampler warm draw submission failed\n");
      ok = 0;
      goto cleanup;
    }
  }
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathAllocBytes != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u ||
      device->currentFrameStats.hotPathFreeBytes != 0u ||
      device->currentFrameStats.requestedBindCalls !=
        GPU_SOURCE_SAMPLER_WARM_BIND_REQUESTS ||
      device->currentFrameStats.emittedBindCalls !=
        GPU_SOURCE_SAMPLER_WARM_BIND_EMISSIONS ||
      device->currentFrameStats.requestedStateCalls !=
        GPU_SOURCE_SAMPLER_WARM_STATE_REQUESTS ||
      device->currentFrameStats.emittedStateCalls !=
        GPU_SOURCE_SAMPLER_WARM_STATE_EMISSIONS) {
    fprintf(stderr,
            "source sampler warm path allocated: %llu/%llu bytes, "
            "%llu/%llu bytes freed; binds %u/%u; state %u/%u\n",
            (unsigned long long)device->currentFrameStats.hotPathAllocCount,
            (unsigned long long)device->currentFrameStats.hotPathAllocBytes,
            (unsigned long long)device->currentFrameStats.hotPathFreeCount,
            (unsigned long long)device->currentFrameStats.hotPathFreeBytes,
            device->currentFrameStats.requestedBindCalls,
            device->currentFrameStats.emittedBindCalls,
            device->currentFrameStats.requestedStateCalls,
            device->currentFrameStats.emittedStateCalls);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(targetTexture);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyBindGroup(group);
  GPUDestroyTextureView(sampledView);
  GPUDestroyTexture(sampledTexture);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
