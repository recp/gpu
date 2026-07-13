#include "../../src/backend/dx12/common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  DX12_TRANSFER_TEST_BYTES = 64u * 1024u
};

typedef struct CompletionProbe {
  GPUCommandBuffer *cmdb;
  uint32_t          count;
} CompletionProbe;

static void
on_complete(void             * __restrict sender,
            GPUCommandBuffer * __restrict cmdb) {
  CompletionProbe *probe;

  probe = sender;
  if (!probe) {
    return;
  }

  probe->cmdb = cmdb;
  probe->count++;
}

static GPUAdapter*
first_adapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    count;
  GPUResult   result;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      count == 0u) {
    return NULL;
  }
  return adapter;
}

static GPUCommandBuffer*
submit_empty(GPUQueue *queue,
             GPUFence        *fence,
             CompletionProbe *probe) {
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "dx12-empty", &cmdb) != GPU_OK || !cmdb) {
    return NULL;
  }

  GPUSetCommandBufferCompletionHandler(cmdb, probe, on_complete);
  buffers[0] = cmdb;
  memset(&submitInfo, 0, sizeof(submitInfo));
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      !GPUIsFenceSignaled(fence)) {
    return NULL;
  }
  return cmdb;
}

static bool
wait_queue(GPUQueue *queue, GPUFence *fence) {
  CompletionProbe probe;

  memset(&probe, 0, sizeof(probe));
  return submit_empty(queue, fence, &probe) != NULL && probe.count == 1u;
}

static bool
submit_error_propagates(GPUQueue *queue) {
  GPUCommandBuffer     *cmdb;
  GPUCommandBuffer     *buffers[1];
  GPUCommandBufferDX12 *native;
  GPUQueueSubmitInfo    submitInfo;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "dx12-submit-error", &cmdb) != GPU_OK ||
      !cmdb || !(native = cmdb->_priv) || !native->commandList) {
    return false;
  }
  if (FAILED(native->commandList->lpVtbl->Close(native->commandList))) {
    return false;
  }

  buffers[0] = cmdb;
  memset(&submitInfo, 0, sizeof(submitInfo));
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  return GPUQueueSubmit(queue, &submitInfo) == GPU_ERROR_BACKEND_FAILURE;
}

static bool
buffer_transfers_reuse(GPUQueue *queue,
                       GPUDevice       *device,
                       GPUFence        *queueFence) {
  GPUQueueDX12         *native;
  GPUBufferCreateInfo   bufferInfo;
  GPUTransferSlotDX12   slots[GPU_DX12_TRANSFER_SLOT_COUNT];
  GPUBuffer            *buffer;
  ID3D12Fence          *fence;
  ID3D12Resource       *readback;
  HANDLE                event;
  uint64_t              readbackCapacity;
  static uint8_t        upload[DX12_TRANSFER_TEST_BYTES];
  uint32_t              value;
  uint32_t              copied;
  bool                  ok;

  native = queue ? queue->_priv : NULL;
  buffer = NULL;
  if (!native || !device) {
    return false;
  }

  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "dx12-sync-transfer";
  bufferInfo.sizeBytes        = sizeof(upload);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer) {
    return false;
  }

  ok = true;
  for (uint32_t i = 0u; ok && i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    value = UINT32_C(0x12340000) + i;
    memcpy(upload, &value, sizeof(value));
    ok = GPUQueueWriteBuffer(queue,
                             buffer,
                             0u,
                             upload,
                             sizeof(upload)) == GPU_OK;
  }
  copied = 0u;
  ok = ok && GPUQueueReadBuffer(queue,
                                buffer,
                                0u,
                                &copied,
                                sizeof(copied)) == GPU_OK &&
       copied == value && !native->transferOpen;
  if (!ok || !native->transferFence || !native->transferEvent ||
      !native->readbackStaging) {
    (void)wait_queue(queue, queueFence);
    GPUDestroyBuffer(buffer);
    return false;
  }

  for (uint32_t i = 0u; i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    slots[i] = native->transferSlots[i];
    if (!slots[i].allocator || !slots[i].commandList ||
        !slots[i].uploadStaging || !slots[i].uploadMapped ||
        slots[i].uploadCapacity < DX12_TRANSFER_TEST_BYTES) {
      (void)wait_queue(queue, queueFence);
      GPUDestroyBuffer(buffer);
      return false;
    }
  }

  fence            = native->transferFence;
  event            = native->transferEvent;
  readback         = native->readbackStaging;
  readbackCapacity = native->readbackCapacity;
  for (uint32_t i = 0u; i < 16u; i++) {
    value  = UINT32_C(0xabc00000) + i;
    copied = 0u;
    if (GPUQueueWriteBuffer(queue,
                            buffer,
                            0u,
                            &value,
                            sizeof(value)) != GPU_OK ||
        GPUQueueReadBuffer(queue,
                           buffer,
                           0u,
                           &copied,
                           sizeof(copied)) != GPU_OK ||
        copied != value ||
        native->transferFence != fence ||
        native->transferEvent != event ||
        native->readbackStaging != readback ||
        native->readbackCapacity != readbackCapacity ||
        native->transferOpen) {
      ok = false;
      break;
    }
  }

  for (uint32_t i = 0u; ok && i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    GPUTransferSlotDX12 *slot;

    slot = &native->transferSlots[i];
    ok = slot->allocator == slots[i].allocator &&
         slot->commandList == slots[i].commandList &&
         slot->uploadStaging == slots[i].uploadStaging &&
         slot->uploadMapped == slots[i].uploadMapped &&
         slot->uploadCapacity == slots[i].uploadCapacity;
  }
  ok = wait_queue(queue, queueFence) && ok;
  GPUDestroyBuffer(buffer);
  return ok;
}

