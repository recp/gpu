#include "test.h"

enum {
  GPU_STORAGE_VIEW_WIDTH      = 4u,
  GPU_STORAGE_VIEW_HEIGHT     = 4u,
  GPU_STORAGE_BASE_WIDTH      = GPU_STORAGE_VIEW_WIDTH * 2u,
  GPU_STORAGE_BASE_HEIGHT     = GPU_STORAGE_VIEW_HEIGHT * 2u,
  GPU_STORAGE_VIEW_BYTES      = GPU_STORAGE_VIEW_WIDTH *
                                GPU_STORAGE_VIEW_HEIGHT * 4u,
  GPU_STORAGE_BASE_BYTES      = GPU_STORAGE_BASE_WIDTH *
                                GPU_STORAGE_BASE_HEIGHT * 4u,
  GPU_STORAGE_ROW_PITCH       = 256u,
  GPU_STORAGE_VIEW_COPY_BYTES = GPU_STORAGE_ROW_PITCH *
                                GPU_STORAGE_VIEW_HEIGHT,
  GPU_STORAGE_BASE_COPY_BYTES = GPU_STORAGE_ROW_PITCH *
                                GPU_STORAGE_BASE_HEIGHT,
  GPU_STORAGE_TARGET_OFFSET   = 0u,
  GPU_STORAGE_LAYER_OFFSET    = GPU_STORAGE_VIEW_COPY_BYTES,
  GPU_STORAGE_MIP_OFFSET      = GPU_STORAGE_LAYER_OFFSET +
                                GPU_STORAGE_VIEW_COPY_BYTES,
  GPU_STORAGE_READBACK_BYTES  = GPU_STORAGE_MIP_OFFSET +
                                GPU_STORAGE_BASE_COPY_BYTES
};

static void
fill_rgba8(uint8_t *pixels,
           uint32_t pixelCount,
           uint8_t  red,
           uint8_t  green,
           uint8_t  blue) {
  for (uint32_t i = 0u; i < pixelCount; i++) {
    pixels[i * 4u + 0u] = red;
    pixels[i * 4u + 1u] = green;
    pixels[i * 4u + 2u] = blue;
    pixels[i * 4u + 3u] = 255u;
  }
}

static int
check_rgba8(const uint8_t *pixels,
            uint32_t       baseOffset,
            uint32_t       width,
            uint32_t       height,
            uint8_t        red,
            uint8_t        green,
            uint8_t        blue) {
  for (uint32_t y = 0u; y < height; y++) {
    for (uint32_t x = 0u; x < width; x++) {
      uint32_t offset = baseOffset + y * GPU_STORAGE_ROW_PITCH + x * 4u;

      if (pixels[offset + 0u] != red ||
          pixels[offset + 1u] != green ||
          pixels[offset + 2u] != blue ||
          pixels[offset + 3u] != 255u) {
        fprintf(stderr,
                "storage texture pixel mismatch at %u,%u: %u %u %u %u\n",
                x,
                y,
                (unsigned)pixels[offset + 0u],
                (unsigned)pixels[offset + 1u],
                (unsigned)pixels[offset + 2u],
                (unsigned)pixels[offset + 3u]);
        return 0;
      }
    }
  }
  return 1;
}

