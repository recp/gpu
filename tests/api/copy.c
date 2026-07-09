#include "test.h"
#include "../../src/api/cmdqueue_internal.h"

static int
check_copy_pass_validation(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer fakeCmdb = {0};
  GPUCopyPassEncoder endedPass = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence *fence;
  GPUCopyPassEncoder *copyPass;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUBufferCopyRegion bufferRegion = {0};
  GPUBufferTextureCopyRegion bufferTextureRegion = {0};
  GPUTextureToTextureCopyRegion textureRegion = {0};
  GPUBuffer *sourceBuffer;
  GPUBuffer *bufferCopy;
  GPUBuffer *textureReadback;
  GPUTexture *textureA;
  GPUTexture *textureB;
  uint8_t pixels[4u * 4u * 4u];
  uint8_t bufferCopyBytes[sizeof(pixels)] = {0};
  uint8_t textureBytes[sizeof(pixels)] = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for copy test\n");
    return 0;
  }

  if (GPUBeginCopyPass(NULL, "null")) {
    fprintf(stderr, "copy pass accepted null command buffer\n");
    return 0;
  }
  fakeCmdb._submitted = true;
  if (GPUBeginCopyPass(&fakeCmdb, "submitted")) {
    fprintf(stderr, "copy pass accepted submitted command buffer\n");
    return 0;
  }

  fakeCmdb._submitted = false;
  fakeCmdb._activeEncoder = true;
  if (GPUBeginCopyPass(&fakeCmdb, "active")) {
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
  textureReadback = NULL;
  textureA = NULL;
  textureB = NULL;
  fence = NULL;
  cmdb = NULL;
  copyPass = NULL;
  ok = GPUCreateBuffer(device, &bufferInfo, &sourceBuffer) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &bufferCopy) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &textureReadback) == GPU_OK &&
       GPUQueueWriteBuffer(queue, sourceBuffer, 0u, pixels, sizeof(pixels)) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "copy test buffer setup failed\n");
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
  GPUEndCopyPass(&endedPass);

  ok = GPUAcquireCommandBuffer(queue, "reflection-copy-pass", &cmdb) == GPU_OK && cmdb;
  if (!ok) {
    fprintf(stderr, "failed to acquire command buffer for copy test\n");
    goto cleanup;
  }

  copyPass = GPUBeginCopyPass(cmdb, "reflection-copy");
  if (!copyPass) {
    fprintf(stderr, "failed to begin copy pass\n");
    ok = 0;
    goto cleanup;
  }
  if (GPUBeginCopyPass(cmdb, "nested-copy")) {
    fprintf(stderr, "copy pass accepted nested encoder\n");
    ok = 0;
    goto cleanup;
  }

  bufferRegion.sizeBytes = sizeof(pixels);
  GPUCopyBufferToBuffer(copyPass, sourceBuffer, bufferCopy, &bufferRegion);

  bufferTextureRegion.bytesPerRow = 4u * 4u;
  bufferTextureRegion.rowsPerImage = 4u;
  bufferTextureRegion.texture.width = 4u;
  bufferTextureRegion.texture.height = 4u;
  bufferTextureRegion.texture.depth = 1u;
  bufferTextureRegion.texture.layerCount = 1u;
  GPUCopyBufferToTexture(copyPass, sourceBuffer, textureA, &bufferTextureRegion);

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
  GPUEndCopyPass(copyPass);
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
       memcmp(pixels, textureBytes, sizeof(pixels)) == 0;
  if (!ok) {
    fprintf(stderr, "copy pass readback mismatch\n");
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(textureB);
  GPUDestroyTexture(textureA);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(bufferCopy);
  GPUDestroyBuffer(sourceBuffer);
  return ok;
}

static int
check_copy_pass_invalid_copy_noops(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence *fence;
  GPUCopyPassEncoder *copyPass;
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
  uint8_t textureOut[sizeof(protectedBytes)] = {0};
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
       GPUCreateBuffer(device, &bufferInfo, &protectedBuffer) == GPU_OK &&
       GPUCreateBuffer(device, &bufferInfo, &textureReadback) == GPU_OK;
  if (!ok) {
    fprintf(stderr, "invalid copy buffer setup failed\n");
    goto cleanup;
  }

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

  copyPass = GPUBeginCopyPass(cmdb, "invalid-copy-noops");
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

  fullTextureRegion.bytesPerRow = 4u * 4u;
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
  GPUEndCopyPass(copyPass);
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
       memcmp(protectedBytes, textureOut, sizeof(protectedBytes)) == 0;
  if (!ok) {
    fprintf(stderr, "invalid copy no-op target changed\n");
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
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

int
gpu_test_copy(GPUDevice *device) {
  return check_copy_pass_validation(device) &&
         check_copy_pass_invalid_copy_noops(device);
}
