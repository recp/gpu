#include "test.h"
#include "../../src/api/device_internal.h"

enum {
  INTEGER_CLEAR_WIDTH          = 4u,
  INTEGER_CLEAR_HEIGHT         = 4u,
  INTEGER_CLEAR_ROW_PITCH      = 256u,
  INTEGER_CLEAR_READBACK_BYTES = INTEGER_CLEAR_ROW_PITCH *
                                 INTEGER_CLEAR_HEIGHT
};

static bool
integer_clear_pixels_equal(const uint8_t *pixels,
                           const uint8_t  expected[4]) {
  for (uint32_t y = 0u; y < INTEGER_CLEAR_HEIGHT; y++) {
    const uint8_t *row;

    row = pixels + (uint64_t)y * INTEGER_CLEAR_ROW_PITCH;
    for (uint32_t x = 0u; x < INTEGER_CLEAR_WIDTH; x++) {
      if (memcmp(row + x * 4u, expected, 4u) != 0) {
        return false;
      }
    }
  }
  return true;
}

static bool
integer_clear_submit(GPUDevice        *device,
                     GPUQueue         *queue,
                     GPUCommandBuffer *cmdb) {
  GPUCommandBuffer  *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence          *fence;
  GPUResult          result;

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    return false;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  result                        = GPUQueueSubmit(queue, &submitInfo);
  if (result == GPU_OK) {
    result = GPUWaitFence(fence, UINT64_MAX);
  }
  GPUDestroyFence(fence);
  return result == GPU_OK;
}

static int
integer_clear_case(GPUDevice                *device,
                   GPUFormat                 format,
                   const GPUClearColorValue *clearColor,
                   const uint8_t             expected[4],
                   const char               *label) {
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *renderPass;
  GPUCopyPassEncoder           *copyPass;
  GPUTexture                   *texture;
  GPUTextureView               *view;
  GPUBuffer                    *readback;
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureViewCreateInfo      viewInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrierBatch = {0};
  GPUBufferTextureCopyRegion    copyRegion = {0};
  uint8_t                       pixels[INTEGER_CLEAR_READBACK_BYTES] = {0};
  int                           ok;

  queue      = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb       = NULL;
  renderPass = NULL;
  copyPass   = NULL;
  texture    = NULL;
  view       = NULL;
  readback   = NULL;
  ok         = queue != NULL;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = INTEGER_CLEAR_WIDTH;
  textureInfo.height           = INTEGER_CLEAR_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  ok = ok && GPUCreateTexture(device, &textureInfo, &texture) == GPU_OK &&
       texture;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  ok = ok && GPUCreateTextureView(texture, &viewInfo, &view) == GPU_OK && view;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = label;
  bufferInfo.sizeBytes        = INTEGER_CLEAR_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK &&
       readback;
  if (!ok || GPUAcquireCommandBuffer(queue, label, &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "%s setup failed\n", label);
    ok = 0;
    goto cleanup;
  }

  color.view                    = view;
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  color.clearColor              = *clearColor;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = label;
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "%s render pass failed\n", label);
    ok = 0;
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture    = texture;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, label);
  if (!copyPass) {
    fprintf(stderr, "%s copy pass failed\n", label);
    ok = 0;
    goto cleanup;
  }
  copyRegion.bytesPerRow        = INTEGER_CLEAR_ROW_PITCH;
  copyRegion.rowsPerImage       = INTEGER_CLEAR_HEIGHT;
  copyRegion.texture.width      = INTEGER_CLEAR_WIDTH;
  copyRegion.texture.height     = INTEGER_CLEAR_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  ok   = integer_clear_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok || GPUQueueReadBuffer(queue,
                                readback,
                                0u,
                                pixels,
                                sizeof(pixels)) != GPU_OK ||
      !integer_clear_pixels_equal(pixels, expected)) {
    fprintf(stderr,
            "%s mismatch: got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
            label,
            (unsigned)pixels[0],
            (unsigned)pixels[1],
            (unsigned)pixels[2],
            (unsigned)pixels[3],
            (unsigned)expected[0],
            (unsigned)expected[1],
            (unsigned)expected[2],
            (unsigned)expected[3]);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  return ok;
}

int
gpu_test_texture_integer_clear(GPUDevice *device) {
  static const GPUClearColorValue uintClear = {
    .uint32 = {1u, 2u, 3u, 4u}
  };
  static const GPUClearColorValue sintClear = {
    .sint32 = {-1, -2, 3, 4}
  };
  static const uint8_t uintExpected[4] = {1u, 2u, 3u, 4u};
  static const uint8_t sintExpected[4] = {255u, 254u, 3u, 4u};
  GPUFormatCapabilities caps;

  if (GPUGetFormatCapabilities(device->adapter,
                               GPU_FORMAT_RGBA8_UINT,
                               &caps) != GPU_OK ||
      !caps.colorAttachment) {
    printf("integer clear skipped: RGBA8_UINT unsupported\n");
    return 1;
  }
  if (!integer_clear_case(device,
                          GPU_FORMAT_RGBA8_UINT,
                          &uintClear,
                          uintExpected,
                          "texture-clear-uint")) {
    return 0;
  }

  if (GPUGetFormatCapabilities(device->adapter,
                               GPU_FORMAT_RGBA8_SINT,
                               &caps) != GPU_OK ||
      !caps.colorAttachment) {
    printf("integer clear skipped: RGBA8_SINT unsupported\n");
    return 1;
  }
  return integer_clear_case(device,
                            GPU_FORMAT_RGBA8_SINT,
                            &sintClear,
                            sintExpected,
                            "texture-clear-sint");
}
