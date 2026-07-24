#include "test.h"

enum {
  SAMPLER_FEEDBACK_TARGET_WIDTH   = 64u,
  SAMPLER_FEEDBACK_TARGET_HEIGHT  = 64u,
  SAMPLER_FEEDBACK_REGION_SIZE    = 4u,
  SAMPLER_FEEDBACK_DECODE_WIDTH   = 16u,
  SAMPLER_FEEDBACK_DECODE_HEIGHT  = 16u,
  SAMPLER_FEEDBACK_MIP_COUNT      = 4u,
  SAMPLER_FEEDBACK_ROW_PITCH      = 256u,
  SAMPLER_FEEDBACK_READBACK_BYTES =
    SAMPLER_FEEDBACK_ROW_PITCH * (16u + 8u + 4u + 2u)
};

static uint32_t
gpu_test_sampler_feedback_mip_extent(uint32_t extent, uint32_t mipLevel) {
  extent >>= mipLevel;
  return extent ? extent : 1u;
}

static int
gpu_test_sampler_feedback_clear_result(
  const uint8_t                         *bytes,
  const GPUSamplerFeedbackDecodeInfoEXT *decodeInfo,
  GPUSamplerFeedbackModeEXT              mode) {
  const uint8_t expected = mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
                             ? UINT8_MAX
                             : 0u;
  uint64_t offset;

  offset = 0u;
  for (uint32_t mip = 0u; mip < decodeInfo->mipLevelCount; mip++) {
    uint32_t width;
    uint32_t height;

    width  = gpu_test_sampler_feedback_mip_extent(decodeInfo->width, mip);
    height = gpu_test_sampler_feedback_mip_extent(decodeInfo->height, mip);
    for (uint32_t row = 0u; row < height; row++) {
      for (uint32_t column = 0u; column < width; column++) {
        uint8_t actual;

        actual = bytes[offset + (uint64_t)row * SAMPLER_FEEDBACK_ROW_PITCH +
                       column];
        if (actual != expected) {
          fprintf(stderr,
                  "sampler feedback clear mismatch at mip %u, (%u, %u): "
                  "expected 0x%02x, got 0x%02x\n",
                  mip,
                  column,
                  row,
                  expected,
                  actual);
          return 0;
        }
      }
    }
    offset += (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH * height;
  }
  return 1;
}

static int
gpu_test_sampler_feedback_write_result(
  const uint8_t                         *bytes,
  const GPUSamplerFeedbackDecodeInfoEXT *decodeInfo) {
  for (uint32_t row = 0u; row < decodeInfo->height; row++) {
    for (uint32_t column = 0u; column < decodeInfo->width; column++) {
      uint8_t actual;

      actual = bytes[(uint64_t)row * SAMPLER_FEEDBACK_ROW_PITCH + column];
      if (actual != 1u) {
        fprintf(stderr,
                "sampler feedback write mismatch at (%u, %u): "
                "expected 0x01, got 0x%02x\n",
                column,
                row,
                actual);
        return 0;
      }
    }
  }
  return 1;
}

static int
gpu_test_sampler_feedback_write(GPUDevice *device, const char *bytecodePath) {
  GPUQueue                         *queue;
  GPUShaderLibrary                 *library;
  GPUShaderLayout                  *shaderLayout;
  GPURenderPipeline                *pipeline;
  GPUBindGroup                     *group;
  GPUTexture                       *sampledTexture;
  GPUTexture                       *targetTexture;
  GPUTexture                       *decodedTexture;
  GPUTextureView                   *sampledView;
  GPUTextureView                   *targetView;
  GPUSampler                       *sampler;
  GPUSamplerFeedbackMapEXT         *map;
  GPUBuffer                        *readback;
  GPUCommandBuffer                 *cmdb;
  GPURenderPassEncoder             *renderPass;
  GPUCopyPassEncoder               *copyPass;
  GPUFence                         *fence;
  const GPUBindGroupLayoutEntry    *layoutEntries;
  void                             *bytecode;
  GPUTextureCreateInfo                 textureInfo     = {0};
  GPUTextureViewCreateInfo             viewInfo        = {0};
  GPUTextureWriteRegion                writeRegion     = {0};
  GPUSamplerCreateInfo                 samplerInfo     = {0};
  GPUSamplerFeedbackMapCreateInfoEXT   mapInfo         = {0};
  GPUSamplerFeedbackDecodeInfoEXT      decodeInfo      = {0};
  GPUBindGroupEntry                    groupEntries[3] = {0};
  GPUBindGroupCreateInfo               groupInfo       = {0};
  GPUColorTargetState                  colorTarget     = {0};
  GPURenderPipelineCreateInfo          pipelineInfo    = {0};
  GPURenderPassColorAttachment         color           = {0};
  GPURenderPassCreateInfo              passInfo        = {0};
  GPUBufferCreateInfo                  bufferInfo      = {0};
  GPUBufferTextureCopyRegion           copyRegion      = {0};
  GPUQueueSubmitInfo                   submitInfo      = {0};
  uint8_t                            sampledPixels[
    SAMPLER_FEEDBACK_TARGET_WIDTH *
    SAMPLER_FEEDBACK_TARGET_HEIGHT * 4u
  ];
  uint8_t                            readbackBytes[
    SAMPLER_FEEDBACK_ROW_PITCH * SAMPLER_FEEDBACK_DECODE_HEIGHT
  ] = {0};
  uint64_t                           bytecodeSize;
  uint32_t                           layoutEntryCount;
  int                                ok;

  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library        = NULL;
  shaderLayout   = NULL;
  pipeline       = NULL;
  group          = NULL;
  sampledTexture = NULL;
  targetTexture  = NULL;
  decodedTexture = NULL;
  sampledView    = NULL;
  targetView     = NULL;
  sampler        = NULL;
  map            = NULL;
  readback       = NULL;
  cmdb           = NULL;
  renderPass     = NULL;
  copyPass       = NULL;
  fence          = NULL;
  bytecodeSize   = 0u;
  bytecode       = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok             = queue && bytecode;
  memset(sampledPixels, 0xff, sizeof(sampledPixels));
  if (!ok ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "sampler feedback shader setup failed\n");
    ok = 0;
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[1],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 3u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_SAMPLER ||
      layoutEntries[2].binding != 2u ||
      layoutEntries[2].bindingType != GPU_BINDING_SAMPLER_FEEDBACK_EXT) {
    fprintf(stderr, "sampler feedback reflection mismatch\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sampler-feedback-write-source";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = SAMPLER_FEEDBACK_TARGET_WIDTH;
  textureInfo.height           = SAMPLER_FEEDBACK_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = SAMPLER_FEEDBACK_MIP_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &sampledTexture) != GPU_OK ||
      !sampledTexture) {
    fprintf(stderr, "sampler feedback sampled texture setup failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.depth          = 1u;
  writeRegion.baseArrayLayer = 0u;
  writeRegion.layerCount     = 1u;
  for (uint32_t mip = 0u; mip < SAMPLER_FEEDBACK_MIP_COUNT; mip++) {
    writeRegion.width = gpu_test_sampler_feedback_mip_extent(
      SAMPLER_FEEDBACK_TARGET_WIDTH,
      mip
    );
    writeRegion.height = gpu_test_sampler_feedback_mip_extent(
      SAMPLER_FEEDBACK_TARGET_HEIGHT,
      mip
    );
    writeRegion.mipLevel     = mip;
    writeRegion.bytesPerRow  = writeRegion.width * 4u;
    writeRegion.rowsPerImage = writeRegion.height;
    if (GPUQueueWriteTexture(queue,
                             sampledTexture,
                             &writeRegion,
                             sampledPixels,
                             (uint64_t)writeRegion.bytesPerRow *
                               writeRegion.height) != GPU_OK) {
      fprintf(stderr, "sampler feedback texture upload failed\n");
      ok = 0;
      goto cleanup;
    }
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "sampler-feedback-write-source-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = SAMPLER_FEEDBACK_MIP_COUNT;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(sampledTexture, &viewInfo, &sampledView) != GPU_OK ||
      !sampledView) {
    fprintf(stderr, "sampler feedback sampled view setup failed\n");
    ok = 0;
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "sampler-feedback-write-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(device, &samplerInfo, false, &sampler) != GPU_OK ||
      !sampler) {
    fprintf(stderr, "sampler feedback sampler setup failed\n");
    ok = 0;
    goto cleanup;
  }

  mapInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_SAMPLER_FEEDBACK_MAP_CREATE_INFO_EXT;
  mapInfo.chain.structSize = sizeof(mapInfo);
  mapInfo.label            = "sampler-feedback-write-map";
  mapInfo.texture          = sampledTexture;
  mapInfo.mode             = GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT;
  mapInfo.mipRegionWidth   = SAMPLER_FEEDBACK_REGION_SIZE;
  mapInfo.mipRegionHeight  = SAMPLER_FEEDBACK_REGION_SIZE;
  if (GPUCreateSamplerFeedbackMapEXT(device, &mapInfo, &map) != GPU_OK ||
      !map ||
      GPUGetSamplerFeedbackDecodeInfoEXT(map, &decodeInfo) != GPU_OK ||
      decodeInfo.format != GPU_FORMAT_R8_UINT ||
      decodeInfo.width != SAMPLER_FEEDBACK_DECODE_WIDTH ||
      decodeInfo.height != SAMPLER_FEEDBACK_DECODE_HEIGHT ||
      decodeInfo.mipLevelCount != 1u) {
    fprintf(stderr, "sampler feedback write map setup failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntries[0].textureView     = sampledView;
  groupEntries[0].binding         = 0u;
  groupEntries[0].bindingType     = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[1].sampler         = sampler;
  groupEntries[1].binding         = 1u;
  groupEntries[1].bindingType     = GPU_BINDING_SAMPLER;
  groupEntries[2].samplerFeedback = map;
  groupEntries[2].binding         = 2u;
  groupEntries[2].bindingType     = GPU_BINDING_SAMPLER_FEEDBACK_EXT;
  groupInfo.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize  = sizeof(groupInfo);
  groupInfo.label             = "sampler-feedback-write-group";
  groupInfo.layout            = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries          = groupEntries;
  groupInfo.entryCount        = GPU_ARRAY_LEN(groupEntries);
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "sampler feedback bind group setup failed\n");
    ok = 0;
    goto cleanup;
  }

  colorTarget.format            = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask   = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "sampler-feedback-write-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "sampler_feedback_vs";
  pipelineInfo.fragmentEntry    = "sampler_feedback_fs";
  pipelineInfo.pColorTargets    = &colorTarget;
  pipelineInfo.colorTargetCount = 1u;
  pipelineInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode                = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace               = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "sampler feedback pipeline setup failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.label         = "sampler-feedback-write-target";
  textureInfo.mipLevelCount = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (GPUCreateTexture(device, &textureInfo, &targetTexture) != GPU_OK ||
      !targetTexture) {
    fprintf(stderr, "sampler feedback render target setup failed\n");
    ok = 0;
    goto cleanup;
  }
  viewInfo.label         = "sampler-feedback-write-target-view";
  viewInfo.mipLevelCount = 1u;
  if (GPUCreateTextureView(targetTexture, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "sampler feedback render view setup failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.label         = "sampler-feedback-write-decoded";
  textureInfo.format        = decodeInfo.format;
  textureInfo.width         = decodeInfo.width;
  textureInfo.height        = decodeInfo.height;
  textureInfo.mipLevelCount = decodeInfo.mipLevelCount;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC |
                              GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &decodedTexture) != GPU_OK ||
      !decodedTexture) {
    fprintf(stderr, "sampler feedback decoded texture setup failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sampler-feedback-write-readback";
  bufferInfo.sizeBytes        = sizeof(readbackBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "sampler feedback write readback setup failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "sampler-feedback-write",
                              &cmdb) != GPU_OK ||
      !cmdb || GPUClearSamplerFeedbackEXT(cmdb, map) != GPU_OK) {
    fprintf(stderr, "sampler feedback write command setup failed\n");
    ok = 0;
    goto cleanup;
  }
  color.view                    = targetView;
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_DONT_CARE;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "sampler-feedback-write";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "sampler feedback write render pass failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderGroup(renderPass, 1u, group, 0u, NULL);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  if (GPUDecodeSamplerFeedbackEXT(cmdb, map, decodedTexture) != GPU_OK ||
      !(copyPass = GPUBeginCopyPass(cmdb, "sampler-feedback-write-readback"))) {
    fprintf(stderr, "sampler feedback write decode failed\n");
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = SAMPLER_FEEDBACK_ROW_PITCH;
  copyRegion.rowsPerImage       = decodeInfo.height;
  copyRegion.texture.width      = decodeInfo.width;
  copyRegion.texture.height     = decodeInfo.height;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, decodedTexture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "sampler feedback write fence setup failed\n");
    ok = 0;
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "sampler feedback write submit failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;
  ok = GPUQueueReadBuffer(queue,
                          readback,
                          0u,
                          readbackBytes,
                          sizeof(readbackBytes)) == GPU_OK &&
       gpu_test_sampler_feedback_write_result(readbackBytes, &decodeInfo);

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(decodedTexture);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(targetTexture);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyBindGroup(group);
  GPUDestroySamplerFeedbackMapEXT(map);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(sampledView);
  GPUDestroyTexture(sampledTexture);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}

static int
gpu_test_sampler_feedback_mode(GPUDevice                  *device,
                               GPUSamplerFeedbackModeEXT   mode) {
  GPUTextureCreateInfo                 textureInfo = {0};
  GPUBufferCreateInfo                  bufferInfo  = {0};
  GPUSamplerFeedbackMapCreateInfoEXT   mapInfo     = {0};
  GPUSamplerFeedbackDecodeInfoEXT      decodeInfo  = {0};
  GPUBufferTextureCopyRegion           copyRegion  = {0};
  GPUQueueSubmitInfo                   submitInfo  = {0};
  GPUQueue                            *queue;
  GPUTexture                          *target;
  GPUTexture                          *decoded;
  GPUBuffer                           *readback;
  GPUSamplerFeedbackMapEXT            *map;
  GPUCommandBuffer                    *cmdb;
  GPUCopyPassEncoder                  *copyPass;
  GPUFence                            *fence;
  uint8_t                              readbackBytes[
    SAMPLER_FEEDBACK_READBACK_BYTES] = {0};
  uint64_t                             readbackSize;
  int                                  ok;

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  target       = NULL;
  decoded      = NULL;
  readback     = NULL;
  map          = NULL;
  cmdb         = NULL;
  copyPass     = NULL;
  fence        = NULL;
  readbackSize = 0u;
  ok           = queue != NULL;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sampler-feedback-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = SAMPLER_FEEDBACK_TARGET_WIDTH;
  textureInfo.height           = SAMPLER_FEEDBACK_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = SAMPLER_FEEDBACK_MIP_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  ok = ok && GPUCreateTexture(device, &textureInfo, &target) == GPU_OK &&
       target;

  mapInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_SAMPLER_FEEDBACK_MAP_CREATE_INFO_EXT;
  mapInfo.chain.structSize = sizeof(mapInfo);
  mapInfo.label            = "sampler-feedback-map";
  mapInfo.texture          = target;
  mapInfo.mode             = mode;
  mapInfo.mipRegionWidth   = SAMPLER_FEEDBACK_REGION_SIZE;
  mapInfo.mipRegionHeight  = SAMPLER_FEEDBACK_REGION_SIZE;
  ok = ok && GPUCreateSamplerFeedbackMapEXT(device, &mapInfo, &map) == GPU_OK &&
       map &&
       GPUGetSamplerFeedbackDecodeInfoEXT(map, &decodeInfo) == GPU_OK &&
       decodeInfo.format == GPU_FORMAT_R8_UINT &&
       decodeInfo.width == SAMPLER_FEEDBACK_DECODE_WIDTH &&
       decodeInfo.height == SAMPLER_FEEDBACK_DECODE_HEIGHT &&
       decodeInfo.arrayLayerCount == 1u &&
       decodeInfo.mipLevelCount ==
         (mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
            ? 1u
            : SAMPLER_FEEDBACK_MIP_COUNT);

  textureInfo.label         = "sampler-feedback-decoded";
  textureInfo.format        = decodeInfo.format;
  textureInfo.width         = decodeInfo.width;
  textureInfo.height        = decodeInfo.height;
  textureInfo.depthOrLayers = decodeInfo.arrayLayerCount;
  textureInfo.mipLevelCount = decodeInfo.mipLevelCount;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC |
                              GPU_TEXTURE_USAGE_COPY_DST;
  ok = ok && GPUCreateTexture(device, &textureInfo, &decoded) == GPU_OK &&
       decoded;

  for (uint32_t mip = 0u; mip < decodeInfo.mipLevelCount; mip++) {
    readbackSize +=
      (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH *
      gpu_test_sampler_feedback_mip_extent(decodeInfo.height, mip);
  }
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sampler-feedback-readback";
  bufferInfo.sizeBytes        = readbackSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = ok && readbackSize <= sizeof(readbackBytes) &&
       GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK && readback;
  memset(readbackBytes, 0xa5, sizeof(readbackBytes));
  ok = ok && GPUQueueWriteBuffer(queue,
                                 readback,
                                 0u,
                                 readbackBytes,
                                 readbackSize) == GPU_OK;

  if (!ok || GPUAcquireCommandBuffer(queue,
                                     "sampler-feedback",
                                     &cmdb) != GPU_OK ||
      !cmdb || GPUClearSamplerFeedbackEXT(cmdb, map) != GPU_OK ||
      GPUDecodeSamplerFeedbackEXT(cmdb, map, decoded) != GPU_OK) {
    ok = 0;
    goto cleanup;
  }

  copyPass = GPUBeginCopyPass(cmdb, "sampler-feedback-readback");
  if (!copyPass) {
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = SAMPLER_FEEDBACK_ROW_PITCH;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  for (uint32_t mip = 0u; mip < decodeInfo.mipLevelCount; mip++) {
    uint32_t width;
    uint32_t height;

    width  = gpu_test_sampler_feedback_mip_extent(decodeInfo.width, mip);
    height = gpu_test_sampler_feedback_mip_extent(decodeInfo.height, mip);
    copyRegion.rowsPerImage            = height;
    copyRegion.texture.texture.mipLevel = mip;
    copyRegion.texture.width           = width;
    copyRegion.texture.height          = height;
    GPUCopyTextureToBuffer(copyPass, decoded, readback, &copyRegion);
    copyRegion.bufferOffset +=
      (uint64_t)SAMPLER_FEEDBACK_ROW_PITCH * height;
  }
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUEncodeSamplerFeedbackEXT(cmdb, decoded, map) != GPU_OK ||
      GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    ok = 0;
    goto cleanup;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         readbackSize) != GPU_OK ||
      !gpu_test_sampler_feedback_clear_result(readbackBytes,
                                              &decodeInfo,
                                              mode)) {
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(decoded);
  GPUDestroySamplerFeedbackMapEXT(map);
  GPUDestroyTexture(target);
  return ok;
}

int
gpu_test_sampler_feedback(GPUAdapter *adapter,
                          GPUDevice  *defaultDevice,
                          const char *bytecodePath) {
  static const char * const entries[] = {
    "GPUCreateSamplerFeedbackMapEXT",
    "GPUDestroySamplerFeedbackMapEXT",
    "GPUGetSamplerFeedbackDecodeInfoEXT",
    "GPUClearSamplerFeedbackEXT",
    "GPUDecodeSamplerFeedbackEXT",
    "GPUEncodeSamplerFeedbackEXT"
  };
  GPUSamplerFeedbackPropertiesEXT properties = {0};
  GPUDeviceCreateInfo             deviceInfo = {0};
  GPUDevice                      *device;
  GPUFeature                      feature;
  GPUResult                       result;
  int                             supported;
  int                             ok;

  if (!adapter || !defaultDevice) {
    return 0;
  }

  feature   = GPU_FEATURE_SAMPLER_FEEDBACK;
  supported = GPUIsFeatureSupported(adapter, feature);
  result    = GPUGetSamplerFeedbackPropertiesEXT(adapter, &properties);
  if (!supported) {
    deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.chain.structSize      = sizeof(deviceInfo);
    deviceInfo.required.pFeatures    = &feature;
    deviceInfo.required.featureCount = 1u;
    device = NULL;
    if (result != GPU_ERROR_UNSUPPORTED ||
        properties.tier != GPU_SAMPLER_FEEDBACK_TIER_NONE_EXT ||
        GPUCreateDevice(adapter, &deviceInfo, &device) !=
          GPU_ERROR_UNSUPPORTED ||
        device) {
      fprintf(stderr, "unsupported sampler feedback was exposed\n");
      GPUDestroyDevice(device);
      return 0;
    }
    puts("sampler-feedback execution skipped: unsupported adapter");
    return 1;
  }

  if (result != GPU_OK ||
      (properties.tier != GPU_SAMPLER_FEEDBACK_TIER_0_9_EXT &&
       properties.tier != GPU_SAMPLER_FEEDBACK_TIER_1_0_EXT)) {
    fprintf(stderr, "sampler feedback properties mismatch\n");
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (GPUGetProcAddr(defaultDevice, entries[i])) {
      fprintf(stderr, "sampler feedback entry enabled by default: %s\n",
              entries[i]);
      return 0;
    }
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  device = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "sampler feedback feature enablement failed\n");
    GPUDestroyDevice(device);
    return 0;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    if (!GPUGetProcAddr(device, entries[i])) {
      fprintf(stderr, "sampler feedback entry unavailable: %s\n", entries[i]);
      GPUDestroyDevice(device);
      return 0;
    }
  }

  ok = gpu_test_sampler_feedback_mode(device,
                                      GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT) &&
       gpu_test_sampler_feedback_mode(
         device,
         GPU_SAMPLER_FEEDBACK_MIP_REGION_USED_EXT
       ) &&
       bytecodePath &&
       gpu_test_sampler_feedback_write(device, bytecodePath);
  GPUDestroyDevice(device);
  return ok;
}
