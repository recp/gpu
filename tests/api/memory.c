#include "test.h"

static int
gpu_test_placed_memory(GPUAdapter *adapter) {
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

static int
gpu_test_sparse_memory(GPUAdapter *adapter) {
  GPUDevice                    *device          = NULL;
  GPUDevice                    *disabledDevice  = NULL;
  GPUQueue                     *queue           = NULL;
  GPUHeap                      *heap            = NULL;
  GPUTexture                   *texture         = NULL;
  GPUBuffer                    *readback        = NULL;
  GPUFence                     *fence           = NULL;
  GPUSemaphore                 *semaphore       = NULL;
  GPUCommandBuffer             *cmdb            = NULL;
  GPUCopyPassEncoder           *copyPass        = NULL;
  uint8_t                      *pixels          = NULL;
  GPUCommandBuffer             *submitList[1]   = {0};
  GPUSparseTextureRequirements  requirements    = {0};
  GPUDeviceCreateInfo           deviceInfo      = {0};
  GPUTextureCreateInfo          textureInfo     = {0};
  GPUBufferCreateInfo           bufferInfo      = {0};
  GPUHeapCreateInfo             heapInfo        = {0};
  GPUSparseTextureMapping       mappings[3]     = {0};
  GPUBufferTextureCopyRegion    copyRegion      = {0};
  GPUQueueSemaphoreWait         wait            = {0};
  GPUQueueSemaphoreSignal       signal          = {0};
  GPUQueueSparseSubmitInfo      submitInfo      = {0};
  GPUQueueSubmitInfo            copySubmitInfo  = {0};
  GPUTextureWriteRegion         writeRegion     = {0};
  uint64_t                      heapTileCount;
  uint64_t                      heapSize;
  uint64_t                      pixelSize;
  uint32_t                      mappingCount;
  GPUFeature                    feature         = GPU_FEATURE_SPARSE_TEXTURES;
  GPUResult                     result;
  bool                          explicitPlacement;
  int                           ok              = 0;

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
    fprintf(stderr, "sparse disabled-device setup failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "api-sparse-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1024u;
  textureInfo.height           = 1024u;
  textureInfo.depthOrLayers    = 2u;
  textureInfo.mipLevelCount    = 11u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  result = GPUGetSparseTextureRequirements(disabledDevice,
                                            &textureInfo,
                                            &requirements);
  if (result != GPU_ERROR_UNSUPPORTED || requirements.pageSizeBytes != 0u) {
    fprintf(stderr, "sparse query accepted without feature enablement\n");
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature)) {
    fprintf(stderr, "sparse feature enablement failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "sparse queue unavailable\n");
    goto cleanup;
  }
  if (GPUGetSparseTextureRequirements(device,
                                      &textureInfo,
                                      &requirements) != GPU_OK) {
    fprintf(stderr, "sparse texture requirements query failed\n");
    goto cleanup;
  }

  mappingCount = 0u;
  heapTileCount = 0u;
  if (requirements.firstMipInTail > 0u) {
    GPUSparseTextureMapping *mapping;

    mapping = &mappings[mappingCount++];
    mapping->tileWidth =
      textureInfo.width / requirements.tileWidth +
      (textureInfo.width % requirements.tileWidth != 0u);
    mapping->tileHeight =
      textureInfo.height / requirements.tileHeight +
      (textureInfo.height % requirements.tileHeight != 0u);
    mapping->tileDepth  = 1u;
    mapping->arrayLayer = 1u;
    heapTileCount = (uint64_t)mapping->tileWidth *
                    mapping->tileHeight * mapping->tileDepth;
  }
  if (requirements.firstMipInTail < textureInfo.mipLevelCount) {
    uint32_t tailCount;

    tailCount = requirements.mipTailLayerStrideTiles != 0u
                  ? textureInfo.depthOrLayers
                  : 1u;
    if (tailCount > GPU_ARRAY_LEN(mappings) - mappingCount ||
        requirements.mipTailTileCount == 0u ||
        requirements.mipTailTileCount > UINT32_MAX) {
      fprintf(stderr, "sparse mip-tail layout is invalid\n");
      goto cleanup;
    }
    for (uint32_t i = 0u; i < tailCount; i++) {
      GPUSparseTextureMapping *mapping;

      mapping = &mappings[mappingCount++];
      mapping->tileWidth  = (uint32_t)requirements.mipTailTileCount;
      mapping->tileHeight = 1u;
      mapping->tileDepth  = 1u;
      mapping->mipLevel   = requirements.firstMipInTail;
      mapping->arrayLayer = i;
      if (heapTileCount > UINT64_MAX - requirements.mipTailTileCount) {
        fprintf(stderr, "sparse mip-tail size overflow\n");
        goto cleanup;
      }
      heapTileCount += requirements.mipTailTileCount;
    }
  }
  if (mappingCount == 0u || heapTileCount == 0u ||
      heapTileCount > UINT64_MAX / requirements.pageSizeBytes) {
    fprintf(stderr, "sparse mapping size is invalid\n");
    goto cleanup;
  }
  heapSize = heapTileCount * requirements.pageSizeBytes;

  heapInfo.chain.sType       = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
  heapInfo.chain.structSize  = sizeof(heapInfo);
  heapInfo.label             = "api-sparse-heap";
  heapInfo.sizeBytes         = heapSize;
  heapInfo.compatibilityMask = requirements.compatibilityMask;
  heapInfo.pageSizeBytes     = requirements.pageSizeBytes;
  heapInfo.usage             = GPU_HEAP_USAGE_SPARSE;
  if (GPUCreateHeap(device, &heapInfo, &heap) != GPU_OK || !heap ||
      GPUCreateSparseTexture(device,
                             &textureInfo,
                             heap,
                             &texture) != GPU_OK || !texture ||
      GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence ||
      GPUCreateSemaphore(device, NULL, &semaphore) != GPU_OK || !semaphore) {
    fprintf(stderr, "sparse resource setup failed\n");
    goto cleanup;
  }

  explicitPlacement      = GPUIsFeatureEnabled(
    device,
    GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT
  );
  heapTileCount = 0u;
  for (uint32_t i = 0u; i < mappingCount; i++) {
    uint64_t tileCount;

    tileCount = (uint64_t)mappings[i].tileWidth *
                mappings[i].tileHeight * mappings[i].tileDepth;
    mappings[i].texture        = texture;
    mappings[i].heap           = heap;
    mappings[i].heapTileOffset = explicitPlacement
                                   ? heapTileCount
                                   : GPU_SPARSE_HEAP_TILE_AUTO;
    mappings[i].mode           = GPU_SPARSE_MAPPING_MAP;
    heapTileCount += tileCount;
  }
  signal.semaphore       = semaphore;
  signal.value           = 1u;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SPARSE_SUBMIT_INFO;
  submitInfo.chain.structSize    = sizeof(submitInfo);
  submitInfo.pTextureMappings    = mappings;
  submitInfo.pSignals            = &signal;
  submitInfo.fence               = fence;
  submitInfo.textureMappingCount = mappingCount;
  submitInfo.signalCount         = 1u;
  mappings[0].heapTileOffset = explicitPlacement
                                 ? GPU_SPARSE_HEAP_TILE_AUTO
                                 : 0u;
  if (GPUQueueSubmitSparse(queue, &submitInfo) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "sparse placement mode validation failed\n");
    goto cleanup;
  }
  mappings[0].heapTileOffset = explicitPlacement
                                 ? 0u
                                 : GPU_SPARSE_HEAP_TILE_AUTO;
  if (GPUQueueSubmitSparse(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "sparse map submit failed\n");
    goto cleanup;
  }

  pixelSize = (uint64_t)textureInfo.width * textureInfo.height * 4u;
  pixels    = malloc((size_t)pixelSize);
  if (!pixels) {
    goto cleanup;
  }
  for (uint64_t i = 0u; i < pixelSize; i++) {
    pixels[i] = (uint8_t)(i * 17u + 3u);
  }
  writeRegion.aspect         = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width          = textureInfo.width;
  writeRegion.height         = textureInfo.height;
  writeRegion.depth          = 1u;
  writeRegion.baseArrayLayer = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = textureInfo.width * 4u;
  writeRegion.rowsPerImage   = textureInfo.height;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           pixels,
                           pixelSize) != GPU_OK) {
    fprintf(stderr, "sparse texture upload failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-sparse-readback";
  bufferInfo.sizeBytes        = pixelSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK ||
      !readback ||
      GPUAcquireCommandBuffer(queue, "api-sparse-readback", &cmdb) != GPU_OK ||
      !cmdb || !(copyPass = GPUBeginCopyPass(cmdb, "api-sparse-readback"))) {
    fprintf(stderr, "sparse texture readback setup failed\n");
    goto cleanup;
  }
  copyRegion.texture.texture.baseArrayLayer = 1u;
  copyRegion.texture.width                  = textureInfo.width;
  copyRegion.texture.height                 = textureInfo.height;
  copyRegion.texture.depth                  = 1u;
  copyRegion.texture.layerCount             = 1u;
  copyRegion.bytesPerRow                    = textureInfo.width * 4u;
  copyRegion.rowsPerImage                   = textureInfo.height;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  GPUResetFence(fence);
  submitList[0]                      = cmdb;
  copySubmitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  copySubmitInfo.chain.structSize   = sizeof(copySubmitInfo);
  copySubmitInfo.ppCommandBuffers   = submitList;
  copySubmitInfo.fence              = fence;
  copySubmitInfo.commandBufferCount = 1u;
  if (GPUQueueSubmit(queue, &copySubmitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "sparse texture readback submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUQueueReadBuffer(queue, readback, 0u, pixels, pixelSize) != GPU_OK) {
    fprintf(stderr, "sparse texture readback failed\n");
    goto cleanup;
  }
  for (uint64_t i = 0u; i < pixelSize; i++) {
    if (pixels[i] != (uint8_t)(i * 17u + 3u)) {
      fprintf(stderr, "sparse texture readback mismatch\n");
      goto cleanup;
    }
  }

  GPUResetFence(fence);
  for (uint32_t i = 0u; i < mappingCount; i++) {
    mappings[i].mode = GPU_SPARSE_MAPPING_UNMAP;
  }
  wait.semaphore         = semaphore;
  wait.value             = 1u;
  wait.waitStages        = GPU_STAGE_TRANSFER;
  submitInfo.pWaits      = &wait;
  submitInfo.pSignals    = NULL;
  submitInfo.waitCount   = 1u;
  submitInfo.signalCount = 0u;
  if (GPUQueueSubmitSparse(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "sparse unmap submit failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  free(pixels);
  GPUDestroySemaphore(semaphore);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(texture);
  GPUDestroyHeap(heap);
  GPUDestroyDevice(device);
  GPUDestroyDevice(disabledDevice);
  return ok;
}

static int
gpu_test_sparse_buffer_memory(GPUAdapter *adapter) {
  GPUDevice                    *device         = NULL;
  GPUDevice                    *disabledDevice = NULL;
  GPUQueue                     *queue          = NULL;
  GPUHeap                      *heap           = NULL;
  GPUBuffer                    *buffer         = NULL;
  GPUFence                     *fence          = NULL;
  uint8_t                      *input          = NULL;
  uint8_t                      *output         = NULL;
  GPUSparseBufferRequirements  requirements   = {0};
  GPUDeviceCreateInfo          deviceInfo     = {0};
  GPUBufferCreateInfo          bufferInfo     = {0};
  GPUHeapCreateInfo            heapInfo       = {0};
  GPUSparseBufferMapping       mapping        = {0};
  GPUQueueSparseSubmitInfo     submitInfo     = {0};
  GPUFeature                   feature        = GPU_FEATURE_SPARSE_BUFFERS;
  GPUResult                    result;
  uint64_t                     heapSize;
  int                          ok             = 0;

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
    fprintf(stderr, "sparse buffer disabled-device setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-sparse-buffer";
  bufferInfo.sizeBytes        = 257u * 1024u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  result = GPUGetSparseBufferRequirements(disabledDevice,
                                           &bufferInfo,
                                           &requirements);
  if (result != GPU_ERROR_UNSUPPORTED || requirements.tileCount != 0u) {
    fprintf(stderr, "sparse buffer query accepted without feature\n");
    goto cleanup;
  }
  GPUDestroyDevice(disabledDevice);
  disabledDevice = NULL;

  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUIsFeatureEnabled(device,
                           GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)) {
    fprintf(stderr, "sparse buffer feature enablement failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUGetSparseBufferRequirements(device,
                                     &bufferInfo,
                                     &requirements) != GPU_OK ||
      requirements.pageSizeBytes == 0u || requirements.tileCount == 0u ||
      requirements.tileCount >
        UINT64_MAX / requirements.pageSizeBytes) {
    fprintf(stderr, "sparse buffer requirements query failed\n");
    goto cleanup;
  }

  heapSize = requirements.tileCount * requirements.pageSizeBytes;
  heapInfo.chain.sType       = GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO;
  heapInfo.chain.structSize  = sizeof(heapInfo);
  heapInfo.label             = "api-sparse-buffer-heap";
  heapInfo.sizeBytes         = heapSize;
  heapInfo.compatibilityMask = requirements.compatibilityMask;
  heapInfo.pageSizeBytes     = requirements.pageSizeBytes;
  heapInfo.usage             = GPU_HEAP_USAGE_SPARSE;
  if (GPUCreateHeap(device, &heapInfo, &heap) != GPU_OK || !heap ||
      GPUCreateSparseBuffer(device,
                            &bufferInfo,
                            heap,
                            &buffer) != GPU_OK || !buffer ||
      GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "sparse buffer resource setup failed\n");
    goto cleanup;
  }

  mapping.buffer                = buffer;
  mapping.heap                  = heap;
  mapping.heapTileOffset        = GPU_SPARSE_HEAP_TILE_AUTO;
  mapping.tileCount             = requirements.tileCount;
  mapping.mode                  = GPU_SPARSE_MAPPING_MAP;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SPARSE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.pBufferMappings    = &mapping;
  submitInfo.fence              = fence;
  submitInfo.bufferMappingCount = 1u;
  if (GPUQueueSubmitSparse(queue, &submitInfo) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "sparse buffer AUTO placement was accepted\n");
    goto cleanup;
  }
  mapping.heapTileOffset   = 0u;
  mapping.bufferTileOffset = requirements.tileCount;
  mapping.tileCount        = 1u;
  if (GPUQueueSubmitSparse(queue, &submitInfo) !=
      GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "sparse buffer range overflow was accepted\n");
    goto cleanup;
  }
  mapping.bufferTileOffset = 0u;
  mapping.tileCount        = requirements.tileCount;
  if (GPUQueueSubmitSparse(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "sparse buffer map submit failed\n");
    goto cleanup;
  }

  input  = malloc((size_t)bufferInfo.sizeBytes);
  output = malloc((size_t)bufferInfo.sizeBytes);
  if (!input || !output) {
    goto cleanup;
  }
  for (uint64_t i = 0u; i < bufferInfo.sizeBytes; i++) {
    input[i] = (uint8_t)(i * 29u + 7u);
  }
  if (GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          input,
                          bufferInfo.sizeBytes) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         output,
                         bufferInfo.sizeBytes) != GPU_OK ||
      memcmp(input, output, (size_t)bufferInfo.sizeBytes) != 0) {
    fprintf(stderr, "sparse buffer upload/readback failed\n");
    goto cleanup;
  }

  GPUResetFence(fence);
  mapping.mode = GPU_SPARSE_MAPPING_UNMAP;
  if (GPUQueueSubmitSparse(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "sparse buffer unmap submit failed\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  free(output);
  free(input);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  GPUDestroyHeap(heap);
  GPUDestroyDevice(device);
  GPUDestroyDevice(disabledDevice);
  return ok;
}

int
gpu_test_memory(GPUAdapter *adapter) {
  return gpu_test_placed_memory(adapter) &&
         gpu_test_sparse_memory(adapter) &&
         gpu_test_sparse_buffer_memory(adapter);
}
