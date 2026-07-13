#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/texture_internal.h"

static GPUBuffer gScopedBuffer;
static uint8_t   gScopedBufferStorage[32];
static uint32_t  gScopedBufferCreateCalls;
static uint32_t  gScopedBufferWriteCalls;
static uint32_t  gScopedBufferReadCalls;
static uint32_t  gScopedBufferDestroyCalls;
static uint32_t  gScopedTextureWriteCalls;

static GPUResult
create_scoped_buffer(GPUDevice                 * __restrict device,
                     const GPUBufferCreateInfo * __restrict info,
                     GPUBuffer                ** __restrict outBuffer) {
  (void)device;
  (void)info;
  memset(&gScopedBuffer, 0, sizeof(gScopedBuffer));
  memset(gScopedBufferStorage, 0, sizeof(gScopedBufferStorage));
  *outBuffer = &gScopedBuffer;
  gScopedBufferCreateCalls++;
  return GPU_OK;
}

static void
destroy_scoped_buffer(GPUBuffer * __restrict buffer) {
  (void)buffer;
  gScopedBufferDestroyCalls++;
}

static GPUResult
write_scoped_buffer(GPUCommandQueue * __restrict queue,
                    GPUBuffer       * __restrict buffer,
                    uint64_t                     offset,
                    const void      * __restrict data,
                    uint64_t                     sizeBytes) {
  (void)queue;
  (void)buffer;
  memcpy(gScopedBufferStorage + offset, data, (size_t)sizeBytes);
  gScopedBufferWriteCalls++;
  return GPU_OK;
}

static GPUResult
read_scoped_buffer(GPUCommandQueue * __restrict queue,
                   GPUBuffer       * __restrict buffer,
                   uint64_t                     offset,
                   void           * __restrict outData,
                   uint64_t                     sizeBytes) {
  (void)queue;
  (void)buffer;
  memcpy(outData, gScopedBufferStorage + offset, (size_t)sizeBytes);
  gScopedBufferReadCalls++;
  return GPU_OK;
}

static GPUResult
write_scoped_texture(GPUCommandQueue             * __restrict queue,
                     GPUTexture                  * __restrict texture,
                     const GPUTextureWriteRegion * __restrict region,
                     const void                  * __restrict data,
                     uint64_t                                 sizeBytes) {
  (void)queue;
  (void)texture;
  (void)region;
  (void)data;
  (void)sizeBytes;
  gScopedTextureWriteCalls++;
  return GPU_OK;
}

static int
check_buffer_device_dispatch(GPUDevice *activeDevice) {
  GPUBuffer           *buffer;
  GPUBufferCreateInfo info = {0};
  GPUCommandQueue     queue = {0};
  GPUCommandQueue     foreignQueue = {0};
  GPUDevice           device = {0};
  GPUApi              scopedApi;
  uint32_t             source[4] = { 2u, 4u, 6u, 8u };
  uint32_t             result[4] = {0};

  if (!activeDevice || !gpuDeviceApi(activeDevice)) {
    fprintf(stderr, "buffer dispatch has no device api\n");
    return 0;
  }

  scopedApi                          = *gpuDeviceApi(activeDevice);
  scopedApi.buf.create               = create_scoped_buffer;
  scopedApi.buf.destroy              = destroy_scoped_buffer;
  scopedApi.buf.write                = write_scoped_buffer;
  scopedApi.buf.read                 = read_scoped_buffer;
  device._api                        = &scopedApi;
  queue._device                      = &device;
  foreignQueue._device               = activeDevice;
  gScopedBufferCreateCalls  = 0u;
  gScopedBufferWriteCalls   = 0u;
  gScopedBufferReadCalls    = 0u;
  gScopedBufferDestroyCalls = 0u;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = sizeof(source);
  info.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                          GPU_BUFFER_USAGE_COPY_DST;
  buffer = NULL;
  if (GPUCreateBuffer(&device, &info, &buffer) != GPU_OK ||
      buffer != &gScopedBuffer || buffer->device != &device ||
      buffer->sizeBytes != sizeof(source) || buffer->usage != info.usage ||
      GPUQueueWriteBuffer(&queue,
                          buffer,
                          0u,
                          source,
                          sizeof(source)) != GPU_OK ||
      GPUQueueReadBuffer(&queue,
                         buffer,
                         0u,
                         result,
                         sizeof(result)) != GPU_OK ||
      memcmp(source, result, sizeof(source)) != 0) {
    fprintf(stderr, "buffer device dispatch failed\n");
    return 0;
  }

  if (GPUQueueWriteBuffer(&foreignQueue,
                          buffer,
                          0u,
                          source,
                          sizeof(source)) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueReadBuffer(&foreignQueue,
                         buffer,
                         0u,
                         result,
                         sizeof(result)) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "buffer accepted a foreign queue\n");
    return 0;
  }

  GPUDestroyBuffer(buffer);
  if (gScopedBufferCreateCalls != 1u ||
      gScopedBufferWriteCalls != 1u ||
      gScopedBufferReadCalls != 1u ||
      gScopedBufferDestroyCalls != 1u) {
    fprintf(stderr, "buffer dispatch called wrong backend\n");
    return 0;
  }

  return 1;
}