static bool
texture_transfers_reuse(GPUQueue *queue,
                        GPUDevice       *device,
                        GPUFence        *queueFence) {
  GPUQueueDX12         *native;
  GPUTextureCreateInfo  textureInfo;
  GPUTextureWriteRegion writeRegion;
  GPUTransferSlotDX12   slots[GPU_DX12_TRANSFER_SLOT_COUNT];
  GPUTexture           *texture;
  ID3D12Fence          *fence;
  HANDLE                event;
  static uint8_t        pixels[DX12_TRANSFER_TEST_BYTES];
  bool                  ok;

  native  = queue ? queue->_priv : NULL;
  texture = NULL;
  if (!native || !device) {
    return false;
  }

  memset(&textureInfo, 0, sizeof(textureInfo));
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "dx12-sync-texture-transfer";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 128u;
  textureInfo.height           = 128u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK ||
      !texture) {
    return false;
  }

  memset(&writeRegion, 0, sizeof(writeRegion));
  writeRegion.width        = 128u;
  writeRegion.height       = 128u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 128u * 4u;
  writeRegion.rowsPerImage = 128u;
  memset(pixels, 0, sizeof(pixels));

  ok = true;
  for (uint32_t i = 0u; ok && i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    pixels[0] = (uint8_t)i;
    pixels[1] = (uint8_t)(i + 1u);
    pixels[2] = (uint8_t)(i + 2u);
    pixels[3] = 255u;
    if (GPUQueueWriteTexture(queue,
                             texture,
                             &writeRegion,
                             pixels,
                             sizeof(pixels)) != GPU_OK) {
      ok = false;
      break;
    }
  }
  ok = ok && wait_queue(queue, queueFence);
  for (uint32_t i = 0u; ok && i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    slots[i] = native->transferSlots[i];
    ok = slots[i].allocator && slots[i].commandList &&
         slots[i].uploadStaging && slots[i].uploadMapped &&
         slots[i].uploadCapacity >= sizeof(pixels);
  }
  fence = native->transferFence;
  event = native->transferEvent;
  for (uint32_t i = 0u; ok && i < 16u; i++) {
    pixels[0] = (uint8_t)i;
    pixels[1] = (uint8_t)(i + 1u);
    pixels[2] = (uint8_t)(i + 2u);
    pixels[3] = 255u;
    if (GPUQueueWriteTexture(queue,
                             texture,
                             &writeRegion,
                             pixels,
                             sizeof(pixels)) != GPU_OK ||
        native->transferFence != fence ||
        native->transferEvent != event ||
        !native->transferOpen) {
      ok = false;
      break;
    }
  }

  for (uint32_t i = 0u; ok && i < GPU_DX12_TRANSFER_SLOT_COUNT; i++) {
    GPUTransferSlotDX12 *slot;

    slot = &native->transferSlots[i];
    ok = slot->allocator == slots[i].allocator &&
         slot->commandList == slots[i].commandList &&
         slot->uploadStaging == slots[i].uploadStaging &&
         slot->uploadMapped == slots[i].uploadMapped &&
         slot->uploadCapacity == slots[i].uploadCapacity;
  }
  ok = wait_queue(queue, queueFence) && ok;
  GPUDestroyTexture(texture);
  return ok;
}

