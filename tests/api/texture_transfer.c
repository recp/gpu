/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

enum {
  TIGHT_SOURCE_OFFSET      = 4u,
  TIGHT_DESTINATION_OFFSET = 12u
};

enum {
  SAME_LAYER_COPY_OFFSET = 0u,
  SAME_MIP_COPY_OFFSET   = TRANSFER_IMAGE_STRIDE,
  SAME_READBACK_BYTES    = TRANSFER_IMAGE_STRIDE * 2u
};

enum {
  DS_DEPTH_AFTER_DEPTH_OFFSET     = 0u,
  DS_STENCIL_AFTER_DEPTH_OFFSET   = TRANSFER_DS_STRIDE,
  DS_DEPTH_AFTER_STENCIL_OFFSET   = TRANSFER_DS_STRIDE * 2u,
  DS_STENCIL_AFTER_STENCIL_OFFSET = TRANSFER_DS_STRIDE * 3u,
  DS_READBACK_BYTES               = TRANSFER_DS_STRIDE * 4u
};

enum {
  LARGE_WIDTH       = 2048u,
  LARGE_HEIGHT      = 2048u,
  LARGE_PIXEL_BYTES = 4u,
  LARGE_ROW_BYTES   = LARGE_WIDTH * LARGE_PIXEL_BYTES,
  LARGE_IMAGE_BYTES = LARGE_ROW_BYTES * LARGE_HEIGHT
};

enum {
  SEQUENCE_TEXTURE_COUNT       = 5u,
  SEQUENCE_TEXTURE_WIDTH       = 2048u,
  SEQUENCE_TEXTURE_HEIGHT      = 2048u,
  SEQUENCE_TEXTURE_PIXEL_BYTES = 4u,
  SEQUENCE_TEXTURE_ROW_BYTES   = SEQUENCE_TEXTURE_WIDTH * SEQUENCE_TEXTURE_PIXEL_BYTES,
  SEQUENCE_TEXTURE_IMAGE_BYTES = SEQUENCE_TEXTURE_ROW_BYTES * SEQUENCE_TEXTURE_HEIGHT,
  SEQUENCE_DIFFUSE_CUBE_SIZE   = 32u,
  SEQUENCE_SPECULAR_CUBE_SIZE  = 64u,
  SEQUENCE_SPECULAR_MIP_COUNT  = 7u,
  SEQUENCE_CUBE_FACE_COUNT     = 6u,
  SEQUENCE_CUBE_PIXEL_BYTES    = 8u
};

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

static void
transfer_pack(uint8_t       *padded,
              const uint8_t *tight,
              uint32_t       imageCount) {
  uint32_t image;
  uint32_t row;

  for (image = 0u; image < imageCount; image++) {
    for (row = 0u; row < TRANSFER_HEIGHT; row++) {
      memcpy(padded + image * TRANSFER_IMAGE_STRIDE + row * TRANSFER_ROW_PITCH,
             tight + image * TRANSFER_IMAGE_BYTES + row * TRANSFER_ROW_BYTES,
             TRANSFER_ROW_BYTES);
    }
  }
}

