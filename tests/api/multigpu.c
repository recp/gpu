#include "test.h"

static int
gpu_test_shared_semaphore(GPUDeviceInteropEXT *interop,
                          GPUDevice           *firstDevice,
                          GPUDevice           *secondDevice) {
  GPUQueueSemaphoreWait   wait = {0};
  GPUQueueSemaphoreSignal signal = {0};
  GPUQueueSubmitExInfo    submit = {0};
  GPUSemaphoreCreateInfo  semaphoreInfo = {0};
  GPUCommandBuffer       *firstCmdb, *secondCmdb;
  GPUSemaphore           *firstSemaphore, *secondSemaphore;
  GPUQueue               *firstQueue, *secondQueue;
  GPUFence               *fence;
  int                     firstSubmitted, secondSubmitted, ok;

  firstQueue      = GPUGetQueue(firstDevice, GPU_QUEUE_GRAPHICS, 0u);
  secondQueue     = GPUGetQueue(secondDevice, GPU_QUEUE_GRAPHICS, 0u);
  firstCmdb       = NULL;
  secondCmdb      = NULL;
  firstSemaphore  = NULL;
  secondSemaphore = NULL;
  fence           = NULL;
  if (!firstQueue || !secondQueue ||
      GPUAcquireCommandBuffer(firstQueue, "interop-signal", &firstCmdb) !=
        GPU_OK ||
      GPUAcquireCommandBuffer(secondQueue, "interop-wait", &secondCmdb) !=
        GPU_OK ||
      GPUCreateFence(secondDevice, NULL, &fence) != GPU_OK ||
      !fence) {
    GPUDiscardCommandBuffer(secondCmdb);
    GPUDiscardCommandBuffer(firstCmdb);
    GPUDestroyFence(fence);
    return 0;
  }

  semaphoreInfo.chain.sType      = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label            = "device-interop";
  if (GPUCreateSharedSemaphoreEXT(interop,
                                  &semaphoreInfo,
                                  &firstSemaphore,
                                  &secondSemaphore) != GPU_OK ||
      !firstSemaphore || !secondSemaphore) {
    GPUDiscardCommandBuffer(secondCmdb);
    GPUDiscardCommandBuffer(firstCmdb);
    GPUDestroyFence(fence);
    return 0;
  }

  signal.semaphore          = firstSemaphore;
  signal.value              = 1u;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &firstCmdb;
  submit.pSignals           = &signal;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  firstSubmitted = GPUQueueSubmitEx(firstQueue, &submit) == GPU_OK;

  wait.semaphore          = secondSemaphore;
  wait.value              = 1u;
  wait.waitStages         = GPU_STAGE_TOP;
  submit.ppCommandBuffers = &secondCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = NULL;
  submit.fence            = fence;
  submit.waitCount        = 1u;
  submit.signalCount      = 0u;
  secondSubmitted = firstSubmitted &&
                    GPUQueueSubmitEx(secondQueue, &submit) == GPU_OK;
  ok = secondSubmitted && GPUWaitFence(fence, UINT64_MAX) == GPU_OK;

  if (!secondSubmitted) {
    GPUDiscardCommandBuffer(secondCmdb);
  }
  if (!firstSubmitted) {
    GPUDiscardCommandBuffer(firstCmdb);
  }

  GPUDestroySemaphore(secondSemaphore);
  GPUDestroySemaphore(firstSemaphore);
  GPUDestroyFence(fence);
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
    "GPUCreateSharedSemaphoreEXT"
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
                           sizeof(source)) == GPU_OK &&
       GPUQueueReadBuffer(secondQueue,
                          secondBuffer,
                          0u,
                          result,
                          sizeof(result)) == GPU_OK &&
       memcmp(source, result, sizeof(source)) == 0;

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
  secondTextureInfo.usage           = GPU_TEXTURE_USAGE_SAMPLED;
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
       gpu_test_shared_semaphore(interop, firstDevice, secondDevice);

  GPUDestroyTexture(secondTexture);
  GPUDestroyTexture(firstTexture);
  GPUDestroyBuffer(secondBuffer);
  GPUDestroyBuffer(firstBuffer);
  GPUDestroyDeviceInteropEXT(interop);
  GPUDestroyDevice(secondDevice);
  return ok;
}
