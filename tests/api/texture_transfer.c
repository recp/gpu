#include "test.h"

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

int
gpu_test_texture_transfer(GPUDevice *device) {
  return check_array_mip_transfers(device) &&
         check_3d_texture_transfers(device);
}
