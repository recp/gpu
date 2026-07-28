#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/texture_internal.h"

enum {
  COPY_TEST_WIDTH      = 4u,
  COPY_TEST_HEIGHT     = 4u,
  COPY_TEST_PIXEL_SIZE = 4u,
  COPY_TEST_ROW_PITCH  = 256u,
  COPY_TEST_WARM_RUNS  = 16u
};

static GPUTransferPassEncoder gScopedCopyPass;
static uint32_t           gScopedCopyBeginCalls;
static uint32_t           gScopedCopyEndCalls;
static uint32_t           gScopedIndirectCopyCalls;
static uint32_t           gScopedIndirectTextureCopyCalls;
static uint32_t           gScopedBlitCalls;

static GPUTransferPassEncoder *
begin_scoped_copy_pass(GPUCommandBuffer *cmdb, const char *label) {
  (void)cmdb;
  (void)label;
  memset(&gScopedCopyPass, 0, sizeof(gScopedCopyPass));
  gScopedCopyBeginCalls++;
  return &gScopedCopyPass;
}

static void
end_scoped_copy_pass(GPUTransferPassEncoder *pass) {
  (void)pass;
  gScopedCopyEndCalls++;
}

static void
copy_scoped_memory_indirect(GPUTransferPassEncoder                  *pass,
                            const GPUIndirectMemoryCopyInfoEXT *info) {
  (void)pass;
  (void)info;
  gScopedIndirectCopyCalls++;
}

static void
copy_scoped_memory_to_texture_indirect(
  GPUTransferPassEncoder                           *pass,
  const GPUIndirectMemoryToTextureCopyInfoEXT *info) {
  (void)pass;
  (void)info;
  gScopedIndirectTextureCopyCalls++;
}

static void
blit_scoped_texture(GPUCommandBuffer         *cmdb,
                    const GPUTextureBlitInfo *info) {
  (void)cmdb;
  (void)info;
  gScopedBlitCalls++;
}

static int
check_copy_pass_device_dispatch(GPUDevice *activeDevice) {
  GPUApi             *api;
  GPUTransferPassEncoder *pass;
  GPUApi              scopedApi;
  GPUDevice           device = {0};
  GPUQueue            queue  = {0};
  GPUCommandBuffer    cmdb   = {0};
  GPUBuffer           commandBuffer = {0};
  GPUTexture          texture = {0};
  GPUTexture          blitSource = {0};
  GPUTexture          blitDestination = {0};
  GPUTextureBlitInfo  blitInfo = {0};
  GPUIndirectTextureSubresourceEXT subresource = {0};
  GPUIndirectMemoryCopyInfoEXT indirectInfo = {0};
  GPUIndirectMemoryToTextureCopyInfoEXT indirectTextureInfo = {0};

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "copy pass dispatch has no device api\n");
    return 0;
  }

  scopedApi                          = *api;
  scopedApi.renderPass.beginTransferPass = begin_scoped_copy_pass;
  scopedApi.renderPass.endTransferPass   = end_scoped_copy_pass;
  scopedApi.renderPass.copyMemoryIndirect = copy_scoped_memory_indirect;
  scopedApi.renderPass.copyMemoryToTextureIndirect =
    copy_scoped_memory_to_texture_indirect;
  scopedApi.renderPass.blitTexture = blit_scoped_texture;
  device._api                        = &scopedApi;
  device.adapter                     = activeDevice->adapter;
  device.enabledFeatureMask          =
    (1ull << GPU_FEATURE_BUFFER_DEVICE_ADDRESS) |
    (1ull << GPU_FEATURE_INDIRECT_MEMORY_COPY) |
    (1ull << GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY);
  queue._device                      = &device;
  queue.bits                         = GPU_QUEUE_GRAPHICS_BIT;
  cmdb._queue                        = &queue;
  gScopedCopyBeginCalls              = 0u;
  gScopedCopyEndCalls                = 0u;
  gScopedIndirectCopyCalls           = 0u;
  gScopedIndirectTextureCopyCalls    = 0u;
  gScopedBlitCalls                   = 0u;

  pass = GPUBeginTransferPass(&cmdb, "device-scoped-copy");
  if (pass != &gScopedCopyPass ||
      gScopedCopyBeginCalls != 1u ||
      !cmdb._activeEncoder) {
    fprintf(stderr, "copy pass did not use device dispatch\n");
    return 0;
  }

  commandBuffer.device      = &device;
  commandBuffer._gpuAddress = 0x1000u;
  commandBuffer.sizeBytes   = 128u;
  commandBuffer.usage       = GPU_BUFFER_USAGE_INDIRECT |
                              GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT;
  texture.device            = &device;
  texture.format            = GPU_FORMAT_RGBA8_UNORM;
  texture.dimension         = GPU_TEXTURE_DIMENSION_2D;
  texture.width             = 4u;
  texture.height            = 4u;
  texture.depthOrLayers     = 1u;
  texture.mipLevelCount     = 1u;
  texture.sampleCount       = 1u;
  texture.usage             = GPU_TEXTURE_USAGE_COPY_DST;

  indirectInfo.commands.buffer      = &commandBuffer;
  indirectInfo.commands.sizeBytes   = 48u;
  indirectInfo.commands.strideBytes =
    sizeof(GPUIndirectMemoryCopyCommandEXT);
  indirectInfo.commandCount = 2u;
  GPUCopyMemoryIndirectEXT(pass, &indirectInfo);

  subresource.aspectMask = GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT;
  subresource.layerCount = 1u;
  indirectTextureInfo.dst                  = &texture;
  indirectTextureInfo.pTextureSubresources = &subresource;
  indirectTextureInfo.commands.buffer      = &commandBuffer;
  indirectTextureInfo.commands.sizeBytes   =
    sizeof(GPUIndirectMemoryToTextureCommandEXT);
  indirectTextureInfo.commands.strideBytes =
    sizeof(GPUIndirectMemoryToTextureCommandEXT);
  indirectTextureInfo.commandCount = 1u;
  GPUCopyMemoryToTextureIndirectEXT(pass, &indirectTextureInfo);

  indirectInfo.commands.strideBytes = 4u;
  GPUCopyMemoryIndirectEXT(pass, &indirectInfo);
  if (gScopedIndirectCopyCalls != 1u ||
      gScopedIndirectTextureCopyCalls != 1u ||
      GPUGetBufferDeviceAddressEXT(&commandBuffer) != 0x1000u) {
    fprintf(stderr, "indirect copy did not validate or use device dispatch\n");
    return 0;
  }

  GPUEndTransferPass(pass);
  if (gScopedCopyEndCalls != 1u || cmdb._activeEncoder) {
    fprintf(stderr, "copy pass end did not use device dispatch\n");
    return 0;
  }

  blitSource.device        = &device;
  blitSource.format        = GPU_FORMAT_RGBA8_UNORM;
  blitSource.dimension     = GPU_TEXTURE_DIMENSION_2D;
  blitSource.width         = 2u;
  blitSource.height        = 2u;
  blitSource.depthOrLayers = 1u;
  blitSource.mipLevelCount = 1u;
  blitSource.sampleCount   = 1u;
  blitSource.usage         = GPU_TEXTURE_USAGE_SAMPLED |
                             GPU_TEXTURE_USAGE_COPY_SRC;
  blitDestination          = blitSource;
  blitDestination.width    = 4u;
  blitDestination.height   = 4u;
  blitDestination.usage    = GPU_TEXTURE_USAGE_COLOR_TARGET |
                             GPU_TEXTURE_USAGE_COPY_DST;
  blitInfo.src             = &blitSource;
  blitInfo.dst             = &blitDestination;
  blitInfo.srcRegion.width = 2u;
  blitInfo.srcRegion.height = 2u;
  blitInfo.srcRegion.depth = 1u;
  blitInfo.srcRegion.layerCount = 1u;
  blitInfo.dstRegion.width = 4u;
  blitInfo.dstRegion.height = 4u;
  blitInfo.dstRegion.depth = 1u;
  blitInfo.dstRegion.layerCount = 1u;
  blitInfo.filter = GPU_FILTER_NEAREST;
  GPUBlit(&cmdb, &blitInfo);
  blitSource.usage = GPU_TEXTURE_USAGE_COPY_SRC;
  GPUBlit(&cmdb, &blitInfo);
  if (gScopedBlitCalls != 1u) {
    fprintf(stderr, "texture blit validation or device dispatch failed\n");
    return 0;
  }

  return 1;
}

