#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  TEST_WIDTH       = 4u,
  TEST_HEIGHT      = 4u,
  TEST_PIXEL_BYTES = 4u,
  TEST_DATA_BYTES  = TEST_WIDTH * TEST_HEIGHT * TEST_PIXEL_BYTES
};

static GPUBuffer *
create_buffer(GPUDevice          *device,
              const char         *label,
              uint64_t            sizeBytes,
              GPUBufferUsageFlags usage) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer          *buffer;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.sizeBytes        = sizeBytes;
  info.usage            = usage;
  buffer                = NULL;
  if (GPUCreateBuffer(device, &info, &buffer) != GPU_OK) {
    return NULL;
  }
  return buffer;
}

static GPUTexture *
create_texture(GPUDevice *device) {
  GPUTextureCreateInfo info = {0};
  GPUTexture          *texture;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "indirect-copy-texture";
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = GPU_FORMAT_RGBA8_UNORM;
  info.width            = TEST_WIDTH;
  info.height           = TEST_HEIGHT;
  info.depthOrLayers    = 1u;
  info.mipLevelCount    = 1u;
  info.sampleCount      = 1u;
  info.usage            = GPU_TEXTURE_USAGE_COPY_SRC |
                          GPU_TEXTURE_USAGE_COPY_DST;
  texture               = NULL;
  if (GPUCreateTexture(device, &info, &texture) != GPU_OK) {
    return NULL;
  }
  return texture;
}

