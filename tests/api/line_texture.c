#include "test.h"

enum {
  GPU_LINE_WIDTH          = 4u,
  GPU_LINE_LAYER_COUNT    = 3u,
  GPU_LINE_PIXEL_BYTES    = 4u,
  GPU_LINE_BYTES          = GPU_LINE_WIDTH * GPU_LINE_PIXEL_BYTES,
  GPU_LINE_ARRAY_BYTES    = GPU_LINE_BYTES * GPU_LINE_LAYER_COUNT,
  GPU_LINE_ROW_PITCH      = 256u,
  GPU_LINE_ROWS_PER_IMAGE = 2u,
  GPU_LINE_IMAGE_BYTES    = GPU_LINE_ROW_PITCH * GPU_LINE_ROWS_PER_IMAGE,
  GPU_LINE_SINGLE_OFFSET  = 0u,
  GPU_LINE_ARRAY_OFFSET   = GPU_LINE_IMAGE_BYTES,
  GPU_LINE_INPUT_OFFSET   = GPU_LINE_ARRAY_OFFSET +
                            GPU_LINE_IMAGE_BYTES * GPU_LINE_LAYER_COUNT,
  GPU_LINE_READBACK_BYTES = GPU_LINE_INPUT_OFFSET +
                            GPU_LINE_IMAGE_BYTES * GPU_LINE_LAYER_COUNT
};

static void
fill_lines(uint8_t *pixels, uint32_t lineCount) {
  for (uint32_t i = 0u; i < GPU_LINE_WIDTH * lineCount; i++) {
    pixels[i * GPU_LINE_PIXEL_BYTES + 0u] = 0u;
    pixels[i * GPU_LINE_PIXEL_BYTES + 1u] = 0u;
    pixels[i * GPU_LINE_PIXEL_BYTES + 2u] = 0u;
    pixels[i * GPU_LINE_PIXEL_BYTES + 3u] = 255u;
  }
}

static void
set_line_pixel(uint8_t *pixels,
               uint32_t line,
               uint32_t x,
               uint8_t  red,
               uint8_t  green,
               uint8_t  blue) {
  uint32_t offset;

  offset = (line * GPU_LINE_WIDTH + x) * GPU_LINE_PIXEL_BYTES;
  pixels[offset + 0u] = red;
  pixels[offset + 1u] = green;
  pixels[offset + 2u] = blue;
  pixels[offset + 3u] = 255u;
}

static int
check_line_layout(GPUShaderLayout *shaderLayout) {
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
  if (!entries || count != 4u) {
    return 0;
  }

  seen = 0u;
  for (uint32_t i = 0u; i < count; i++) {
    GPUBindingType expectedType;

    expectedType = entries[i].binding == 0u || entries[i].binding == 2u
                     ? GPU_BINDING_SAMPLED_TEXTURE
                     : GPU_BINDING_STORAGE_TEXTURE;
    if (entries[i].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
        entries[i].binding > 3u ||
        entries[i].bindingType != expectedType) {
      return 0;
    }
    seen |= 1u << entries[i].binding;
  }
  return seen == 0xfu;
}

static GPUResult
create_line_texture(GPUDevice            *device,
                    const char           *label,
                    uint32_t              layerCount,
                    GPUTextureUsageFlags  usage,
                    GPUTexture          **outTexture) {
  GPUTextureCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.dimension        = GPU_TEXTURE_DIMENSION_1D;
  info.format           = GPU_FORMAT_RGBA8_UNORM;
  info.width            = GPU_LINE_WIDTH;
  info.height           = 1u;
  info.depthOrLayers    = layerCount;
  info.mipLevelCount    = 1u;
  info.sampleCount      = 1u;
  info.usage            = usage;
  return GPUCreateTexture(device, &info, outTexture);
}

static GPUResult
create_line_view(GPUTexture         *texture,
                 const char         *label,
                 GPUTextureViewType  viewType,
                 uint32_t            baseLayer,
                 uint32_t            layerCount,
                 GPUTextureView    **outView) {
  GPUTextureViewCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.viewType         = viewType;
  info.format           = GPU_FORMAT_RGBA8_UNORM;
  info.mipLevelCount    = 1u;
  info.baseArrayLayer   = baseLayer;
  info.arrayLayerCount  = layerCount;
  return GPUCreateTextureView(texture, &info, outView);
}

