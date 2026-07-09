#include "test.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/frame_internal.h"

static int
check_adapter_enumeration(void) {
  GPUAdapter *adapters[8] = {0};
  GPUAdapterCapabilities caps;
  GPUFormatCapabilities formatCaps;
  GPUNativeSurfaceCreateInfo nativeSurfaceInfo;
  GPUAdapterProperties props;
  GPUInstance *fakeInstance;
  GPUInstance *instance;
  GPUInstanceCreateInfo instanceInfo;
  GPUSurfaceCreateInfo surfaceInfo;
  GPUSurface *fakeSurface;
  GPUSurfaceCapabilities surfaceCaps;
  uint32_t adapterCount;
  bool hasBgra8;

  if (GPUCreateInstance(NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "instance create accepted null out pointer\n");
    return 0;
  }

  memset(&instanceInfo, 0, sizeof(instanceInfo));
  instanceInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  fakeInstance = (GPUInstance *)(uintptr_t)1u;
  if (GPUCreateInstance(&instanceInfo, &fakeInstance) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      fakeInstance != NULL) {
    fprintf(stderr, "instance create accepted wrong sType\n");
    return 0;
  }

  memset(&instanceInfo, 0, sizeof(instanceInfo));
  instanceInfo.chain.sType = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = (uint32_t)(sizeof(instanceInfo) - 1u);
  fakeInstance = (GPUInstance *)(uintptr_t)1u;
  if (GPUCreateInstance(&instanceInfo, &fakeInstance) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      fakeInstance != NULL) {
    fprintf(stderr, "instance create accepted short structSize\n");
    return 0;
  }

  memset(&instanceInfo, 0, sizeof(instanceInfo));
  instanceInfo.chain.sType = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = (GPUBackend)999u;
  fakeInstance = (GPUInstance *)(uintptr_t)1u;
  if (GPUCreateInstance(&instanceInfo, &fakeInstance) != GPU_ERROR_UNSUPPORTED ||
      fakeInstance != NULL) {
    fprintf(stderr, "instance create accepted unsupported backend\n");
    return 0;
  }

  instance = NULL;
  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "instance create failed\n");
    return 0;
  }
  GPUDestroyInstance(instance);

  if (GPUEnumerateAdapters(NULL, NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetAdapterProperties(NULL, &props) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetAdapterProperties((GPUAdapter *)(uintptr_t)1u, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetAdapterCapabilities(NULL, &caps) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetFormatCapabilities(NULL, GPU_FORMAT_RGBA8_UNORM, &formatCaps) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetSurfaceCapabilities(NULL, NULL, &surfaceCaps) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "adapter validation accepted invalid input\n");
    return 0;
  }

  adapterCount = 0;
  if (GPUEnumerateAdapters(NULL, &adapterCount, NULL) != GPU_OK ||
      adapterCount == 0) {
    fprintf(stderr, "adapter count query failed\n");
    return 0;
  }

  if (adapterCount > GPU_ARRAY_LEN(adapters)) {
    adapterCount = (uint32_t)GPU_ARRAY_LEN(adapters);
  }
  if (GPUEnumerateAdapters(NULL, &adapterCount, adapters) != GPU_OK ||
      adapterCount == 0 ||
      !adapters[0]) {
    fprintf(stderr, "adapter enumeration failed\n");
    return 0;
  }

  memset(&props, 0, sizeof(props));
  if (GPUGetAdapterProperties(adapters[0], &props) != GPU_OK ||
      props.backend == GPU_BACKEND_NULL ||
      !props.name) {
    fprintf(stderr, "adapter properties query failed\n");
    return 0;
  }
  memset(&caps, 0, sizeof(caps));
  if (GPUGetAdapterCapabilities(adapters[0], &caps) != GPU_OK ||
      !GPUIsFeatureSupported(adapters[0], GPU_FEATURE_COMPUTE) ||
      GPUIsFeatureSupported(adapters[0], GPU_FEATURE_SHADER_F16) ||
      caps.supported.featureCount == 0 ||
      !caps.supported.pFeatures ||
      caps.limits.maxBindGroups == 0 ||
      caps.limits.minUniformBufferOffsetAlignment == 0) {
    fprintf(stderr, "adapter capabilities query failed\n");
    return 0;
  }

  memset(&formatCaps, 0, sizeof(formatCaps));
  if (GPUGetFormatCapabilities(adapters[0],
                               GPU_FORMAT_RGBA8_UNORM,
                               &formatCaps) != GPU_OK ||
      !formatCaps.sampled ||
      !formatCaps.filterable ||
      !formatCaps.colorAttachment ||
      !formatCaps.blendable ||
      formatCaps.depthStencil) {
    fprintf(stderr, "RGBA8 format capabilities query failed\n");
    return 0;
  }
  memset(&formatCaps, 0, sizeof(formatCaps));
  if (GPUGetFormatCapabilities(adapters[0],
                               GPU_FORMAT_DEPTH32_FLOAT,
                               &formatCaps) != GPU_OK ||
      !formatCaps.depthStencil ||
      formatCaps.colorAttachment ||
      formatCaps.sampled) {
    fprintf(stderr, "depth format capabilities query failed\n");
    return 0;
  }
  fakeSurface = (GPUSurface *)(uintptr_t)1u;
  if (GPUGetSurfaceCapabilities(adapters[0], fakeSurface, NULL) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetSurfaceCapabilities(adapters[0], NULL, &surfaceCaps) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetSurfaceCapabilities(adapters[0], fakeSurface, &surfaceCaps) !=
        GPU_OK ||
      surfaceCaps.minImageCount == 0 ||
      surfaceCaps.maxImageCount < surfaceCaps.minImageCount ||
      surfaceCaps.formatCount == 0 ||
      !surfaceCaps.pFormats) {
    fprintf(stderr, "surface capabilities query failed\n");
    return 0;
  }
  hasBgra8 = false;
  for (uint32_t i = 0; i < surfaceCaps.formatCount; i++) {
    if (surfaceCaps.pFormats[i] == GPU_FORMAT_BGRA8_UNORM) {
      hasBgra8 = true;
    }
  }
  if (!hasBgra8) {
    fprintf(stderr, "surface capabilities missing BGRA8 format\n");
    return 0;
  }
  if (GPUCreateSurface(NULL, NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "surface create accepted null args\n");
    return 0;
  }

  memset(&surfaceInfo, 0, sizeof(surfaceInfo));
  surfaceInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      fakeSurface != NULL) {
    fprintf(stderr, "surface create accepted wrong sType\n");
    return 0;
  }

  memset(&surfaceInfo, 0, sizeof(surfaceInfo));
  surfaceInfo.chain.sType = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  surfaceInfo.chain.structSize = (uint32_t)(sizeof(surfaceInfo) - 1u);
  fakeSurface = (GPUSurface *)(uintptr_t)1u;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      fakeSurface != NULL) {
    fprintf(stderr, "surface create accepted short structSize\n");
    return 0;
  }

  memset(&surfaceInfo, 0, sizeof(surfaceInfo));
  surfaceInfo.chain.sType = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  surfaceInfo.chain.structSize = sizeof(surfaceInfo);
  fakeSurface = (GPUSurface *)(uintptr_t)1u;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      fakeSurface != NULL) {
    fprintf(stderr, "surface create accepted missing native info\n");
    return 0;
  }

  memset(&nativeSurfaceInfo, 0, sizeof(nativeSurfaceInfo));
  nativeSurfaceInfo.chain.sType = GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO;
  nativeSurfaceInfo.chain.structSize = sizeof(nativeSurfaceInfo);
  nativeSurfaceInfo.adapter = adapters[0];
  nativeSurfaceInfo.nativeHandle = (void *)(uintptr_t)1u;
  nativeSurfaceInfo.type = GPU_SURFACE_APPLE_NSVIEW;
  nativeSurfaceInfo.scale = 1.0f;
  surfaceInfo.chain.pNext = &nativeSurfaceInfo;

  nativeSurfaceInfo.adapter = NULL;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateSurfaceFromNative(NULL,
                                 NULL,
                                 (void *)(uintptr_t)1u,
                                 GPU_SURFACE_APPLE_NSVIEW,
                                 1.0f)) {
    fprintf(stderr, "surface create accepted null adapter\n");
    return 0;
  }
  nativeSurfaceInfo.adapter = adapters[0];

  nativeSurfaceInfo.nativeHandle = NULL;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateSurfaceFromNative(NULL,
                                 adapters[0],
                                 NULL,
                                 GPU_SURFACE_APPLE_NSVIEW,
                                 1.0f)) {
    fprintf(stderr, "surface create accepted null native handle\n");
    return 0;
  }
  nativeSurfaceInfo.nativeHandle = (void *)(uintptr_t)1u;

  nativeSurfaceInfo.type = (GPUSurfaceType)999u;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateSurfaceFromNative(NULL,
                                 adapters[0],
                                 (void *)(uintptr_t)1u,
                                 (GPUSurfaceType)999u,
                                 1.0f)) {
    fprintf(stderr, "surface create accepted invalid type\n");
    return 0;
  }
  nativeSurfaceInfo.type = GPU_SURFACE_APPLE_NSVIEW;

  nativeSurfaceInfo.scale = 0.0f;
  if (GPUCreateSurface(NULL, &surfaceInfo, &fakeSurface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUCreateSurfaceFromNative(NULL,
                                 adapters[0],
                                 (void *)(uintptr_t)1u,
                                 GPU_SURFACE_APPLE_NSVIEW,
                                 0.0f)) {
    fprintf(stderr, "surface create accepted invalid scale\n");
    return 0;
  }

  adapterCount = 0;
  if (GPUEnumerateAdapters(NULL, &adapterCount, adapters) !=
      GPU_ERROR_INSUFFICIENT_CAPACITY) {
    fprintf(stderr, "adapter enumeration accepted insufficient capacity\n");
    return 0;
  }

  return 1;
}

