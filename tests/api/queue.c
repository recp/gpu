#include "test.h"
#include "../../src/api/cmdqueue_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/frame_internal.h"
#include "../../src/api/surface_internal.h"
#include "../../src/api/swapchain_internal.h"

typedef struct QueueCompletionProbe {
  void             *sender;
  GPUCommandBuffer *cmdb;
  int               count;
} QueueCompletionProbe;

static GPUQueue  gScopedQueue;
static GPUCommandBuffer gScopedCmdb;
static GPUFrame         gScopedFrame;
static uint32_t         gScopedQueueGetCalls;
static uint32_t         gScopedCmdbNewCalls;
static uint32_t         gScopedPresentCalls;
static uint32_t         gScopedCommitCalls;
static uint32_t         gScopedFrameBeginCalls;
static uint32_t         gScopedFrameEndCalls;

static GPUAdapter gOwnershipAdapter;
static GPUDevice  gOwnershipDevice;
static GPUSurface gOwnershipSurface;
static uint32_t   gOwnershipAdapterCalls;
static uint32_t   gOwnershipAdapterDestroyCalls;
static uint32_t   gOwnershipAdapterSelectCalls;
static uint32_t   gOwnershipPropertiesCalls;
static uint32_t   gOwnershipFeatureCalls;
static uint32_t   gOwnershipLimitsCalls;
static uint32_t   gOwnershipFormatCalls;
static uint32_t   gOwnershipDeviceCreateCalls;
static uint32_t   gOwnershipDeviceWaitCalls;
static uint32_t   gOwnershipDeviceDestroyCalls;
static bool       gOwnershipDeviceDestroyOrderValid;
static uint32_t   gOwnershipSurfaceCreateCalls;
static uint32_t   gOwnershipSurfaceDestroyCalls;
static uint32_t   gOwnershipInstanceDestroyCalls;

static GPUAdapter *
get_ownership_adapters(GPUInstance * __restrict instance,
                       uint32_t                   maxCount) {
  (void)maxCount;
  memset(&gOwnershipAdapter, 0, sizeof(gOwnershipAdapter));
  gOwnershipAdapter.inst = instance;
  gOwnershipAdapterCalls++;
  return &gOwnershipAdapter;
}

static void
destroy_ownership_adapter(GPUAdapter * __restrict adapter) {
  (void)adapter;
  gOwnershipAdapterDestroyCalls++;
}

static GPUAdapter *
select_ownership_adapter(GPUInstance * __restrict instance,
                         GPUAdapter  * __restrict adapters) {
  (void)instance;
  gOwnershipAdapterSelectCalls++;
  return adapters;
}

static GPUResult
get_ownership_properties(const GPUAdapter     * __restrict adapter,
                         GPUAdapterProperties * __restrict outProps) {
  outProps->name    = "scoped-adapter";
  outProps->backend = gpuAdapterApi(adapter)->backend;
  outProps->type    = GPU_ADAPTER_TYPE_INTEGRATED;
  gOwnershipPropertiesCalls++;
  return GPU_OK;
}

static bool
supports_ownership_feature(const GPUAdapter * __restrict adapter,
                           GPUFeature feature) {
  (void)adapter;
  (void)feature;
  gOwnershipFeatureCalls++;
  return false;
}

static void
get_ownership_limits(const GPUAdapter * __restrict adapter,
                     GPULimits       * __restrict outLimits) {
  (void)adapter;
  outLimits->maxColorAttachments      = 7u;
  outLimits->maxComputeWorkgroupSizeX = 321u;
  gOwnershipLimitsCalls++;
}

static void
get_ownership_format_capabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps) {
  (void)adapter;
  (void)format;
  outCaps->filterable = false;
  gOwnershipFormatCalls++;
}

static GPUDevice *
create_ownership_device(GPUAdapter               * __restrict adapter,
                        GPUQueueCreateInfo *queueInfos,
                        uint32_t                   queueInfoCount) {
  (void)queueInfos;
  (void)queueInfoCount;
  memset(&gOwnershipDevice, 0, sizeof(gOwnershipDevice));
  gOwnershipDevice.inst      = adapter->inst;
  gOwnershipDevice.phyDevice = adapter;
  gOwnershipDeviceCreateCalls++;
  return &gOwnershipDevice;
}

static void
destroy_ownership_device(GPUDevice * __restrict device) {
  (void)device;
  if (gOwnershipDeviceWaitCalls != gOwnershipDeviceDestroyCalls + 1u) {
    gOwnershipDeviceDestroyOrderValid = false;
  }
  gOwnershipDeviceDestroyCalls++;
}

