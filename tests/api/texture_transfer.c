#include "test.h"
#include "../../src/api/device_internal.h"

enum {
  TRANSFER_WIDTH        = 4u,
  TRANSFER_HEIGHT       = 4u,
  TRANSFER_PIXEL_BYTES  = 4u,
  TRANSFER_ROW_BYTES    = TRANSFER_WIDTH * TRANSFER_PIXEL_BYTES,
  TRANSFER_IMAGE_BYTES  = TRANSFER_ROW_BYTES * TRANSFER_HEIGHT,
  TRANSFER_ROW_PITCH    = 256u,
  TRANSFER_IMAGE_STRIDE = TRANSFER_ROW_PITCH * TRANSFER_HEIGHT,
  TRANSFER_SECOND_COPY  = TRANSFER_IMAGE_STRIDE * 2u,
  TRANSFER_DS_ROW_PITCH = 512u,
  TRANSFER_DS_STRIDE    = TRANSFER_DS_ROW_PITCH * TRANSFER_HEIGHT
};

static void
transfer_pack(uint8_t       *padded,
              const uint8_t *tight,
              uint32_t       imageCount) {
  for (uint32_t image = 0u; image < imageCount; image++) {
    for (uint32_t row = 0u; row < TRANSFER_HEIGHT; row++) {
      memcpy(padded + image * TRANSFER_IMAGE_STRIDE +
                        row * TRANSFER_ROW_PITCH,
             tight + image * TRANSFER_IMAGE_BYTES +
                     row * TRANSFER_ROW_BYTES,
             TRANSFER_ROW_BYTES);
    }
  }
}

static bool
transfer_equal(const uint8_t *tight,
               const uint8_t *padded,
               uint32_t       imageCount) {
  for (uint32_t image = 0u; image < imageCount; image++) {
    for (uint32_t row = 0u; row < TRANSFER_HEIGHT; row++) {
      if (memcmp(tight + image * TRANSFER_IMAGE_BYTES +
                          row * TRANSFER_ROW_BYTES,
                 padded + image * TRANSFER_IMAGE_STRIDE +
                            row * TRANSFER_ROW_PITCH,
                 TRANSFER_ROW_BYTES) != 0) {
        return false;
      }
    }
  }
  return true;
}

static void
transfer_fill_depth(uint8_t *pixels,
                    uint32_t rowPitch,
                    uint32_t value) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    uint8_t *row;

    row = pixels + (uint64_t)y * rowPitch;
    for (uint32_t x = 0u; x < TRANSFER_WIDTH; x++) {
      memcpy(row + x * sizeof(value), &value, sizeof(value));
    }
  }
}

static bool
transfer_depth_equal(const uint8_t *pixels,
                     uint64_t       offset,
                     uint32_t       rowPitch,
                     uint32_t       expected,
                     uint32_t       mask) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * rowPitch;
    for (uint32_t x = 0u; x < TRANSFER_WIDTH; x++) {
      uint32_t value;

      memcpy(&value, row + x * sizeof(value), sizeof(value));
      if ((value & mask) != expected) {
        return false;
      }
    }
  }
  return true;
}

static bool
transfer_stencil_equal(const uint8_t *pixels,
                       uint64_t       offset,
                       uint32_t       rowPitch,
                       uint8_t        expected) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * rowPitch;
    for (uint32_t x = 0u; x < TRANSFER_WIDTH; x++) {
      if (row[x] != expected) {
        return false;
      }
    }
  }
  return true;
}

static bool
transfer_submit(GPUDevice        *device,
                GPUQueue         *queue,
                GPUCommandBuffer *cmdb) {
  GPUCommandBuffer  *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence          *fence;
  GPUResult          submitResult;
  GPUResult          waitResult;
  bool               ok;

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
  submitResult = GPUQueueSubmit(queue, &submitInfo);
  waitResult   = submitResult == GPU_OK
                   ? GPUWaitFence(fence, UINT64_MAX)
                   : GPU_ERROR_BACKEND_FAILURE;
  ok = submitResult == GPU_OK && waitResult == GPU_OK;
  if (!ok) {
    fprintf(stderr,
            "texture transfer submit failed: submit=%d wait=%d\n",
            submitResult,
            waitResult);
  }
  GPUDestroyFence(fence);
  return ok;
}