static int
check_fence_create_validation(GPUDevice *device) {
  GPUFenceCreateInfo fenceInfo = {0};
  GPUFence *fence;

  if (GPUWaitFence(NULL, 0) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUIsFenceSignaled(NULL)) {
    fprintf(stderr, "null fence query behaved unexpectedly\n");
    return 0;
  }
  GPUResetFence(NULL);
  GPUDestroyFence(NULL);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);

  fence = (GPUFence *)(uintptr_t)1u;
  if (GPUCreateFence(NULL, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted null device\n");
    return 0;
  }
  if (GPUCreateFence(device, &fenceInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "fence create accepted null output\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted wrong sType\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = (uint32_t)(sizeof(fenceInfo) - 1u);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted short structSize\n");
    return 0;
  }

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK ||
      !fence ||
      GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_ERROR_TIMEOUT) {
    fprintf(stderr, "fence create default state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK ||
      !fence ||
      !GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_OK) {
    fprintf(stderr, "fence create signaled state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  return 1;
}

static int
check_queue_selection(GPUDevice *device) {
  GPUDeviceCapabilities caps;
  GPUCommandQueue *graphics0;
  GPUCommandQueue *compute0;

  if (!device) {
    return 0;
  }

  if (GPUResizeSwapchain(NULL, 640, 480) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUResizeSwapchain((GPUSwapchain *)(uintptr_t)1u, 0, 480) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      GPUResizeSwapchain((GPUSwapchain *)(uintptr_t)1u, 640, 0) !=
        GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "swapchain resize accepted invalid input\n");
    return 0;
  }

  graphics0 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  compute0 = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0);
  if (!graphics0 || !compute0) {
    fprintf(stderr, "default device missing index-0 queues\n");
    return 0;
  }
  if (GPUGetCommandQueue(device, GPU_QUEUE_GRAPHICS) != graphics0) {
    fprintf(stderr, "GPUGetCommandQueue is not the index-0 graphics alias\n");
    return 0;
  }
  if (GPUGetQueue(device, GPU_QUEUE_GRAPHICS, UINT32_MAX) ||
      GPUGetQueue(device, GPU_QUEUE_COMPUTE, UINT32_MAX) ||
      GPUGetQueue(device, 0, 0)) {
    fprintf(stderr, "queue lookup accepted invalid request\n");
    return 0;
  }
  memset(&caps, 0, sizeof(caps));
  if (GPUGetDeviceCapabilities(NULL, &caps) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetDeviceCapabilities(device, NULL) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetDeviceCapabilities(device, &caps) != GPU_OK ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16) ||
      caps.enabled.featureCount == 0 ||
      !caps.enabled.pFeatures ||
      caps.limits.maxBindGroups == 0 ||
      caps.limits.maxComputeWorkgroupSizeX == 0) {
    fprintf(stderr, "device capabilities query failed\n");
    return 0;
  }

  return 1;
}

