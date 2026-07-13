#include "test.h"

enum {
  GPU_VOLUME_BASE_SIZE      = 8u,
  GPU_VOLUME_VIEW_SIZE      = GPU_VOLUME_BASE_SIZE / 2u,
  GPU_VOLUME_PIXEL_BYTES    = 4u,
  GPU_VOLUME_MIP0_BYTES     = GPU_VOLUME_BASE_SIZE * GPU_VOLUME_BASE_SIZE *
                              GPU_VOLUME_BASE_SIZE * GPU_VOLUME_PIXEL_BYTES,
  GPU_VOLUME_MIP1_BYTES     = GPU_VOLUME_VIEW_SIZE * GPU_VOLUME_VIEW_SIZE *
                              GPU_VOLUME_VIEW_SIZE * GPU_VOLUME_PIXEL_BYTES,
  GPU_VOLUME_ROW_PITCH      = 256u,
  GPU_VOLUME_ROWS_PER_IMAGE = GPU_VOLUME_VIEW_SIZE,
  GPU_VOLUME_IMAGE_BYTES    = GPU_VOLUME_ROW_PITCH *
                              GPU_VOLUME_ROWS_PER_IMAGE,
  GPU_VOLUME_READBACK_BYTES = GPU_VOLUME_IMAGE_BYTES * GPU_VOLUME_VIEW_SIZE
};

static void
fill_volume(uint8_t *pixels,
            uint32_t size,
            uint8_t  red,
            uint8_t  green,
            uint8_t  blue) {
  for (uint32_t i = 0u; i < size * size * size; i++) {
    pixels[i * GPU_VOLUME_PIXEL_BYTES + 0u] = red;
    pixels[i * GPU_VOLUME_PIXEL_BYTES + 1u] = green;
    pixels[i * GPU_VOLUME_PIXEL_BYTES + 2u] = blue;
    pixels[i * GPU_VOLUME_PIXEL_BYTES + 3u] = 255u;
  }
}

static void
fill_volume_slice(uint8_t *pixels,
                  uint32_t size,
                  uint32_t z,
                  uint8_t  red,
                  uint8_t  green,
                  uint8_t  blue) {
  uint32_t sliceOffset;

  sliceOffset = z * size * size * GPU_VOLUME_PIXEL_BYTES;
  for (uint32_t i = 0u; i < size * size; i++) {
    uint32_t offset = sliceOffset + i * GPU_VOLUME_PIXEL_BYTES;

    pixels[offset + 0u] = red;
    pixels[offset + 1u] = green;
    pixels[offset + 2u] = blue;
    pixels[offset + 3u] = 255u;
  }
}

static int
check_volume_layout(GPUShaderLayout *shaderLayout) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       count;
  uint32_t                       seen;

  if (!shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->pipelineLayout) {
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0],
                                          &count);
  if (!entries || count != 2u) {
    return 0;
  }

  seen = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    if (entries[i].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        entries[i].binding > 1u ||
        (entries[i].binding == 0u &&
         entries[i].bindingType != GPU_BINDING_SAMPLED_TEXTURE) ||
        (entries[i].binding == 1u &&
         entries[i].bindingType != GPU_BINDING_STORAGE_TEXTURE)) {
      return 0;
    }
    seen |= 1u << entries[i].binding;
  }
  return seen == 0x3u;
}

static int
check_volume_pixels(const uint8_t *pixels) {
  for (uint32_t z = 0u; z < GPU_VOLUME_VIEW_SIZE; z++) {
    for (uint32_t y = 0u; y < GPU_VOLUME_VIEW_SIZE; y++) {
      for (uint32_t x = 0u; x < GPU_VOLUME_VIEW_SIZE; x++) {
        uint32_t offset;
        uint8_t  red;
        uint8_t  green;
        uint8_t  blue;

        offset = z * GPU_VOLUME_IMAGE_BYTES +
                 y * GPU_VOLUME_ROW_PITCH +
                 x * GPU_VOLUME_PIXEL_BYTES;
        red    = x == 1u && y == 1u && z == 3u ? 255u : 0u;
        green  = x == 1u && y == 1u && z == 2u ? 255u : 0u;
        blue   = 0u;
        if (pixels[offset + 0u] != red ||
            pixels[offset + 1u] != green ||
            pixels[offset + 2u] != blue ||
            pixels[offset + 3u] != 255u) {
          fprintf(stderr,
                  "volume texture pixel mismatch at %u,%u,%u: %u %u %u %u\n",
                  x,
                  y,
                  z,
                  (unsigned)pixels[offset + 0u],
                  (unsigned)pixels[offset + 1u],
                  (unsigned)pixels[offset + 2u],
                  (unsigned)pixels[offset + 3u]);
          return 0;
        }
      }
    }
  }
  return 1;
}