static int
copy_test_rows_equal(const uint8_t *tight, const uint8_t *padded) {
  const size_t rowBytes = COPY_TEST_WIDTH * COPY_TEST_PIXEL_SIZE;

  for (uint32_t row = 0u; row < COPY_TEST_HEIGHT; row++) {
    if (memcmp(tight + row * rowBytes,
               padded + row * COPY_TEST_ROW_PITCH,
               rowBytes) != 0) {
      return 0;
    }
  }
  return 1;
}

static int
check_copy_pass_validation(GPUDevice *device) {
  GPUQueue        *queue;
  GPUCommandBuffer fakeCmdb = {0};
  GPUTransferPassEncoder endedPass = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence *fence;
  GPUTransferPassEncoder *copyPass;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUBufferCopyRegion bufferRegion = {0};
  GPUBufferTextureCopyRegion bufferTextureRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUBuffer *sourceBuffer;
  GPUBuffer *bufferCopy;
  GPUBuffer *textureUpload;
  GPUBuffer *textureReadback;
  GPUTexture *textureA;
  GPUTexture *textureB;
  uint8_t pixels[4u * 4u * 4u];
  uint8_t bufferCopyBytes[sizeof(pixels)] = {0};
  uint8_t textureUploadBytes[COPY_TEST_ROW_PITCH * COPY_TEST_HEIGHT] = {0};
  uint8_t textureBytes[sizeof(textureUploadBytes)] = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for copy test\n");
    return 0;
  }

  if (GPUBeginTransferPass(NULL, "null")) {
    fprintf(stderr, "copy pass accepted null command buffer\n");
    return 0;
  }
  fakeCmdb._submitted = true;
  if (GPUBeginTransferPass(&fakeCmdb, "submitted")) {
    fprintf(stderr, "copy pass accepted submitted command buffer\n");
    return 0;
  }

  fakeCmdb._submitted = false;
  fakeCmdb._activeEncoder = true;
  if (GPUBeginTransferPass(&fakeCmdb, "active")) {
    fprintf(stderr, "copy pass accepted command buffer with active encoder\n");
    return 0;
  }

  for (uint32_t i = 0; i < (uint32_t)sizeof(pixels); i++) {
    pixels[i] = (uint8_t)(i * 3u + 1u);
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  sourceBuffer = NULL;
  bufferCopy = NULL;
  textureUpload = NULL;
  textureReadback = NULL;
  textureA = NULL;
  textureB = NULL;
  fence = NULL;
  cmdb = NULL;
  copyPass = NULL;
  ok = GPUCreateBuffer(device, &bufferInfo, &sourceBuffer) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &bufferCopy) == GPU_OK &&
       GPUQueueWriteBuffer(queue, sourceBuffer, 0u, pixels, sizeof(pixels)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test buffer setup failed\n");
    goto cleanup;
  }

  for (uint32_t row = 0u; row < COPY_TEST_HEIGHT; row++) {
    memcpy(textureUploadBytes + row * COPY_TEST_ROW_PITCH,
           pixels + row * COPY_TEST_WIDTH * COPY_TEST_PIXEL_SIZE,
           COPY_TEST_WIDTH * COPY_TEST_PIXEL_SIZE);
  }
  bufferInfo.sizeBytes = sizeof(textureUploadBytes);
  ok = GPUCreateBuffer(device, &bufferInfo, &textureUpload) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &textureReadback) == GPU_OK &&
       GPUQueueWriteBuffer(queue,
                           textureUpload,
                           0u,
                           textureUploadBytes,
                           sizeof(textureUploadBytes)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test texture buffer setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED |
                      GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test texture setup failed\n");
    goto cleanup;
  }

  endedPass._ended = true;
  GPUCopyBufferToBuffer(&endedPass, sourceBuffer, bufferCopy, &bufferRegion);
  GPUCopyBufferToTexture(&endedPass, sourceBuffer, textureA, &bufferTextureRegion);
  GPUCopyTextureToBuffer(&endedPass, textureB, textureReadback, &bufferTextureRegion);
  GPUCopyTextureToTexture(&endedPass, textureA, textureB, &textureRegion);
  GPUEndTransferPass(&endedPass);

  ok = GPUAcquireCommandBuffer(queue, "reflection-copy-pass", &cmdb) == GPU_OK && cmdb;
  if (!ok) {
    fprintf(stderr, "failed to acquire command buffer for copy test\n");
    goto cleanup;
  }

  copyPass = GPUBeginTransferPass(cmdb, "reflection-copy");
  if (!copyPass) {
    fprintf(stderr, "failed to begin copy pass\n");
    ok = 0;
    goto cleanup;
  }
  if (GPUBeginTransferPass(cmdb, "nested-copy")) {
    fprintf(stderr, "copy pass accepted nested encoder\n");
    ok = 0;
    goto cleanup;
  }

  bufferRegion.sizeBytes = sizeof(pixels);
  GPUCopyBufferToBuffer(copyPass, sourceBuffer, bufferCopy, &bufferRegion);

  bufferTextureRegion.bytesPerRow = COPY_TEST_ROW_PITCH;
  bufferTextureRegion.rowsPerImage = 4u;
  bufferTextureRegion.texture.width = 4u;
  bufferTextureRegion.texture.height = 4u;
  bufferTextureRegion.texture.depth = 1u;
  bufferTextureRegion.texture.layerCount = 1u;
  GPUCopyBufferToTexture(copyPass, textureUpload, textureA, &bufferTextureRegion);

  textureRegion.width = 4u;
  textureRegion.height = 4u;
  textureRegion.depth = 1u;
  textureRegion.layerCount = 1u;
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);
  GPUCopyTextureToBuffer(copyPass, textureB, textureReadback, &bufferTextureRegion);

  GPUCopyBufferToBuffer(NULL, sourceBuffer, bufferCopy, &bufferRegion);
  GPUCopyBufferToTexture(copyPass, NULL, textureA, &bufferTextureRegion);
  GPUCopyTextureToBuffer(copyPass, textureB, NULL, &bufferTextureRegion);
  GPUCopyTextureToTexture(copyPass, textureA, textureB, NULL);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = GPUCreateFence(device, NULL, &fence) == GPU_OK && fence;
  if (!ok) {
    fprintf(stderr, "failed to create fence for copy test\n");
    goto cleanup;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok) {
    fprintf(stderr, "copy pass submit failed\n");
    goto cleanup;
  }

  ok = GPUQueueReadBuffer(queue,
                          bufferCopy,
                          0u,
                          bufferCopyBytes,
                          sizeof(bufferCopyBytes)) == GPU_OK &&
       GPUQueueReadBuffer(queue,
                          textureReadback,
                          0u,
                          textureBytes,
                          sizeof(textureBytes)) == GPU_OK &&
       memcmp(pixels, bufferCopyBytes, sizeof(pixels)) == 0 &&
       copy_test_rows_equal(pixels, textureBytes);
  if (!ok) {
    fprintf(stderr, "copy pass readback mismatch\n");
    goto cleanup;
  }

  GPUResetStats(device);
  for (uint32_t i = 0u; i < COPY_TEST_WARM_RUNS; i++) {
    ok = GPUAcquireCommandBuffer(queue, "warm-copy-pass", &cmdb) == GPU_OK &&
         cmdb;
    if (!ok) {
      fprintf(stderr, "failed to acquire warm copy command buffer\n");
      goto cleanup;
    }

    copyPass = GPUBeginTransferPass(cmdb, "warm-copy-pass");
    if (!copyPass) {
      fprintf(stderr, "failed to begin warm copy pass\n");
      ok = 0;
      goto cleanup;
    }

    GPUCopyBufferToBuffer(copyPass, sourceBuffer, bufferCopy, &bufferRegion);
    GPUCopyBufferToTexture(copyPass,
                           textureUpload,
                           textureA,
                           &bufferTextureRegion);
    GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);
    GPUCopyTextureToBuffer(copyPass,
                           textureB,
                           textureReadback,
                           &bufferTextureRegion);
    GPUEndTransferPass(copyPass);
    copyPass = NULL;

    buffers[0]                  = cmdb;
    submitInfo.ppCommandBuffers = buffers;
    ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
    cmdb = NULL;
    if (!ok) {
      fprintf(stderr, "warm copy submit failed\n");
      goto cleanup;
    }
  }

  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathAllocBytes != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u ||
      device->currentFrameStats.hotPathFreeBytes != 0u) {
    fprintf(stderr,
            "warm copy path allocated %llu bytes in %llu calls and freed "
            "%llu bytes in %llu calls\n",
            (unsigned long long)
              device->currentFrameStats.hotPathAllocBytes,
            (unsigned long long)
              device->currentFrameStats.hotPathAllocCount,
            (unsigned long long)
              device->currentFrameStats.hotPathFreeBytes,
            (unsigned long long)
              device->currentFrameStats.hotPathFreeCount);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(textureUpload);
  GPUDestroyBuffer(bufferCopy);
  GPUDestroyBuffer(sourceBuffer);
  return ok;
}

