#include "test.h"

enum {
  GPU_CUBE_BASE_SIZE   = 8u,
  GPU_CUBE_VIEW_SIZE   = GPU_CUBE_BASE_SIZE / 2u,
  GPU_CUBE_FACE_COUNT  = 6u,
  GPU_CUBE_ARRAY_COUNT = 2u,
  GPU_CUBE_LAYER_COUNT = GPU_CUBE_FACE_COUNT * GPU_CUBE_ARRAY_COUNT,
  GPU_CUBE_PIXEL_BYTES = 4u,
  GPU_CUBE_MIP0_BYTES  = GPU_CUBE_BASE_SIZE * GPU_CUBE_BASE_SIZE *
                         GPU_CUBE_PIXEL_BYTES * GPU_CUBE_LAYER_COUNT,
  GPU_CUBE_MIP1_BYTES  = GPU_CUBE_VIEW_SIZE * GPU_CUBE_VIEW_SIZE *
                         GPU_CUBE_PIXEL_BYTES * GPU_CUBE_LAYER_COUNT
};

static void
fill_cube_layer(uint8_t *pixels,
                uint32_t size,
                uint32_t layer,
                uint8_t  red,
                uint8_t  green,
                uint8_t  blue) {
  uint32_t layerOffset;

  layerOffset = layer * size * size * GPU_CUBE_PIXEL_BYTES;
  for (uint32_t i = 0u; i < size * size; i++) {
    uint32_t offset = layerOffset + i * GPU_CUBE_PIXEL_BYTES;

    pixels[offset + 0u] = red;
    pixels[offset + 1u] = green;
    pixels[offset + 2u] = blue;
    pixels[offset + 3u] = 255u;
  }
}

static int
check_cube_layout(GPUShaderLayout *shaderLayout) {
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
  if (!entries || count != 3u) {
    return 0;
  }

  seen = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    if (entries[i].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        entries[i].binding > 2u) {
      return 0;
    }
    if (entries[i].binding < 2u &&
        entries[i].bindingType != GPU_BINDING_SAMPLED_TEXTURE) {
      return 0;
    }
    if (entries[i].binding == 2u &&
        entries[i].bindingType != GPU_BINDING_STORAGE_BUFFER) {
      return 0;
    }
    seen |= 1u << entries[i].binding;
  }
  return seen == 0x7u;
}