static int
check_texture_transfer_layout(GPUDevice *activeDevice) {
  GPUTextureWriteRegion region = {0};
  GPUCommandQueue       queue = {0};
  GPUTexture            texture = {0};
  GPUDevice             device = {0};
  GPUApi                scopedApi;
  uint8_t               blocks[64] = {0};

  if (!activeDevice || !gpuDeviceApi(activeDevice)) {
    fprintf(stderr, "texture layout has no device api\n");
    return 0;
  }

  scopedApi               = *gpuDeviceApi(activeDevice);
  scopedApi.texture.write = write_scoped_texture;
  device._api             = &scopedApi;
  queue._device           = &device;
  texture.device          = &device;
  texture.format          = GPU_FORMAT_ASTC_5X4_UNORM;
  texture.dimension       = GPU_TEXTURE_DIMENSION_2D;
  texture.width           = 7u;
  texture.height          = 5u;
  texture.depthOrLayers   = 1u;
  texture.mipLevelCount   = 1u;
  texture.sampleCount     = 1u;
  texture.usage           = GPU_TEXTURE_USAGE_COPY_DST;

  region.width        = 7u;
  region.height       = 5u;
  region.depth        = 1u;
  region.layerCount   = 1u;
  region.bytesPerRow  = 32u;
  region.rowsPerImage = 8u;

  gScopedTextureWriteCalls = 0u;

  if (GPUQueueWriteTexture(&queue,
                           &texture,
                           &region,
                           blocks,
                           sizeof(blocks)) != GPU_OK ||
      gScopedTextureWriteCalls != 1u) {
    fprintf(stderr, "texture layout rejected a valid mip edge\n");
    return 0;
  }
  if (GPUQueueWriteTexture(&queue,
                           &texture,
                           &region,
                           blocks,
                           sizeof(blocks) - 1u) != GPU_ERROR_INVALID_ARGUMENT ||
      gScopedTextureWriteCalls != 1u) {
    fprintf(stderr, "texture layout accepted undersized block data\n");
    return 0;
  }

  region.bytesPerRow = 16u;
  if (GPUQueueWriteTexture(&queue,
                           &texture,
                           &region,
                           blocks,
                           sizeof(blocks)) != GPU_ERROR_INVALID_ARGUMENT ||
      gScopedTextureWriteCalls != 1u) {
    fprintf(stderr, "texture layout accepted a short block row\n");
    return 0;
  }
  region.bytesPerRow  = 32u;
  region.rowsPerImage = 5u;
  if (GPUQueueWriteTexture(&queue,
                           &texture,
                           &region,
                           blocks,
                           sizeof(blocks)) != GPU_ERROR_INVALID_ARGUMENT ||
      gScopedTextureWriteCalls != 1u) {
    fprintf(stderr, "texture layout accepted unaligned rowsPerImage\n");
    return 0;
  }
  region.rowsPerImage = 8u;
  region.width        = 6u;
  if (GPUQueueWriteTexture(&queue,
                           &texture,
                           &region,
                           blocks,
                           sizeof(blocks)) != GPU_ERROR_INVALID_ARGUMENT ||
      gScopedTextureWriteCalls != 1u) {
    fprintf(stderr, "texture layout accepted a partial compressed block\n");
    return 0;
  }

  return 1;
}