static int
check_copy_pass_invalid_copy_noops(GPUDevice *device) {
  GPUQueue        *queue;
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence *fence;
  GPUTransferPassEncoder *copyPass;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureWriteRegion writeRegion = {0};
  GPUBufferCopyRegion fullBufferRegion = {0};
  GPUBufferCopyRegion badBufferRegion = {0};
  GPUBufferTextureCopyRegion fullTextureRegion = {0};
  GPUBufferTextureCopyRegion badTextureRegion = {0};
  GPUTextureToTextureCopyRegion fullTextureCopy = {0};
  GPUTextureToTextureCopyRegion badTextureCopy = {0};
  GPUBuffer *sourceBuffer;
  GPUBuffer *protectedBuffer;
  GPUBuffer *noCopySrcBuffer;
  GPUBuffer *textureReadback;
  GPUTexture *sourceTexture;
  GPUTexture *protectedTexture;
  GPUTexture *noCopySrcTexture;
  uint8_t protectedBytes[4u * 4u * 4u];
  uint8_t overwriteBytes[sizeof(protectedBytes)];
  uint8_t bufferOut[sizeof(protectedBytes)] = {0};
  uint8_t textureOut[COPY_TEST_ROW_PITCH * COPY_TEST_HEIGHT] = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for invalid copy test\n");
    return 0;
  }

  for (uint32_t i = 0; i < (uint32_t)sizeof(protectedBytes); i++) {
    protectedBytes[i] = (uint8_t)(0xa5u ^ (i * 11u));
    overwriteBytes[i] = (uint8_t)(0x3du + (i * 7u));
  }

  sourceBuffer = NULL;
  protectedBuffer = NULL;
  noCopySrcBuffer = NULL;
  textureReadback = NULL;
  sourceTexture = NULL;
  protectedTexture = NULL;
  noCopySrcTexture = NULL;
  fence = NULL;
  cmdb = NULL;
  copyPass = NULL;

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(protectedBytes);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  ok = GPUCreateBuffer(device, &bufferInfo, &sourceBuffer) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &protectedBuffer) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy buffer setup failed\n");
    goto cleanup;
  }

  bufferInfo.sizeBytes = sizeof(textureOut);
  ok = GPUCreateBuffer(device, &bufferInfo, &textureReadback) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy texture readback setup failed\n");
    goto cleanup;
  }

  bufferInfo.sizeBytes = sizeof(protectedBytes);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_DST;
  ok = GPUCreateBuffer(device, &bufferInfo, &noCopySrcBuffer) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy no-copy-src buffer setup failed\n");
    goto cleanup;
  }

  ok = GPUQueueWriteBuffer(queue, sourceBuffer, 0u, overwriteBytes, sizeof(overwriteBytes)) == GPU_OK &&
       GPUQueueWriteBuffer(queue, protectedBuffer, 0u, protectedBytes, sizeof(protectedBytes)) == GPU_OK &&
       GPUQueueWriteBuffer(queue, noCopySrcBuffer, 0u, overwriteBytes, sizeof(overwriteBytes)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy buffer upload failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED |
                      GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &sourceTexture) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &protectedTexture) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy texture setup failed\n");
    goto cleanup;
  }

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &noCopySrcTexture) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy no-copy-src texture setup failed\n");
    goto cleanup;
  }

  writeRegion.width = 4u;
  writeRegion.height = 4u;
  writeRegion.depth = 1u;
  writeRegion.layerCount = 1u;
  writeRegion.bytesPerRow = 4u * 4u;
  writeRegion.rowsPerImage = 4u;
  ok = GPUQueueWriteTexture(queue,
                            sourceTexture,
                            &writeRegion,
                            overwriteBytes,
                            sizeof(overwriteBytes)) == GPU_OK &&
       GPUQueueWriteTexture(queue,
                            protectedTexture,
                            &writeRegion,
                            protectedBytes,
                            sizeof(protectedBytes)) == GPU_OK &&
       GPUQueueWriteTexture(queue,
                            noCopySrcTexture,
                            &writeRegion,
                            overwriteBytes,
                            sizeof(overwriteBytes)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy texture upload failed\n");
    goto cleanup;
  }

  ok = GPUAcquireCommandBuffer(queue, "invalid-copy-noops", &cmdb) == GPU_OK && cmdb;
  if (!ok) {
    fprintf(stderr, "failed to acquire invalid copy command buffer\n");
    goto cleanup;
  }

  copyPass = GPUBeginTransferPass(cmdb, "invalid-copy-noops");
  if (!copyPass) {
    fprintf(stderr, "failed to begin invalid copy pass\n");
    ok = 0;
    goto cleanup;
  }

  fullBufferRegion.sizeBytes = sizeof(protectedBytes);
  badBufferRegion = fullBufferRegion;
  badBufferRegion.sizeBytes = 0u;
  GPUCopyBufferToBuffer(copyPass, sourceBuffer, protectedBuffer, &badBufferRegion);
  badBufferRegion = fullBufferRegion;
  badBufferRegion.dstOffset = sizeof(protectedBytes) - 4u;
  badBufferRegion.sizeBytes = 8u;
  GPUCopyBufferToBuffer(copyPass, sourceBuffer, protectedBuffer, &badBufferRegion);
  GPUCopyBufferToBuffer(copyPass, noCopySrcBuffer, protectedBuffer, &fullBufferRegion);

  fullTextureRegion.bytesPerRow = COPY_TEST_ROW_PITCH;
  fullTextureRegion.rowsPerImage = 4u;
  fullTextureRegion.texture.width = 4u;
  fullTextureRegion.texture.height = 4u;
  fullTextureRegion.texture.depth = 1u;
  fullTextureRegion.texture.layerCount = 1u;

  badTextureRegion = fullTextureRegion;
  badTextureRegion.bytesPerRow = 0u;
  GPUCopyBufferToTexture(copyPass, sourceBuffer, protectedTexture, &badTextureRegion);
  badTextureRegion = fullTextureRegion;
  badTextureRegion.rowsPerImage = 3u;
  GPUCopyBufferToTexture(copyPass, sourceBuffer, protectedTexture, &badTextureRegion);
  badTextureRegion = fullTextureRegion;
  badTextureRegion.texture.width = 5u;
  GPUCopyBufferToTexture(copyPass, sourceBuffer, protectedTexture, &badTextureRegion);
  GPUCopyBufferToTexture(copyPass, noCopySrcBuffer, protectedTexture, &fullTextureRegion);

  badTextureRegion = fullTextureRegion;
  badTextureRegion.texture.height = 5u;
  GPUCopyTextureToBuffer(copyPass, sourceTexture, protectedBuffer, &badTextureRegion);
  GPUCopyTextureToBuffer(copyPass, noCopySrcTexture, protectedBuffer, &fullTextureRegion);

  fullTextureCopy.width = 4u;
  fullTextureCopy.height = 4u;
  fullTextureCopy.depth = 1u;
  fullTextureCopy.layerCount = 1u;
  badTextureCopy = fullTextureCopy;
  badTextureCopy.width = 5u;
  GPUCopyTextureToTexture(copyPass, sourceTexture, protectedTexture, &badTextureCopy);
  badTextureCopy = fullTextureCopy;
  badTextureCopy.dst.x = 1u;
  GPUCopyTextureToTexture(copyPass, sourceTexture, protectedTexture, &badTextureCopy);
  GPUCopyTextureToTexture(copyPass, noCopySrcTexture, protectedTexture, &fullTextureCopy);

  GPUCopyTextureToBuffer(copyPass, protectedTexture, textureReadback, &fullTextureRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = GPUCreateFence(device, NULL, &fence) == GPU_OK && fence;
  if (!ok) {
    fprintf(stderr, "failed to create invalid copy fence\n");
    goto cleanup;
  }

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  submitInfo.fence = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok) {
    fprintf(stderr, "invalid copy submit failed\n");
    goto cleanup;
  }

  ok = GPUQueueReadBuffer(queue,
                          protectedBuffer,
                          0u,
                          bufferOut,
                          sizeof(bufferOut)) == GPU_OK &&
       GPUQueueReadBuffer(queue,
                          textureReadback,
                          0u,
                          textureOut,
                          sizeof(textureOut)) == GPU_OK &&
       memcmp(protectedBytes, bufferOut, sizeof(protectedBytes)) == 0 &&
       copy_test_rows_equal(protectedBytes, textureOut);
  if (!ok) {
    fprintf(stderr, "invalid copy no-op target changed\n");
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(noCopySrcTexture);
  GPUDestroyTexture(protectedTexture);
  GPUDestroyTexture(sourceTexture);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(noCopySrcBuffer);
  GPUDestroyBuffer(protectedBuffer);
  GPUDestroyBuffer(sourceBuffer);
  return ok;
}

