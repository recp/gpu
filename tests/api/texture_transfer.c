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
  TRANSFER_SECOND_COPY  = TRANSFER_IMAGE_STRIDE * 2u
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
transfer_fill_depth(uint8_t *pixels, uint32_t value) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    uint8_t *row;

    row = pixels + (uint64_t)y * TRANSFER_ROW_PITCH;
    for (uint32_t x = 0u; x < TRANSFER_WIDTH; x++) {
      memcpy(row + x * sizeof(value), &value, sizeof(value));
    }
  }
}

static bool
transfer_depth_equal(const uint8_t *pixels,
                     uint64_t       offset,
                     uint32_t       expected,
                     uint32_t       mask) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * TRANSFER_ROW_PITCH;
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
                       uint8_t        expected) {
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * TRANSFER_ROW_PITCH;
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
                GPUCommandQueue  *queue,
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
check_array_mip_transfers(GPUDevice *device) {
  GPUCommandQueue              *queue;
  GPUCommandBuffer             *cmdb;
  GPUCopyPassEncoder           *copyPass;
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
  copyPass = GPUBeginCopyPass(cmdb, "array-mip-transfer");
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
  GPUEndCopyPass(copyPass);
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
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(upload);
  return ok;
}

static int
check_3d_texture_transfers(GPUDevice *device) {
  GPUCommandQueue              *queue;
  GPUCommandBuffer             *cmdb;
  GPUCopyPassEncoder           *copyPass;
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
  copyPass = GPUBeginCopyPass(cmdb, "3d-texture-transfer");
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
  GPUEndCopyPass(copyPass);
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
    GPUEndCopyPass(copyPass);
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

  GPUCommandQueue              *queue;
  GPUCommandBuffer             *cmdb;
  GPUCopyPassEncoder           *copyPass;
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
  copyPass = GPUBeginCopyPass(cmdb, "same-texture-copy");
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
  GPUEndCopyPass(copyPass);
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
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(readback);
  return ok;
}

static int
check_depth_stencil_plane_copies(GPUDevice *device, GPUFormat format) {
  enum {
    DEPTH_AFTER_DEPTH_OFFSET     = 0u,
    STENCIL_AFTER_DEPTH_OFFSET   = TRANSFER_IMAGE_STRIDE,
    DEPTH_AFTER_STENCIL_OFFSET   = TRANSFER_IMAGE_STRIDE * 2u,
    STENCIL_AFTER_STENCIL_OFFSET = TRANSFER_IMAGE_STRIDE * 3u,
    READBACK_BYTES               = TRANSFER_IMAGE_STRIDE * 4u
  };

  GPUCommandQueue              *queue;
  GPUCommandBuffer             *cmdb;
  GPUCopyPassEncoder           *copyPass;
  GPUBuffer                    *readback;
  GPUTexture                   *source;
  GPUTexture                   *destination;
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUFormatCapabilities         formatCaps;
  uint8_t                       sourceDepth[TRANSFER_IMAGE_STRIDE] = {0};
  uint8_t                       sourceStencil[TRANSFER_IMAGE_STRIDE] = {0};
  uint8_t                       destinationDepth[TRANSFER_IMAGE_STRIDE] = {0};
  uint8_t                       destinationStencil[TRANSFER_IMAGE_STRIDE] = {0};
  uint8_t                       readbackBytes[READBACK_BYTES] = {0};
  uint32_t                      sourceDepthValue;
  uint32_t                      destinationDepthValue;
  uint32_t                      depthMask;
  bool                          depthAfterDepth;
  bool                          stencilAfterDepth;
  bool                          depthAfterStencil;
  bool                          stencilAfterStencil;
  int                           ok;

  if (GPUGetFormatCapabilities(device->phyDevice,
                               format,
                               &formatCaps) != GPU_OK ||
      !formatCaps.depthStencil) {
    printf("depth-stencil plane copy skipped: unsupported format=%u\n",
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
  transfer_fill_depth(sourceDepth, sourceDepthValue);
  transfer_fill_depth(destinationDepth, destinationDepthValue);
  for (uint32_t y = 0u; y < TRANSFER_HEIGHT; y++) {
    memset(sourceStencil + (uint64_t)y * TRANSFER_ROW_PITCH,
           91,
           TRANSFER_WIDTH);
    memset(destinationStencil + (uint64_t)y * TRANSFER_ROW_PITCH,
           17,
           TRANSFER_WIDTH);
  }

  cmdb        = NULL;
  copyPass    = NULL;
  readback    = NULL;
  source      = NULL;
  destination = NULL;
  ok          = 0;

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
  writeRegion.bytesPerRow    = TRANSFER_ROW_PITCH;
  writeRegion.rowsPerImage   = TRANSFER_HEIGHT;
  writeRegion.aspect         = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  if (GPUQueueWriteTexture(queue,
                           source,
                           &writeRegion,
                           sourceDepth,
                           sizeof(sourceDepth)) != GPU_OK ||
      GPUQueueWriteTexture(queue,
                           destination,
                           &writeRegion,
                           destinationDepth,
                           sizeof(destinationDepth)) != GPU_OK) {
    fprintf(stderr, "depth-stencil plane depth upload failed\n");
    goto cleanup;
  }
  writeRegion.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  if (GPUQueueWriteTexture(queue,
                           source,
                           &writeRegion,
                           sourceStencil,
                           sizeof(sourceStencil)) != GPU_OK ||
      GPUQueueWriteTexture(queue,
                           destination,
                           &writeRegion,
                           destinationStencil,
                           sizeof(destinationStencil)) != GPU_OK) {
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
  copyPass = GPUBeginCopyPass(cmdb, "depth-stencil-plane-copy");
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
  bufferRegion.bytesPerRow        = TRANSFER_ROW_PITCH;
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
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  ok   = transfer_submit(device, queue, cmdb);
  cmdb = NULL;
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
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterDepth   = transfer_stencil_equal(readbackBytes,
                                               STENCIL_AFTER_DEPTH_OFFSET,
                                               17u);
  depthAfterStencil   = transfer_depth_equal(readbackBytes,
                                             DEPTH_AFTER_STENCIL_OFFSET,
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterStencil = transfer_stencil_equal(
    readbackBytes,
    STENCIL_AFTER_STENCIL_OFFSET,
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
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyTexture(destination);
  GPUDestroyTexture(source);
  GPUDestroyBuffer(readback);
  return ok;
}

int
gpu_test_texture_transfer(GPUDevice *device) {
  return check_array_mip_transfers(device) &&
         check_3d_texture_transfers(device) &&
         check_same_texture_copies(device) &&
         check_depth_stencil_plane_copies(
           device,
           GPU_FORMAT_DEPTH32_FLOAT_STENCIL8
         ) &&
         check_depth_stencil_plane_copies(
           device,
           GPU_FORMAT_DEPTH24_UNORM_STENCIL8
         );
}