static int
check_tight_texture_copies(GPUDevice *device) {
  enum {
    SOURCE_OFFSET      = 4u,
    DESTINATION_OFFSET = 12u
  };

  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *source;
  GPUBuffer                    *staging;
  GPUBuffer                    *destination;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUBufferCopyRegion           bufferCopy = {0};
  GPUBufferTextureCopyRegion    region = {0};
  GPUTextureToTextureCopyRegion textureCopy = {0};
  GPUBufferBarrier              bufferBarrier = {0};
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrier = {0};
  uint8_t                       sourceBytes[SOURCE_OFFSET +
                                            TRANSFER_IMAGE_BYTES] = {0};
  uint8_t                       destinationBytes[DESTINATION_OFFSET +
                                                 TRANSFER_IMAGE_BYTES] = {0};
  int                           ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "tight texture copy has no graphics queue\n");
    return 0;
  }

  for (uint32_t i = 0u; i < TRANSFER_IMAGE_BYTES; i++) {
    sourceBytes[SOURCE_OFFSET + i] = (uint8_t)(0x31u + i * 9u);
  }

  cmdb        = NULL;
  copyPass    = NULL;
  source      = NULL;
  staging     = NULL;
  destination = NULL;
  textureA    = NULL;
  textureB    = NULL;
  ok          = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "tight-copy-source";
  bufferInfo.sizeBytes        = sizeof(sourceBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &source) != GPU_OK || !source ||
      GPUQueueWriteBuffer(queue,
                          source,
                          0u,
                          sourceBytes,
                          sizeof(sourceBytes)) != GPU_OK) {
    fprintf(stderr, "tight texture source setup failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "tight-copy-staging";
  bufferInfo.sizeBytes = sizeof(destinationBytes);
  if (GPUCreateBuffer(device, &bufferInfo, &staging) != GPU_OK || !staging) {
    fprintf(stderr, "tight texture staging setup failed\n");
    goto cleanup;
  }
  bufferInfo.label = "tight-copy-destination";
  if (GPUCreateBuffer(device, &bufferInfo, &destination) != GPU_OK ||
      !destination) {
    fprintf(stderr, "tight texture destination setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "tight-copy-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = TRANSFER_WIDTH * 2u;
  textureInfo.height           = TRANSFER_HEIGHT * 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &textureA) != GPU_OK || !textureA ||
      GPUCreateTexture(device, &textureInfo, &textureB) != GPU_OK || !textureB) {
    fprintf(stderr, "tight texture setup failed\n");
    goto cleanup;
  }

  region.bufferOffset       = SOURCE_OFFSET;
  region.bytesPerRow        = TRANSFER_ROW_BYTES;
  region.rowsPerImage       = TRANSFER_HEIGHT;
  region.texture.texture.x  = 0u;
  region.texture.texture.y  = 0u;
  region.texture.width      = TRANSFER_WIDTH;
  region.texture.height     = TRANSFER_HEIGHT;
  region.texture.depth      = 1u;
  region.texture.layerCount = 1u;
  bufferCopy.sizeBytes      = sizeof(sourceBytes);
  textureCopy.width         = TRANSFER_WIDTH;
  textureCopy.height        = TRANSFER_HEIGHT;
  textureCopy.depth         = 1u;
  textureCopy.layerCount    = 1u;
  for (uint32_t iteration = 0u; iteration < 8u; iteration++) {
    if (GPUAcquireCommandBuffer(queue,
                                "tight-texture-copy",
                                &cmdb) != GPU_OK ||
        !cmdb || !(copyPass = GPUBeginTransferPass(cmdb, "tight-texture-copy"))) {
      fprintf(stderr, "tight texture command setup failed\n");
      goto cleanup;
    }

    GPUCopyBufferToBuffer(copyPass, source, staging, &bufferCopy);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    bufferBarrier.buffer    = staging;
    bufferBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
    bufferBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
    bufferBarrier.sizeBytes = sizeof(sourceBytes);
    barrier.pBufferBarriers    = &bufferBarrier;
    barrier.srcStages          = GPU_STAGE_TRANSFER;
    barrier.dstStages          = GPU_STAGE_TRANSFER;
    barrier.bufferBarrierCount = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    region.bufferOffset = SOURCE_OFFSET;
    copyPass = GPUBeginTransferPass(cmdb, "tight-buffer-to-texture");
    if (!copyPass) {
      fprintf(stderr, "tight buffer-to-texture pass failed\n");
      goto cleanup;
    }
    GPUCopyBufferToTexture(copyPass, staging, textureA, &region);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    barrier = (GPUBarrierBatch){0};
    textureBarrier.texture    = textureA;
    textureBarrier.srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
    textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
    textureBarrier.mipCount   = 1u;
    textureBarrier.layerCount = 1u;
    barrier.pTextureBarriers     = &textureBarrier;
    barrier.srcStages            = GPU_STAGE_TRANSFER;
    barrier.dstStages            = GPU_STAGE_TRANSFER;
    barrier.textureBarrierCount  = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    copyPass = GPUBeginTransferPass(cmdb, "tight-texture-copy");
    if (!copyPass) {
      fprintf(stderr, "tight texture-to-texture pass failed\n");
      goto cleanup;
    }
    GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureCopy);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    barrier = (GPUBarrierBatch){0};
    textureBarrier.texture   = textureB;
    textureBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
    textureBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
    barrier.pTextureBarriers     = &textureBarrier;
    barrier.srcStages            = GPU_STAGE_TRANSFER;
    barrier.dstStages            = GPU_STAGE_TRANSFER;
    barrier.textureBarrierCount  = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    region.bufferOffset = DESTINATION_OFFSET;
    copyPass = GPUBeginTransferPass(cmdb, "tight-texture-readback");
    if (!copyPass) {
      fprintf(stderr, "tight texture readback pass failed\n");
      goto cleanup;
    }
    GPUCopyTextureToBuffer(copyPass, textureB, destination, &region);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    ok   = transfer_submit(device, queue, cmdb);
    cmdb = NULL;
    if (!ok) {
      goto cleanup;
    }
    if (iteration == 0u) {
      GPUResetStats(device);
    }
  }
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "tight texture copy allocated after warm-up\n");
    ok = 0;
    goto cleanup;
  }
  if (GPUQueueReadBuffer(queue,
                         destination,
                         0u,
                         destinationBytes,
                         sizeof(destinationBytes)) != GPU_OK ||
      memcmp(sourceBytes + SOURCE_OFFSET,
             destinationBytes + DESTINATION_OFFSET,
             TRANSFER_IMAGE_BYTES) != 0) {
    fprintf(stderr, "tight texture copy readback mismatch\n");
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(destination);
  GPUDestroyBuffer(staging);
  GPUDestroyBuffer(source);
  return ok;
}

static int
check_array_mip_transfers(GPUDevice *device) {
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder           *copyPass;
  GPUBuffer                    *upload;
  GPUBuffer                    *readback;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  uint8_t                       written[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       copied[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       uploadBytes[TRANSFER_IMAGE_STRIDE * 2u] = {0};
  uint8_t                       readbackBytes[TRANSFER_SECOND_COPY * 2u] = {0};
  int                           ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "array texture transfer has no graphics queue\n");
    return 0;
  }

  for (uint32_t i = 0u; i < (uint32_t)sizeof(written); i++) {
    written[i] = (uint8_t)(0x21u + i * 7u);
    copied[i]  = (uint8_t)(0xd3u ^ (i * 11u));
  }
  transfer_pack(uploadBytes, copied, 2u);

  upload   = NULL;
  readback = NULL;
  textureA = NULL;
  textureB = NULL;
  cmdb     = NULL;
  copyPass = NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(uploadBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = GPUCreateBuffer(device, &bufferInfo, &upload) == GPU_OK &&
       GPUQueueWriteBuffer(queue,
                           upload,
                           0u,
                           uploadBytes,
                           sizeof(uploadBytes)) == GPU_OK;
  bufferInfo.sizeBytes = sizeof(readbackBytes);
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "array texture transfer buffer setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 8u;
  textureInfo.height           = 8u;
  textureInfo.depthOrLayers    = 3u;
  textureInfo.mipLevelCount    = 3u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "array texture transfer texture setup failed\n");
    goto cleanup;
  }

  writeRegion.width          = TRANSFER_WIDTH;
  writeRegion.height         = TRANSFER_HEIGHT;
  writeRegion.depth          = 1u;
  writeRegion.mipLevel       = 1u;
  writeRegion.baseArrayLayer = 1u;
  writeRegion.layerCount     = 2u;
  writeRegion.bytesPerRow    = TRANSFER_ROW_BYTES;
  writeRegion.rowsPerImage   = TRANSFER_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           textureA,
                           &writeRegion,
                           written,
                           sizeof(written)) != GPU_OK) {
    fprintf(stderr, "array mip texture write failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "array-mip-transfer", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "array mip transfer command buffer failed\n");
    ok = 0;
    goto cleanup;
  }
  copyPass = GPUBeginTransferPass(cmdb, "array-mip-transfer");
  if (!copyPass) {
    fprintf(stderr, "array mip transfer copy pass failed\n");
    ok = 0;
    goto cleanup;
  }

  textureRegion.src.mipLevel       = 1u;
  textureRegion.src.baseArrayLayer = 1u;
  textureRegion.dst.mipLevel       = 1u;
  textureRegion.dst.baseArrayLayer = 1u;
  textureRegion.width              = TRANSFER_WIDTH;
  textureRegion.height             = TRANSFER_HEIGHT;
  textureRegion.depth              = 1u;
  textureRegion.layerCount         = 2u;
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);

  bufferRegion.texture.texture.mipLevel       = 1u;
  bufferRegion.texture.texture.baseArrayLayer = 1u;
  bufferRegion.bytesPerRow                    = TRANSFER_ROW_PITCH;
  bufferRegion.rowsPerImage                   = TRANSFER_HEIGHT;
  bufferRegion.texture.width                  = TRANSFER_WIDTH;
  bufferRegion.texture.height                 = TRANSFER_HEIGHT;
  bufferRegion.texture.depth                  = 1u;
  bufferRegion.texture.layerCount             = 2u;
  GPUCopyTextureToBuffer(copyPass, textureB, readback, &bufferRegion);
  GPUCopyBufferToTexture(copyPass, upload, textureA, &bufferRegion);
  bufferRegion.bufferOffset = TRANSFER_SECOND_COPY;
  GPUCopyTextureToBuffer(copyPass, textureA, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         sizeof(readbackBytes)) != GPU_OK ||
      !transfer_equal(written, readbackBytes, 2u) ||
      !transfer_equal(copied,
                      readbackBytes + TRANSFER_SECOND_COPY,
                      2u)) {
    fprintf(stderr, "array mip transfer readback mismatch\n");
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(upload);
  return ok;
}