static int
check_compressed_texture_copies(GPUDevice *device) {
  const uint8_t textureBlock[8] = {
    0x00u, 0xf8u, 0x00u, 0xf8u, 0x00u, 0x00u, 0x00u, 0x00u
  };
  const uint8_t bufferBlock[8] = {
    0xe0u, 0x07u, 0xe0u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u
  };
  GPUQueue                     *queue;
  GPUCommandBuffer             *cmdb;
  GPUCommandBuffer             *buffers[1];
  GPUFence                     *fence;
  GPUTransferPassEncoder           *copyPass;
  GPUBuffer                    *upload;
  GPUBuffer                    *readback;
  GPUTexture                   *textureA;
  GPUTexture                   *textureB;
  GPUQueueSubmitInfo            submitInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferTextureCopyRegion    bufferRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUFormatCapabilities         formatCaps;
  uint8_t                       uploadBytes[1024] = {0};
  uint8_t                       readbackBytes[1024] = {0};
  int                           ok;

  if (!device ||
      GPUGetFormatCapabilities(device->adapter,
                               GPU_FORMAT_BC1_RGBA_UNORM,
                               &formatCaps) != GPU_OK ||
      !formatCaps.sampled) {
    return 1;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "compressed copy has no graphics queue\n");
    return 0;
  }

  memcpy(uploadBytes, bufferBlock, sizeof(bufferBlock));
  upload   = NULL;
  readback = NULL;
  textureA = NULL;
  textureB = NULL;
  fence    = NULL;
  cmdb     = NULL;
  copyPass = NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(uploadBytes);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = GPUCreateBuffer(device, &bufferInfo, &upload) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK &&
       GPUQueueWriteBuffer(queue,
                           upload,
                           0u,
                           uploadBytes,
                           sizeof(uploadBytes)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "compressed copy buffer setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BC1_RGBA_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &textureA) == GPU_OK &&
       GPUCreateTexture(device, &textureInfo, &textureB) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "compressed copy texture setup failed\n");
    goto cleanup;
  }

  writeRegion.width        = 4u;
  writeRegion.height       = 4u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = sizeof(textureBlock);
  writeRegion.rowsPerImage = 4u;
  ok = GPUQueueWriteTexture(queue,
                            textureA,
                            &writeRegion,
                            textureBlock,
                            sizeof(textureBlock)) == GPU_OK &&
       GPUQueueWriteTexture(queue,
                            textureA,
                            &writeRegion,
                            textureBlock,
                            sizeof(textureBlock) - 1u) ==
         GPU_ERROR_INVALID_ARGUMENT;
  if (!ok) {
    fprintf(stderr, "compressed texture write failed\n");
    goto cleanup;
  }

  writeRegion.bytesPerRow = 7u;
  if (GPUQueueWriteTexture(queue,
                           textureA,
                           &writeRegion,
                           textureBlock,
                           sizeof(textureBlock)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "compressed write accepted a partial block row\n");
    ok = 0;
    goto cleanup;
  }
  writeRegion.bytesPerRow  = sizeof(textureBlock);
  writeRegion.rowsPerImage = 3u;
  if (GPUQueueWriteTexture(queue,
                           textureA,
                           &writeRegion,
                           textureBlock,
                           sizeof(textureBlock)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "compressed write accepted an unaligned image height\n");
    ok = 0;
    goto cleanup;
  }
  writeRegion.rowsPerImage = 4u;
  writeRegion.width        = 3u;
  if (GPUQueueWriteTexture(queue,
                           textureA,
                           &writeRegion,
                           textureBlock,
                           sizeof(textureBlock)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "compressed write accepted an unaligned extent\n");
    ok = 0;
    goto cleanup;
  }

  ok = GPUAcquireCommandBuffer(queue, "compressed-copy", &cmdb) == GPU_OK &&
       cmdb;
  if (!ok) {
    fprintf(stderr, "compressed copy command buffer failed\n");
    goto cleanup;
  }
  copyPass = GPUBeginTransferPass(cmdb, "compressed-copy");
  if (!copyPass) {
    fprintf(stderr, "compressed copy pass failed\n");
    ok = 0;
    goto cleanup;
  }

  textureRegion.width      = 4u;
  textureRegion.height     = 4u;
  textureRegion.depth      = 1u;
  textureRegion.layerCount = 1u;
  GPUCopyTextureToTexture(copyPass, textureA, textureB, &textureRegion);

  bufferRegion.bytesPerRow        = COPY_TEST_ROW_PITCH;
  bufferRegion.rowsPerImage       = 4u;
  bufferRegion.texture.width      = 4u;
  bufferRegion.texture.height     = 4u;
  bufferRegion.texture.depth      = 1u;
  bufferRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, textureB, readback, &bufferRegion);
  GPUCopyBufferToTexture(copyPass, upload, textureA, &bufferRegion);
  bufferRegion.bufferOffset = 512u;
  GPUCopyTextureToBuffer(copyPass, textureA, readback, &bufferRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  ok = GPUCreateFence(device, NULL, &fence) == GPU_OK && fence;
  if (!ok) {
    fprintf(stderr, "compressed copy fence failed\n");
    goto cleanup;
  }

  buffers[0]                           = cmdb;
  submitInfo.chain.sType               = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize          = sizeof(submitInfo);
  submitInfo.commandBufferCount        = 1u;
  submitInfo.ppCommandBuffers          = buffers;
  submitInfo.fence                     = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok) {
    fprintf(stderr, "compressed copy submit failed\n");
    goto cleanup;
  }

  ok = GPUQueueReadBuffer(queue,
                          readback,
                          0u,
                          readbackBytes,
                          sizeof(readbackBytes)) == GPU_OK &&
       memcmp(readbackBytes, textureBlock, sizeof(textureBlock)) == 0 &&
       memcmp(readbackBytes + 512u, bufferBlock, sizeof(bufferBlock)) == 0;
  if (!ok) {
    fprintf(stderr, "compressed copy readback mismatch\n");
  }

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(upload);
  return ok;
}