int
gpu_test_volume_texture_view(GPUDevice *device, const char *bytecodePath) {
  GPUCommandQueue               *queue;
  GPUShaderLibrary              *library;
  GPUShaderLayout               *shaderLayout;
  GPUComputePipeline            *pipeline;
  GPUTexture                    *inputTexture;
  GPUTexture                    *outputTexture;
  GPUTextureView                *inputView;
  GPUTextureView                *outputView;
  GPUBuffer                     *readback;
  GPUBindGroup                  *group;
  GPUCommandBuffer              *cmdb;
  GPUComputePassEncoder         *computePass;
  GPUCopyPassEncoder            *copyPass;
  GPUFence                      *fence;
  void                          *bytecode;
  GPUCommandBuffer              *submitBuffers[1];
  GPUComputePipelineCreateInfo   pipelineInfo    = {0};
  GPUTextureCreateInfo           textureInfo     = {0};
  GPUTextureViewCreateInfo       viewInfo        = {0};
  GPUTextureWriteRegion          writeRegion     = {0};
  GPUBufferCreateInfo            bufferInfo      = {0};
  GPUBindGroupEntry              groupEntries[2] = {0};
  GPUBindGroupCreateInfo         groupInfo       = {0};
  GPUTextureBarrier              textureBarrier  = {0};
  GPUBarrierBatch                barrierBatch    = {0};
  GPUBufferTextureCopyRegion     copyRegion      = {0};
  GPUQueueSubmitInfo             submitInfo      = {0};
  uint8_t                        inputMip0[GPU_VOLUME_MIP0_BYTES];
  uint8_t                        inputMip1[GPU_VOLUME_MIP1_BYTES];
  uint8_t                        outputMip1[GPU_VOLUME_MIP1_BYTES];
  uint8_t                        pixels[GPU_VOLUME_READBACK_BYTES];
  uint64_t                       bytecodeSize;
  int                            ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue         = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library       = NULL;
  shaderLayout  = NULL;
  pipeline      = NULL;
  inputTexture  = NULL;
  outputTexture = NULL;
  inputView     = NULL;
  outputView    = NULL;
  readback      = NULL;
  group         = NULL;
  cmdb          = NULL;
  computePass   = NULL;
  copyPass      = NULL;
  fence         = NULL;
  bytecodeSize  = 0u;
  bytecode      = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok            = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "volume texture fixture setup failed\n");
    goto cleanup;
  }

  fill_volume(inputMip0, GPU_VOLUME_BASE_SIZE, 0u, 0u, 0u);
  fill_volume(inputMip1, GPU_VOLUME_VIEW_SIZE, 0u, 0u, 0u);
  fill_volume(outputMip1, GPU_VOLUME_VIEW_SIZE, 0u, 0u, 0u);
  fill_volume_slice(inputMip1,
                    GPU_VOLUME_VIEW_SIZE,
                    1u,
                    0u,
                    0u,
                    255u);
  fill_volume_slice(inputMip1,
                    GPU_VOLUME_VIEW_SIZE,
                    2u,
                    0u,
                    255u,
                    0u);
  memset(pixels, 0, sizeof(pixels));

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !check_volume_layout(shaderLayout)) {
    fprintf(stderr, "volume texture shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-volume-texture-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "volume_view_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "volume texture compute pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_3D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = GPU_VOLUME_BASE_SIZE;
  textureInfo.height           = GPU_VOLUME_BASE_SIZE;
  textureInfo.depthOrLayers    = GPU_VOLUME_BASE_SIZE;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.label            = "api-volume-input";
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &inputTexture) != GPU_OK ||
      !inputTexture) {
    fprintf(stderr, "volume input texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.label = "api-volume-output";
  textureInfo.usage = GPU_TEXTURE_USAGE_STORAGE |
                      GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &outputTexture) != GPU_OK ||
      !outputTexture) {
    fprintf(stderr, "volume output texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width        = GPU_VOLUME_BASE_SIZE;
  writeRegion.height       = GPU_VOLUME_BASE_SIZE;
  writeRegion.depth        = GPU_VOLUME_BASE_SIZE;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = GPU_VOLUME_BASE_SIZE * GPU_VOLUME_PIXEL_BYTES;
  writeRegion.rowsPerImage = GPU_VOLUME_BASE_SIZE;
  if (GPUQueueWriteTexture(queue,
                           inputTexture,
                           &writeRegion,
                           inputMip0,
                           sizeof(inputMip0)) != GPU_OK) {
    fprintf(stderr, "volume input base mip upload failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width        = GPU_VOLUME_VIEW_SIZE;
  writeRegion.height       = GPU_VOLUME_VIEW_SIZE;
  writeRegion.depth        = GPU_VOLUME_VIEW_SIZE;
  writeRegion.mipLevel     = 1u;
  writeRegion.bytesPerRow  = GPU_VOLUME_VIEW_SIZE * GPU_VOLUME_PIXEL_BYTES;
  writeRegion.rowsPerImage = GPU_VOLUME_VIEW_SIZE;
  if (GPUQueueWriteTexture(queue,
                           inputTexture,
                           &writeRegion,
                           inputMip1,
                           sizeof(inputMip1)) != GPU_OK ||
      GPUQueueWriteTexture(queue,
                           outputTexture,
                           &writeRegion,
                           outputMip1,
                           sizeof(outputMip1)) != GPU_OK) {
    fprintf(stderr, "volume view mip upload failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_3D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.baseMipLevel     = 1u;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  viewInfo.label            = "api-volume-input-view";
  if (GPUCreateTextureView(inputTexture, &viewInfo, &inputView) != GPU_OK ||
      !inputView) {
    fprintf(stderr, "volume input view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.label = "api-volume-output-view";
  if (GPUCreateTextureView(outputTexture, &viewInfo, &outputView) != GPU_OK ||
      !outputView) {
    fprintf(stderr, "volume output view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-volume-readback";
  bufferInfo.sizeBytes        = GPU_VOLUME_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "volume readback buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntries[0].binding       = 0u;
  groupEntries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[0].textureView   = inputView;
  groupEntries[1].binding       = 1u;
  groupEntries[1].bindingType   = GPU_BINDING_STORAGE_TEXTURE;
  groupEntries[1].textureView   = outputView;

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-volume-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "volume bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-volume-texture", &cmdb) != GPU_OK ||
      !cmdb || !(computePass = GPUBeginComputePass(cmdb,
                                                   "api-volume-texture"))) {
    fprintf(stderr, "volume compute pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  textureBarrier.texture    = outputTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.baseMip    = 1u;
  textureBarrier.mipCount   = 1u;
  textureBarrier.baseLayer  = 0u;
  textureBarrier.layerCount = 1u;

  barrierBatch.srcStages           = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-volume-readback");
  if (!copyPass) {
    fprintf(stderr, "volume copy pass creation failed\n");
    ok = 0;
    goto cleanup;
  }

  copyRegion.bytesPerRow              = GPU_VOLUME_ROW_PITCH;
  copyRegion.rowsPerImage             = GPU_VOLUME_ROWS_PER_IMAGE;
  copyRegion.texture.texture.mipLevel = 1u;
  copyRegion.texture.width           = GPU_VOLUME_VIEW_SIZE;
  copyRegion.texture.height          = GPU_VOLUME_VIEW_SIZE;
  copyRegion.texture.depth           = GPU_VOLUME_VIEW_SIZE;
  copyRegion.texture.layerCount      = 1u;
  GPUCopyTextureToBuffer(copyPass, outputTexture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "volume fence creation failed\n");
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
    fprintf(stderr, "volume texture submission failed\n");
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
      !check_volume_pixels(pixels)) {
    fprintf(stderr, "volume texture readback failed\n");
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
  GPUDestroyBuffer(readback);
  GPUDestroyTextureView(outputView);
  GPUDestroyTextureView(inputView);
  GPUDestroyTexture(outputTexture);
  GPUDestroyTexture(inputTexture);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