static GPUResult
wait_ownership_device(GPUDevice * __restrict device) {
  (void)device;
  gOwnershipDeviceWaitCalls++;
  return GPU_OK;
}

static GPUSurface *
create_ownership_surface(GPUApi      * __restrict api,
                         GPUInstance * __restrict instance,
                         GPUAdapter  * __restrict adapter,
                         void        * __restrict nativeHandle,
                         GPUSurfaceType           type,
                         float                    scale) {
  (void)api;
  (void)instance;
  (void)adapter;
  (void)nativeHandle;
  (void)type;
  (void)scale;
  memset(&gOwnershipSurface, 0, sizeof(gOwnershipSurface));
  gOwnershipSurfaceCreateCalls++;
  return &gOwnershipSurface;
}

static void
destroy_ownership_surface(GPUSurface * __restrict surface) {
  (void)surface;
  gOwnershipSurfaceDestroyCalls++;
}

static void
destroy_ownership_instance(GPUApi      * __restrict api,
                           GPUInstance * __restrict instance) {
  (void)api;
  (void)instance;
  gOwnershipInstanceDestroyCalls++;
}

static int
check_instance_ownership_dispatch(GPUInstance *activeInstance) {
  GPUNativeSurfaceCreateInfo nativeInfo = {0};
  GPUSurfaceCreateInfo       surfaceInfo = {0};
  GPUAdapterProperties       properties;
  GPUAdapterCapabilities     adapterCaps;
  GPUDeviceCapabilities      deviceCaps;
  GPUFormatCapabilities      formatCaps;
  GPUApi                    *activeApi;
  GPUAdapter                *adapter;
  GPUDevice                 *device;
  GPUSurface                *surface;
  GPUApi                     scopedApi;
  GPUInstance                instance = {0};
  GPUInstance                otherInstance = {0};
  uint32_t                   adapterCount;

  activeApi = gpuInstanceApi(activeInstance);
  if (!activeApi) {
    fprintf(stderr, "instance ownership has no instance api\n");
    return 0;
  }

  scopedApi                              = *activeApi;
  scopedApi.device.getAvailableAdapters = get_ownership_adapters;
  scopedApi.device.selectAdapter        = select_ownership_adapter;
  scopedApi.device.destroyAdapter       = destroy_ownership_adapter;
  scopedApi.device.getAdapterProperties = get_ownership_properties;
  scopedApi.device.supportsFeature      = supports_ownership_feature;
  scopedApi.device.getLimits            = get_ownership_limits;
  scopedApi.device.getFormatCapabilities =
    get_ownership_format_capabilities;
  scopedApi.device.createDevice         = create_ownership_device;
  scopedApi.device.waitIdle             = wait_ownership_device;
  scopedApi.device.destroyDevice        = destroy_ownership_device;
  scopedApi.surface.createSurface       = create_ownership_surface;
  scopedApi.surface.destroySurface      = destroy_ownership_surface;
  scopedApi.instance.destroyInstance    = destroy_ownership_instance;
  instance._api                         = &scopedApi;
  otherInstance._api                    = &scopedApi;
  gOwnershipAdapterCalls                = 0u;
  gOwnershipAdapterDestroyCalls         = 0u;
  gOwnershipAdapterSelectCalls          = 0u;
  gOwnershipPropertiesCalls             = 0u;
  gOwnershipFeatureCalls                = 0u;
  gOwnershipLimitsCalls                 = 0u;
  gOwnershipFormatCalls                 = 0u;
  gOwnershipDeviceCreateCalls           = 0u;
  gOwnershipDeviceWaitCalls             = 0u;
  gOwnershipDeviceDestroyCalls          = 0u;
  gOwnershipDeviceDestroyOrderValid     = true;
  gOwnershipSurfaceCreateCalls          = 0u;
  gOwnershipSurfaceDestroyCalls         = 0u;
  gOwnershipInstanceDestroyCalls        = 0u;

  adapter      = NULL;
  adapterCount = 0u;
  if (GPUEnumerateAdapters(&instance, &adapterCount, NULL) != GPU_OK ||
      adapterCount != 1u) {
    fprintf(stderr, "instance-scoped adapter count failed\n");
    return 0;
  }
  adapterCount = 1u;
  if (GPUEnumerateAdapters(&instance, &adapterCount, &adapter) != GPU_OK ||
      adapterCount != 1u || adapter != &gOwnershipAdapter) {
    fprintf(stderr, "instance-scoped adapter enumeration failed\n");
    return 0;
  }

  memset(&properties, 0, sizeof(properties));
  memset(&adapterCaps, 0, sizeof(adapterCaps));
  memset(&deviceCaps, 0, sizeof(deviceCaps));
  memset(&formatCaps, 0, sizeof(formatCaps));
  device = NULL;
  if (GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      strcmp(properties.name, "scoped-adapter") != 0 ||
      GPUGetAdapterCapabilities(adapter, &adapterCaps) != GPU_OK ||
      adapterCaps.limits.maxColorAttachments != 7u ||
      adapterCaps.limits.maxComputeWorkgroupSizeX != 321u ||
      GPUGetFormatCapabilities(adapter,
                               GPU_FORMAT_RGBA8_UNORM,
                               &formatCaps) != GPU_OK ||
      !formatCaps.sampled || formatCaps.filterable ||
      gOwnershipFormatCalls != 1u ||
      GPUCreateDevice(adapter, NULL, &device) != GPU_OK ||
      device != &gOwnershipDevice || device->_api != &scopedApi ||
      GPUGetDeviceCapabilities(device, &deviceCaps) != GPU_OK ||
      deviceCaps.limits.maxColorAttachments != 7u ||
      deviceCaps.limits.maxComputeWorkgroupSizeX != 321u ||
      gOwnershipLimitsCalls != 2u) {
    fprintf(stderr, "instance-scoped adapter/device dispatch failed\n");
    return 0;
  }

  nativeInfo.chain.sType      = GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO;
  nativeInfo.chain.structSize = sizeof(nativeInfo);
  nativeInfo.adapter          = adapter;
  nativeInfo.nativeHandle     = &instance;
  nativeInfo.type             = GPU_SURFACE_APPLE_NSVIEW;
  nativeInfo.scale            = 1.0f;
  surfaceInfo.chain.sType      = GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO;
  surfaceInfo.chain.structSize = sizeof(surfaceInfo);
  surfaceInfo.chain.pNext      = &nativeInfo;
  surface = NULL;
  if (GPUCreateSurface(&otherInstance, &surfaceInfo, &surface) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      surface ||
      GPUCreateSurface(&instance, &surfaceInfo, &surface) != GPU_OK ||
      surface != &gOwnershipSurface || surface->inst != &instance) {
    fprintf(stderr, "instance-scoped surface dispatch failed\n");
    return 0;
  }

  GPUDestroySurface(surface);
  GPUDestroyDevice(device);
  device = GPUCreateSystemDefaultDevice(&instance);
  if (device != &gOwnershipDevice) {
    fprintf(stderr, "instance-scoped default device selection failed\n");
    return 0;
  }
  GPUDestroyDevice(device);
  GPUDestroyInstance(&instance);

  if (gOwnershipAdapterCalls != 1u ||
      gOwnershipAdapterDestroyCalls != 1u ||
      gOwnershipAdapterSelectCalls != 1u ||
      gOwnershipPropertiesCalls != 1u ||
      gOwnershipFeatureCalls == 0u ||
      gOwnershipDeviceCreateCalls != 2u ||
      gOwnershipDeviceWaitCalls != 2u ||
      gOwnershipDeviceDestroyCalls != 2u ||
      !gOwnershipDeviceDestroyOrderValid ||
      gOwnershipSurfaceCreateCalls != 1u ||
      gOwnershipSurfaceDestroyCalls != 1u ||
      gOwnershipInstanceDestroyCalls != 1u) {
    fprintf(stderr, "instance ownership called wrong backend\n");
    return 0;
  }

  return 1;
}