static int
blit_result_matches(const uint8_t *result,
                    const uint8_t *sourcePixels,
                    const char    *path) {
  for (uint32_t y = 0u; y < 4u; y++) {
    for (uint32_t x = 0u; x < 4u; x++) {
      const uint8_t *expected =
        sourcePixels + (((y / 2u) * 2u + x / 2u) * 4u);
      const uint8_t *actual =
        result + y * COPY_TEST_ROW_PITCH + x * 4u;

      if (memcmp(actual, expected, 4u) != 0) {
        fprintf(stderr,
                "%s blit mismatch at (%u, %u): "
                "got %u %u %u %u, expected %u %u %u %u\n",
                path,
                x,
                y,
                actual[0],
                actual[1],
                actual[2],
                actual[3],
                expected[0],
                expected[1],
                expected[2],
                expected[3]);
        return 0;
      }
    }
  }
  return 1;
}

static int
blit_linear_result_matches(const uint8_t *result,
                           const uint8_t *sourcePixels,
                           const char    *path) {
  static const uint32_t weights[4] = {0u, 1u, 3u, 4u};

  for (uint32_t y = 0u; y < 4u; y++) {
    for (uint32_t x = 0u; x < 4u; x++) {
      const uint8_t *actual =
        result + y * COPY_TEST_ROW_PITCH + x * 4u;
      uint32_t wx = weights[x];
      uint32_t wy = weights[y];

      for (uint32_t channel = 0u; channel < 4u; channel++) {
        uint32_t expected =
          ((4u - wx) * (4u - wy) * sourcePixels[channel] +
           wx * (4u - wy) * sourcePixels[4u + channel] +
           (4u - wx) * wy * sourcePixels[8u + channel] +
           wx * wy * sourcePixels[12u + channel] +
           8u) /
          16u;
        uint32_t delta = actual[channel] > expected
                           ? actual[channel] - expected
                           : expected - actual[channel];

        if (delta > 1u) {
          fprintf(stderr,
                  "%s linear blit mismatch at (%u, %u), channel %u: "
                  "got %u, expected %u\n",
                  path,
                  x,
                  y,
                  channel,
                  actual[channel],
                  expected);
          return 0;
        }
      }
    }
  }
  return 1;
}

