#include "test.h"

static int
check_resource_validation(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUBufferCreateInfo bufferInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureInfo queriedTextureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTextureWriteRegion region = {0};
  GPUBuffer *buffer;
  GPUTexture *texture;
  GPUTextureView *view;
  uint32_t writeWords[4] = { 1u, 2u, 3u, 4u };
  uint32_t readWords[4] = { 0u, 0u, 0u, 0u };
  uint8_t pixels[4u * 4u * 4u] = {0};

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for resource test\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes = sizeof(writeWords);
  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(NULL, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted null device\n");
    return 0;
  }
  if (GPUCreateBuffer(device, &bufferInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "buffer create accepted null output\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted wrong sType\n");
    return 0;
  }

  bufferInfo.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = (uint32_t)(sizeof(bufferInfo) - 1u);
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted short structSize\n");
    return 0;
  }

  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage = 0u;
  buffer = (GPUBuffer *)(uintptr_t)1u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_ERROR_INVALID_ARGUMENT ||
      buffer != NULL) {
    fprintf(stderr, "buffer create accepted zero usage\n");
    return 0;
  }

  bufferInfo.usage = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  buffer = NULL;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "buffer create failed\n");
    return 0;
  }
  if (GPUQueueWriteBuffer(queue, buffer, 0u, writeWords, sizeof(writeWords)) != GPU_OK ||
      GPUQueueReadBuffer(queue, buffer, 0u, readWords, sizeof(readWords)) != GPU_OK ||
      memcmp(writeWords, readWords, sizeof(writeWords)) != 0) {
    fprintf(stderr, "buffer write/read failed\n");
    GPUDestroyBuffer(buffer);
    return 0;
  }
  if (GPUQueueWriteBuffer(queue, buffer, 12u, writeWords, 8u) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueReadBuffer(queue, buffer, 12u, readWords, 8u) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "buffer write/read accepted out-of-bounds range\n");
    GPUDestroyBuffer(buffer);
    return 0;
  }
  GPUDestroyBuffer(buffer);

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 4u;
  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted wrong sType\n");
    return 0;
  }

  textureInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = (uint32_t)(sizeof(textureInfo) - 1u);
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted short structSize\n");
    return 0;
  }

  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension = (GPUTextureDimension)99;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted invalid dimension\n");
    return 0;
  }

  textureInfo.dimension = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.usage = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero usage\n");
    return 0;
  }

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  texture = NULL;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "texture create failed\n");
    return 0;
  }
  if (GPUGetTextureInfo(NULL, &queriedTextureInfo) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetTextureInfo(texture, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture info accepted invalid arguments\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (GPUGetTextureInfo(texture, &queriedTextureInfo) != GPU_OK ||
      queriedTextureInfo.dimension != textureInfo.dimension ||
      queriedTextureInfo.format != textureInfo.format ||
      queriedTextureInfo.width != textureInfo.width ||
      queriedTextureInfo.height != textureInfo.height ||
      queriedTextureInfo.depthOrLayers != textureInfo.depthOrLayers ||
      queriedTextureInfo.mipLevelCount != textureInfo.mipLevelCount ||
      queriedTextureInfo.sampleCount != textureInfo.sampleCount ||
      queriedTextureInfo.usage != textureInfo.usage) {
    fprintf(stderr, "texture info query returned wrong metadata\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  region.width = 4u;
  region.height = 4u;
  region.depth = 1u;
  region.layerCount = 1u;
  region.bytesPerRow = 4u * 4u;
  region.rowsPerImage = 4u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "texture write failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  region.mipLevel = 1u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted invalid mip level\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.mipLevel = 0u;
  region.layerCount = 2u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted invalid layer range\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.layerCount = 1u;

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType = GPU_TEXTURE_VIEW_2D;
  viewInfo.format = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount = 1u;
  viewInfo.arrayLayerCount = 1u;

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted wrong sType\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = (uint32_t)(sizeof(viewInfo) - 1u);
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted short structSize\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.baseMipLevel = 1u;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted invalid mip range\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.baseMipLevel = 0u;
  view = NULL;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "texture view create failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);

  return 1;
}

int
gpu_test_resources(GPUDevice *device) {
  return check_resource_validation(device);
}
