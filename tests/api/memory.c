#include "test.h"

int
gpu_test_memory(GPUAdapter *adapter) {
  GPUFeature                feature         = GPU_FEATURE_PLACED_RESOURCES;
  GPUDeviceCreateInfo       deviceInfo      = {0};
  GPUBufferCreateInfo       bufferInfo      = {0};
  GPUTextureCreateInfo      textureInfo     = {0};
  GPUMemoryRequirements     bufferMemory    = {0};
  GPUMemoryRequirements     textureMemory   = {0};
  GPUHeapCreateInfo         heapInfo        = {0};
  GPUAliasingBarrier        aliasingBarrier = {0};
  GPUBarrierBatch           barrierBatch    = {0};
  GPUQueueSubmitInfo        submitInfo      = {0};
  GPUCommandBuffer         *submitList[1]   = {0};
  uint32_t                  input[4]        = {1u, 2u, 3u, 4u};
  uint32_t                  output[4]       = {0};
  GPUDevice                *disabledDevice  = NULL;
  GPUDevice                *device          = NULL;
  GPUQueue                 *queue           = NULL;
  GPUHeap                  *heap            = NULL;
  GPUHeap                  *textureHeap     = NULL;
  GPUBuffer                *buffer          = NULL;
  GPUBuffer                *aliasBuffer     = NULL;
  GPUBuffer                *invalidBuffer   = NULL;
  GPUTexture               *texture         = NULL;
  GPUCommandBuffer         *cmdb            = NULL;
  GPUFence                 *fence           = NULL;
  uint64_t                  compatibility;
  uint64_t                  heapSize;
  GPUResult                 result;
  int                       ok              = 0;

  if (!adapter) {
    return 0;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (!GPUIsFeatureSupported(adapter, feature)) {
    result = GPUCreateDevice(adapter, &deviceInfo, &device);
    GPUDestroyDevice(device);
    return result == GPU_ERROR_UNSUPPORTED && !device;
  }

  deviceInfo.required.pFeatures    = NULL;
  deviceInfo.required.featureCount = 0u;
  if (GPUCreateDevice(adapter, &deviceInfo, &disabledDevice) != GPU_OK ||
      !disabledDevice) {
    fprintf(stderr, "placed memory disabled-device setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(input);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  result = GPUGetBufferMemoryRequirements(disabledDevice,
                                          &bufferInfo,
                                          &bufferMemory);
  if (result != GPU_ERROR_UNSUPPORTED || bufferMemory.sizeBytes != 0u) {
    fprintf(stderr, "placed memory accepted without feature enablement\n");
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "placed memory feature enablement failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "placed memory queue unavailable\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  if (GPUGetBufferMemoryRequirements(device,
                                     &bufferInfo,
                                     &bufferMemory) != GPU_OK ||
      GPUGetTextureMemoryRequirements(device,
                                      &textureInfo,
                                      &textureMemory) != GPU_OK) {
    fprintf(stderr, "placed memory requirements query failed\n");
    goto cleanup;
  }
  compatibility = bufferMemory.compatibilityMask &
                  textureMemory.compatibilityMask;
  heapSize      = compatibility != 0u &&
                  bufferMemory.sizeBytes < textureMemory.sizeBytes
                    ? textureMemory.sizeBytes
                    : bufferMemory.sizeBytes;

  heapInfo.chain.sType       = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
  heapInfo.chain.structSize  = sizeof(heapInfo);
  heapInfo.label             = "api-placed-memory";
  heapInfo.sizeBytes         = heapSize;
  heapInfo.compatibilityMask = compatibility != 0u
                                 ? compatibility
                                 : bufferMemory.compatibilityMask;
  if (GPUCreateHeap(device, &heapInfo, &heap) != GPU_OK || !heap) {
    fprintf(stderr, "placed heap creation failed\n");
    goto cleanup;
  }
  if (GPUCreatePlacedBuffer(device,
                            &bufferInfo,
                            heap,
                            heapSize,
                            &invalidBuffer) != GPU_ERROR_INVALID_ARGUMENT ||
      invalidBuffer) {
    fprintf(stderr, "placed buffer accepted an out-of-range offset\n");
    goto cleanup;
  }
  if (GPUCreatePlacedBuffer(device,
                            &bufferInfo,
                            heap,
                            0u,
                            &buffer) != GPU_OK || !buffer) {
    fprintf(stderr, "placed resource creation failed\n");
    goto cleanup;
  }
  if (compatibility != 0u) {
    if (GPUCreatePlacedTexture(device,
                               &textureInfo,
                               heap,
                               0u,
                               &texture) != GPU_OK || !texture) {
      fprintf(stderr, "placed texture creation failed\n");
      goto cleanup;
    }
  } else {
    if (GPUCreatePlacedBuffer(device,
                              &bufferInfo,
                              heap,
                              0u,
                              &aliasBuffer) != GPU_OK || !aliasBuffer) {
      fprintf(stderr, "placed buffer alias creation failed\n");
      goto cleanup;
    }
    heapInfo.sizeBytes         = textureMemory.sizeBytes;
    heapInfo.compatibilityMask = textureMemory.compatibilityMask;
    if (GPUCreateHeap(device, &heapInfo, &textureHeap) != GPU_OK ||
        !textureHeap ||
        GPUCreatePlacedTexture(device,
                               &textureInfo,
                               textureHeap,
                               0u,
                               &texture) != GPU_OK || !texture) {
      fprintf(stderr, "separate placed texture creation failed\n");
      goto cleanup;
    }
  }

  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          input,
                          sizeof(input)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      memcmp(input, output, sizeof(input)) != 0) {
    fprintf(stderr, "placed buffer upload/readback failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-placed-alias", &cmdb) != GPU_OK ||
      !cmdb || GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "placed alias command setup failed\n");
    goto cleanup;
  }
  aliasingBarrier.beforeBuffer = buffer;
  if (aliasBuffer) {
    aliasingBarrier.afterBuffer = aliasBuffer;
  } else {
    aliasingBarrier.afterTexture = texture;
  }
  barrierBatch.pAliasingBarriers    = &aliasingBarrier;
  barrierBatch.srcStages            = GPU_STAGE_TRANSFER;
  barrierBatch.dstStages            = GPU_STAGE_FRAGMENT;
  barrierBatch.aliasingBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "placed alias submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;
  ok   = 1;

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(invalidBuffer);
  GPUDestroyBuffer(aliasBuffer);
  GPUDestroyBuffer(buffer);
  GPUDestroyHeap(textureHeap);
  GPUDestroyHeap(heap);
  GPUDestroyDevice(device);
  GPUDestroyDevice(disabledDevice);
  return ok;
}