static int
check_secondary_backend_instance(const GPUInstance *activeInstance) {
  GPUInstanceCreateInfo info = {0};
  GPUInstance          *instance;
  GPUBackend            backend;
  GPUResult             result;

  if (!activeInstance || !gpuInstanceApi(activeInstance)) {
    fprintf(stderr, "secondary backend test has no active instance api\n");
    return 0;
  }

  backend = gpuInstanceApi(activeInstance)->backend;
  if (backend == GPU_BACKEND_METAL) {
    backend = GPU_BACKEND_VULKAN;
  } else if (backend == GPU_BACKEND_VULKAN) {
    backend = GPU_BACKEND_METAL;
  } else {
    return 1;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = backend;
  instance = NULL;
  result   = GPUCreateInstance(&info, &instance);
  if (result == GPU_ERROR_UNSUPPORTED) {
    return 1;
  }
  if (result != GPU_OK || !instance ||
      !gpuInstanceApi(instance) ||
      gpuInstanceApi(instance)->backend != backend ||
      gpuInstanceApi(instance) == gpuInstanceApi(activeInstance)) {
    fprintf(stderr, "secondary backend instance selection failed\n");
    return 0;
  }

  GPUDestroyInstance(instance);
  return 1;
}

static GPUQueue *
get_scoped_queue(GPUDevice * __restrict device,
                 GPUQueueFlagBits       bits,
                 uint32_t               index) {
  (void)index;
  memset(&gScopedQueue, 0, sizeof(gScopedQueue));
  gScopedQueue._device = device;
  gScopedQueue.bits    = bits;
  gScopedQueueGetCalls++;
  return &gScopedQueue;
}

static GPUCommandBuffer *
new_scoped_cmdb(GPUQueue * __restrict queue,
                const char      * __restrict label,
                void            * __restrict sender,
                GPUCommandBufferCompletionFn onComplete) {
  (void)queue;
  (void)label;
  (void)sender;
  (void)onComplete;
  memset(&gScopedCmdb, 0, sizeof(gScopedCmdb));
  gScopedCmdbNewCalls++;
  return &gScopedCmdb;
}

static void
present_scoped_frame(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  (void)cmdb;
  (void)frame;
  gScopedPresentCalls++;
}

static GPUResult
commit_scoped_cmdb(GPUCommandBuffer * __restrict cmdb) {
  (void)cmdb;
  gScopedCommitCalls++;
  return GPU_OK;
}

static GPUFrame *
begin_scoped_frame(GPUApi       * __restrict api,
                   GPUSwapchain * __restrict swapchain) {
  (void)api;
  (void)swapchain;
  memset(&gScopedFrame, 0, sizeof(gScopedFrame));
  gScopedFrame.drawable = &gScopedFrame;
  gScopedFrameBeginCalls++;
  return &gScopedFrame;
}

static void
end_scoped_frame(GPUApi   * __restrict api,
                 GPUFrame * __restrict frame) {
  (void)api;
  (void)frame;
  gScopedFrameEndCalls++;
}

static int
check_queue_frame_device_dispatch(GPUDevice *activeDevice) {
  GPUApi           *api;
  GPUQueue         *queue;
  GPUCommandBuffer *cmdb;
  GPUFrame         *frame;
  GPUApi            scopedApi;
  GPUDevice         device    = {0};
  GPUSwapchain      swapchain = {0};

  api = gpuDeviceApi(activeDevice);
  if (!api) {
    fprintf(stderr, "queue/frame dispatch has no device api\n");
    return 0;
  }

  scopedApi                                = *api;
  scopedApi.cmdque.getCommandQueue         = get_scoped_queue;
  scopedApi.cmdque.newCommandBuffer        = new_scoped_cmdb;
  scopedApi.cmdque.commit                  = commit_scoped_cmdb;
  scopedApi.cmdbuf.presentDrawable         = present_scoped_frame;
  scopedApi.frame.beginFrame               = begin_scoped_frame;
  scopedApi.frame.endFrame                 = end_scoped_frame;
  device._api                              = &scopedApi;
  swapchain.device                         = &device;
  gScopedQueueGetCalls                     = 0u;
  gScopedCmdbNewCalls                      = 0u;
  gScopedPresentCalls                      = 0u;
  gScopedCommitCalls                       = 0u;
  gScopedFrameBeginCalls                   = 0u;
  gScopedFrameEndCalls                     = 0u;

  queue = GPUGetQueue(&device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb  = NULL;
  frame = GPUBeginFrame(&swapchain);
  if (queue != &gScopedQueue || !frame ||
      GPUAcquireCommandBuffer(queue, "device-scoped", &cmdb) != GPU_OK ||
      cmdb != &gScopedCmdb ||
      GPUFinishFrame(queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "queue/frame device dispatch failed\n");
    return 0;
  }

  if (gScopedQueueGetCalls != 1u ||
      gScopedCmdbNewCalls != 1u ||
      gScopedPresentCalls != 1u ||
      gScopedCommitCalls != 1u ||
      gScopedFrameBeginCalls != 1u ||
      gScopedFrameEndCalls != 1u) {
    fprintf(stderr, "queue/frame device dispatch called wrong backend\n");
    return 0;
  }

  return 1;
}

static void
queue_completion_probe(void            * __restrict sender,
                       GPUCommandBuffer * __restrict cmdb) {
  QueueCompletionProbe *probe = sender;

  if (!probe) {
    return;
  }

  probe->count++;
  probe->sender = sender;
  probe->cmdb = cmdb;
}

static bool
feature_set_contains(const GPUFeatureSet *set, GPUFeature feature) {
  if (!set || !set->pFeatures) {
    return false;
  }

  for (uint32_t i = 0; i < set->featureCount; i++) {
    if (set->pFeatures[i] == feature) {
      return true;
    }
  }

  return false;
}

static bool
feature_set_matches_adapter(const GPUAdapter    *adapter,
                            const GPUFeatureSet *set) {
  for (GPUFeature feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_VARIABLE_RATE_SHADING;
       feature = (GPUFeature)(feature + 1)) {
    if (feature_set_contains(set, feature) !=
        GPUIsFeatureSupported(adapter, feature)) {
      return false;
    }
  }

  return true;
}

static bool
feature_set_matches_device(const GPUDevice     *device,
                           const GPUFeatureSet *set) {
  for (GPUFeature feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_VARIABLE_RATE_SHADING;
       feature = (GPUFeature)(feature + 1)) {
    if (feature_set_contains(set, feature) !=
        GPUIsFeatureEnabled(device, feature)) {
      return false;
    }
  }

  return true;
}

static int
check_adapter_enumeration(GPUInstance *activeInstance) {
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
  if (GPUEnumerateAdapters(activeInstance, &adapterCount, NULL) != GPU_OK ||
      adapterCount == 0) {
    fprintf(stderr, "adapter count query failed\n");
    return 0;
  }

  if (adapterCount > GPU_ARRAY_LEN(adapters)) {
    adapterCount = (uint32_t)GPU_ARRAY_LEN(adapters);
  }
  if (GPUEnumerateAdapters(activeInstance, &adapterCount, adapters) != GPU_OK ||
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
  if (GPUGetFormatCapabilities(adapters[0],
                               GPU_FORMAT_UNDEFINED,
                               &formatCaps) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUGetFormatCapabilities(adapters[0],
                               GPU_FORMAT_COUNT,
                               &formatCaps) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "format capabilities accepted invalid format\n");
    return 0;
  }
  for (GPUFormat format = GPU_FORMAT_R8_UNORM;
       format < GPU_FORMAT_COUNT;
       format = (GPUFormat)(format + 1)) {
    if (GPUGetFormatCapabilities(adapters[0], format, &formatCaps) != GPU_OK) {
      fprintf(stderr,
              "format capabilities query failed for %u\n",
              (uint32_t)format);
      return 0;
    }
  }
  memset(&caps, 0, sizeof(caps));
  if (GPUGetAdapterCapabilities(adapters[0], &caps) != GPU_OK ||
      !GPUIsFeatureSupported(adapters[0], GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureSupported(adapters[0], GPU_FEATURE_INDIRECT_DRAW) ||
      caps.supported.featureCount == 0 ||
      !caps.supported.pFeatures ||
      !feature_set_contains(&caps.supported, GPU_FEATURE_COMPUTE) ||
      !feature_set_contains(&caps.supported, GPU_FEATURE_INDIRECT_DRAW) ||
      !feature_set_matches_adapter(adapters[0], &caps.supported) ||
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
      formatCaps.colorAttachment) {
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
  if (GPUEnumerateAdapters(activeInstance, &adapterCount, adapters) !=
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
  GPUQueue        *graphics0;
  GPUQueue        *compute0;

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
      !GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      caps.enabled.featureCount == 0 ||
      !caps.enabled.pFeatures ||
      !feature_set_contains(&caps.enabled, GPU_FEATURE_COMPUTE) ||
      !feature_set_contains(&caps.enabled, GPU_FEATURE_INDIRECT_DRAW) ||
      !feature_set_matches_device(device, &caps.enabled) ||
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

  requiredFeature = GPU_FEATURE_INDIRECT_DRAW;
  device = NULL;
  if (GPUCreateDevice(adapter, &createInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "device create rejected required indirect draw feature\n");
    return 0;
  }
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE)) {
    fprintf(stderr, "device create reported wrong indirect draw feature set\n");
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
  request.count = 1;
  device = NULL;
  if (GPUCreateDevice(adapter, &createInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "device create rejected explicit queue count\n");
    return 0;
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW)) {
    fprintf(stderr, "device create enabled unrequested feature\n");
    GPUDestroyDevice(device);
    return 0;
  }

  ok = (GPUGetAvailableQueueBits(device) & GPU_QUEUE_GRAPHICS) == GPU_QUEUE_GRAPHICS &&
       GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0) &&
       !GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 1) &&
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
  GPUQueue        *queue;
  GPUCommandBuffer submittedCmdb = {0};
  GPUCommandBuffer noDrawableCmdb = {0};
  GPUCommandBuffer foreignCmdb = {0};
  GPUQueue        foreignQueue = {0};
  GPUFrame fakeFrame = {0};
  GPUFrame noDrawableFrame = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUCommandBuffer *nullBuffers[1];
  GPUCommandBuffer *duplicateBuffers[2];
  QueueCompletionProbe completionProbe = {0};
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
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         &completionProbe,
                                         queue_completion_probe);
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
    if (ok &&
        (completionProbe.count != 1 ||
         completionProbe.sender != &completionProbe ||
         completionProbe.cmdb != cmdb)) {
      fprintf(stderr, "queue submit completion handler did not run\n");
      ok = 0;
    }
  }

  GPUDestroyFence(fence);
  return ok;
}