int
main(void) {
  GPUInstanceCreateInfo instanceInfo;
  GPUDeviceCreateInfo   deviceInfo;
  GPUFenceCreateInfo    fenceInfo;
  GPUQueueRequest       queueRequest;
  GPUFeature            requiredFeature;
  GPUCommandBuffer     *firstCmdb;
  GPUCommandBuffer     *secondCmdb;
  GPUQueue             *queue0;
  GPUQueue             *queue1;
  CompletionProbe       probe;
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUFence             *fence;

  memset(&instanceInfo, 0, sizeof(instanceInfo));
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;

  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "DX12 instance creation failed\n");
    return 1;
  }

  adapter = first_adapter(instance);
  if (!adapter) {
    fprintf(stderr, "DX12 adapter enumeration failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "DX12 feature reporting failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  memset(&queueRequest, 0, sizeof(queueRequest));
  queueRequest.type  = GPU_QUEUE_GRAPHICS;
  queueRequest.count = 2u;

  memset(&deviceInfo, 0, sizeof(deviceInfo));
  deviceInfo.chain.sType             = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize        = sizeof(deviceInfo);
  requiredFeature                    = GPU_FEATURE_MULTI_DRAW;
  deviceInfo.required.featureCount   = 1u;
  deviceInfo.required.pFeatures      = &requiredFeature;
  deviceInfo.queues.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  deviceInfo.queues.chain.structSize = sizeof(deviceInfo.queues);
  deviceInfo.queues.requestCount     = 1u;
  deviceInfo.queues.pRequests        = &queueRequest;

  device = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "DX12 device creation failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "DX12 enabled wrong feature set\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  queue0 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  queue1 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 1u);
  if (!queue0 || !queue1 || queue0 == queue1 ||
      GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 2u) ||
      GPUGetAvailableQueueBits(device) != GPU_QUEUE_GRAPHICS) {
    fprintf(stderr, "DX12 indexed queue lookup failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  memset(&fenceInfo, 0, sizeof(fenceInfo));
  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 fence creation failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!buffer_transfers_reuse(queue0, device, fence)) {
    fprintf(stderr, "DX12 buffer transfer reuse failed\n");
    goto fail;
  }
  if (!texture_transfers_reuse(queue0, device, fence)) {
    fprintf(stderr, "DX12 texture transfer reuse failed\n");
    goto fail;
  }

  memset(&probe, 0, sizeof(probe));
  firstCmdb = submit_empty(queue0, fence, &probe);
  if (!firstCmdb || probe.count != 1u || probe.cmdb != firstCmdb) {
    fprintf(stderr, "DX12 first submit failed\n");
    goto fail;
  }

  secondCmdb = submit_empty(queue0, fence, &probe);
  if (!secondCmdb || secondCmdb != firstCmdb ||
      probe.count != 2u || probe.cmdb != secondCmdb) {
    fprintf(stderr, "DX12 command-buffer reuse failed\n");
    goto fail;
  }

  if (!submit_empty(queue1, fence, &probe) || probe.count != 3u) {
    fprintf(stderr, "DX12 second queue submit failed\n");
    goto fail;
  }
  if (!submit_error_propagates(queue0)) {
    fprintf(stderr, "DX12 submit error was not propagated\n");
    goto fail;
  }
  if (!submit_empty(queue0, fence, &probe) || probe.count != 4u) {
    fprintf(stderr, "DX12 queue did not recover after submit error\n");
    goto fail;
  }

  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  printf("DX12 queue validation passed\n");
  return 0;

fail:
  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return 1;
}