static int
check_device_queue_create_validation(GPUAdapter *adapter) {
  GPUDeviceCreateInfo createInfo = {0};
  GPUFeature requiredFeature = GPU_FEATURE_SHADER_F16;
  GPUQueueRequest request = {0};
  GPUDevice *device;
  int ok;

  if (GPUCreateDevice(NULL, NULL, &device) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "device create accepted null adapter\n");
    return 0;
  }
  if (GPUCreateDevice(adapter, NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "device create accepted null output\n");
    return 0;
  }

  device = (GPUDevice *)(uintptr_t)1u;
  createInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateDevice(adapter, &createInfo, &device) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      device != NULL) {
    fprintf(stderr, "device create accepted wrong sType\n");
    return 0;
  }

  device = (GPUDevice *)(uintptr_t)1u;
  createInfo.chain.sType = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.chain.structSize = (uint32_t)(sizeof(createInfo) - 1u);
  if (GPUCreateDevice(adapter, &createInfo, &device) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      device != NULL) {
    fprintf(stderr, "device create accepted short structSize\n");
    return 0;
  }

  memset(&createInfo, 0, sizeof(createInfo));
  createInfo.chain.sType = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.chain.structSize = sizeof(createInfo);
  createInfo.optional.featureCount = 1;
  device = (GPUDevice *)(uintptr_t)1u;
  if (GPUCreateDevice(adapter, &createInfo, &device) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      device != NULL) {
    fprintf(stderr, "device create accepted malformed optional feature set\n");
    return 0;
  }

  createInfo.optional.featureCount = 0;
  createInfo.required.featureCount = 1;
  createInfo.required.pFeatures = &requiredFeature;
  device = (GPUDevice *)(uintptr_t)1u;
  if (GPUCreateDevice(adapter, &createInfo, &device) != GPU_ERROR_UNSUPPORTED ||
      device != NULL) {
    fprintf(stderr, "device create accepted unsupported required feature\n");
    return 0;
  }

  requiredFeature = GPU_FEATURE_COMPUTE;
  device = NULL;
  if (GPUCreateDevice(adapter, &createInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "device create rejected required compute feature\n");
    return 0;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE)) {
    fprintf(stderr, "device create did not enable required compute feature\n");
    GPUDestroyDevice(device);
    return 0;
  }
  GPUDestroyDevice(device);

  memset(&createInfo, 0, sizeof(createInfo));
  createInfo.chain.sType = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.chain.structSize = sizeof(createInfo);
  createInfo.queues.chain.sType = GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  createInfo.queues.chain.structSize = sizeof(createInfo.queues);
  createInfo.queues.requestCount = 1;
  createInfo.queues.pRequests = &request;

  request.type = 0;
  request.count = 1;
  device = (GPUDevice *)(uintptr_t)1u;
  if (GPUCreateDevice(adapter, &createInfo, &device) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      device != NULL) {
    fprintf(stderr, "device create accepted queue request with no flags\n");
    return 0;
  }

  request.type = GPU_QUEUE_GRAPHICS;
  request.count = 0;
  device = (GPUDevice *)(uintptr_t)1u;
  if (GPUCreateDevice(adapter, &createInfo, &device) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      device != NULL) {
    fprintf(stderr, "device create accepted zero queue count\n");
    return 0;
  }

  request.type = GPU_QUEUE_GRAPHICS;
  request.count = 2;
  device = NULL;
  if (GPUCreateDevice(adapter, &createInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "device create rejected explicit queue count\n");
    return 0;
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE)) {
    fprintf(stderr, "device create enabled unrequested compute feature\n");
    GPUDestroyDevice(device);
    return 0;
  }

  ok = (GPUGetAvailableQueueBits(device) & GPU_QUEUE_GRAPHICS) == GPU_QUEUE_GRAPHICS &&
       GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0) &&
       GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 1) &&
       !GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 2) &&
       !GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0) &&
       GPUGetCommandQueue(device, GPU_QUEUE_GRAPHICS) ==
         GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!ok) {
    fprintf(stderr, "explicit queue count lookup failed\n");
  }

  GPUDestroyDevice(device);
  return ok;
}