static int
check_device_destroy_waits_for_submission(GPUAdapter *adapter) {
  enum { submitCount = 8 };

  QueueCompletionProbe  completionProbes[submitCount] = {0};
  GPUCommandBuffer     *buffers[submitCount];
  GPUQueue             *queue;
  GPUDevice            *device;
  GPUQueueSubmitInfo    submitInfo = {0};

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "failed to create device for destroy wait test\n");
    return 0;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get queue for destroy wait test\n");
    GPUDestroyDevice(device);
    return 0;
  }

  for (uint32_t i = 0u; i < submitCount; i++) {
    buffers[i] = NULL;
    if (GPUAcquireCommandBuffer(queue,
                                "destroy-wait",
                                &buffers[i]) != GPU_OK ||
        !buffers[i]) {
      fprintf(stderr, "failed to acquire command buffer for destroy wait test\n");
      GPUDestroyDevice(device);
      return 0;
    }
    GPUSetCommandBufferCompletionHandler(buffers[i],
                                         &completionProbes[i],
                                         queue_completion_probe);
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = submitCount;
  submitInfo.ppCommandBuffers   = buffers;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "queue submit failed for destroy wait test\n");
    GPUDestroyDevice(device);
    return 0;
  }

  GPUDestroyDevice(device);
  for (uint32_t i = 0u; i < submitCount; i++) {
    if (completionProbes[i].count != 1 ||
        completionProbes[i].sender != &completionProbes[i] ||
        completionProbes[i].cmdb != buffers[i]) {
      fprintf(stderr, "device destroy returned before queue completion\n");
      return 0;
    }
  }
  return 1;
}