static int
check_3d_texture_transfers(GPUDevice *device) {
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder           *copyPass;
  GPUBuffer                    *upload;
  GPUBuffer                    *readback;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  uint8_t                       written[TRANSFER_IMAGE_BYTES * 4u];
  uint8_t                       copied[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       uploadBytes[TRANSFER_IMAGE_STRIDE * 2u] = {0};
  uint8_t                       readbackBytes[TRANSFER_SECOND_COPY * 2u] = {0};
  bool                          firstEqual;
  bool                          secondEqual;
  int                           ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "3D texture transfer has no graphics queue\n");
    return 0;
  }

  for (uint32_t i = 0u; i < (uint32_t)sizeof(written); i++) {
    written[i] = (uint8_t)(0x43u + i * 5u);
  }
  for (uint32_t i = 0u; i < (uint32_t)sizeof(copied); i++) {
    copied[i] = (uint8_t)(0xb7u ^ (i * 13u));
  }
  transfer_pack(uploadBytes, copied, 2u);

  upload   = NULL;
  readback = NULL;
  textureA = NULL;
  textureB = NULL;
  cmdb     = NULL;
  copyPass = NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(uploadBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = GPUCreateBuffer(device, &bufferInfo, &upload) == GPU_OK &&
       GPUQueueWriteBuffer(queue,
                           upload,
                           0u,
                           uploadBytes,
                           sizeof(uploadBytes)) == GPU_OK;
  bufferInfo.sizeBytes = sizeof(readbackBytes);
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "3D texture transfer buffer setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_3D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = TRANSFER_WIDTH;
  textureInfo.height           = TRANSFER_HEIGHT;
  textureInfo.depthOrLayers    = 4u;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "3D texture transfer texture setup failed\n");
    goto cleanup;
  }

  writeRegion.width        = TRANSFER_WIDTH;
  writeRegion.height       = TRANSFER_HEIGHT;
  writeRegion.depth        = 4u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = TRANSFER_ROW_BYTES;
  writeRegion.rowsPerImage = TRANSFER_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           textureA,
                           &writeRegion,
                           written,
                           sizeof(written)) != GPU_OK) {
    fprintf(stderr, "3D texture write failed\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "3d-texture-transfer", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "3D transfer command buffer failed\n");
    ok = 0;
    goto cleanup;
  }
  copyPass = GPUBeginTransferPass(cmdb, "3d-texture-transfer");
  if (!copyPass) {
    fprintf(stderr, "3D transfer copy pass failed\n");
    ok = 0;
    goto cleanup;
  }

  textureRegion.src.z      = 1u;
  textureRegion.dst.z      = 0u;
  textureRegion.width      = TRANSFER_WIDTH;
  textureRegion.height     = TRANSFER_HEIGHT;
  textureRegion.depth      = 2u;
  textureRegion.layerCount = 1u;
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);

  bufferRegion.bytesPerRow        = TRANSFER_ROW_PITCH;
  bufferRegion.rowsPerImage       = TRANSFER_HEIGHT;
  bufferRegion.texture.width      = TRANSFER_WIDTH;
  bufferRegion.texture.height     = TRANSFER_HEIGHT;
  bufferRegion.texture.depth      = 2u;
  bufferRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, textureB, readback, &bufferRegion);
  bufferRegion.texture.texture.z = 1u;
  GPUCopyBufferToTexture(copyPass, upload, textureA, &bufferRegion);
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);
  bufferRegion.bufferOffset       = TRANSFER_SECOND_COPY;
  bufferRegion.texture.texture.z = 0u;
  GPUCopyTextureToBuffer(copyPass, textureB, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "3D texture transfer readback failed\n");
    ok = 0;
    goto cleanup;
  }

  firstEqual = transfer_equal(written + TRANSFER_IMAGE_BYTES,
                              readbackBytes,
                              2u);
  secondEqual = transfer_equal(copied,
                               readbackBytes + TRANSFER_SECOND_COPY,
                               2u);
  if (!firstEqual || !secondEqual) {
    fprintf(stderr,
            "3D texture transfer mismatch: write=%u buffer=%u\n",
            firstEqual ? 1u : 0u,
            secondEqual ? 1u : 0u);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(upload);
  return ok;
}

