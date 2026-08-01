#include "test.h"

enum {
  GPU_TEST_SHARED_TEXTURE_WIDTH  = 4u,
  GPU_TEST_SHARED_TEXTURE_HEIGHT = 4u,
  GPU_TEST_SHARED_TEXTURE_BYTES  = 4u * 4u * 4u
};

static int
gpu_test_shared_transfer(GPUDeviceInteropEXT *interop,
                         GPUDevice           *firstDevice,
                         GPUDevice           *secondDevice,
                         GPUBuffer           *firstBuffer,
                         GPUBuffer           *secondBuffer,
                         GPUTexture          *firstTexture,
                         GPUTexture          *secondTexture) {
  GPUCommandBuffer          *firstCmdb, *secondCmdb;
  GPUSemaphore              *firstSemaphore, *secondSemaphore;
  GPUTransferPassEncoder    *copyPass;
  GPUQueue                  *firstQueue, *secondQueue;
  GPUBuffer                 *imposter, *readback;
  GPUFence                  *firstFence, *secondFence;
  GPUSharedBufferBarrierEXT  sharedBuffer = {0};
  GPUSharedTextureBarrierEXT sharedTexture = {0};
  GPUSharedBarrierBatchEXT   sharedBarriers = {0};
  GPUBufferCreateInfo        imposterInfo = {0}, readbackInfo = {0};
  GPUTextureWriteRegion      writeRegion = {0};
  GPUBufferTextureCopyRegion copyRegion = {0};
  GPUQueueSemaphoreWait      wait = {0};
  GPUQueueSemaphoreSignal    signal = {0};
  GPUQueueSubmitExInfo       submit = {0};
  GPUSemaphoreCreateInfo     semaphoreInfo = {0};
  uint32_t                   sourceTexture[16], textureResult[16];
  int                        firstSubmitted, secondSubmitted, ok;

  firstQueue      = GPUGetQueue(firstDevice, GPU_QUEUE_GRAPHICS, 0u);
  secondQueue     = GPUGetQueue(secondDevice, GPU_QUEUE_GRAPHICS, 0u);
  firstCmdb       = NULL;
  secondCmdb      = NULL;
  firstSemaphore  = NULL;
  secondSemaphore = NULL;
  copyPass        = NULL;
  imposter        = NULL;
  readback        = NULL;
  firstFence      = NULL;
  secondFence     = NULL;
  firstSubmitted  = 0;
  secondSubmitted = 0;
  ok              = 0;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(sourceTexture); i++) {
    sourceTexture[i] = UINT32_C(0xff000000) |
                       ((i * 37u) & 0xffu) |
                       (((i * 59u) & 0xffu) << 8u) |
                       (((i * 83u) & 0xffu) << 16u);
  }
  memset(textureResult, 0, sizeof(textureResult));

  imposterInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  imposterInfo.chain.structSize = sizeof(imposterInfo);
  imposterInfo.label            = "interop-imposter";
  imposterInfo.sizeBytes        = sizeof(uint32_t) * 4u;
  imposterInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                  GPU_BUFFER_USAGE_COPY_SRC;
  readbackInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  readbackInfo.chain.structSize = sizeof(readbackInfo);
  readbackInfo.label            = "interop-texture-readback";
  readbackInfo.sizeBytes        = sizeof(textureResult);
  readbackInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                  GPU_BUFFER_USAGE_COPY_SRC;
  writeRegion.width        = GPU_TEST_SHARED_TEXTURE_WIDTH;
  writeRegion.height       = GPU_TEST_SHARED_TEXTURE_HEIGHT;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = GPU_TEST_SHARED_TEXTURE_WIDTH * 4u;
  writeRegion.rowsPerImage = GPU_TEST_SHARED_TEXTURE_HEIGHT;
  if (!firstQueue || !secondQueue ||
      GPUCreateBuffer(secondDevice, &imposterInfo, &imposter) != GPU_OK ||
      !imposter ||
      GPUCreateBuffer(secondDevice, &readbackInfo, &readback) != GPU_OK ||
      !readback ||
      GPUQueueWriteTexture(firstQueue,
                           firstTexture,
                           &writeRegion,
                           sourceTexture,
                           sizeof(sourceTexture)) != GPU_OK ||
      GPUAcquireCommandBuffer(firstQueue, "interop-signal", &firstCmdb) !=
        GPU_OK ||
      GPUAcquireCommandBuffer(secondQueue, "interop-wait", &secondCmdb) !=
        GPU_OK ||
      GPUCreateFence(firstDevice, NULL, &firstFence) != GPU_OK ||
      GPUCreateFence(secondDevice, NULL, &secondFence) != GPU_OK ||
      !firstFence || !secondFence) {
    goto cleanup;
  }

  semaphoreInfo.chain.sType      = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label            = "device-interop";
  if (GPUCreateSharedSemaphoreEXT(interop,
                                  &semaphoreInfo,
                                  &firstSemaphore,
                                  &secondSemaphore) != GPU_OK ||
      !firstSemaphore || !secondSemaphore) {
    goto cleanup;
  }

  sharedBuffer.sourceBuffer      = firstBuffer;
  sharedBuffer.destinationBuffer = secondBuffer;
  sharedBuffer.offset            = 0u;
  sharedBuffer.sizeBytes         = sizeof(uint32_t) * 4u;
  sharedBuffer.srcAccess         = GPU_ACCESS_TRANSFER_WRITE;
  sharedBuffer.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  sharedTexture.sourceTexture      = firstTexture;
  sharedTexture.destinationTexture = secondTexture;
  sharedTexture.srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  sharedTexture.dstAccess          = GPU_ACCESS_TRANSFER_READ;
  sharedTexture.mipCount           = 1u;
  sharedTexture.layerCount         = 1u;
  sharedBarriers.pBufferBarriers    = &sharedBuffer;
  sharedBarriers.pTextureBarriers   = &sharedTexture;
  sharedBarriers.srcStages          = GPU_STAGE_TRANSFER;
  sharedBarriers.dstStages          = GPU_STAGE_TRANSFER;
  sharedBarriers.bufferBarrierCount = 1u;
  sharedBarriers.textureBarrierCount = 1u;
  sharedBuffer.destinationBuffer = imposter;
  if (GPUEncodeSharedReleaseEXT(interop,
                                firstCmdb,
                                &sharedBarriers) != GPU_ERROR_INVALID_ARGUMENT) {
    goto cleanup;
  }
  sharedBuffer.destinationBuffer = secondBuffer;
  if (GPUEncodeSharedReleaseEXT(interop,
                                firstCmdb,
                                &sharedBarriers) != GPU_OK ||
      GPUEncodeSharedAcquireEXT(interop,
                                secondCmdb,
                                &sharedBarriers) != GPU_OK) {
    goto cleanup;
  }

  copyPass = GPUBeginTransferPass(secondCmdb, "interop-texture-readback");
  if (!copyPass) {
    goto cleanup;
  }
  copyRegion.bytesPerRow        = GPU_TEST_SHARED_TEXTURE_WIDTH * 4u;
  copyRegion.rowsPerImage       = GPU_TEST_SHARED_TEXTURE_HEIGHT;
  copyRegion.texture.width      = GPU_TEST_SHARED_TEXTURE_WIDTH;
  copyRegion.texture.height     = GPU_TEST_SHARED_TEXTURE_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         secondTexture,
                         readback,
                         &copyRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  signal.semaphore          = firstSemaphore;
  signal.value              = 1u;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &firstCmdb;
  submit.pSignals           = &signal;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  submit.fence              = firstFence;
  firstSubmitted = GPUQueueSubmitEx(firstQueue, &submit) == GPU_OK;

  wait.semaphore          = secondSemaphore;
  wait.value              = 1u;
  wait.waitStages         = GPU_STAGE_TOP;
  submit.ppCommandBuffers = &secondCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = NULL;
  submit.fence            = secondFence;
  submit.waitCount        = 1u;
  submit.signalCount      = 0u;
  secondSubmitted = firstSubmitted &&
                    GPUQueueSubmitEx(secondQueue, &submit) == GPU_OK;
  ok = secondSubmitted &&
       GPUWaitFence(secondFence, UINT64_MAX) == GPU_OK &&
       GPUQueueReadBuffer(secondQueue,
                          readback,
                          0u,
                          textureResult,
                          sizeof(textureResult)) == GPU_OK &&
       memcmp(sourceTexture, textureResult, sizeof(sourceTexture)) == 0;