static int
check_destroy_null_handles(void) {
  GPUDestroyInstance(NULL);
  GPUDestroySurface(NULL);
  GPUDestroyDevice(NULL);
  GPUDestroySwapchain(NULL);
  GPUDestroyFence(NULL);
  GPUDestroySemaphore(NULL);
  GPUDestroyBuffer(NULL);
  GPUDestroyTexture(NULL);
  GPUDestroyTextureView(NULL);
  GPUDestroySampler(NULL);
  GPUDestroyShaderLibrary(NULL);
  GPUDestroyBindGroupLayout(NULL);
  GPUDestroyBindGroup(NULL);
  GPUDestroyShaderLayout(NULL);
  GPUDestroyPipelineLayout(NULL);
  GPUDestroyPipelineCache(NULL);
  GPUDestroyRenderPipeline(NULL);
  GPUDestroyComputePipeline(NULL);
  GPUDestroyQuerySet(NULL);
  return 1;
}

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
  GPUTexture *textureNoCopyDst;
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

  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(NULL, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted null device\n");
    return 0;
  }
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, NULL, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted null info\n");
    return 0;
  }
  if (GPUCreateTexture(device, &textureInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture create accepted null output\n");
    return 0;
  }

  textureInfo.sampleCount = 3u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) !=
        GPU_ERROR_INVALID_ARGUMENT || texture != NULL) {
    fprintf(stderr, "texture create accepted invalid sample count\n");
    return 0;
  }
  textureInfo.sampleCount = 4u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) !=
        GPU_ERROR_INVALID_ARGUMENT || texture != NULL) {
    fprintf(stderr, "texture create accepted multisampled copy texture\n");
    return 0;
  }
  textureInfo.sampleCount = 1u;

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
  textureInfo.format = GPU_FORMAT_UNDEFINED;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted undefined format\n");
    return 0;
  }

  textureInfo.format = GPU_FORMAT_COUNT;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) !=
        GPU_ERROR_INVALID_ARGUMENT || texture != NULL) {
    fprintf(stderr, "texture create accepted out-of-range format\n");
    return 0;
  }

  textureInfo.format = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero width\n");
    return 0;
  }

  textureInfo.width = 4u;
  textureInfo.height = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero height\n");
    return 0;
  }

  textureInfo.height = 4u;
  textureInfo.depthOrLayers = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero depth/layers\n");
    return 0;
  }

  textureInfo.depthOrLayers = 1u;
  textureInfo.usage = 0u;
  texture = (GPUTexture *)(uintptr_t)1u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_ERROR_INVALID_ARGUMENT ||
      texture != NULL) {
    fprintf(stderr, "texture create accepted zero usage\n");
    return 0;
  }

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  texture = NULL;
  textureNoCopyDst = NULL;
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
  if (GPUQueueWriteTexture(NULL, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueWriteTexture(queue, NULL, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueWriteTexture(queue, texture, NULL, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueWriteTexture(queue, texture, &region, NULL, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT ||
      GPUQueueWriteTexture(queue, texture, &region, pixels, 0u) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted null or empty arguments\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "texture write failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels) - 1u) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted undersized data\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  region.bytesPerRow = 0u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted zero bytesPerRow\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.bytesPerRow = 4u * 4u;

  region.rowsPerImage = 3u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted short rowsPerImage\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.rowsPerImage = 4u;

  region.width = 5u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted out-of-range width\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.width = 4u;

  region.height = 5u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted out-of-range height\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.height = 4u;

  region.depth = 0u;
  if (GPUQueueWriteTexture(queue, texture, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted zero depth\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  region.depth = 1u;

  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUCreateTexture(device, &textureInfo, &textureNoCopyDst) != GPU_OK ||
      !textureNoCopyDst) {
    fprintf(stderr, "texture without copy dst setup failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (GPUQueueWriteTexture(queue, textureNoCopyDst, &region, pixels, sizeof(pixels)) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture write accepted texture without copy dst usage\n");
    GPUDestroyTexture(textureNoCopyDst);
    GPUDestroyTexture(texture);
    return 0;
  }
  GPUDestroyTexture(textureNoCopyDst);
  textureNoCopyDst = NULL;

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

  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(NULL, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted null texture\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, NULL, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted null info\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (GPUCreateTextureView(texture, &viewInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "texture view create accepted null output\n");
    GPUDestroyTexture(texture);
    return 0;
  }

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
  viewInfo.viewType = (GPUTextureViewType)99;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted invalid view type\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.viewType = GPU_TEXTURE_VIEW_2D;
  viewInfo.format = GPU_FORMAT_UNDEFINED;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted undefined format\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.format = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.baseMipLevel = 1u;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted invalid mip range\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.baseMipLevel = 0u;
  viewInfo.baseArrayLayer = 1u;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted invalid array range\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.baseArrayLayer = 0u;
  viewInfo.arrayLayerCount = 0u;
  view = (GPUTextureView *)(uintptr_t)1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_ERROR_INVALID_ARGUMENT ||
      view != NULL) {
    fprintf(stderr, "texture view create accepted zero array layer count\n");
    GPUDestroyTexture(texture);
    return 0;
  }

  viewInfo.arrayLayerCount = 1u;
  view = NULL;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "texture view create failed\n");
    GPUDestroyTexture(texture);
    return 0;
  }
  if (view->_texture != texture ||
      view->format != viewInfo.format ||
      view->viewType != viewInfo.viewType ||
      view->baseMipLevel != viewInfo.baseMipLevel ||
      view->mipLevelCount != viewInfo.mipLevelCount ||
      view->baseArrayLayer != viewInfo.baseArrayLayer ||
      view->arrayLayerCount != viewInfo.arrayLayerCount) {
    fprintf(stderr, "texture view metadata mismatch\n");
    GPUDestroyTextureView(view);
    GPUDestroyTexture(texture);
    return 0;
  }
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);

  return 1;
}

static int
check_cube_view_validation(GPUDevice *device) {
  typedef struct CubeViewCase {
    GPUTextureViewType viewType;
    uint32_t           baseLayer;
    uint32_t           layerCount;
    bool               valid;
  } CubeViewCase;

  static const CubeViewCase cases[] = {
    {GPU_TEXTURE_VIEW_CUBE,       0u,  6u, true},
    {GPU_TEXTURE_VIEW_CUBE,       6u,  6u, false},
    {GPU_TEXTURE_VIEW_CUBE,       0u, 12u, false},
    {GPU_TEXTURE_VIEW_CUBE_ARRAY, 0u,  6u, true},
    {GPU_TEXTURE_VIEW_CUBE_ARRAY, 6u,  6u, true},
    {GPU_TEXTURE_VIEW_CUBE_ARRAY, 1u,  6u, false},
    {GPU_TEXTURE_VIEW_CUBE_ARRAY, 6u,  5u, false}
  };
  GPUTexture               *texture;
  GPUTextureView           *view;
  GPUTextureCreateInfo      textureInfo = {0};
  GPUTextureViewCreateInfo  viewInfo    = {0};
  GPUResult                 result;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-cube-view-validation";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 12u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  texture                      = NULL;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "cube view validation texture creation failed\n");
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-cube-view-validation";
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
    viewInfo.viewType        = cases[i].viewType;
    viewInfo.baseArrayLayer  = cases[i].baseLayer;
    viewInfo.arrayLayerCount = cases[i].layerCount;

    view   = (GPUTextureView *)(uintptr_t)1u;
    result = GPUCreateTextureView(texture, &viewInfo, &view);
    if ((cases[i].valid && (result != GPU_OK || !view)) ||
        (!cases[i].valid &&
         (result != GPU_ERROR_INVALID_ARGUMENT || view != NULL))) {
      fprintf(stderr,
              "cube view validation case %u returned %d\n",
              i,
              result);
      GPUDestroyTextureView(view);
      GPUDestroyTexture(texture);
      return 0;
    }
    GPUDestroyTextureView(view);
  }

  GPUDestroyTexture(texture);
  return 1;
}

static int
check_3d_view_validation(GPUDevice *device) {
  typedef struct Texture3DViewCase {
    uint32_t baseLayer;
    uint32_t layerCount;
    bool     valid;
  } Texture3DViewCase;

  static const Texture3DViewCase cases[] = {
    {0u, 1u, true},
    {1u, 1u, false},
    {0u, 2u, false}
  };
  GPUTexture               *texture;
  GPUTextureView           *view;
  GPUTextureCreateInfo      textureInfo = {0};
  GPUTextureViewCreateInfo  viewInfo    = {0};
  GPUResult                 result;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-3d-view-validation";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_3D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 4u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  texture                      = NULL;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "3D view validation texture creation failed\n");
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "api-3d-view-validation";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_3D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
    viewInfo.baseArrayLayer  = cases[i].baseLayer;
    viewInfo.arrayLayerCount = cases[i].layerCount;

    view   = (GPUTextureView *)(uintptr_t)1u;
    result = GPUCreateTextureView(texture, &viewInfo, &view);
    if ((cases[i].valid && (result != GPU_OK || !view)) ||
        (!cases[i].valid &&
         (result != GPU_ERROR_INVALID_ARGUMENT || view != NULL))) {
      fprintf(stderr,
              "3D view validation case %u returned %d\n",
              i,
              result);
      GPUDestroyTextureView(view);
      GPUDestroyTexture(texture);
      return 0;
    }
    GPUDestroyTextureView(view);
  }

  GPUDestroyTexture(texture);
  return 1;
}

int
gpu_test_resources(GPUDevice *device) {
  return check_destroy_null_handles() &&
         check_buffer_device_dispatch(device) &&
         check_texture_transfer_layout(device) &&
         check_resource_validation(device) &&
         check_cube_view_validation(device) &&
         check_3d_view_validation(device);
}