static int
check_same_texture_copies(GPUDevice *device) {
  enum {
    LAYER_COPY_OFFSET = 0u,
    MIP_COPY_OFFSET   = TRANSFER_IMAGE_STRIDE,
    READBACK_BYTES    = TRANSFER_IMAGE_STRIDE * 2u
  };

  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder           *copyPass;
  GPUBuffer                    *readback;
  GPUTexture                   *texture;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  uint8_t                       sourceBytes[TRANSFER_IMAGE_BYTES];
  uint8_t                       readbackBytes[READBACK_BYTES] = {0};
  bool                          layerEqual;
  bool                          mipEqual;
  int                           ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "same-texture copy has no graphics queue\n");
    return 0;
  }

  for (uint32_t i = 0u; i < (uint32_t)sizeof(sourceBytes); i++) {
    sourceBytes[i] = (uint8_t)(0x35u + i * 13u);
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok       = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "same-texture-copy-readback";
  bufferInfo.sizeBytes        = READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "same-texture copy readback setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "same-texture-copy";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = TRANSFER_WIDTH * 2u;
  textureInfo.height           = TRANSFER_HEIGHT * 2u;
  textureInfo.depthOrLayers    = 2u;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "same-texture copy texture setup failed\n");
    goto cleanup;
  }

  writeRegion.width          = TRANSFER_WIDTH;
  writeRegion.height         = TRANSFER_HEIGHT;
  writeRegion.depth          = 1u;
  writeRegion.mipLevel       = 1u;
  writeRegion.baseArrayLayer = 0u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = TRANSFER_ROW_BYTES;
  writeRegion.rowsPerImage   = TRANSFER_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           sourceBytes,
                           sizeof(sourceBytes)) != GPU_OK) {
    fprintf(stderr, "same-texture copy upload failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "same-texture-copy", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "same-texture copy command buffer failed\n");
    goto cleanup;
  }
  copyPass = GPUBeginTransferPass(cmdb, "same-texture-copy");
  if (!copyPass) {
    fprintf(stderr, "same-texture copy pass failed\n");
    goto cleanup;
  }

  textureRegion.src.mipLevel       = 1u;
  textureRegion.src.baseArrayLayer = 0u;
  textureRegion.dst.mipLevel       = 1u;
  textureRegion.dst.baseArrayLayer = 1u;
  textureRegion.width              = TRANSFER_WIDTH;
  textureRegion.height             = TRANSFER_HEIGHT;
  textureRegion.depth              = 1u;
  textureRegion.layerCount         = 1u;
  GPUCopyTextureToTexture(copyPass, texture, texture, &textureRegion);

  textureRegion.src                  = textureRegion.dst;
  textureRegion.dst.x                = 1u;
  textureRegion.width                = TRANSFER_WIDTH - 1u;
  GPUCopyTextureToTexture(copyPass, texture, texture, &textureRegion);

  memset(&textureRegion, 0, sizeof(textureRegion));
  textureRegion.src.mipLevel = 1u;
  textureRegion.dst.mipLevel = 0u;
  textureRegion.width        = TRANSFER_WIDTH;
  textureRegion.height       = TRANSFER_HEIGHT;
  textureRegion.depth        = 1u;
  textureRegion.layerCount   = 1u;
  GPUCopyTextureToTexture(copyPass, texture, texture, &textureRegion);

  bufferRegion.texture.texture.mipLevel       = 1u;
  bufferRegion.texture.texture.baseArrayLayer = 1u;
  bufferRegion.texture.width                  = TRANSFER_WIDTH;
  bufferRegion.texture.height                 = TRANSFER_HEIGHT;
  bufferRegion.texture.depth                  = 1u;
  bufferRegion.texture.layerCount             = 1u;
  bufferRegion.bufferOffset                   = LAYER_COPY_OFFSET;
  bufferRegion.bytesPerRow                    = TRANSFER_ROW_PITCH;
  bufferRegion.rowsPerImage                   = TRANSFER_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &bufferRegion);

  bufferRegion.texture.texture.mipLevel       = 0u;
  bufferRegion.texture.texture.baseArrayLayer = 0u;
  bufferRegion.bufferOffset                   = MIP_COPY_OFFSET;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "same-texture copy readback failed\n");
    ok = 0;
    goto cleanup;
  }

  layerEqual = transfer_equal(sourceBytes,
                              readbackBytes + LAYER_COPY_OFFSET,
                              1u);
  mipEqual   = transfer_equal(sourceBytes,
                              readbackBytes + MIP_COPY_OFFSET,
                              1u);
  if (!layerEqual || !mipEqual) {
    fprintf(stderr,
            "same-texture copy mismatch: layer=%u mip=%u\n",
            layerEqual ? 1u : 0u,
            mipEqual ? 1u : 0u);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(readback);
  return ok;
}