int
gpu_test_cube_texture_view(GPUDevice *device, const char *bytecodePath) {
  GPUQueue                     *queue;
  GPUShaderLibrary             *library;
  GPUShaderLayout              *shaderLayout;
  GPUComputePipeline           *pipeline;
  GPUTexture                   *texture;
  GPUTextureView               *cubeView;
  GPUTextureView               *cubeArrayView;
  GPUBuffer                    *output;
  GPUBindGroup                 *group;
  GPUCommandBuffer             *cmdb;
  GPUComputePassEncoder        *computePass;
  GPUFence                     *fence;
  void                         *bytecode;
  GPUCommandBuffer             *submitBuffers[1];
  GPUComputePipelineCreateInfo  pipelineInfo    = {0};
  GPUTextureCreateInfo          textureInfo     = {0};
  GPUTextureViewCreateInfo      viewInfo        = {0};
  GPUTextureWriteRegion         writeRegion     = {0};
  GPUBufferCreateInfo           bufferInfo      = {0};
  GPUBindGroupEntry             groupEntries[3] = {0};
  GPUBindGroupCreateInfo        groupInfo       = {0};
  GPUBufferBarrier              outputBarrier   = {0};
  GPUBarrierBatch               barrierBatch    = {0};
  GPUQueueSubmitInfo            submitInfo      = {0};
  uint8_t                       mip0Pixels[GPU_CUBE_MIP0_BYTES];
  uint8_t                       mip1Pixels[GPU_CUBE_MIP1_BYTES];
  float                         result[4];
  const float                   zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  uint64_t                      bytecodeSize;
  int                           ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue         = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library       = NULL;
  shaderLayout  = NULL;
  pipeline      = NULL;
  texture       = NULL;
  cubeView      = NULL;
  cubeArrayView = NULL;
  output        = NULL;
  group         = NULL;
  cmdb          = NULL;
  computePass   = NULL;
  fence         = NULL;
  bytecodeSize  = 0u;
  bytecode      = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok            = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "cube texture fixture setup failed\n");
    goto cleanup;
  }

  memset(mip0Pixels, 0, sizeof(mip0Pixels));
  memset(mip1Pixels, 0, sizeof(mip1Pixels));
  for (uint32_t layer = 0u; layer < GPU_CUBE_LAYER_COUNT; layer++) {
    fill_cube_layer(mip0Pixels,
                    GPU_CUBE_BASE_SIZE,
                    layer,
                    0u,
                    0u,
                    0u);
    fill_cube_layer(mip1Pixels,
                    GPU_CUBE_VIEW_SIZE,
                    layer,
                    0u,
                    0u,
                    0u);
  }
  fill_cube_layer(mip1Pixels, GPU_CUBE_VIEW_SIZE, 0u, 255u, 0u, 0u);
  fill_cube_layer(mip1Pixels, GPU_CUBE_VIEW_SIZE, 2u, 0u, 0u, 255u);
  fill_cube_layer(mip1Pixels, GPU_CUBE_VIEW_SIZE, 8u, 0u, 255u, 0u);

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !check_cube_layout(shaderLayout)) {
    fprintf(stderr, "cube texture shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-cube-texture-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "cube_view_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "cube texture compute pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-cube-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = GPU_CUBE_BASE_SIZE;
  textureInfo.height           = GPU_CUBE_BASE_SIZE;
  textureInfo.depthOrLayers    = GPU_CUBE_LAYER_COUNT;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "cube texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width          = GPU_CUBE_BASE_SIZE;
  writeRegion.height         = GPU_CUBE_BASE_SIZE;
  writeRegion.depth          = 1u;
  writeRegion.layerCount     = GPU_CUBE_LAYER_COUNT;
  writeRegion.bytesPerRow    = GPU_CUBE_BASE_SIZE * GPU_CUBE_PIXEL_BYTES;
  writeRegion.rowsPerImage   = GPU_CUBE_BASE_SIZE;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           mip0Pixels,
                           sizeof(mip0Pixels)) != GPU_OK) {
    fprintf(stderr, "cube texture base mip upload failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width        = GPU_CUBE_VIEW_SIZE;
  writeRegion.height       = GPU_CUBE_VIEW_SIZE;
  writeRegion.mipLevel     = 1u;
  writeRegion.bytesPerRow  = GPU_CUBE_VIEW_SIZE * GPU_CUBE_PIXEL_BYTES;
  writeRegion.rowsPerImage = GPU_CUBE_VIEW_SIZE;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           mip1Pixels,
                           sizeof(mip1Pixels)) != GPU_OK) {
    fprintf(stderr, "cube texture view mip upload failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-cube-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_CUBE;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.baseMipLevel     = 1u;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = GPU_CUBE_FACE_COUNT;
  if (GPUCreateTextureView(texture, &viewInfo, &cubeView) != GPU_OK ||
      !cubeView) {
    fprintf(stderr, "cube texture view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  viewInfo.label           = "api-cube-array-view";
  viewInfo.viewType        = GPU_TEXTURE_VIEW_CUBE_ARRAY;
  viewInfo.baseArrayLayer  = GPU_CUBE_FACE_COUNT;
  viewInfo.arrayLayerCount = GPU_CUBE_FACE_COUNT;
  if (GPUCreateTextureView(texture, &viewInfo, &cubeArrayView) != GPU_OK ||
      !cubeArrayView) {
    fprintf(stderr, "cube array texture view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-cube-texture-output";
  bufferInfo.sizeBytes        = sizeof(result);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &output) != GPU_OK || !output ||
      GPUQueueWriteBuffer(queue,
                          output,
                          0u,
                          zero,
                          sizeof(zero)) != GPU_OK) {
    fprintf(stderr, "cube texture output buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntries[0].binding       = 0u;
  groupEntries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[0].textureView   = cubeView;
  groupEntries[1].binding       = 1u;
  groupEntries[1].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[1].textureView   = cubeArrayView;
  groupEntries[2].binding       = 2u;
  groupEntries[2].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[2].buffer.buffer = output;
  groupEntries[2].buffer.size   = sizeof(result);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-cube-texture-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 3u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "cube texture bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-cube-texture", &cmdb) != GPU_OK ||
      !cmdb || !(computePass = GPUBeginComputePass(cmdb,
                                                   "api-cube-texture"))) {
    fprintf(stderr, "cube texture compute pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  outputBarrier.buffer     = output;
  outputBarrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  outputBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  outputBarrier.sizeBytes  = sizeof(result);

  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = 1u;
  barrierBatch.pBufferBarriers    = &outputBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "cube texture fence creation failed\n");
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
    fprintf(stderr, "cube texture submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         output,
                         0u,
                         result,
                         sizeof(result)) != GPU_OK ||
      result[0] < 0.99f || result[0] > 1.01f ||
      result[1] < 0.99f || result[1] > 1.01f ||
      result[2] < -0.01f || result[2] > 0.01f ||
      result[3] < 0.99f || result[3] > 1.01f) {
    fprintf(stderr,
            "cube texture readback mismatch: %.3f %.3f %.3f %.3f\n",
            result[0],
            result[1],
            result[2],
            result[3]);
    ok = 0;
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(output);
  GPUDestroyTextureView(cubeArrayView);
  GPUDestroyTextureView(cubeView);
  GPUDestroyTexture(texture);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