static int
check_queue_submit_fence(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer submittedCmdb = {0};
  GPUCommandBuffer noDrawableCmdb = {0};
  GPUCommandBuffer foreignCmdb = {0};
  GPUCommandQueue foreignQueue = {0};
  GPUFrame fakeFrame = {0};
  GPUFrame noDrawableFrame = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUCommandBuffer *nullBuffers[1];
  GPUCommandBuffer *duplicateBuffers[2];
  GPUFence *fence;
  GPUFenceCreateInfo fenceInfo = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for fence test\n");
    return 0;
  }
  if (!check_fence_create_validation(device)) {
    return 0;
  }

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label = "reflection-submit-fence";
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create fence\n");
    return 0;
  }

  ok = GPUIsFenceSignaled(fence);
  GPUResetFence(fence);
  ok = ok && !GPUIsFenceSignaled(fence);
  ok = ok && GPUWaitFence(fence, 0) == GPU_ERROR_TIMEOUT;

  submittedCmdb._submitted = true;
  submittedCmdb._priv = (void *)(uintptr_t)0xdeadbeefu;
  fakeFrame.drawable = (void *)(uintptr_t)0xdeadbeefu;
  GPUSchedulePresent(&submittedCmdb, &fakeFrame);
  GPUPresent(&submittedCmdb, &fakeFrame);

  noDrawableCmdb._priv = (void *)(uintptr_t)0xdeadbeefu;
  GPUSchedulePresent(&noDrawableCmdb, &noDrawableFrame);
  GPUPresent(&noDrawableCmdb, &noDrawableFrame);

  ok = ok && GPUQueueSubmit(queue, NULL) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(NULL, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUFinishFrame(NULL, NULL, NULL) == GPU_ERROR_INVALID_ARGUMENT;

  cmdb = NULL;
  if (ok &&
      (GPUAcquireCommandBuffer(queue, "reflection-fence-submit", &cmdb) != GPU_OK ||
       !cmdb)) {
    fprintf(stderr, "failed to acquire command buffer for fence test\n");
    ok = 0;
  }

  if (ok) {
    buffers[0] = cmdb;
    nullBuffers[0] = NULL;

    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted wrong sType\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = (uint32_t)(sizeof(submitInfo) - 1u);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted short structSize\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = nullBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted null command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    foreignCmdb._queue = &foreignQueue;
    buffers[0] = &foreignCmdb;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted command buffer from another queue\n");
      ok = 0;
    }
  }

  if (ok) {
    duplicateBuffers[0] = cmdb;
    duplicateBuffers[1] = cmdb;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 2u;
    submitInfo.ppCommandBuffers = duplicateBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted duplicate command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    cmdb->_activeEncoder = true;
    memset(&submitInfo, 0, sizeof(submitInfo));
    buffers[0] = cmdb;
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted command buffer with active encoder\n");
      ok = 0;
    }
    cmdb->_activeEncoder = false;
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    buffers[0] = cmdb;
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    submitInfo.fence = fence;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
        !GPUIsFenceSignaled(fence)) {
      fprintf(stderr, "queue submit fence did not signal\n");
      ok = 0;
    }
  }

  GPUDestroyFence(fence);
  return ok;
}