static int
check_line_pixels(const uint8_t *pixels) {
  for (uint32_t line = 0u; line < GPU_LINE_LAYER_COUNT; line++) {
    for (uint32_t x = 0u; x < GPU_LINE_WIDTH; x++) {
      uint32_t offset;
      uint8_t  green;
      uint8_t  blue;

      offset = GPU_LINE_INPUT_OFFSET + line * GPU_LINE_IMAGE_BYTES +
               x * GPU_LINE_PIXEL_BYTES;
      green  = line == 2u && x == 2u ? 255u : 0u;
      blue   = line == 1u && x == 2u ? 255u : 0u;
      if (pixels[offset + 0u] != 0u ||
          pixels[offset + 1u] != green ||
          pixels[offset + 2u] != blue ||
          pixels[offset + 3u] != 255u) {
        fprintf(stderr,
                "line texture input mismatch at %u,%u: %u %u %u %u\n",
                line,
                x,
                (unsigned)pixels[offset + 0u],
                (unsigned)pixels[offset + 1u],
                (unsigned)pixels[offset + 2u],
                (unsigned)pixels[offset + 3u]);
        return 0;
      }
    }
  }

  for (uint32_t line = 0u; line < GPU_LINE_LAYER_COUNT + 1u; line++) {
    uint32_t baseOffset;

    baseOffset = line == 0u
                   ? GPU_LINE_SINGLE_OFFSET
                   : GPU_LINE_ARRAY_OFFSET +
                       (line - 1u) * GPU_LINE_IMAGE_BYTES;
    for (uint32_t x = 0u; x < GPU_LINE_WIDTH; x++) {
      uint8_t green;
      uint8_t blue;
      uint32_t offset;

      green = line == 0u && x == 1u ? 255u : 0u;
      blue  = ((line == 0u && x == 1u) ||
               (line == 1u && x == 1u))
                ? 255u
                : 0u;
      offset = baseOffset + x * GPU_LINE_PIXEL_BYTES;
      if (pixels[offset + 0u] != 0u ||
          pixels[offset + 1u] != green ||
          pixels[offset + 2u] != blue ||
          pixels[offset + 3u] != 255u) {
        fprintf(stderr,
                "line texture pixel mismatch at %u,%u: %u %u %u %u\n",
                line,
                x,
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
gpu_test_line_texture_view(GPUDevice *device, const char *bytecodePath) {
  GPUCommandQueue               *queue;
  GPUShaderLibrary              *library;
  GPUShaderLayout               *shaderLayout;
  GPUComputePipeline            *pipeline;
  GPUTexture                    *inputLine;
  GPUTexture                    *outputLine;
  GPUTexture                    *inputLines;
  GPUTexture                    *outputLines;
  GPUTextureView                *inputLineView;
  GPUTextureView                *outputLineView;
  GPUTextureView                *inputLinesView;
  GPUTextureView                *outputLinesView;
  GPUBuffer                     *readback;
  GPUBindGroup                  *group;
  GPUCommandBuffer              *cmdb;
  GPUComputePassEncoder         *computePass;
  GPUCopyPassEncoder            *copyPass;
  GPUFence                      *fence;
  void                          *bytecode;
  GPUCommandBuffer              *submitBuffers[1];
  GPUComputePipelineCreateInfo   pipelineInfo       = {0};
  GPUTextureWriteRegion          writeRegion        = {0};
  GPUBufferCreateInfo            bufferInfo         = {0};
  GPUBindGroupEntry              groupEntries[4]    = {0};
  GPUBindGroupCreateInfo         groupInfo           = {0};
  GPUTextureBarrier              textureBarriers[2] = {0};
  GPUBarrierBatch                barrierBatch        = {0};
  GPUBufferTextureCopyRegion     copyRegion          = {0};
  GPUQueueSubmitInfo             submitInfo          = {0};
  uint8_t                        inputLinePixels[GPU_LINE_BYTES];
  uint8_t                        outputLinePixels[GPU_LINE_BYTES];
  uint8_t                        inputArrayPixels[GPU_LINE_ARRAY_BYTES];
  uint8_t                        outputArrayPixels[GPU_LINE_ARRAY_BYTES];
  uint8_t                        pixels[GPU_LINE_READBACK_BYTES];
  uint64_t                       bytecodeSize;
  int                            ok;

  if (!device || !bytecodePath) {
    return 0;
  }

  queue           = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library         = NULL;
  shaderLayout    = NULL;
  pipeline        = NULL;
  inputLine       = NULL;
  outputLine      = NULL;
  inputLines      = NULL;
  outputLines     = NULL;
  inputLineView   = NULL;
  outputLineView  = NULL;
  inputLinesView  = NULL;
  outputLinesView = NULL;
  readback        = NULL;
  group           = NULL;
  cmdb            = NULL;
  computePass     = NULL;
  copyPass        = NULL;
  fence           = NULL;
  bytecodeSize    = 0u;
  bytecode        = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok              = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "line texture fixture setup failed\n");
    goto cleanup;
  }

  fill_lines(inputLinePixels, 1u);
  fill_lines(outputLinePixels, 1u);
  fill_lines(inputArrayPixels, GPU_LINE_LAYER_COUNT);
  fill_lines(outputArrayPixels, GPU_LINE_LAYER_COUNT);
  set_line_pixel(inputLinePixels, 0u, 2u, 0u, 255u, 255u);
  set_line_pixel(inputArrayPixels, 1u, 2u, 0u, 0u, 255u);
  set_line_pixel(inputArrayPixels, 2u, 2u, 0u, 255u, 0u);
  memset(pixels, 0, sizeof(pixels));

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !check_line_layout(shaderLayout)) {
    fprintf(stderr, "line texture shader layout creation failed\n");
    ok = 0;
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-line-texture-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "line_view_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "line texture compute pipeline creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (create_line_texture(device,
                          "api-line-input",
                          1u,
                          GPU_TEXTURE_USAGE_SAMPLED |
                            GPU_TEXTURE_USAGE_COPY_DST,
                          &inputLine) != GPU_OK ||
      create_line_texture(device,
                          "api-line-output",
                          1u,
                          GPU_TEXTURE_USAGE_STORAGE |
                            GPU_TEXTURE_USAGE_COPY_SRC |
                            GPU_TEXTURE_USAGE_COPY_DST,
                          &outputLine) != GPU_OK ||
      create_line_texture(device,
                          "api-lines-input",
                          GPU_LINE_LAYER_COUNT,
                          GPU_TEXTURE_USAGE_SAMPLED |
                            GPU_TEXTURE_USAGE_COPY_SRC |
                            GPU_TEXTURE_USAGE_COPY_DST,
                          &inputLines) != GPU_OK ||
      create_line_texture(device,
                          "api-lines-output",
                          GPU_LINE_LAYER_COUNT,
                          GPU_TEXTURE_USAGE_STORAGE |
                            GPU_TEXTURE_USAGE_COPY_SRC |
                            GPU_TEXTURE_USAGE_COPY_DST,
                          &outputLines) != GPU_OK) {
    fprintf(stderr, "line texture creation failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.width        = GPU_LINE_WIDTH;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = GPU_LINE_BYTES;
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(queue,
                           inputLine,
                           &writeRegion,
                           inputLinePixels,
                           sizeof(inputLinePixels)) != GPU_OK ||
      GPUQueueWriteTexture(queue,
                           outputLine,
                           &writeRegion,
                           outputLinePixels,
                           sizeof(outputLinePixels)) != GPU_OK) {
    fprintf(stderr, "single line texture upload failed\n");
    ok = 0;
    goto cleanup;
  }

  writeRegion.layerCount = GPU_LINE_LAYER_COUNT;
  if (GPUQueueWriteTexture(queue,
                           inputLines,
                           &writeRegion,
                           inputArrayPixels,
                           sizeof(inputArrayPixels)) != GPU_OK ||
      GPUQueueWriteTexture(queue,
                           outputLines,
                           &writeRegion,
                           outputArrayPixels,
                           sizeof(outputArrayPixels)) != GPU_OK) {
    fprintf(stderr, "array line texture upload failed\n");
    ok = 0;
    goto cleanup;
  }

  if (create_line_view(inputLine,
                       "api-line-input-view",
                       GPU_TEXTURE_VIEW_1D,
                       0u,
                       1u,
                       &inputLineView) != GPU_OK ||
      create_line_view(outputLine,
                       "api-line-output-view",
                       GPU_TEXTURE_VIEW_1D,
                       0u,
                       1u,
                       &outputLineView) != GPU_OK ||
      create_line_view(inputLines,
                       "api-lines-input-view",
                       GPU_TEXTURE_VIEW_1D_ARRAY,
                       1u,
                       2u,
                       &inputLinesView) != GPU_OK ||
      create_line_view(outputLines,
                       "api-lines-output-view",
                       GPU_TEXTURE_VIEW_1D_ARRAY,
                       0u,
                       GPU_LINE_LAYER_COUNT,
                       &outputLinesView) != GPU_OK) {
    fprintf(stderr, "line texture view creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-line-readback";
  bufferInfo.sizeBytes        = GPU_LINE_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "line texture readback buffer creation failed\n");
    ok = 0;
    goto cleanup;
  }

  groupEntries[0].binding     = 0u;
  groupEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[0].textureView = inputLineView;
  groupEntries[1].binding     = 1u;
  groupEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  groupEntries[1].textureView = outputLineView;
  groupEntries[2].binding     = 2u;
  groupEntries[2].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[2].textureView = inputLinesView;
  groupEntries[3].binding     = 3u;
  groupEntries[3].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  groupEntries[3].textureView = outputLinesView;

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-line-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 4u;
  groupInfo.pEntries         = groupEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "line texture bind group creation failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-line-texture", &cmdb) != GPU_OK ||
      !cmdb || !(computePass = GPUBeginComputePass(cmdb,
                                                   "api-line-texture"))) {
    fprintf(stderr, "line texture compute pass creation failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, pipeline);
  GPUBindComputeGroup(computePass, 0u, group, 0u, NULL);
  GPUDispatch(computePass, 1u, 1u, 1u);
  GPUEndComputePass(computePass);
  computePass = NULL;

  textureBarriers[0].texture    = outputLine;
  textureBarriers[0].srcAccess  = GPU_ACCESS_SHADER_WRITE;
  textureBarriers[0].dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[0].mipCount   = 1u;
  textureBarriers[0].layerCount = 1u;
  textureBarriers[1]            = textureBarriers[0];
  textureBarriers[1].texture    = outputLines;
  textureBarriers[1].layerCount = GPU_LINE_LAYER_COUNT;

  barrierBatch.srcStages           = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 2u;
  barrierBatch.pTextureBarriers    = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-line-readback");
  if (!copyPass) {
    fprintf(stderr, "line texture copy pass creation failed\n");
    ok = 0;
    goto cleanup;
  }

  copyRegion.bufferOffset       = GPU_LINE_SINGLE_OFFSET;
  copyRegion.bytesPerRow        = GPU_LINE_ROW_PITCH;
  copyRegion.rowsPerImage       = GPU_LINE_ROWS_PER_IMAGE;
  copyRegion.texture.width      = GPU_LINE_WIDTH;
  copyRegion.texture.height     = 1u;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, outputLine, readback, &copyRegion);

  copyRegion.bufferOffset                   = GPU_LINE_ARRAY_OFFSET;
  copyRegion.texture.texture.baseArrayLayer = 0u;
  copyRegion.texture.layerCount             = GPU_LINE_LAYER_COUNT;
  GPUCopyTextureToBuffer(copyPass, outputLines, readback, &copyRegion);

  copyRegion.bufferOffset = GPU_LINE_INPUT_OFFSET;
  GPUCopyTextureToBuffer(copyPass, inputLines, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "line texture fence creation failed\n");
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
    fprintf(stderr, "line texture submission failed\n");
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
      !check_line_pixels(pixels)) {
    fprintf(stderr, "line texture readback failed\n");
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
  GPUDestroyTextureView(outputLinesView);
  GPUDestroyTextureView(inputLinesView);
  GPUDestroyTextureView(outputLineView);
  GPUDestroyTextureView(inputLineView);
  GPUDestroyTexture(outputLines);
  GPUDestroyTexture(inputLines);
  GPUDestroyTexture(outputLine);
  GPUDestroyTexture(inputLine);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