static int
blit_partial_result_matches(const uint8_t *result,
                            const uint8_t *sourcePixels,
                            const uint8_t *clearPixel,
                            const char    *path) {
  for (uint32_t y = 0u; y < 4u; y++) {
    for (uint32_t x = 0u; x < 4u; x++) {
      const uint8_t *actual =
        result + y * COPY_TEST_ROW_PITCH + x * 4u;
      const uint8_t *expected =
        x >= 1u && x < 3u && y >= 1u && y < 3u
          ? sourcePixels
          : clearPixel;

      if (memcmp(actual, expected, 4u) != 0) {
        fprintf(stderr,
                "%s partial blit mismatch at (%u, %u): "
                "got %u %u %u %u, expected %u %u %u %u\n",
                path,
                x,
                y,
                actual[0],
                actual[1],
                actual[2],
                actual[3],
                expected[0],
                expected[1],
                expected[2],
                expected[3]);
        return 0;
      }
    }
  }
  return 1;
}

static int
run_texture_blit(GPUQueue                  *queue,
                 const GPUTextureBlitInfo *blitInfo,
                 GPUTexture               *destination,
                 GPUBuffer                *readback,
                 GPUFence                 *fence,
                 const char               *label,
                 uint8_t                  *result) {
  GPUBufferTextureCopyRegion readRegion = {0};
  GPUQueueSubmitInfo         submitInfo = {0};
  GPUCommandBuffer          *commandBuffers[1];
  GPUCommandBuffer          *cmdb;
  GPUTransferPassEncoder    *transferPass;
  int                        ok;

  cmdb         = NULL;
  transferPass = NULL;
  ok = GPUAcquireCommandBuffer(queue, label, &cmdb) == GPU_OK && cmdb;
  if (!ok) {
    fprintf(stderr, "%s command buffer creation failed\n", label);
    goto cleanup;
  }

  GPUBlit(cmdb, blitInfo);
  transferPass = GPUBeginTransferPass(cmdb, "api-blit-readback");
  if (!transferPass) {
    fprintf(stderr, "%s readback transfer pass failed\n", label);
    ok = 0;
    goto cleanup;
  }

  readRegion.bytesPerRow        = COPY_TEST_ROW_PITCH;
  readRegion.rowsPerImage       = 4u;
  readRegion.texture.width      = 4u;
  readRegion.texture.height     = 4u;
  readRegion.texture.depth      = 1u;
  readRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(transferPass,
                         destination,
                         readback,
                         &readRegion);
  GPUEndTransferPass(transferPass);
  transferPass = NULL;

  commandBuffers[0]                = cmdb;
  submitInfo.chain.sType           = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize      = sizeof(submitInfo);
  submitInfo.ppCommandBuffers      = commandBuffers;
  submitInfo.commandBufferCount    = 1u;
  submitInfo.fence                 = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
  cmdb = NULL;
  if (!ok ||
      GPUQueueReadBuffer(queue,
                         readback,
                         0u,
                         result,
                         COPY_TEST_ROW_PITCH * 4u) != GPU_OK) {
    fprintf(stderr, "%s submit or readback failed\n", label);
    ok = 0;
  }

cleanup:
  if (transferPass) {
    GPUEndTransferPass(transferPass);
  }
  return ok;
}