static int
run_indirect_copies(GPUDevice *device, GPUQueue *queue) {
  const GPUBufferUsageFlags addressUsage =
    GPU_BUFFER_USAGE_COPY_SRC |
    GPU_BUFFER_USAGE_COPY_DST |
    GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT;
  const GPUBufferUsageFlags commandUsage =
    GPU_BUFFER_USAGE_COPY_DST |
    GPU_BUFFER_USAGE_INDIRECT |
    GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT;
  GPUIndirectMemoryCopyCommandEXT          bufferCommand = {0};
  GPUIndirectMemoryToTextureCommandEXT     textureCommand = {0};
  GPUIndirectTextureSubresourceEXT         textureSubresource = {0};
  GPUIndirectMemoryCopyInfoEXT             bufferCopyInfo = {0};
  GPUIndirectMemoryToTextureCopyInfoEXT    textureCopyInfo = {0};
  GPUBufferCopyRegion                      recordCopies[2] = {0};
  GPUBufferBarrier                         commandBarriers[2] = {0};
  GPUBarrierBatch                          barrierBatch = {0};
  GPUBufferTextureCopyRegion               readbackRegion = {0};
  GPUQueueSubmitInfo                       submitInfo = {0};
  GPUCommandBuffer                        *submitList[1] = {0};
  GPUBuffer                               *bufferSource;
  GPUBuffer                               *bufferDestination;
  GPUBuffer                               *textureSource;
  GPUBuffer                               *textureReadback;
  GPUBuffer                               *bufferRecordUpload;
  GPUBuffer                               *textureRecordUpload;
  GPUBuffer                               *bufferRecords;
  GPUBuffer                               *textureRecords;
  GPUTexture                              *texture;
  GPUCommandBuffer                        *cmdb;
  GPUCopyPassEncoder                      *copyPass;
  GPUFence                                *fence;
  uint8_t                                  bufferInput[TEST_DATA_BYTES];
  uint8_t                                  textureInput[TEST_DATA_BYTES];
  uint8_t                                  bufferOutput[TEST_DATA_BYTES] = {0};
  uint8_t                                  textureOutput[TEST_DATA_BYTES] = {0};
  int                                      ok;

  bufferSource       = NULL;
  bufferDestination  = NULL;
  textureSource      = NULL;
  textureReadback    = NULL;
  bufferRecordUpload = NULL;
  textureRecordUpload = NULL;
  bufferRecords      = NULL;
  textureRecords     = NULL;
  texture            = NULL;
  cmdb               = NULL;
  copyPass           = NULL;
  fence              = NULL;
  ok                 = 0;

  for (uint32_t i = 0u; i < TEST_DATA_BYTES; i++) {
    bufferInput[i]  = (uint8_t)(i * 3u + 7u);
    textureInput[i] = (uint8_t)(i * 5u + 11u);
  }

  bufferSource = create_buffer(device,
                               "indirect-copy-source",
                               sizeof(bufferInput),
                               addressUsage);
  bufferDestination = create_buffer(device,
                                    "indirect-copy-destination",
                                    sizeof(bufferInput),
                                    addressUsage);
  textureSource = create_buffer(device,
                                "indirect-texture-source",
                                sizeof(textureInput),
                                addressUsage);
  textureReadback = create_buffer(device,
                                  "indirect-texture-readback",
                                  sizeof(textureInput),
                                  GPU_BUFFER_USAGE_COPY_SRC |
                                  GPU_BUFFER_USAGE_COPY_DST);
  bufferRecordUpload = create_buffer(device,
                                     "indirect-copy-record-upload",
                                     sizeof(bufferCommand),
                                     GPU_BUFFER_USAGE_COPY_SRC |
                                     GPU_BUFFER_USAGE_COPY_DST);
  textureRecordUpload = create_buffer(device,
                                      "indirect-texture-record-upload",
                                      sizeof(textureCommand),
                                      GPU_BUFFER_USAGE_COPY_SRC |
                                      GPU_BUFFER_USAGE_COPY_DST);
  bufferRecords = create_buffer(device,
                                "indirect-copy-records",
                                sizeof(bufferCommand),
                                commandUsage);
  textureRecords = create_buffer(device,
                                 "indirect-texture-records",
                                 sizeof(textureCommand),
                                 commandUsage);
  texture = create_texture(device);
  if (!bufferSource || !bufferDestination || !textureSource ||
      !textureReadback || !bufferRecordUpload || !textureRecordUpload ||
      !bufferRecords || !textureRecords || !texture) {
    fprintf(stderr, "indirect copy resource creation failed\n");
    goto cleanup;
  }

  bufferCommand.srcAddress = GPUGetBufferDeviceAddressEXT(bufferSource);
  bufferCommand.dstAddress = GPUGetBufferDeviceAddressEXT(bufferDestination);
  bufferCommand.sizeBytes  = sizeof(bufferInput);
  textureCommand.srcAddress        = GPUGetBufferDeviceAddressEXT(textureSource);
  textureCommand.bufferRowLength   = TEST_WIDTH;
  textureCommand.bufferImageHeight = TEST_HEIGHT;
  textureCommand.texture.aspectMask =
    GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT;
  textureCommand.texture.layerCount = 1u;
  textureCommand.width              = TEST_WIDTH;
  textureCommand.height             = TEST_HEIGHT;
  textureCommand.depth              = 1u;
  if (bufferCommand.srcAddress == 0u || bufferCommand.dstAddress == 0u ||
      textureCommand.srcAddress == 0u) {
    fprintf(stderr, "indirect copy device address unavailable\n");
    goto cleanup;
  }

  if (GPUQueueWriteBuffer(queue,
                          bufferSource,
                          0u,
                          bufferInput,
                          sizeof(bufferInput)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          textureSource,
                          0u,
                          textureInput,
                          sizeof(textureInput)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          bufferRecordUpload,
                          0u,
                          &bufferCommand,
                          sizeof(bufferCommand)) != GPU_OK ||
      GPUQueueWriteBuffer(queue,
                          textureRecordUpload,
                          0u,
                          &textureCommand,
                          sizeof(textureCommand)) != GPU_OK) {
    fprintf(stderr, "indirect copy upload failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "indirect-copy-test",
                              &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "indirect copy command buffer failed\n");
    goto cleanup;
  }
  copyPass = GPUBeginCopyPass(cmdb, "upload-indirect-records");
  if (!copyPass) {
    fprintf(stderr, "indirect record upload pass failed\n");
    goto cleanup;
  }
  recordCopies[0].sizeBytes = sizeof(bufferCommand);
  recordCopies[1].sizeBytes = sizeof(textureCommand);
  GPUCopyBufferToBuffer(copyPass,
                        bufferRecordUpload,
                        bufferRecords,
                        &recordCopies[0]);
  GPUCopyBufferToBuffer(copyPass,
                        textureRecordUpload,
                        textureRecords,
                        &recordCopies[1]);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  commandBarriers[0].buffer    = bufferRecords;
  commandBarriers[0].srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  commandBarriers[0].dstAccess = GPU_ACCESS_INDIRECT_READ;
  commandBarriers[0].sizeBytes = sizeof(bufferCommand);
  commandBarriers[1].buffer    = textureRecords;
  commandBarriers[1].srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  commandBarriers[1].dstAccess = GPU_ACCESS_INDIRECT_READ;
  commandBarriers[1].sizeBytes = sizeof(textureCommand);
  barrierBatch.pBufferBarriers  = commandBarriers;
  barrierBatch.srcStages        = GPU_STAGE_TRANSFER;
  barrierBatch.dstStages        = GPU_STAGE_TRANSFER;
  barrierBatch.bufferBarrierCount = 2u;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "execute-indirect-copies");
  if (!copyPass) {
    fprintf(stderr, "indirect execution pass failed\n");
    goto cleanup;
  }

  bufferCopyInfo.commands.buffer      = bufferRecords;
  bufferCopyInfo.commands.sizeBytes   = sizeof(bufferCommand);
  bufferCopyInfo.commands.strideBytes = sizeof(bufferCommand);
  bufferCopyInfo.srcFlags = GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT;
  bufferCopyInfo.dstFlags = GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT;
  bufferCopyInfo.commandCount = 1u;
  GPUCopyMemoryIndirectEXT(copyPass, &bufferCopyInfo);

  textureSubresource.aspectMask =
    GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT;
  textureSubresource.layerCount = 1u;
  textureCopyInfo.dst                  = texture;
  textureCopyInfo.pTextureSubresources = &textureSubresource;
  textureCopyInfo.commands.buffer      = textureRecords;
  textureCopyInfo.commands.sizeBytes   = sizeof(textureCommand);
  textureCopyInfo.commands.strideBytes = sizeof(textureCommand);
  textureCopyInfo.srcFlags = GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT;
  textureCopyInfo.commandCount = 1u;
  GPUCopyMemoryToTextureIndirectEXT(copyPass, &textureCopyInfo);

  readbackRegion.texture.width      = TEST_WIDTH;
  readbackRegion.texture.height     = TEST_HEIGHT;
  readbackRegion.texture.depth      = 1u;
  readbackRegion.texture.layerCount = 1u;
  readbackRegion.bytesPerRow        = TEST_WIDTH * TEST_PIXEL_BYTES;
  readbackRegion.rowsPerImage       = TEST_HEIGHT;
  GPUCopyTextureToBuffer(copyPass,
                         texture,
                         textureReadback,
                         &readbackRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "indirect copy fence failed\n");
    goto cleanup;
  }
  submitList[0]                    = cmdb;
  submitInfo.chain.sType           = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize      = sizeof(submitInfo);
  submitInfo.ppCommandBuffers      = submitList;
  submitInfo.fence                 = fence;
  submitInfo.commandBufferCount    = 1u;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "indirect copy submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         bufferDestination,
                         0u,
                         bufferOutput,
                         sizeof(bufferOutput)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         textureReadback,
                         0u,
                         textureOutput,
                         sizeof(textureOutput)) != GPU_OK) {
    fprintf(stderr, "indirect copy readback failed\n");
    goto cleanup;
  }
  if (memcmp(bufferInput, bufferOutput, sizeof(bufferInput)) != 0) {
    fprintf(stderr, "indirect buffer copy mismatch\n");
    goto cleanup;
  }
  if (memcmp(textureInput, textureOutput, sizeof(textureInput)) != 0) {
    fprintf(stderr, "indirect texture copy mismatch\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(textureRecords);
  GPUDestroyBuffer(bufferRecords);
  GPUDestroyBuffer(textureRecordUpload);
  GPUDestroyBuffer(bufferRecordUpload);
  GPUDestroyBuffer(textureReadback);
  GPUDestroyBuffer(textureSource);
  GPUDestroyBuffer(bufferDestination);
  GPUDestroyBuffer(bufferSource);
  return ok;
}

int
main(void) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo = {0};
  GPUFeature            features[2];
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUQueue             *queue;
  GPUResult             result;
  uint32_t              adapterCount;
  int                   ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  instance                        = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "indirect copy Vulkan instance failed\n");
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "indirect copy Vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_INDIRECT_MEMORY_COPY) ||
      !GPUIsFeatureSupported(
        adapter,
        GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY
      )) {
    puts("indirect copy test skipped: unsupported adapter");
    GPUDestroyInstance(instance);
    return 77;
  }

  features[0] = GPU_FEATURE_INDIRECT_MEMORY_COPY;
  features[1] = GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY;
  deviceInfo.chain.sType            = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize       = sizeof(deviceInfo);
  deviceInfo.label                  = "indirect-copy-test-device";
  deviceInfo.required.pFeatures     = features;
  deviceInfo.required.featureCount  = 2u;
  device                             = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "indirect copy Vulkan device failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "indirect copy Vulkan queue failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  ok = run_indirect_copies(device, queue);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  if (!ok) {
    return 1;
  }

  puts("Vulkan indirect memory copy validation passed");
  return 0;
}