static int
check_queue_submit_ex_semaphore(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUFence *fence;
  GPUFenceCreateInfo fenceInfo = {0};
  GPUSemaphore *semaphore;
  GPUSemaphoreCreateInfo semaphoreInfo = {0};
  GPUQueueSemaphoreWait wait = {0};
  GPUQueueSemaphoreSignal signal = {0};
  GPUQueueSubmitExInfo submitInfo = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for submit ex test\n");
    return 0;
  }

  semaphoreInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphore = (GPUSemaphore *)(uintptr_t)1u;
  if (GPUCreateSemaphore(NULL, &semaphoreInfo, &semaphore) != GPU_ERROR_INVALID_ARGUMENT ||
      semaphore != NULL) {
    fprintf(stderr, "semaphore create accepted null device\n");
    return 0;
  }
  semaphore = (GPUSemaphore *)(uintptr_t)1u;
  if (GPUCreateSemaphore(device, &semaphoreInfo, &semaphore) != GPU_ERROR_INVALID_ARGUMENT ||
      semaphore != NULL) {
    fprintf(stderr, "semaphore create accepted wrong sType\n");
    return 0;
  }
  semaphoreInfo.chain.sType = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = (uint32_t)(sizeof(semaphoreInfo) - 1u);
  semaphore = (GPUSemaphore *)(uintptr_t)1u;
  if (GPUCreateSemaphore(device, &semaphoreInfo, &semaphore) != GPU_ERROR_INVALID_ARGUMENT ||
      semaphore != NULL) {
    fprintf(stderr, "semaphore create accepted short structSize\n");
    return 0;
  }
  if (GPUCreateSemaphore(device, NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "semaphore create accepted null output\n");
    return 0;
  }

  memset(&semaphoreInfo, 0, sizeof(semaphoreInfo));
  semaphoreInfo.chain.sType = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label = "submit-ex-semaphore";
  semaphoreInfo.initialValue = 1u;
  semaphore = NULL;
  if (GPUCreateSemaphore(device, &semaphoreInfo, &semaphore) != GPU_OK || !semaphore) {
    fprintf(stderr, "failed to create semaphore\n");
    return 0;
  }

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create fence for submit ex\n");
    GPUDestroySemaphore(semaphore);
    return 0;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "submit-ex", &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "failed to acquire command buffer for submit ex\n");
    GPUDestroyFence(fence);
    GPUDestroySemaphore(semaphore);
    return 0;
  }

  buffers[0] = cmdb;
  ok = GPUQueueSubmitEx(queue, NULL) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmitEx(NULL, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submitInfo.chain.structSize = (uint32_t)(sizeof(submitInfo) - 1u);
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.waitCount = 1u;
  submitInfo.pWaits = NULL;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  wait.semaphore = semaphore;
  wait.value = 1u;
  wait.waitStages = GPU_STAGE_TOP;
  submitInfo.pWaits = &wait;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_UNSUPPORTED;

  submitInfo.waitCount = 0u;
  submitInfo.pWaits = NULL;
  submitInfo.signalCount = 1u;
  submitInfo.pSignals = NULL;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  signal.semaphore = semaphore;
  signal.value = 2u;
  submitInfo.pSignals = &signal;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_UNSUPPORTED;

  submitInfo.signalCount = 0u;
  submitInfo.pSignals = NULL;
  submitInfo.fence = fence;
  ok = ok &&
       GPUQueueSubmitEx(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK &&
       GPUIsFenceSignaled(fence);

  GPUDestroyFence(fence);
  GPUDestroySemaphore(semaphore);
  return ok;
}

int
gpu_test_queue(GPUAdapter *adapter, GPUDevice *device) {
  return check_adapter_enumeration() &&
         check_device_queue_create_validation(adapter) &&
         check_queue_selection(device) &&
         check_queue_submit_fence(device) &&
         check_queue_submit_ex_semaphore(device);
}