int
gpu_test_storage_texture_view(GPUDevice *device, const char *bytecodePath) {
  GPUQueue                      *queue;
  GPUShaderLibrary              *library;
  GPUShaderLayout               *shaderLayout;
  GPUComputePipeline            *pipeline;
  GPUBindGroup                  *group;
  GPUTexture                    *texture;
  GPUTextureView                *view;
  GPUBuffer                     *readback;
  GPUCommandBuffer              *cmdb;
  GPUComputePassEncoder         *computePass;
  GPUCopyPassEncoder            *copyPass;
  GPUFence                      *fence;
  void                          *bytecode;
  GPUCommandBuffer              *submitBuffers[1];
  GPUComputePipelineCreateInfo   pipelineInfo   = {0};
  GPUTextureCreateInfo           textureInfo    = {0};
  GPUTextureViewCreateInfo       viewInfo       = {0};
  GPUTextureWriteRegion          writeRegion    = {0};
  GPUBindGroupEntry              groupEntry     = {0};
  GPUBindGroupCreateInfo         groupInfo      = {0};
  GPUBufferCreateInfo            bufferInfo     = {0};
  GPUTextureBarrier              textureBarrier = {0};
  GPUBarrierBatch                barrierBatch   = {0};
  GPUBufferTextureCopyRegion     copyRegion     = {0};
  GPUQueueSubmitInfo             submitInfo     = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  uint8_t                        greenPixels[GPU_STORAGE_BASE_BYTES];
  uint8_t                        bluePixels[GPU_STORAGE_VIEW_BYTES];
  uint8_t                        blackPixels[GPU_STORAGE_VIEW_BYTES];
  uint8_t                        pixels[GPU_STORAGE_READBACK_BYTES];
  uint64_t                       bytecodeSize;
  uint32_t                       layoutEntryCount;
  int                            ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  group        = NULL;
  texture      = NULL;
  view         = NULL;
  readback     = NULL;
  cmdb         = NULL;
  computePass  = NULL;
  copyPass     = NULL;
  fence        = NULL;
  bytecodeSize = 0u;
  bytecode     = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok           = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "storage texture fixture setup failed\n");
    goto cleanup;
  }

  fill_rgba8(greenPixels,
             GPU_STORAGE_BASE_WIDTH * GPU_STORAGE_BASE_HEIGHT,
             0u,
             255u,
             0u);
  fill_rgba8(bluePixels,
             GPU_STORAGE_VIEW_WIDTH * GPU_STORAGE_VIEW_HEIGHT,
             0u,
             0u,
             255u);
  fill_rgba8(blackPixels,
             GPU_STORAGE_VIEW_WIDTH * GPU_STORAGE_VIEW_HEIGHT,
             0u,
             0u,
             0u);
  memset(pixels, 0, sizeof(pixels));

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
    fprintf(stderr, "storage texture shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_TEXTURE ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT) {
    fprintf(stderr, "storage texture reflection mismatch\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-storage-texture-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "storage_view_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "storage texture compute pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-storage-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = GPU_STORAGE_BASE_WIDTH;
  textureInfo.height           = GPU_STORAGE_BASE_HEIGHT;
  textureInfo.depthOrLayers    = 3u;
  textureInfo.mipLevelCount    = 3u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_STORAGE |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "storage texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width          = GPU_STORAGE_BASE_WIDTH;
  writeRegion.height         = GPU_STORAGE_BASE_HEIGHT;
  writeRegion.depth          = 1u;
  writeRegion.baseArrayLayer = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = GPU_STORAGE_BASE_WIDTH * 4u;
  writeRegion.rowsPerImage   = GPU_STORAGE_BASE_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           greenPixels,
                           sizeof(greenPixels)) != GPU_OK) {
    fprintf(stderr, "storage texture mip sentinel upload failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.mipLevel       = 1u;
  writeRegion.width          = GPU_STORAGE_VIEW_WIDTH;
  writeRegion.height         = GPU_STORAGE_VIEW_HEIGHT;
  writeRegion.baseArrayLayer = 0u;
  writeRegion.bytesPerRow    = GPU_STORAGE_VIEW_WIDTH * 4u;
  writeRegion.rowsPerImage   = GPU_STORAGE_VIEW_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           bluePixels,
                           sizeof(bluePixels)) != GPU_OK) {
    fprintf(stderr, "storage texture layer sentinel upload failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.baseArrayLayer = 1u;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           blackPixels,
                           sizeof(blackPixels)) != GPU_OK) {
    fprintf(stderr, "storage texture target upload failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-storage-texture-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.baseMipLevel     = 1u;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.baseArrayLayer   = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "storage texture view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_TEXTURE;
  groupEntry.textureView   = view;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-storage-texture-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "storage texture bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-storage-texture-readback";
  bufferInfo.sizeBytes        = GPU_STORAGE_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "storage texture readback buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "api-storage-texture",
                              &cmdb) != GPU_OK ||
      !cmdb || !(computePass = GPUBeginComputePass(cmdb,
                                                   "api-storage-texture"))) {
    fprintf(stderr, "storage texture compute pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  textureBarrier.texture    = texture;
  textureBarrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.baseMip    = 0u;
  textureBarrier.mipCount   = 2u;
  textureBarrier.baseLayer  = 0u;
  textureBarrier.layerCount = 2u;
  barrierBatch.srcStages           = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-storage-texture-readback");
  if (!copyPass) {
    fprintf(stderr, "storage texture copy pass creation failed\n");
    ok = 0;
    goto cleanup;
  }

  copyRegion.bytesPerRow                 = GPU_STORAGE_ROW_PITCH;
  copyRegion.rowsPerImage                = GPU_STORAGE_VIEW_HEIGHT;
  copyRegion.texture.texture.mipLevel    = 1u;
  copyRegion.texture.texture.baseArrayLayer = 1u;
  copyRegion.texture.width               = GPU_STORAGE_VIEW_WIDTH;
  copyRegion.texture.height              = GPU_STORAGE_VIEW_HEIGHT;
  copyRegion.texture.depth               = 1u;
  copyRegion.texture.layerCount          = 1u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);

  copyRegion.bufferOffset                   = GPU_STORAGE_LAYER_OFFSET;
  copyRegion.texture.texture.baseArrayLayer = 0u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);

  copyRegion.bufferOffset                   = GPU_STORAGE_MIP_OFFSET;
  copyRegion.rowsPerImage                   = GPU_STORAGE_BASE_HEIGHT;
  copyRegion.texture.texture.mipLevel       = 0u;
  copyRegion.texture.texture.baseArrayLayer = 1u;
  copyRegion.texture.width                  = GPU_STORAGE_BASE_WIDTH;
  copyRegion.texture.height                 = GPU_STORAGE_BASE_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "storage texture fence creation failed\n");
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
    fprintf(stderr, "storage texture submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK ||
      !check_rgba8(pixels,
                   GPU_STORAGE_TARGET_OFFSET,
                   GPU_STORAGE_VIEW_WIDTH,
                   GPU_STORAGE_VIEW_HEIGHT,
                   255u,
                   0u,
                   0u) ||
      !check_rgba8(pixels,
                   GPU_STORAGE_LAYER_OFFSET,
                   GPU_STORAGE_VIEW_WIDTH,
                   GPU_STORAGE_VIEW_HEIGHT,
                   0u,
                   0u,
                   255u) ||
      !check_rgba8(pixels,
                   GPU_STORAGE_MIP_OFFSET,
                   GPU_STORAGE_BASE_WIDTH,
                   GPU_STORAGE_BASE_HEIGHT,
                   0u,
                   255u,
                   0u)) {
    fprintf(stderr, "storage texture subresource readback failed\n");
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
  GPUDestroyBuffer(readback);
  GPUDestroyBindGroup(group);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