cleanup:
  if (firstSubmitted && !secondSubmitted) {
    (void)GPUWaitFence(firstFence, UINT64_MAX);
  }
  if (!secondSubmitted) {
    GPUDiscardCommandBuffer(secondCmdb);
  }
  if (!firstSubmitted) {
    GPUDiscardCommandBuffer(firstCmdb);
  }

  GPUDestroySemaphore(secondSemaphore);
  GPUDestroySemaphore(firstSemaphore);
  GPUDestroyFence(secondFence);
  GPUDestroyFence(firstFence);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(imposter);
  return ok;
}

int
gpu_test_multigpu(GPUAdapter *adapter, GPUDevice *firstDevice) {
  static const char *const entryPoints[] = {
    "GPUCreateDeviceInteropEXT",
    "GPUDestroyDeviceInteropEXT",
    "GPUGetSharedBufferMemoryRequirementsEXT",
    "GPUCreateSharedBufferEXT",
    "GPUGetSharedTextureMemoryRequirementsEXT",
    "GPUCreateSharedTextureEXT",
    "GPUCreateSharedSemaphoreEXT",
    "GPUEncodeSharedReleaseEXT",
    "GPUEncodeSharedAcquireEXT"
  };
  GPUBufferCreateInfo  firstBufferInfo = {0}, secondBufferInfo = {0};
  GPUTextureCreateInfo firstTextureInfo = {0}, secondTextureInfo = {0};
  GPUMemoryRequirements requirements;
  GPUAdapterProperties  properties;
  GPUDeviceInteropEXT  *interop;
  GPUBuffer            *firstBuffer, *secondBuffer;
  GPUTexture           *firstTexture, *secondTexture;
  GPUDevice            *secondDevice;
  GPUQueue             *firstQueue, *secondQueue;
  uint32_t              source[4] = { 3u, 5u, 8u, 13u };
  uint32_t              result[4] = {0};
  GPUResult             interopResult;
  int                   ok;

  if (!adapter || !firstDevice ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    return 0;
  }

  interop = NULL;
  if (GPUCreateDeviceInteropEXT(firstDevice, firstDevice, &interop) !=
        GPU_ERROR_INVALID_ARGUMENT || interop) {
    return 0;
  }
  if (gpu_test_create_device(adapter, NULL, &secondDevice) != GPU_OK ||
      !secondDevice) {
    return 0;
  }

  interopResult = GPUCreateDeviceInteropEXT(firstDevice,
                                             secondDevice,
                                             &interop);
  if (interopResult != GPU_OK || !interop) {
    GPUDestroyDevice(secondDevice);
    return properties.backend != GPU_BACKEND_METAL &&
           properties.backend != GPU_BACKEND_DX12 &&
           interopResult == GPU_ERROR_UNSUPPORTED && !interop;
  }
  for (size_t i = 0u; i < GPU_ARRAY_LEN(entryPoints); i++) {
    if (!GPUGetProcAddr(firstDevice, entryPoints[i])) {
      GPUDestroyDeviceInteropEXT(interop);
      GPUDestroyDevice(secondDevice);
      return 0;
    }
  }

  firstBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  firstBufferInfo.chain.structSize = sizeof(firstBufferInfo);
  firstBufferInfo.label            = "interop-first-buffer";
  firstBufferInfo.sizeBytes        = sizeof(source);
  firstBufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                     GPU_BUFFER_USAGE_COPY_SRC;
  secondBufferInfo                 = firstBufferInfo;
  secondBufferInfo.label           = "interop-second-buffer";
  firstBuffer                      = NULL;
  secondBuffer                     = NULL;
  ok = GPUGetSharedBufferMemoryRequirementsEXT(interop,
                                                &firstBufferInfo,
                                                &secondBufferInfo,
                                                &requirements) == GPU_OK &&
       requirements.sizeBytes >= sizeof(source) &&
       GPUCreateSharedBufferEXT(interop,
                                &firstBufferInfo,
                                &secondBufferInfo,
                                &firstBuffer,
                                &secondBuffer) == GPU_OK &&
       firstBuffer && secondBuffer;
  firstQueue  = GPUGetQueue(firstDevice, GPU_QUEUE_GRAPHICS, 0u);
  secondQueue = GPUGetQueue(secondDevice, GPU_QUEUE_GRAPHICS, 0u);
  ok = ok && firstQueue && secondQueue &&
       GPUQueueWriteBuffer(firstQueue,
                           firstBuffer,
                           0u,
                           source,
                           sizeof(source)) == GPU_OK;

  firstTextureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  firstTextureInfo.chain.structSize = sizeof(firstTextureInfo);
  firstTextureInfo.label            = "interop-first-texture";
  firstTextureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  firstTextureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  firstTextureInfo.width            = 16u;
  firstTextureInfo.height           = 16u;
  firstTextureInfo.depthOrLayers    = 1u;
  firstTextureInfo.mipLevelCount    = 1u;
  firstTextureInfo.sampleCount      = 1u;
  firstTextureInfo.usage            = GPU_TEXTURE_USAGE_COPY_DST;
  secondTextureInfo                 = firstTextureInfo;
  secondTextureInfo.label           = "interop-second-texture";
  secondTextureInfo.usage           = GPU_TEXTURE_USAGE_SAMPLED |
                                      GPU_TEXTURE_USAGE_COPY_SRC;
  firstTexture                      = NULL;
  secondTexture                     = NULL;
  ok = ok &&
       GPUGetSharedTextureMemoryRequirementsEXT(interop,
                                                 &firstTextureInfo,
                                                 &secondTextureInfo,
                                                 &requirements) == GPU_OK &&
       requirements.sizeBytes >= 16u * 16u * 4u &&
       GPUCreateSharedTextureEXT(interop,
                                 &firstTextureInfo,
                                 &secondTextureInfo,
                                 &firstTexture,
                                 &secondTexture) == GPU_OK &&
       firstTexture && secondTexture &&
       gpu_test_shared_transfer(interop,
                                firstDevice,
                                secondDevice,
                                firstBuffer,
                                secondBuffer,
                                firstTexture,
                                secondTexture) &&
       GPUQueueReadBuffer(secondQueue,
                          secondBuffer,
                          0u,
                          result,
                          sizeof(result)) == GPU_OK &&
       memcmp(source, result, sizeof(source)) == 0;

  GPUDestroyTexture(secondTexture);
  GPUDestroyTexture(firstTexture);
  GPUDestroyBuffer(secondBuffer);
  GPUDestroyBuffer(firstBuffer);
  GPUDestroyDeviceInteropEXT(interop);
  GPUDestroyDevice(secondDevice);
  return ok;
}