static bool
transfer_equal(const uint8_t *tight,
               const uint8_t *padded,
               uint32_t       imageCount) {
  uint32_t image;
  uint32_t row;

  for (image = 0u; image < imageCount; image++) {
    for (row = 0u; row < TRANSFER_HEIGHT; row++) {
      if (memcmp(tight + image * TRANSFER_IMAGE_BYTES + row * TRANSFER_ROW_BYTES,
                 padded + image * TRANSFER_IMAGE_STRIDE + row * TRANSFER_ROW_PITCH,
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
  uint8_t *row;
  uint32_t y;
  uint32_t x;

  for (y = 0u; y < TRANSFER_HEIGHT; y++) {
    row = pixels + (uint64_t)y * rowPitch;

    for (x = 0u; x < TRANSFER_WIDTH; x++) {
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
  const uint8_t *row;
  uint32_t       y;
  uint32_t       x;
  uint32_t       value;

  for (y = 0u; y < TRANSFER_HEIGHT; y++) {
    row = pixels + offset + (uint64_t)y * rowPitch;

    for (x = 0u; x < TRANSFER_WIDTH; x++) {
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
  const uint8_t *row;
  uint32_t       y;
  uint32_t       x;

  for (y = 0u; y < TRANSFER_HEIGHT; y++) {
    row = pixels + offset + (uint64_t)y * rowPitch;

    for (x = 0u; x < TRANSFER_WIDTH; x++) {
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

  buffers[0] = cmdb;
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
  GPUBufferCreateInfo           bufferInfo     = {0};
  GPUTextureCreateInfo          textureInfo    = {0};
  GPUBufferCopyRegion           bufferCopy     = {0};
  GPUBufferTextureCopyRegion    region         = {0};
  GPUTextureToTextureCopyRegion textureCopy    = {0};
  GPUBufferBarrier              bufferBarrier  = {0};
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrier        = {0};
  uint8_t                       sourceBytes[TIGHT_SOURCE_OFFSET +
                                            TRANSFER_IMAGE_BYTES] = {0};
  uint8_t                       destinationBytes[TIGHT_DESTINATION_OFFSET +
                                                 TRANSFER_IMAGE_BYTES] = {0};
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *source;
  GPUBuffer                    *staging;
  GPUBuffer                    *destination;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  int                           ok;
  uint32_t                      i;
  uint32_t                      iteration;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "tight texture copy has no graphics queue\n");
    return 0;
  }

  for (i = 0u; i < TRANSFER_IMAGE_BYTES; i++) {
    sourceBytes[TIGHT_SOURCE_OFFSET + i] = (uint8_t)(0x31u + i * 9u);
  }

  cmdb        = NULL;
  copyPass    = NULL;
  source      = NULL;
  staging     = NULL;
  destination = NULL;
  textureA    = NULL;
  textureB    = NULL;
  ok = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "tight-copy-source";
  bufferInfo.sizeBytes        = sizeof(sourceBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &source) != GPU_OK || !source
      || GPUQueueWriteBuffer(queue,
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

  if (GPUCreateBuffer(device, &bufferInfo, &destination) != GPU_OK
      || !destination) {
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

  if (GPUCreateTexture(device, &textureInfo, &textureA) != GPU_OK || !textureA
      || GPUCreateTexture(device, &textureInfo, &textureB) != GPU_OK || !textureB) {
    fprintf(stderr, "tight texture setup failed\n");
    goto cleanup;
  }

  region.bufferOffset       = TIGHT_SOURCE_OFFSET;
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

  for (iteration = 0u; iteration < 8u; iteration++) {
    if (GPUAcquireCommandBuffer(queue,
                                "tight-texture-copy",
                                &cmdb) != GPU_OK
        || !cmdb || !(copyPass = GPUBeginTransferPass(cmdb, "tight-texture-copy"))) {
      fprintf(stderr, "tight texture command setup failed\n");
      goto cleanup;
    }

    GPUCopyBufferToBuffer(copyPass, source, staging, &bufferCopy);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    bufferBarrier.buffer       = staging;
    bufferBarrier.srcAccess    = GPU_ACCESS_TRANSFER_WRITE;
    bufferBarrier.dstAccess    = GPU_ACCESS_TRANSFER_READ;
    bufferBarrier.sizeBytes    = sizeof(sourceBytes);
    barrier.pBufferBarriers    = &bufferBarrier;
    barrier.srcStages          = GPU_STAGE_TRANSFER;
    barrier.dstStages          = GPU_STAGE_TRANSFER;
    barrier.bufferBarrierCount = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    region.bufferOffset = TIGHT_SOURCE_OFFSET;

    if (!(copyPass = GPUBeginTransferPass(cmdb, "tight-buffer-to-texture"))) {
      fprintf(stderr, "tight buffer-to-texture pass failed\n");
      goto cleanup;
    }

    GPUCopyBufferToTexture(copyPass, staging, textureA, &region);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    barrier                     = (GPUBarrierBatch){0};
    textureBarrier.texture      = textureA;
    textureBarrier.srcAccess    = GPU_ACCESS_TRANSFER_WRITE;
    textureBarrier.dstAccess    = GPU_ACCESS_TRANSFER_READ;
    textureBarrier.mipCount     = 1u;
    textureBarrier.layerCount   = 1u;
    barrier.pTextureBarriers    = &textureBarrier;
    barrier.srcStages           = GPU_STAGE_TRANSFER;
    barrier.dstStages           = GPU_STAGE_TRANSFER;
    barrier.textureBarrierCount = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    if (!(copyPass = GPUBeginTransferPass(cmdb, "tight-texture-copy"))) {
      fprintf(stderr, "tight texture-to-texture pass failed\n");
      goto cleanup;
    }

    GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureCopy);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    barrier                     = (GPUBarrierBatch){0};
    textureBarrier.texture      = textureB;
    textureBarrier.srcAccess    = GPU_ACCESS_TRANSFER_WRITE;
    textureBarrier.dstAccess    = GPU_ACCESS_TRANSFER_READ;
    barrier.pTextureBarriers    = &textureBarrier;
    barrier.srcStages           = GPU_STAGE_TRANSFER;
    barrier.dstStages           = GPU_STAGE_TRANSFER;
    barrier.textureBarrierCount = 1u;
    GPUEncodeBarriers(cmdb, &barrier);

    region.bufferOffset = TIGHT_DESTINATION_OFFSET;

    if (!(copyPass = GPUBeginTransferPass(cmdb, "tight-texture-readback"))) {
      fprintf(stderr, "tight texture readback pass failed\n");
      goto cleanup;
    }

    GPUCopyTextureToBuffer(copyPass, textureB, destination, &region);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    ok = transfer_submit(device, queue, cmdb);
    cmdb = NULL;

    if (!ok) {
      goto cleanup;
    }

    if (iteration == 0u) {
      GPUResetStats(device);
    }
  }

  if (device->currentFrameStats.hotPathAllocCount != 0u
      || device->currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "tight texture copy allocated after warm-up\n");
    ok = 0;
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         destination,
                         0u,
                         destinationBytes,
                         sizeof(destinationBytes)) != GPU_OK
      || memcmp(sourceBytes + TIGHT_SOURCE_OFFSET,
                destinationBytes + TIGHT_DESTINATION_OFFSET,
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
  GPUBufferCreateInfo           bufferInfo    = {0};
  GPUTextureCreateInfo          textureInfo   = {0};
  GPUTextureWriteRegion         writeRegion   = {0};
  GPUBufferTextureCopyRegion    bufferRegion  = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  uint8_t                       written[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       copied[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       uploadBytes[TRANSFER_IMAGE_STRIDE * 2u]  = {0};
  uint8_t                       readbackBytes[TRANSFER_SECOND_COPY * 2u] = {0};
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *upload;
  GPUBuffer                    *readback;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  int                           ok;
  uint32_t                      i;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "array texture transfer has no graphics queue\n");
    return 0;
  }

  for (i = 0u; i < (uint32_t)sizeof(written); i++) {
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
  ok = GPUCreateBuffer(device, &bufferInfo, &upload) == GPU_OK
       && GPUQueueWriteBuffer(queue,
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
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK
       && GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;

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

  if (GPUAcquireCommandBuffer(queue, "array-mip-transfer", &cmdb) != GPU_OK
      || !cmdb) {
    fprintf(stderr, "array mip transfer command buffer failed\n");
    ok = 0;
    goto cleanup;
  }

  if (!(copyPass = GPUBeginTransferPass(cmdb, "array-mip-transfer"))) {
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

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            readbackBytes,
                            sizeof(readbackBytes)) != GPU_OK
      || !transfer_equal(written, readbackBytes, 2u)
      || !transfer_equal(copied,
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
  GPUBufferCreateInfo           bufferInfo    = {0};
  GPUTextureCreateInfo          textureInfo   = {0};
  GPUTextureWriteRegion         writeRegion   = {0};
  GPUBufferTextureCopyRegion    bufferRegion  = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  uint8_t                       written[TRANSFER_IMAGE_BYTES * 4u];
  uint8_t                       copied[TRANSFER_IMAGE_BYTES * 2u];
  uint8_t                       uploadBytes[TRANSFER_IMAGE_STRIDE * 2u]  = {0};
  uint8_t                       readbackBytes[TRANSFER_SECOND_COPY * 2u] = {0};
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *upload;
  GPUBuffer                    *readback;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  int                           ok;
  uint32_t                      writtenIndex;
  uint32_t                      copiedIndex;
  bool                          firstEqual;
  bool                          secondEqual;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "3D texture transfer has no graphics queue\n");
    return 0;
  }

  for (writtenIndex = 0u; writtenIndex < (uint32_t)sizeof(written); writtenIndex++) {
    written[writtenIndex] = (uint8_t)(0x43u + writtenIndex * 5u);
  }

  for (copiedIndex = 0u; copiedIndex < (uint32_t)sizeof(copied); copiedIndex++) {
    copied[copiedIndex] = (uint8_t)(0xb7u ^ (copiedIndex * 13u));
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
  ok = GPUCreateBuffer(device, &bufferInfo, &upload) == GPU_OK
       && GPUQueueWriteBuffer(queue,
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
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK
       && GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;

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

  if (GPUAcquireCommandBuffer(queue, "3d-texture-transfer", &cmdb) != GPU_OK
      || !cmdb) {
    fprintf(stderr, "3D transfer command buffer failed\n");
    ok = 0;
    goto cleanup;
  }

  if (!(copyPass = GPUBeginTransferPass(cmdb, "3d-texture-transfer"))) {
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

  bufferRegion.bufferOffset      = TRANSFER_SECOND_COPY;
  bufferRegion.texture.texture.z = 0u;
  GPUCopyTextureToBuffer(copyPass, textureB, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            readbackBytes,
                            sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "3D texture transfer readback failed\n");
    ok = 0;
    goto cleanup;
  }

  firstEqual  = transfer_equal(written + TRANSFER_IMAGE_BYTES,
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
  GPUBufferCreateInfo           bufferInfo    = {0};
  GPUTextureCreateInfo          textureInfo   = {0};
  GPUTextureWriteRegion         writeRegion   = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion  = {0};
  uint8_t                       sourceBytes[TRANSFER_IMAGE_BYTES];
  uint8_t                       readbackBytes[SAME_READBACK_BYTES] = {0};
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *readback;
  GPUTexture                   *texture;
  int                           ok;
  uint32_t                      i;
  bool                          layerEqual;
  bool                          mipEqual;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "same-texture copy has no graphics queue\n");
    return 0;
  }

  for (i = 0u; i < (uint32_t)sizeof(sourceBytes); i++) {
    sourceBytes[i] = (uint8_t)(0x35u + i * 13u);
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "same-texture-copy-readback";
  bufferInfo.sizeBytes        = SAME_READBACK_BYTES;
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

  if (GPUAcquireCommandBuffer(queue, "same-texture-copy", &cmdb) != GPU_OK
      || !cmdb) {
    fprintf(stderr, "same-texture copy command buffer failed\n");
    goto cleanup;
  }

  if (!(copyPass = GPUBeginTransferPass(cmdb, "same-texture-copy"))) {
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

  textureRegion.src   = textureRegion.dst;
  textureRegion.dst.x = 1u;
  textureRegion.width = TRANSFER_WIDTH - 1u;
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
  bufferRegion.bufferOffset                   = SAME_LAYER_COPY_OFFSET;
  bufferRegion.bytesPerRow                    = TRANSFER_ROW_PITCH;
  bufferRegion.rowsPerImage                   = TRANSFER_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &bufferRegion);

  bufferRegion.texture.texture.mipLevel       = 0u;
  bufferRegion.texture.texture.baseArrayLayer = 0u;
  bufferRegion.bufferOffset                   = SAME_MIP_COPY_OFFSET;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            readbackBytes,
                            sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "same-texture copy readback failed\n");
    ok = 0;
    goto cleanup;
  }

  layerEqual = transfer_equal(sourceBytes,
                              readbackBytes + SAME_LAYER_COPY_OFFSET,
                              1u);
  mipEqual   = transfer_equal(sourceBytes,
                              readbackBytes + SAME_MIP_COPY_OFFSET,
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
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUTransferPassEncoder       *copyPass;
  GPUBuffer                    *readback;
  GPUTexture                   *source;
  GPUTexture                   *destination;
  GPUBufferCreateInfo           bufferInfo    = {0};
  GPUTextureCreateInfo          textureInfo   = {0};
  GPUTextureWriteRegion         writeRegion   = {0};
  GPUBufferTextureCopyRegion    bufferRegion  = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUFormatCapabilities         formatCaps;
  GPUResult                     sourceDepthWrite;
  GPUResult                     destinationDepthWrite;
  GPUResult                     sourceStencilWrite;
  GPUResult                     destinationStencilWrite;
  uint8_t                       sourceDepth[TRANSFER_DS_STRIDE]        = {0};
  uint8_t                       sourceStencil[TRANSFER_DS_STRIDE]      = {0};
  uint8_t                       destinationDepth[TRANSFER_DS_STRIDE]   = {0};
  uint8_t                       destinationStencil[TRANSFER_DS_STRIDE] = {0};
  uint8_t                       readbackBytes[DS_READBACK_BYTES]       = {0};
  uint32_t                      sourceDepthValue;
  uint32_t                      destinationDepthValue;
  uint32_t                      depthMask;
  bool                          depthAfterDepth;
  bool                          stencilAfterDepth;
  bool                          depthAfterStencil;
  bool                          stencilAfterStencil;
  bool                          uploadsPending;
  int                           ok;
  float                         sourceValue;
  float                         destinationValue;
  uint32_t                      y;

  if (GPUGetFormatCapabilities(device->adapter,
                               format,
                               &formatCaps) != GPU_OK
      || !formatCaps.depthStencil) {
    printf("depth-stencil plane copy skipped: unsupported format=%u\n",
           (uint32_t)format);
    return 1;
  }

  if (device->inst->createInfo.preferredBackend == GPU_BACKEND_WEBGPU
      && (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
          || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)) {
    printf("depth-stencil plane copy skipped: WebGPU format=%u\n",
           (uint32_t)format);
    return 1;
  }

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "depth-stencil plane copy has no graphics queue\n");
    return 0;
  }

  if (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8) {
    sourceDepthValue      = UINT32_C(0x00bfffff);
    destinationDepthValue = UINT32_C(0x00400000);
    depthMask             = UINT32_C(0x00ffffff);
  } else {

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

  for (y = 0u; y < TRANSFER_HEIGHT; y++) {
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
  ok = 0;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "depth-stencil-plane-readback";
  bufferInfo.sizeBytes        = DS_READBACK_BYTES;
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

  if (GPUCreateTexture(device, &textureInfo, &destination) != GPU_OK
      || !destination) {
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
  sourceDepthWrite           = GPUQueueWriteTexture(queue,
                                                    source,
                                                    &writeRegion,
                                                    sourceDepth,
                                                    sizeof(sourceDepth));
  destinationDepthWrite      = GPUQueueWriteTexture(queue,
                                                    destination,
                                                    &writeRegion,
                                                    destinationDepth,
                                                    sizeof(destinationDepth));
  uploadsPending             = sourceDepthWrite == GPU_OK
                               || destinationDepthWrite == GPU_OK;

  if (sourceDepthWrite != GPU_OK || destinationDepthWrite != GPU_OK) {
    fprintf(stderr, "depth-stencil plane depth upload failed\n");
    goto cleanup;
  }

  writeRegion.aspect      = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  sourceStencilWrite      = GPUQueueWriteTexture(queue,
                                                 source,
                                                 &writeRegion,
                                                 sourceStencil,
                                                 sizeof(sourceStencil));
  destinationStencilWrite = GPUQueueWriteTexture(queue,
                                                 destination,
                                                 &writeRegion,
                                                 destinationStencil,
                                                 sizeof(destinationStencil));
  uploadsPending          = uploadsPending || sourceStencilWrite == GPU_OK
                            || destinationStencilWrite == GPU_OK;

  if (sourceStencilWrite == GPU_ERROR_UNSUPPORTED
      || destinationStencilWrite == GPU_ERROR_UNSUPPORTED) {
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
                              &cmdb) != GPU_OK
      || !cmdb) {
    fprintf(stderr, "depth-stencil plane command buffer failed\n");
    goto cleanup;
  }

  if (!(copyPass = GPUBeginTransferPass(cmdb, "depth-stencil-plane-copy"))) {
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
  bufferRegion.bufferOffset       = DS_DEPTH_AFTER_DEPTH_OFFSET;
  bufferRegion.bytesPerRow        = TRANSFER_DS_ROW_PITCH;
  bufferRegion.rowsPerImage       = TRANSFER_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);

  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  bufferRegion.bufferOffset           = DS_STENCIL_AFTER_DEPTH_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);

  GPUResetStats(device);

  textureRegion.src.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  textureRegion.dst.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  GPUCopyTextureToTexture(copyPass, source, destination, &textureRegion);

  if (device->currentFrameStats.hotPathAllocCount != 0u
      || device->currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "depth-stencil plane warm copy allocated\n");
    goto cleanup;
  }

  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  bufferRegion.bufferOffset           = DS_DEPTH_AFTER_STENCIL_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);

  bufferRegion.texture.texture.aspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
  bufferRegion.bufferOffset           = DS_STENCIL_AFTER_STENCIL_OFFSET;
  GPUCopyTextureToBuffer(copyPass, destination, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;

  if (ok) {
    uploadsPending = false;
  }

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            readbackBytes,
                            sizeof(readbackBytes)) != GPU_OK) {
    fprintf(stderr, "depth-stencil plane copy readback failed\n");
    ok = 0;
    goto cleanup;
  }

  depthAfterDepth     = transfer_depth_equal(readbackBytes,
                                             DS_DEPTH_AFTER_DEPTH_OFFSET,
                                             TRANSFER_DS_ROW_PITCH,
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterDepth   = transfer_stencil_equal(readbackBytes,
                                               DS_STENCIL_AFTER_DEPTH_OFFSET,
                                               TRANSFER_DS_ROW_PITCH,
                                               17u);
  depthAfterStencil   = transfer_depth_equal(readbackBytes,
                                             DS_DEPTH_AFTER_STENCIL_OFFSET,
                                             TRANSFER_DS_ROW_PITCH,
                                             sourceDepthValue,
                                             depthMask);
  stencilAfterStencil = transfer_stencil_equal(readbackBytes,
                                               DS_STENCIL_AFTER_STENCIL_OFFSET,
                                               TRANSFER_DS_ROW_PITCH,
                                               91u);

  if (!depthAfterDepth || !stencilAfterDepth
      || !depthAfterStencil || !stencilAfterStencil) {
    fprintf(stderr,
            "depth-stencil plane copy mismatch: depth=%u/%u stencil=%u/%u "
            "values=%u/%u\n",
            depthAfterDepth ? 1u : 0u,
            depthAfterStencil ? 1u : 0u,
            stencilAfterDepth ? 1u : 0u,
            stencilAfterStencil ? 1u : 0u,
            readbackBytes[DS_STENCIL_AFTER_DEPTH_OFFSET],
            readbackBytes[DS_STENCIL_AFTER_STENCIL_OFFSET]);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
    copyPass = NULL;
  }
  if (uploadsPending) {
    if (!cmdb
        && (GPUAcquireCommandBuffer(queue,
                                    "depth-stencil-upload-wait",
                                    &cmdb) != GPU_OK
            || !cmdb)) {
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
  GPUBufferCreateInfo        bufferInfo  = {0};
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion  = {0};
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  GPUTexture                *texture;
  uint8_t                   *pixels;
  int                        ok;
  uint32_t                   uploadRow;
  uint32_t                   uploadByte;
  uint32_t                   readbackRow;
  uint32_t                   readbackByte;
  uint8_t                    expected;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "large texture write has no graphics queue\n");
    return 0;
  }

  if (!(pixels = malloc(LARGE_IMAGE_BYTES))) {
    return 0;
  }

  for (uploadRow = 0u; uploadRow < LARGE_HEIGHT; uploadRow++) {
    for (uploadByte = 0u; uploadByte < LARGE_ROW_BYTES; uploadByte++) {
      pixels[(uint64_t)uploadRow * LARGE_ROW_BYTES + uploadByte] =
        (uint8_t)(0x2bu + uploadRow * 17u + uploadByte * 11u);
    }
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok = 0;

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

  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback
      || GPUAcquireCommandBuffer(queue,
                                 "large-texture-readback",
                                 &cmdb) != GPU_OK
      || !cmdb
      || !(copyPass = GPUBeginTransferPass(cmdb,
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

  ok = transfer_submit(device, queue, cmdb);
  cmdb   = NULL;
  pixels = malloc(LARGE_IMAGE_BYTES);

  if (!ok || !pixels
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            pixels,
                            LARGE_IMAGE_BYTES) != GPU_OK) {
    fprintf(stderr, "large texture readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (readbackRow = 0u; readbackRow < LARGE_HEIGHT; readbackRow++) {
    for (readbackByte = 0u; readbackByte < LARGE_ROW_BYTES; readbackByte++) {
      expected = (uint8_t)(0x2bu + readbackRow * 17u + readbackByte * 11u);

      if (pixels[(uint64_t)readbackRow * LARGE_ROW_BYTES + readbackByte] != expected) {
        fprintf(stderr,
                "large texture mismatch at row=%u byte=%u\n",
                readbackRow,
                readbackByte);
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
  GPUTexture                *textures[SEQUENCE_TEXTURE_COUNT] = {0};
  GPUBufferCreateInfo        bufferInfo                       = {0};
  GPUTextureCreateInfo       textureInfo                      = {0};
  GPUTextureWriteRegion      writeRegion                      = {0};
  GPUBufferTextureCopyRegion copyRegion                       = {0};
  GPUTexture                *diffuseCube;
  GPUTexture                *specularCube;
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  uint8_t                   *pixels;
  uint64_t                   diffuseBytes;
  uint64_t                   diffuseByte;
  uint64_t                   specularBytes;
  uint64_t                   specularByte;
  int                        ok;
  uint32_t                   textureIndex;
  uint32_t                   textureRow;
  uint32_t                   textureByte;
  uint32_t                   diffuseFace;
  uint32_t                   mip;
  uint32_t                   size;
  uint32_t                   specularFace;
  uint32_t                   readbackRow;
  uint32_t                   readbackByte;
  uint32_t                   destroyIndex;
  uint8_t                    expected;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "sequential large texture writes have no graphics queue\n");
    return 0;
  }

  if (!(pixels = malloc(SEQUENCE_TEXTURE_IMAGE_BYTES))) {
    return 0;
  }

  cmdb         = NULL;
  copyPass     = NULL;
  readback     = NULL;
  diffuseCube  = NULL;
  specularCube = NULL;
  ok = 0;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "sequential-large-texture-write";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.width            = SEQUENCE_TEXTURE_WIDTH;
  textureInfo.height           = SEQUENCE_TEXTURE_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = SEQUENCE_TEXTURE_WIDTH;
  writeRegion.height       = SEQUENCE_TEXTURE_HEIGHT;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = SEQUENCE_TEXTURE_ROW_BYTES;
  writeRegion.rowsPerImage = SEQUENCE_TEXTURE_HEIGHT;

  for (textureIndex = 0u;
       textureIndex < SEQUENCE_TEXTURE_COUNT;
       textureIndex++) {
    textureInfo.format = textureIndex == 0u
                           ? GPU_FORMAT_RGBA8_UNORM_SRGB
                           : GPU_FORMAT_RGBA8_UNORM;

    for (textureRow = 0u; textureRow < SEQUENCE_TEXTURE_HEIGHT; textureRow++) {
      for (textureByte = 0u; textureByte < SEQUENCE_TEXTURE_ROW_BYTES; textureByte++) {
        pixels[(uint64_t)textureRow * SEQUENCE_TEXTURE_ROW_BYTES + textureByte] =
          (uint8_t)(0x35u + textureIndex * 41u + textureRow * 17u + textureByte * 11u);
      }
    }

    if (GPUCreateTexture(device,
                         &textureInfo,
                         &textures[textureIndex]) != GPU_OK
        || !textures[textureIndex]
        || GPUQueueWriteTexture(queue,
                                textures[textureIndex],
                                &writeRegion,
                                pixels,
                                SEQUENCE_TEXTURE_IMAGE_BYTES) != GPU_OK) {
      fprintf(stderr,
              "sequential large texture write failed at texture=%u\n",
              textureIndex);
      goto cleanup;
    }
  }

  textureInfo.label         = "sequential-diffuse-cube-write";
  textureInfo.format        = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width         = SEQUENCE_DIFFUSE_CUBE_SIZE;
  textureInfo.height        = SEQUENCE_DIFFUSE_CUBE_SIZE;
  textureInfo.depthOrLayers = SEQUENCE_CUBE_FACE_COUNT;

  if (GPUCreateTexture(device, &textureInfo, &diffuseCube) != GPU_OK
      || !diffuseCube) {
    fprintf(stderr, "sequential diffuse cube setup failed\n");
    goto cleanup;
  }

  writeRegion.width        = SEQUENCE_DIFFUSE_CUBE_SIZE;
  writeRegion.height       = SEQUENCE_DIFFUSE_CUBE_SIZE;
  writeRegion.bytesPerRow  = SEQUENCE_DIFFUSE_CUBE_SIZE * SEQUENCE_CUBE_PIXEL_BYTES;
  writeRegion.rowsPerImage = SEQUENCE_DIFFUSE_CUBE_SIZE;

  for (diffuseFace = 0u; diffuseFace < SEQUENCE_CUBE_FACE_COUNT; diffuseFace++) {
    diffuseBytes = (uint64_t)writeRegion.bytesPerRow * writeRegion.height;

    for (diffuseByte = 0u; diffuseByte < diffuseBytes; diffuseByte++) {
      pixels[diffuseByte] = (uint8_t)(0x19u + diffuseFace * 43u + diffuseByte * 7u);
    }

    writeRegion.baseArrayLayer = diffuseFace;

    if (GPUQueueWriteTexture(queue,
                             diffuseCube,
                             &writeRegion,
                             pixels,
                             diffuseBytes) != GPU_OK) {
      fprintf(stderr,
              "sequential diffuse cube write failed at face=%u\n",
              diffuseFace);
      goto cleanup;
    }
  }

  textureInfo.label         = "sequential-specular-cube-write";
  textureInfo.width         = SEQUENCE_SPECULAR_CUBE_SIZE;
  textureInfo.height        = SEQUENCE_SPECULAR_CUBE_SIZE;
  textureInfo.mipLevelCount = SEQUENCE_SPECULAR_MIP_COUNT;

  if (GPUCreateTexture(device, &textureInfo, &specularCube) != GPU_OK
      || !specularCube) {
    fprintf(stderr, "sequential specular cube setup failed\n");
    goto cleanup;
  }

  for (mip = 0u; mip < SEQUENCE_SPECULAR_MIP_COUNT; mip++) {
    size                     = SEQUENCE_SPECULAR_CUBE_SIZE >> mip;
    writeRegion.width        = size;
    writeRegion.height       = size;
    writeRegion.mipLevel     = mip;
    writeRegion.bytesPerRow  = size * SEQUENCE_CUBE_PIXEL_BYTES;
    writeRegion.rowsPerImage = size;
    specularBytes            = (uint64_t)writeRegion.bytesPerRow * size;

    for (specularFace = 0u; specularFace < SEQUENCE_CUBE_FACE_COUNT; specularFace++) {
      for (specularByte = 0u; specularByte < specularBytes; specularByte++) {
        pixels[specularByte] = (uint8_t)(0x2du + mip * 29u + specularFace * 47u + specularByte * 11u);
      }

      writeRegion.baseArrayLayer = specularFace;

      if (GPUQueueWriteTexture(queue,
                               specularCube,
                               &writeRegion,
                               pixels,
                               specularBytes) != GPU_OK) {
        fprintf(stderr,
                "sequential specular cube write failed at mip=%u face=%u\n",
                mip,
                specularFace);
        goto cleanup;
      }
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sequential-large-texture-readback";
  bufferInfo.sizeBytes        = SEQUENCE_TEXTURE_IMAGE_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback
      || GPUAcquireCommandBuffer(queue,
                                 "sequential-large-texture-readback",
                                 &cmdb) != GPU_OK
      || !cmdb
      || !(copyPass = GPUBeginTransferPass(cmdb,
                                           "sequential-large-texture-readback"))) {
    fprintf(stderr, "sequential large texture readback setup failed\n");
    goto cleanup;
  }

  copyRegion.bytesPerRow        = SEQUENCE_TEXTURE_ROW_BYTES;
  copyRegion.rowsPerImage       = SEQUENCE_TEXTURE_HEIGHT;
  copyRegion.texture.width      = SEQUENCE_TEXTURE_WIDTH;
  copyRegion.texture.height     = SEQUENCE_TEXTURE_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, textures[0], readback, &copyRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            pixels,
                            SEQUENCE_TEXTURE_IMAGE_BYTES) != GPU_OK) {
    fprintf(stderr, "sequential large texture readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (readbackRow = 0u; readbackRow < SEQUENCE_TEXTURE_HEIGHT; readbackRow++) {
    for (readbackByte = 0u; readbackByte < SEQUENCE_TEXTURE_ROW_BYTES; readbackByte++) {
      expected = (uint8_t)(0x35u + readbackRow * 17u + readbackByte * 11u);

      if (pixels[(uint64_t)readbackRow * SEQUENCE_TEXTURE_ROW_BYTES + readbackByte] != expected) {
        fprintf(stderr,
                "sequential large texture mismatch at row=%u byte=%u\n",
                readbackRow,
                readbackByte);
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
  for (destroyIndex = 0u; destroyIndex < SEQUENCE_TEXTURE_COUNT; destroyIndex++) {
    GPUDestroyTexture(textures[destroyIndex]);
  }
  free(pixels);
  return ok;
}

static int
check_sequential_cubemap_writes(GPUDevice *device) {
  GPUBufferCreateInfo        bufferInfo  = {0};
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion  = {0};
  CubeCopy                   copies[CUBE_COPY_COUNT];
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *copyPass;
  GPUBuffer                 *readback;
  GPUTexture                *texture;
  uint8_t                   *expected;
  uint8_t                   *actual;
  CubeCopy                  *layoutCopy;
  const CubeCopy            *fillCopy;
  uint8_t                   *row;
  const CubeCopy            *uploadCopy;
  const CubeCopy            *readbackCopy;
  const CubeCopy            *verifyCopy;
  uint64_t                   tightBytes;
  uint64_t                   readbackBytes;
  uint64_t                   imageBytes;
  uint32_t                   copyIndex;
  int                        ok;
  uint32_t                   mip;
  uint32_t                   size;
  uint32_t                   layoutRowBytes;
  uint32_t                   rowPitch;
  uint32_t                   face;
  uint32_t                   fillIndex;
  uint32_t                   fillRowBytes;
  uint32_t                   fillRow;
  uint32_t                   x;
  uint32_t                   uploadIndex;
  uint32_t                   uploadRowBytes;
  uint32_t                   readbackIndex;
  uint32_t                   verifyIndex;
  uint32_t                   verifyRowBytes;
  uint32_t                   verifyRow;

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "sequential cubemap write has no graphics queue\n");
    return 0;
  }

  tightBytes    = 0u;
  readbackBytes = 0u;
  copyIndex     = 0u;

  for (mip = 0u; mip < CUBE_MIP_COUNT; mip++) {
    size           = CUBE_BASE_SIZE >> mip;
    layoutRowBytes = size * CUBE_PIXEL_BYTES;
    rowPitch       = (layoutRowBytes + 255u) & ~255u;

    for (face = 0u; face < CUBE_FACE_COUNT; face++) {
      readbackBytes              = (readbackBytes + 511u) & ~511u;
      layoutCopy                 = &copies[copyIndex++];
      layoutCopy->tightOffset    = tightBytes;
      layoutCopy->readbackOffset = readbackBytes;
      layoutCopy->rowPitch       = rowPitch;
      layoutCopy->width          = size;
      layoutCopy->height         = size;
      layoutCopy->mip            = mip;
      layoutCopy->face           = face;
      tightBytes    += (uint64_t)layoutRowBytes * size;
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

  for (fillIndex = 0u; fillIndex < CUBE_COPY_COUNT; fillIndex++) {
    fillCopy     = &copies[fillIndex];
    fillRowBytes = fillCopy->width * CUBE_PIXEL_BYTES;

    for (fillRow = 0u; fillRow < fillCopy->height; fillRow++) {
      row = expected + fillCopy->tightOffset + (uint64_t)fillRow * fillRowBytes;

      for (x = 0u; x < fillRowBytes; x++) {
        row[x] = (uint8_t)(0x17u + fillCopy->mip * 31u +
                           fillCopy->face * 47u + fillRow * 13u + x * 7u);
      }
    }
  }

  cmdb     = NULL;
  copyPass = NULL;
  readback = NULL;
  texture  = NULL;
  ok = 0;

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

  for (uploadIndex = 0u; uploadIndex < CUBE_COPY_COUNT; uploadIndex++) {
    uploadCopy                 = &copies[uploadIndex];
    uploadRowBytes             = uploadCopy->width * CUBE_PIXEL_BYTES;
    imageBytes                 = (uint64_t)uploadRowBytes * uploadCopy->height;
    writeRegion.width          = uploadCopy->width;
    writeRegion.height         = uploadCopy->height;
    writeRegion.mipLevel       = uploadCopy->mip;
    writeRegion.baseArrayLayer = uploadCopy->face;
    writeRegion.bytesPerRow    = uploadRowBytes;
    writeRegion.rowsPerImage   = uploadCopy->height;

    if (GPUQueueWriteTexture(queue,
                             texture,
                             &writeRegion,
                             expected + uploadCopy->tightOffset,
                             imageBytes) != GPU_OK) {
      fprintf(stderr,
              "sequential cubemap write failed at mip=%u face=%u\n",
              uploadCopy->mip,
              uploadCopy->face);
      goto cleanup;
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "sequential-cubemap-readback";
  bufferInfo.sizeBytes        = readbackBytes;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback
      || GPUAcquireCommandBuffer(queue,
                                 "sequential-cubemap-readback",
                                 &cmdb) != GPU_OK
      || !cmdb
      || !(copyPass = GPUBeginTransferPass(cmdb,
                                           "sequential-cubemap-readback"))) {
    fprintf(stderr, "sequential cubemap readback setup failed\n");
    goto cleanup;
  }

  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;

  for (readbackIndex = 0u; readbackIndex < CUBE_COPY_COUNT; readbackIndex++) {
    readbackCopy = &copies[readbackIndex];
    copyRegion.bufferOffset                   = readbackCopy->readbackOffset;
    copyRegion.bytesPerRow                    = readbackCopy->rowPitch;
    copyRegion.rowsPerImage                   = readbackCopy->height;
    copyRegion.texture.texture.mipLevel       = readbackCopy->mip;
    copyRegion.texture.texture.baseArrayLayer = readbackCopy->face;
    copyRegion.texture.width                  = readbackCopy->width;
    copyRegion.texture.height                 = readbackCopy->height;
    GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  }

  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = transfer_submit(device, queue, cmdb);
  cmdb = NULL;

  if (!ok
      || GPUQueueReadBuffer(queue,
                            readback,
                            0u,
                            actual,
                            readbackBytes) != GPU_OK) {
    fprintf(stderr, "sequential cubemap readback failed\n");
    ok = 0;
    goto cleanup;
  }

  for (verifyIndex = 0u; verifyIndex < CUBE_COPY_COUNT; verifyIndex++) {
    verifyCopy     = &copies[verifyIndex];
    verifyRowBytes = verifyCopy->width * CUBE_PIXEL_BYTES;

    for (verifyRow = 0u; verifyRow < verifyCopy->height; verifyRow++) {
      if (memcmp(expected + verifyCopy->tightOffset + (uint64_t)verifyRow * verifyRowBytes,
                 actual + verifyCopy->readbackOffset + (uint64_t)verifyRow * verifyCopy->rowPitch,
                 verifyRowBytes) != 0) {
        fprintf(stderr,
                "sequential cubemap mismatch at mip=%u face=%u row=%u\n",
                verifyCopy->mip,
                verifyCopy->face,
                verifyRow);
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
  return check_tight_texture_copies(device)
         && check_array_mip_transfers(device)
         && check_3d_texture_transfers(device)
         && check_same_texture_copies(device)
         && check_large_texture_write(device)
         && check_sequential_large_texture_writes(device)
         && check_sequential_cubemap_writes(device)
         && check_depth_stencil_plane_copies(device,
                                             GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)
         && check_depth_stencil_plane_copies(device,
                                             GPU_FORMAT_DEPTH24_UNORM_STENCIL8);
}