static int
check_depth_stencil_plane_copies(GPUDevice *device, GPUFormat format) {
  enum {
    DEPTH_AFTER_DEPTH_OFFSET     = 0u,
    STENCIL_AFTER_DEPTH_OFFSET   = TRANSFER_DS_STRIDE,
    DEPTH_AFTER_STENCIL_OFFSET   = TRANSFER_DS_STRIDE * 2u,
    STENCIL_AFTER_STENCIL_OFFSET = TRANSFER_DS_STRIDE * 3u,
    READBACK_BYTES               = TRANSFER_DS_STRIDE * 4u
  };

  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder           *copyPass;
  GPUBuffer                    *readback;
  GPUTexture                   *source;
  GPUTexture                   *destination;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUFormatCapabilities         formatCaps;
  GPUResult                     sourceDepthWrite;
  GPUResult                     destinationDepthWrite;
  GPUResult                     sourceStencilWrite;
  GPUResult                     destinationStencilWrite;
  uint8_t                       sourceDepth[TRANSFER_DS_STRIDE] = {0};
  uint8_t                       sourceStencil[TRANSFER_DS_STRIDE] = {0};
  uint8_t                       destinationDepth[TRANSFER_DS_STRIDE] = {0};
  uint8_t                       destinationStencil[TRANSFER_DS_STRIDE] = {0};
  uint8_t                       readbackBytes[READBACK_BYTES] = {0};
  uint32_t                      sourceDepthValue;
  uint32_t                      destinationDepthValue;
  uint32_t                      depthMask;
  bool                          depthAfterDepth;
  bool                          stencilAfterDepth;
  bool                          depthAfterStencil;
  bool                          stencilAfterStencil;
  bool                          uploadsPending;
  int                           ok;

  if (GPUGetFormatCapabilities(device->adapter,
                               format,
                               &formatCaps) != GPU_OK ||
      !formatCaps.depthStencil) {
    printf("depth-stencil plane copy skipped: unsupported format=%u\n",
           (uint32_t)format);
    return 1;
  }
  if (device->inst->createInfo.preferredBackend == GPU_BACKEND_WEBGPU &&
      (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
       format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)) {
    printf("depth-stencil plane copy skipped: WebGPU format=%u\n",
           (uint32_t)format);
    return 1;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "depth-stencil plane copy has no graphics queue\n");
    return 0;
  }

  if (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8) {
    sourceDepthValue      = UINT32_C(0x00bfffff);
    destinationDepthValue = UINT32_C(0x00400000);
    depthMask             = UINT32_C(0x00ffffff);
  } else {
    float sourceValue;
    float destinationValue;

    sourceValue      = 0.75f;
    destinationValue = 0.25f;
    memcpy(&sourceDepthValue, &sourceValue, sizeof(sourceDepthValue));
    memcpy(&destinationDepthValue,
           &destinationValue,
           sizeof(destinationDepthValue));
    depthMask = UINT32_MAX;
  }
  transfer_fill_depth(sourceDepth, TRANSFER_DS_ROW_PITCH, sourceDepthValue);
  transfer_fill_depth(destinationDepth,
                      TRANSFER_DS_ROW_PITCH,
                      destinationDepthValue);
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    memset(sourceStencil + (uint64_t)y * TRANSFER_DS_ROW_PITCH,
           91,
           TRANSFER_WIDTH);
    memset(destinationStencil + (uint64_t)y * TRANSFER_DS_ROW_PITCH,
           17,
           TRANSFER_WIDTH);
  }

  cmdb           = NULL;
  copyPass       = NULL;
  readback       = NULL;
  source         = NULL;
  destination    = NULL;
  uploadsPending = false;
  ok             = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "depth-stencil-plane-readback";
  bufferInfo.sizeBytes        = READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "depth-stencil plane readback setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = TRANSFER_WIDTH * 2u;
  textureInfo.height           = TRANSFER_HEIGHT * 2u;
  textureInfo.depthOrLayers    = 2u;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  textureInfo.label            = "depth-stencil-plane-source";
  if (GPUCreateTexture(device, &textureInfo, &source) != GPU_OK || !source) {
    fprintf(stderr, "depth-stencil plane source setup failed\n");
    goto cleanup;
  }
  textureInfo.label = "depth-stencil-plane-destination";
  if (GPUCreateTexture(device, &textureInfo, &destination) != GPU_OK ||
      !destination) {
    fprintf(stderr, "depth-stencil plane destination setup failed\n");
    goto cleanup;
  }

  writeRegion.width          = TRANSFER_WIDTH;
  writeRegion.height         = TRANSFER_HEIGHT;
  writeRegion.depth          = 1u;
  writeRegion.mipLevel       = 1u;
  writeRegion.baseArrayLayer = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = TRANSFER_DS_ROW_PITCH;
  writeRegion.rowsPerImage   = TRANSFER_HEIGHT;
  writeRegion.aspect         = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  sourceDepthWrite = GPUQueueWriteTexture(queue,
                                          source,
                                          &writeRegion,
                                          sourceDepth,
                                          sizeof(sourceDepth));
  destinationDepthWrite = GPUQueueWriteTexture(queue,
                                               destination,
                                               &writeRegion,
                                               destinationDepth,
                                               sizeof(destinationDepth));
  uploadsPending = sourceDepthWrite == GPU_OK ||
                   destinationDepthWrite == GPU_OK;
  if (sourceDepthWrite != GPU_OK || destinationDepthWrite != GPU_OK) {
    fprintf(stderr, "depth-stencil plane depth upload failed\n");
    goto cleanup;
  }
  writeRegion.aspect          = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  sourceStencilWrite          = GPUQueueWriteTexture(queue,
                                                     source,
                                                     &writeRegion,
                                                     sourceStencil,
                                                     sizeof(sourceStencil));
  destinationStencilWrite     = GPUQueueWriteTexture(
    queue,
    destination,
    &writeRegion,
    destinationStencil,
    sizeof(destinationStencil)
  );
  uploadsPending = uploadsPending || sourceStencilWrite == GPU_OK ||
                   destinationStencilWrite == GPU_OK;
  if (sourceStencilWrite == GPU_ERROR_UNSUPPORTED ||
      destinationStencilWrite == GPU_ERROR_UNSUPPORTED) {
    printf("depth-stencil plane copy skipped: backend limitation format=%u\n",
           (uint32_t)format);
    ok = 1;
    goto cleanup;
  }
  if (sourceStencilWrite != GPU_OK || destinationStencilWrite != GPU_OK) {
    fprintf(stderr, "depth-stencil plane stencil upload failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "depth-stencil-plane-copy",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "depth-stencil plane command buffer failed\n");
    goto cleanup;
  }
  copyPass = GPUBeginTransferPass(cmdb, "depth-stencil-plane-copy");
  if (!copyPass) {
    fprintf(stderr, "depth-stencil plane copy pass failed\n");
    goto cleanup;
  }

  textureRegion.src.aspect         = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  textureRegion.src.mipLevel       = 1u;
  textureRegion.src.baseArrayLayer = 1u;
  textureRegion.dst.aspect         = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  textureRegion.dst.mipLevel       = 1u;
  textureRegion.dst.baseArrayLayer = 1u;
  textureRegion.width              = TRANSFER_WIDTH;
  textureRegion.height             = TRANSFER_HEIGHT;
  textureRegion.depth              = 1u;
  textureRegion.layerCount         = 1u;
  GPUCopyTextureToTexture(copyPass, source, destination, &textureRegion);

  bufferRegion.texture.texture    = textureRegion.dst;
  bufferRegion.texture.width      = TRANSFER_WIDTH;
  bufferRegion.texture.height     = TRANSFER_HEIGHT;
  bufferRegion.texture.depth      = 1u;
  bufferRegion.texture.layerCount = 1u;
  bufferRegion.bufferOffset       = DEPTH_AFTER_DEPTH_OFFSET;
  bufferRegion.bytesPerRow        = TRANSFER_DS_ROW_PITCH;
  bufferRegion.rowsPerImage       = TRANSFER_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);
  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  bufferRegion.bufferOffset           = STENCIL_AFTER_DEPTH_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);

  GPUResetStats(device);
  textureRegion.src.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  textureRegion.dst.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  GPUCopyTextureToTexture(copyPass, source, destination, &textureRegion);
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "depth-stencil plane warm copy allocated\n");
    goto cleanup;
  }

  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  bufferRegion.bufferOffset           = DEPTH_AFTER_STENCIL_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);
  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  bufferRegion.bufferOffset           = STENCIL_AFTER_STENCIL_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (ok) {
    uploadsPending = false;
  }
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         readbackBytes,
                         sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "depth-stencil plane copy readback failed\n");
    ok = 0;
    goto cleanup;
  }

  depthAfterDepth     = transfer_depth_equal(readbackBytes,
                                             DEPTH_AFTER_DEPTH_OFFSET,
                                             TRANSFER_DS_ROW_PITCH,
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterDepth   = transfer_stencil_equal(readbackBytes,
                                               STENCIL_AFTER_DEPTH_OFFSET,
                                               TRANSFER_DS_ROW_PITCH,
                                               17u);
  depthAfterStencil   = transfer_depth_equal(readbackBytes,
                                             DEPTH_AFTER_STENCIL_OFFSET,
                                             TRANSFER_DS_ROW_PITCH,
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterStencil = transfer_stencil_equal(
    readbackBytes,
    STENCIL_AFTER_STENCIL_OFFSET,
    TRANSFER_DS_ROW_PITCH,
    91u);
  if (!depthAfterDepth || !stencilAfterDepth ||
      !depthAfterStencil || !stencilAfterStencil) {
    fprintf(stderr,
            "depth-stencil plane copy mismatch: depth=%u/%u stencil=%u/%u "
            "values=%u/%u\n",
            depthAfterDepth ? 1u : 0u,
            depthAfterStencil ? 1u : 0u,
            stencilAfterDepth ? 1u : 0u,
            stencilAfterStencil ? 1u : 0u,
            readbackBytes[STENCIL_AFTER_DEPTH_OFFSET],
            readbackBytes[STENCIL_AFTER_STENCIL_OFFSET]);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
    copyPass = NULL;
  }
  if (uploadsPending) {
    if (!cmdb &&
        (GPUAcquireCommandBuffer(queue,
                                 "depth-stencil-upload-wait",
                                 &cmdb) != GPU_OK ||
         !cmdb)) {
      ok = 0;
    } else if (!transfer_submit(device, queue, cmdb)) {
      ok = 0;
    }
    cmdb = NULL;
  }
  GPUDestroyTexture(destination);
  GPUDestroyTexture(source);
  GPUDestroyBuffer(readback);
  return ok;
}

static int
check_large_texture_write(GPUDevice *device) {
  enum {
    LARGE_WIDTH       = 2048u,
    LARGE_HEIGHT      = 2048u,
    LARGE_PIXEL_BYTES = 4u,
    LARGE_ROW_BYTES   = LARGE_WIDTH * LARGE_PIXEL_BYTES,
    LARGE_IMAGE_BYTES = LARGE_ROW_BYTES * LARGE_HEIGHT
  };
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  GPUTexture                *texture;
  GPUBufferCreateInfo        bufferInfo = {0};
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  uint8_t                   *pixels;
  int                        ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "large texture write has no graphics queue\n");
    return 0;
  }

  pixels = malloc(LARGE_IMAGE_BYTES);
  if (!pixels) {
    return 0;
  }
  for (uint32_t y = 0u; y < LARGE_HEIGHT; y++) {
    for (uint32_t x = 0u; x < LARGE_ROW_BYTES; x++) {
      pixels[(uint64_t)y * LARGE_ROW_BYTES + x] =
        (uint8_t)(0x2bu + y * 17u + x * 11u);
    }
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok       = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "large-texture-write";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = LARGE_WIDTH;
  textureInfo.height           = LARGE_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "large texture setup failed\n");
    goto cleanup;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = LARGE_WIDTH;
  writeRegion.height       = LARGE_HEIGHT;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = LARGE_ROW_BYTES;
  writeRegion.rowsPerImage = LARGE_HEIGHT;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           pixels,
                           LARGE_IMAGE_BYTES) != GPU_OK) {
    fprintf(stderr, "large texture upload failed\n");
    goto cleanup;
  }
  free(pixels);
  pixels = NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "large-texture-readback";
  bufferInfo.sizeBytes        = LARGE_IMAGE_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback ||
      GPUAcquireCommandBuffer(queue,
                              "large-texture-readback",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(copyPass = GPUBeginTransferPass(cmdb,
                                        "large-texture-readback"))) {
    fprintf(stderr, "large texture readback setup failed\n");
    goto cleanup;
  }

  copyRegion.bytesPerRow        = LARGE_ROW_BYTES;
  copyRegion.rowsPerImage       = LARGE_HEIGHT;
  copyRegion.texture.width      = LARGE_WIDTH;
  copyRegion.texture.height     = LARGE_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  pixels = malloc(LARGE_IMAGE_BYTES);
  if (!ok || !pixels ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         pixels,
                         LARGE_IMAGE_BYTES) != GPU_OK) {
    fprintf(stderr, "large texture readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (uint32_t y = 0u; y < LARGE_HEIGHT; y++) {
    for (uint32_t x = 0u; x < LARGE_ROW_BYTES; x++) {
      uint8_t expected;

      expected = (uint8_t)(0x2bu + y * 17u + x * 11u);
      if (pixels[(uint64_t)y * LARGE_ROW_BYTES + x] != expected) {
        fprintf(stderr,
                "large texture mismatch at row=%u byte=%u\n",
                y,
                x);
        ok = 0;
        goto cleanup;
      }
    }
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(texture);
  free(pixels);
  return ok;
}

static int
check_sequential_large_texture_writes(GPUDevice *device) {
  enum {
    TEXTURE_COUNT       = 5u,
    TEXTURE_WIDTH       = 2048u,
    TEXTURE_HEIGHT      = 2048u,
    TEXTURE_PIXEL_BYTES = 4u,
    TEXTURE_ROW_BYTES   = TEXTURE_WIDTH * TEXTURE_PIXEL_BYTES,
    TEXTURE_IMAGE_BYTES = TEXTURE_ROW_BYTES * TEXTURE_HEIGHT,
    DIFFUSE_CUBE_SIZE   = 32u,
    SPECULAR_CUBE_SIZE  = 64u,
    SPECULAR_MIP_COUNT  = 7u,
    CUBE_FACE_COUNT     = 6u,
    CUBE_PIXEL_BYTES    = 8u
  };
  GPUTexture                *textures[TEXTURE_COUNT] = {0};
  GPUTexture                *diffuseCube;
  GPUTexture                *specularCube;
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  GPUBufferCreateInfo        bufferInfo = {0};
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  uint8_t                   *pixels;
  int                        ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "sequential large texture writes have no graphics queue\n");
    return 0;
  }

  pixels = malloc(TEXTURE_IMAGE_BYTES);
  if (!pixels) {
    return 0;
  }

  cmdb         = NULL;
  copyPass     = NULL;
  readback     = NULL;
  diffuseCube  = NULL;
  specularCube = NULL;
  ok           = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sequential-large-texture-write";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.width            = TEXTURE_WIDTH;
  textureInfo.height           = TEXTURE_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TEXTURE_WIDTH;
  writeRegion.height       = TEXTURE_HEIGHT;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = TEXTURE_ROW_BYTES;
  writeRegion.rowsPerImage = TEXTURE_HEIGHT;

  for (uint32_t textureIndex = 0u;
       textureIndex < TEXTURE_COUNT;
       textureIndex++) {
    textureInfo.format = textureIndex == 0u
                           ? GPU_FORMAT_RGBA8_UNORM_SRGB
                           : GPU_FORMAT_RGBA8_UNORM;
    for (uint32_t y = 0u; y < TEXTURE_HEIGHT; y++) {
      for (uint32_t x = 0u; x < TEXTURE_ROW_BYTES; x++) {
        pixels[(uint64_t)y * TEXTURE_ROW_BYTES + x] =
          (uint8_t)(0x35u + textureIndex * 41u + y * 17u + x * 11u);
      }
    }
    if (GPUCreateTexture(device,
                         &textureInfo,
                         &textures[textureIndex]) != GPU_OK ||
        !textures[textureIndex] ||
        GPUQueueWriteTexture(queue,
                             textures[textureIndex],
                             &writeRegion,
                             pixels,
                             TEXTURE_IMAGE_BYTES) != GPU_OK) {
      fprintf(stderr,
              "sequential large texture write failed at texture=%u\n",
              textureIndex);
      goto cleanup;
    }
  }

  textureInfo.label         = "sequential-diffuse-cube-write";
  textureInfo.format        = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width         = DIFFUSE_CUBE_SIZE;
  textureInfo.height        = DIFFUSE_CUBE_SIZE;
  textureInfo.depthOrLayers = CUBE_FACE_COUNT;
  if (GPUCreateTexture(device, &textureInfo, &diffuseCube) != GPU_OK ||
      !diffuseCube) {
    fprintf(stderr, "sequential diffuse cube setup failed\n");
    goto cleanup;
  }

  writeRegion.width        = DIFFUSE_CUBE_SIZE;
  writeRegion.height       = DIFFUSE_CUBE_SIZE;
  writeRegion.bytesPerRow  = DIFFUSE_CUBE_SIZE * CUBE_PIXEL_BYTES;
  writeRegion.rowsPerImage = DIFFUSE_CUBE_SIZE;
  for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
    uint64_t faceBytes;

    faceBytes = (uint64_t)writeRegion.bytesPerRow * writeRegion.height;
    for (uint64_t i = 0u; i < faceBytes; i++) {
      pixels[i] = (uint8_t)(0x19u + face * 43u + i * 7u);
    }
    writeRegion.baseArrayLayer = face;
    if (GPUQueueWriteTexture(queue,
                             diffuseCube,
                             &writeRegion,
                             pixels,
                             faceBytes) != GPU_OK) {
      fprintf(stderr,
              "sequential diffuse cube write failed at face=%u\n",
              face);
      goto cleanup;
    }
  }

  textureInfo.label         = "sequential-specular-cube-write";
  textureInfo.width         = SPECULAR_CUBE_SIZE;
  textureInfo.height        = SPECULAR_CUBE_SIZE;
  textureInfo.mipLevelCount = SPECULAR_MIP_COUNT;
  if (GPUCreateTexture(device, &textureInfo, &specularCube) != GPU_OK ||
      !specularCube) {
    fprintf(stderr, "sequential specular cube setup failed\n");
    goto cleanup;
  }

  for (uint32_t mip = 0u; mip < SPECULAR_MIP_COUNT; mip++) {
    uint32_t size;
    uint64_t faceBytes;

    size                     = SPECULAR_CUBE_SIZE >> mip;
    writeRegion.width        = size;
    writeRegion.height       = size;
    writeRegion.mipLevel     = mip;
    writeRegion.bytesPerRow  = size * CUBE_PIXEL_BYTES;
    writeRegion.rowsPerImage = size;
    faceBytes                = (uint64_t)writeRegion.bytesPerRow * size;
    for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
      for (uint64_t i = 0u; i < faceBytes; i++) {
        pixels[i] = (uint8_t)(0x2du + mip * 29u + face * 47u + i * 11u);
      }
      writeRegion.baseArrayLayer = face;
      if (GPUQueueWriteTexture(queue,
                               specularCube,
                               &writeRegion,
                               pixels,
                               faceBytes) != GPU_OK) {
        fprintf(stderr,
                "sequential specular cube write failed at mip=%u face=%u\n",
                mip,
                face);
        goto cleanup;
      }
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sequential-large-texture-readback";
  bufferInfo.sizeBytes        = TEXTURE_IMAGE_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback ||
      GPUAcquireCommandBuffer(queue,
                              "sequential-large-texture-readback",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(copyPass = GPUBeginTransferPass(
          cmdb,
          "sequential-large-texture-readback"))) {
    fprintf(stderr, "sequential large texture readback setup failed\n");
    goto cleanup;
  }

  copyRegion.bytesPerRow        = TEXTURE_ROW_BYTES;
  copyRegion.rowsPerImage       = TEXTURE_HEIGHT;
  copyRegion.texture.width      = TEXTURE_WIDTH;
  copyRegion.texture.height     = TEXTURE_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, textures[0], readback, &copyRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         pixels,
                         TEXTURE_IMAGE_BYTES) != GPU_OK) {
    fprintf(stderr, "sequential large texture readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (uint32_t y = 0u; y < TEXTURE_HEIGHT; y++) {
    for (uint32_t x = 0u; x < TEXTURE_ROW_BYTES; x++) {
      uint8_t expected;

      expected = (uint8_t)(0x35u + y * 17u + x * 11u);
      if (pixels[(uint64_t)y * TEXTURE_ROW_BYTES + x] != expected) {
        fprintf(stderr,
                "sequential large texture mismatch at row=%u byte=%u\n",
                y,
                x);
        ok = 0;
        goto cleanup;
      }
    }
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(specularCube);
  GPUDestroyTexture(diffuseCube);
  for (uint32_t i = 0u; i < TEXTURE_COUNT; i++) {
    GPUDestroyTexture(textures[i]);
  }
  free(pixels);
  return ok;
}

static int
check_sequential_cubemap_writes(GPUDevice *device) {
  enum {
    CUBE_BASE_SIZE   = 37u,
    CUBE_FACE_COUNT  = 6u,
    CUBE_MIP_COUNT   = 4u,
    CUBE_PIXEL_BYTES = 8u,
    CUBE_COPY_COUNT  = CUBE_FACE_COUNT * CUBE_MIP_COUNT
  };
  typedef struct CubeCopy {
    uint64_t tightOffset;
    uint64_t readbackOffset;
    uint32_t rowPitch;
    uint32_t width;
    uint32_t height;
    uint32_t mip;
    uint32_t face;
  } CubeCopy;

  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  GPUTexture                *texture;
  GPUBufferCreateInfo        bufferInfo = {0};
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  CubeCopy                   copies[CUBE_COPY_COUNT];
  uint8_t                   *expected;
  uint8_t                   *actual;
  uint64_t                   tightBytes;
  uint64_t                   readbackBytes;
  uint32_t                   copyIndex;
  int                        ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "sequential cubemap write has no graphics queue\n");
    return 0;
  }

  tightBytes    = 0u;
  readbackBytes = 0u;
  copyIndex     = 0u;
  for (uint32_t mip = 0u; mip < CUBE_MIP_COUNT; mip++) {
    uint32_t size;
    uint32_t rowBytes;
    uint32_t rowPitch;

    size     = CUBE_BASE_SIZE >> mip;
    rowBytes = size * CUBE_PIXEL_BYTES;
    rowPitch = (rowBytes + 255u) & ~255u;
    for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
      CubeCopy *copy;

      readbackBytes = (readbackBytes + 511u) & ~511u;
      copy                 = &copies[copyIndex++];
      copy->tightOffset    = tightBytes;
      copy->readbackOffset = readbackBytes;
      copy->rowPitch       = rowPitch;
      copy->width          = size;
      copy->height         = size;
      copy->mip            = mip;
      copy->face           = face;
      tightBytes    += (uint64_t)rowBytes * size;
      readbackBytes += (uint64_t)rowPitch * size;
    }
  }

  expected = malloc((size_t)tightBytes);
  actual   = calloc(1u, (size_t)readbackBytes);
  if (!expected || !actual) {
    free(actual);
    free(expected);
    return 0;
  }

  for (uint32_t i = 0u; i < CUBE_COPY_COUNT; i++) {
    const CubeCopy *copy;
    uint32_t        rowBytes;

    copy     = &copies[i];
    rowBytes = copy->width * CUBE_PIXEL_BYTES;
    for (uint32_t y = 0u; y < copy->height; y++) {
      uint8_t *row;

      row = expected + copy->tightOffset + (uint64_t)y * rowBytes;
      for (uint32_t x = 0u; x < rowBytes; x++) {
        row[x] = (uint8_t)(0x17u + copy->mip * 31u +
                           copy->face * 47u + y * 13u + x * 7u);
      }
    }
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok       = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sequential-cubemap-write";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width            = CUBE_BASE_SIZE;
  textureInfo.height           = CUBE_BASE_SIZE;
  textureInfo.depthOrLayers    = CUBE_FACE_COUNT;
  textureInfo.mipLevelCount    = CUBE_MIP_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "sequential cubemap texture setup failed\n");
    goto cleanup;
  }

  writeRegion.aspect     = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.depth      = 1u;
  writeRegion.layerCount = 1u;
  for (uint32_t i = 0u; i < CUBE_COPY_COUNT; i++) {
    const CubeCopy *copy;
    uint32_t        rowBytes;
    uint64_t        imageBytes;

    copy                     = &copies[i];
    rowBytes                 = copy->width * CUBE_PIXEL_BYTES;
    imageBytes               = (uint64_t)rowBytes * copy->height;
    writeRegion.width        = copy->width;
    writeRegion.height       = copy->height;
    writeRegion.mipLevel     = copy->mip;
    writeRegion.baseArrayLayer = copy->face;
    writeRegion.bytesPerRow  = rowBytes;
    writeRegion.rowsPerImage = copy->height;
    if (GPUQueueWriteTexture(queue,
                             texture,
                             &writeRegion,
                             expected + copy->tightOffset,
                             imageBytes) != GPU_OK) {
      fprintf(stderr,
              "sequential cubemap write failed at mip=%u face=%u\n",
              copy->mip,
              copy->face);
      goto cleanup;
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sequential-cubemap-readback";
  bufferInfo.sizeBytes        = readbackBytes;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback ||
      GPUAcquireCommandBuffer(queue,
                              "sequential-cubemap-readback",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(copyPass = GPUBeginTransferPass(cmdb,
                                        "sequential-cubemap-readback"))) {
    fprintf(stderr, "sequential cubemap readback setup failed\n");
    goto cleanup;
  }

  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  for (uint32_t i = 0u; i < CUBE_COPY_COUNT; i++) {
    const CubeCopy *copy;

    copy                                      = &copies[i];
    copyRegion.bufferOffset                   = copy->readbackOffset;
    copyRegion.bytesPerRow                    = copy->rowPitch;
    copyRegion.rowsPerImage                   = copy->height;
    copyRegion.texture.texture.mipLevel        = copy->mip;
    copyRegion.texture.texture.baseArrayLayer = copy->face;
    copyRegion.texture.width                   = copy->width;
    copyRegion.texture.height                  = copy->height;
    GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  }
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         actual,
                         readbackBytes) != GPU_OK) {
    fprintf(stderr, "sequential cubemap readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (uint32_t i = 0u; i < CUBE_COPY_COUNT; i++) {
    const CubeCopy *copy;
    uint32_t        rowBytes;

    copy     = &copies[i];
    rowBytes = copy->width * CUBE_PIXEL_BYTES;
    for (uint32_t y = 0u; y < copy->height; y++) {
      if (memcmp(expected + copy->tightOffset + (uint64_t)y * rowBytes,
                 actual + copy->readbackOffset +
                   (uint64_t)y * copy->rowPitch,
                 rowBytes) != 0) {
        fprintf(stderr,
                "sequential cubemap mismatch at mip=%u face=%u row=%u\n",
                copy->mip,
                copy->face,
                y);
        ok = 0;
        goto cleanup;
      }
    }
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(texture);
  free(actual);
  free(expected);
  return ok;
}

int
gpu_test_texture_transfer(GPUDevice *device) {
  return check_tight_texture_copies(device) &&
         check_array_mip_transfers(device) &&
         check_3d_texture_transfers(device) &&
         check_same_texture_copies(device) &&
         check_large_texture_write(device) &&
         check_sequential_large_texture_writes(device) &&
         check_sequential_cubemap_writes(device) &&
         check_depth_stencil_plane_copies(
           device,
           GPU_FORMAT_DEPTH32_FLOAT_STENCIL8
         ) &&
         check_depth_stencil_plane_copies(
           device,
           GPU_FORMAT_DEPTH24_UNORM_STENCIL8
         );
}