static int
check_queue_submit_ex_semaphore(GPUDevice *device) {
  GPUQueue        *queue;
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUFence *fence;
  GPUFenceCreateInfo fenceInfo = {0};
  GPUSemaphore *semaphore;
  GPUSemaphoreCreateInfo semaphoreInfo = {0};
  GPUQueueSemaphoreWait wait = {0};
  GPUQueueSemaphoreSignal signal = {0};
  GPUCommandBuffer submittedCmdb = {0};
  GPUCommandBuffer *duplicateBuffers[2];
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
  duplicateBuffers[0] = cmdb;
  duplicateBuffers[1] = cmdb;
  submitInfo.commandBufferCount = 2u;
  submitInfo.ppCommandBuffers = duplicateBuffers;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;

  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers = buffers;
  cmdb->_activeEncoder = true;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  cmdb->_activeEncoder = false;

  submittedCmdb._queue = queue;
  submittedCmdb._submitted = true;
  buffers[0] = &submittedCmdb;
  ok = ok && GPUQueueSubmitEx(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  buffers[0] = cmdb;

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
gpu_test_queue(GPUInstance *instance,
               GPUAdapter  *adapter,
               GPUDevice   *device) {
  return check_instance_ownership_dispatch(instance) &&
         check_secondary_backend_instance(instance) &&
         check_queue_frame_device_dispatch(device) &&
         check_adapter_enumeration(instance) &&
         check_device_queue_create_validation(adapter) &&
         check_device_destroy_waits_for_submission(adapter) &&
         check_queue_selection(device) &&
         check_queue_submit_fence(device) &&
         check_queue_submit_ex_semaphore(device);
}