static int
check_texture_blit(GPUDevice *device) {
  static const uint8_t sourcePixels[2u * 2u * 4u] = {
    255u,   0u,   0u, 255u,   0u, 255u,   0u, 255u,
      0u,   0u, 255u, 255u, 255u, 255u, 255u, 255u
  };
  static const uint8_t clearPixel[4u] = {17u, 34u, 51u, 255u};
  uint8_t                    clearPixels[4u * 4u * 4u];
  GPUTextureCreateInfo       textureInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUTextureBlitInfo         blitInfo = {0};
  GPUBufferCreateInfo        bufferInfo = {0};
  GPUQueueSubmitInfo         submitInfo = {0};
  GPUCommandBuffer          *commandBuffers[1];
  GPUQueue                  *queue;
  GPUCommandBuffer          *cmdb;
  GPUTexture                *source;
  GPUTexture                *destination;
  GPUBuffer                 *readback;
  GPUFence                  *fence;
  uint8_t                    result[COPY_TEST_ROW_PITCH * 4u] = {0};
  int                        ok;

  queue        = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb         = NULL;
  source       = NULL;
  destination  = NULL;
  readback     = NULL;
  fence        = NULL;
  ok           = queue != NULL;
  if (!ok) {
    fprintf(stderr, "failed to get graphics queue for blit test\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-blit-source";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  ok = GPUCreateTexture(device, &textureInfo, &source) == GPU_OK;

  textureInfo.label  = "api-blit-destination";
  textureInfo.width  = 4u;
  textureInfo.height = 4u;
  textureInfo.usage  = GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_COPY_SRC |
                       GPU_TEXTURE_USAGE_COPY_DST;
  ok = ok &&
       GPUCreateTexture(device, &textureInfo, &destination) == GPU_OK;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-blit-readback";
  bufferInfo.sizeBytes        = sizeof(result);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "blit resource creation failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < 4u * 4u; i++) {
    memcpy(clearPixels + i * 4u, clearPixel, sizeof(clearPixel));
  }

  writeRegion.width        = 2u;
  writeRegion.height       = 2u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 2u * 4u;
  writeRegion.rowsPerImage = 2u;
  if (GPUQueueWriteTexture(queue,
                           source,
                           &writeRegion,
                           sourcePixels,
                           sizeof(sourcePixels)) != GPU_OK) {
    fprintf(stderr, "blit source upload failed\n");
    ok = 0;
    goto cleanup;
  }

  blitInfo.src                         = source;
  blitInfo.dst                         = destination;
  blitInfo.srcRegion.texture.aspect    = GPU_TEXTURE_ASPECT_ALL;
  blitInfo.srcRegion.width             = 2u;
  blitInfo.srcRegion.height            = 2u;
  blitInfo.srcRegion.depth             = 1u;
  blitInfo.srcRegion.layerCount        = 1u;
  blitInfo.dstRegion.texture.aspect    = GPU_TEXTURE_ASPECT_ALL;
  blitInfo.dstRegion.width             = 4u;
  blitInfo.dstRegion.height            = 4u;
  blitInfo.dstRegion.depth             = 1u;
  blitInfo.dstRegion.layerCount        = 1u;
  blitInfo.filter                      = GPU_FILTER_NEAREST;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "blit fence creation failed\n");
    ok = 0;
    goto cleanup;
  }
  submitInfo.chain.sType           = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize      = sizeof(submitInfo);
  submitInfo.ppCommandBuffers      = commandBuffers;
  submitInfo.commandBufferCount    = 1u;
  submitInfo.fence                 = fence;
  ok = run_texture_blit(queue,
                        &blitInfo,
                        destination,
                        readback,
                        fence,
                        "api-texture-blit-nearest",
                        result) &&
       blit_result_matches(result, sourcePixels, "public nearest");
  if (!ok) {
    goto cleanup;
  }

  memset(result, 0, sizeof(result));
  blitInfo.filter = GPU_FILTER_LINEAR;
  ok = run_texture_blit(queue,
                        &blitInfo,
                        destination,
                        readback,
                        fence,
                        "api-texture-blit-linear",
                        result) &&
       blit_linear_result_matches(result, sourcePixels, "public");
  if (!ok) {
    goto cleanup;
  }

  writeRegion.width        = 4u;
  writeRegion.height       = 4u;
  writeRegion.bytesPerRow  = 4u * 4u;
  writeRegion.rowsPerImage = 4u;
  if (GPUQueueWriteTexture(queue,
                           destination,
                           &writeRegion,
                           clearPixels,
                           sizeof(clearPixels)) != GPU_OK) {
    fprintf(stderr, "partial blit destination upload failed\n");
    ok = 0;
    goto cleanup;
  }

  memset(result, 0, sizeof(result));
  blitInfo.srcRegion.width          = 1u;
  blitInfo.srcRegion.height         = 1u;
  blitInfo.dstRegion.texture.x      = 1u;
  blitInfo.dstRegion.texture.y      = 1u;
  blitInfo.dstRegion.width          = 2u;
  blitInfo.dstRegion.height         = 2u;
  blitInfo.filter                   = GPU_FILTER_NEAREST;
  ok = run_texture_blit(queue,
                        &blitInfo,
                        destination,
                        readback,
                        fence,
                        "api-texture-blit-partial",
                        result) &&
       blit_partial_result_matches(result,
                                   sourcePixels,
                                   clearPixel,
                                   "public");
  if (!ok) {
    goto cleanup;
  }

  GPUResetStats(device);
  for (uint32_t i = 0u; i < COPY_TEST_WARM_RUNS; i++) {
    ok = GPUAcquireCommandBuffer(queue, "warm-texture-blit", &cmdb) == GPU_OK &&
         cmdb;
    if (!ok) {
      fprintf(stderr, "failed to acquire warm blit command buffer\n");
      goto cleanup;
    }

    GPUBlit(cmdb, &blitInfo);
    commandBuffers[0] = cmdb;
    ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(fence, UINT64_MAX) == GPU_OK;
    cmdb = NULL;
    if (!ok) {
      fprintf(stderr, "warm blit submit failed\n");
      goto cleanup;
    }
  }

  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathAllocBytes != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u ||
      device->currentFrameStats.hotPathFreeBytes != 0u) {
    fprintf(stderr,
            "warm blit path allocated %llu bytes in %llu calls and freed "
            "%llu bytes in %llu calls\n",
            (unsigned long long)
              device->currentFrameStats.hotPathAllocBytes,
            (unsigned long long)
              device->currentFrameStats.hotPathAllocCount,
            (unsigned long long)
              device->currentFrameStats.hotPathFreeBytes,
            (unsigned long long)
              device->currentFrameStats.hotPathFreeCount);
    ok = 0;
  }

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(destination);
  GPUDestroyTexture(source);
  return ok;
}

int
gpu_test_copy(GPUDevice *device) {
  return check_copy_pass_device_dispatch(device) &&
         check_copy_pass_validation(device) &&
         check_copy_pass_invalid_copy_noops(device) &&
         check_compressed_texture_copies(device) &&
         check_texture_blit(device) &&
         gpu_test_texture_transfer(device);
}
