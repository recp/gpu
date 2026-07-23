/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

typedef struct WebGPUAdapterRequest {
  GPUInstance                      *instance;
  GPUBackendAdapterRequestCallback  callback;
  void                             *userData;
} WebGPUAdapterRequest;

typedef struct WebGPUDeviceRequest {
  GPUBackendDeviceRequestCallback  callback;
  GPUDevice                       *device;
  GPUDeviceWebGPU                 *native;
  void                            *userData;
  bool                             ready;
} WebGPUDeviceRequest;

static GPUAdapterType
webgpu_adapterType(WGPUAdapterType type) {
  switch (type) {
    case WGPUAdapterType_IntegratedGPU:
      return GPU_ADAPTER_TYPE_INTEGRATED;
    case WGPUAdapterType_DiscreteGPU:
      return GPU_ADAPTER_TYPE_DISCRETE;
    case WGPUAdapterType_CPU:
      return GPU_ADAPTER_TYPE_SOFTWARE;
    default:
      return GPU_ADAPTER_TYPE_UNKNOWN;
  }
}

static void
webgpu_copyString(char *dst, size_t capacity, WGPUStringView src) {
  size_t size;

  if (!dst || capacity == 0u) {
    return;
  }
  size = src.data ? src.length : 0u;
  if (size == WGPU_STRLEN && src.data) {
    size = strlen(src.data);
  }
  if (size >= capacity) {
    size = capacity - 1u;
  }
  if (size > 0u) {
    memcpy(dst, src.data, size);
  }
  dst[size] = '\0';
}

static void
webgpu_uncapturedError(WGPUDevice const *nativeDevice,
                       WGPUErrorType      type,
                       WGPUStringView     message,
                       void              *userData,
                       void              *unused) {
  WebGPUDeviceRequest *request;
  GPUDeviceErrorType   errorType;
  GPUResult            result;
  char                 text[512];

  GPU__UNUSED(nativeDevice);
  GPU__UNUSED(unused);
  request = userData;
  if (!request || !request->ready || !request->device ||
      type == WGPUErrorType_NoError) {
    return;
  }

  switch (type) {
    case WGPUErrorType_Validation:
      errorType = GPU_DEVICE_ERROR_VALIDATION;
      result    = GPU_ERROR_INVALID_ARGUMENT;
      break;
    case WGPUErrorType_OutOfMemory:
      errorType = GPU_DEVICE_ERROR_OUT_OF_MEMORY;
      result    = GPU_ERROR_OUT_OF_MEMORY;
      break;
    default:
      errorType = GPU_DEVICE_ERROR_BACKEND;
      result    = GPU_ERROR_BACKEND_FAILURE;
      break;
  }
  webgpu_copyString(text, sizeof(text), message);
  gpuDeviceReportError(request->device,
                       errorType,
                       GPU_DEVICE_LOST_REASON_UNKNOWN,
                       result,
                       text[0] ? text : "WebGPU uncaptured device error");
}

static void
webgpu_deviceLost(WGPUDevice const    *nativeDevice,
                  WGPUDeviceLostReason reason,
                  WGPUStringView       message,
                  void                *userData,
                  void                *unused) {
  WebGPUDeviceRequest *request;
  GPUDeviceLostReason  lostReason;
  char                 text[512];

  GPU__UNUSED(nativeDevice);
  GPU__UNUSED(unused);
  request = userData;
  if (!request || !request->ready || !request->device ||
      reason == WGPUDeviceLostReason_Destroyed ||
      reason == WGPUDeviceLostReason_CallbackCancelled) {
    return;
  }

  lostReason = reason == WGPUDeviceLostReason_FailedCreation
                 ? GPU_DEVICE_LOST_REASON_DRIVER_ERROR
                 : GPU_DEVICE_LOST_REASON_UNKNOWN;
  webgpu_copyString(text, sizeof(text), message);
  gpuDeviceReportError(request->device,
                       GPU_DEVICE_ERROR_LOST,
                       lostReason,
                       GPU_ERROR_BACKEND_FAILURE,
                       text[0] ? text : "WebGPU device lost");
}

static void
webgpu_adapterReady(WGPURequestAdapterStatus status,
                    WGPUAdapter              nativeAdapter,
                    WGPUStringView           message,
                    void                    *userData,
                    void                    *unused) {
  WebGPUAdapterRequest *request;
  GPUAdapterWebGPU     *native;
  GPUAdapter           *adapter;
  WGPUAdapterInfo       info = WGPU_ADAPTER_INFO_INIT;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  request = userData;
  adapter = NULL;
  native  = NULL;

  if (status == WGPURequestAdapterStatus_Success && nativeAdapter) {
    adapter = calloc(1, sizeof(*adapter));
    native  = calloc(1, sizeof(*native));
    if (adapter && native) {
      native->adapter = nativeAdapter;
      adapter->_priv  = native;
      adapter->inst   = request->instance;
      adapter->supportsSwapchain = true;
      if (wgpuAdapterGetInfo(nativeAdapter, &info) == WGPUStatus_Success) {
        webgpu_copyString(native->name, sizeof(native->name), info.device);
        if (!native->name[0]) {
          webgpu_copyString(native->name,
                            sizeof(native->name),
                            info.description);
        }
        wgpuAdapterInfoFreeMembers(info);
      }
    } else {
      free(native);
      free(adapter);
      adapter = NULL;
    }
  }

  if (!adapter && nativeAdapter) {
    wgpuAdapterRelease(nativeAdapter);
  }
  request->callback(adapter ? GPU_OK : GPU_ERROR_BACKEND_FAILURE,
                    adapter,
                    request->userData);
  free(request);
}

static GPUResult
webgpu_requestAdapter(GPUInstance                      *instance,
                      GPUBackendAdapterRequestCallback  callback,
                      void                             *userData) {
  WGPURequestAdapterCallbackInfo callbackInfo =
    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  GPUInstanceWebGPU       *native;
  WebGPUAdapterRequest    *request;

  native = gpu_webgpuInstance(instance);
  if (!native || !native->instance || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  request = calloc(1, sizeof(*request));
  if (!request) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  request->instance = instance;
  request->callback = callback;
  request->userData = userData;

  options.powerPreference = WGPUPowerPreference_HighPerformance;
  callbackInfo.mode       = WGPUCallbackMode_AllowSpontaneous;
  callbackInfo.callback   = webgpu_adapterReady;
  callbackInfo.userdata1  = request;
  wgpuInstanceRequestAdapter(native->instance, &options, callbackInfo);
  return GPU_OK;
}

static void
webgpu_destroyAdapter(GPUAdapter *adapter) {
  GPUAdapterWebGPU *native;

  native = gpu_webgpuAdapter(adapter);
  if (native) {
    if (native->adapter) {
      wgpuAdapterRelease(native->adapter);
    }
    free(native);
  }
  free(adapter);
}

static GPUResult
webgpu_getAdapterProperties(const GPUAdapter     *adapter,
                            GPUAdapterProperties *properties) {
  GPUAdapterWebGPU *native;
  WGPUAdapterInfo   info = WGPU_ADAPTER_INFO_INIT;

  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter || !properties) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(properties, 0, sizeof(*properties));
  properties->backend = GPU_BACKEND_WEBGPU;
  properties->name    = native->name[0] ? native->name : "WebGPU adapter";
  if (wgpuAdapterGetInfo(native->adapter, &info) == WGPUStatus_Success) {
    properties->type = webgpu_adapterType(info.adapterType);
    wgpuAdapterInfoFreeMembers(info);
  }
  return GPU_OK;
}

static bool
webgpu_isBCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_BC1_RGBA_UNORM &&
         format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB;
}

static bool
webgpu_isETC2Format(GPUFormat format) {
  return format >= GPU_FORMAT_EAC_R11_UNORM &&
         format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB;
}

static bool
webgpu_isASTCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_ASTC_4X4_UNORM &&
         format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB;
}

static bool
webgpu_isWideNormFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R16_UNORM:
    case GPU_FORMAT_R16_SNORM:
    case GPU_FORMAT_RG16_UNORM:
    case GPU_FORMAT_RG16_SNORM:
    case GPU_FORMAT_RGBA16_UNORM:
    case GPU_FORMAT_RGBA16_SNORM:
      return true;
    default:
      return false;
  }
}

static bool
webgpu_isSRGBFormat(GPUFormat format) {
  return format == GPU_FORMAT_RGBA8_UNORM_SRGB ||
         format == GPU_FORMAT_BGRA8_UNORM_SRGB;
}

static bool
webgpu_isFloat32Format(GPUFormat format) {
  return format == GPU_FORMAT_R32_FLOAT ||
         format == GPU_FORMAT_RG32_FLOAT ||
         format == GPU_FORMAT_RGBA32_FLOAT;
}

static bool
webgpu_hasCoreStorage(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R32_UINT:
    case GPU_FORMAT_R32_SINT:
    case GPU_FORMAT_R32_FLOAT:
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RG32_UINT:
    case GPU_FORMAT_RG32_SINT:
    case GPU_FORMAT_RG32_FLOAT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA16_FLOAT:
    case GPU_FORMAT_RGBA32_UINT:
    case GPU_FORMAT_RGBA32_SINT:
    case GPU_FORMAT_RGBA32_FLOAT:
      return true;
    default:
      return false;
  }
}

static bool
webgpu_hasAdapterFeature(const GPUAdapter *adapter, WGPUFeatureName feature) {
  GPUAdapterWebGPU *native;

  native = gpu_webgpuAdapter(adapter);
  return native && native->adapter &&
         wgpuAdapterHasFeature(native->adapter, feature);
}

static void
webgpu_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat                          format,
  GPUFormatCapabilities * __restrict outCaps) {
  bool float32Blendable;
  bool float32Filterable;
  bool tier1;
  bool wideNorm;

  if (!outCaps) {
    return;
  }
  memset(outCaps, 0, sizeof(*outCaps));
  if (gpu_webgpuFormat(format) == WGPUTextureFormat_Undefined) {
    return;
  }

  if (webgpu_isBCFormat(format)) {
    outCaps->sampled = outCaps->filterable =
      webgpu_hasAdapterFeature(adapter, WGPUFeatureName_TextureCompressionBC);
    return;
  }
  if (webgpu_isETC2Format(format)) {
    outCaps->sampled = outCaps->filterable = webgpu_hasAdapterFeature(
      adapter,
      WGPUFeatureName_TextureCompressionETC2
    );
    return;
  }
  if (webgpu_isASTCFormat(format)) {
    outCaps->sampled = outCaps->filterable = webgpu_hasAdapterFeature(
      adapter,
      WGPUFeatureName_TextureCompressionASTC
    );
    return;
  }

  switch (format) {
    case GPU_FORMAT_DEPTH16_UNORM:
    case GPU_FORMAT_STENCIL8:
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
    case GPU_FORMAT_DEPTH32_FLOAT:
      outCaps->sampled      = true;
      outCaps->depthStencil = true;
      return;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      outCaps->sampled = outCaps->depthStencil = webgpu_hasAdapterFeature(
        adapter,
        WGPUFeatureName_Depth32FloatStencil8
      );
      return;
    case GPU_FORMAT_RGB9E5_UFLOAT:
      outCaps->sampled    = true;
      outCaps->filterable = true;
      return;
    default:
      break;
  }

  tier1 = webgpu_hasAdapterFeature(adapter,
                                   WGPUFeatureName_TextureFormatsTier1);
  wideNorm = webgpu_isWideNormFormat(format);
  if (wideNorm &&
      !tier1 &&
      !webgpu_hasAdapterFeature(adapter,
                                WGPUFeatureName_Unorm16TextureFormats)) {
    return;
  }

  float32Filterable = webgpu_hasAdapterFeature(
    adapter,
    WGPUFeatureName_Float32Filterable
  );
  float32Blendable = webgpu_hasAdapterFeature(
    adapter,
    WGPUFeatureName_Float32Blendable
  );
  outCaps->sampled = true;
  outCaps->filterable =
    gpuFormatNumericType(format) == GPU_FORMAT_NUMERIC_FLOAT &&
    !wideNorm &&
    (!webgpu_isFloat32Format(format) || float32Filterable);

  outCaps->colorAttachment = !wideNorm || tier1;
  if (format == GPU_FORMAT_R8_SNORM ||
      format == GPU_FORMAT_RG8_SNORM ||
      format == GPU_FORMAT_RGBA8_SNORM) {
    outCaps->colorAttachment = tier1;
  } else if (format == GPU_FORMAT_RG11B10_UFLOAT) {
    outCaps->colorAttachment = webgpu_hasAdapterFeature(
      adapter,
      WGPUFeatureName_RG11B10UfloatRenderable
    );
  }

  outCaps->blendable = outCaps->colorAttachment &&
                       gpuFormatNumericType(format) ==
                         GPU_FORMAT_NUMERIC_FLOAT &&
                       (!webgpu_isFloat32Format(format) ||
                        float32Blendable);
  outCaps->storage = !webgpu_isSRGBFormat(format) &&
                     (webgpu_hasCoreStorage(format) || tier1);
  if (format == GPU_FORMAT_BGRA8_UNORM) {
    outCaps->storage = webgpu_hasAdapterFeature(
      adapter,
      WGPUFeatureName_BGRA8UnormStorage
    );
  }
}

static bool
webgpu_supportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  GPUAdapterWebGPU *native;

  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      return true;
    case GPU_FEATURE_TIMESTAMPS:
#if defined(__EMSCRIPTEN__)
      return false;
#else
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_TimestampQuery);
#endif
    case GPU_FEATURE_SHADER_F16:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_ShaderF16);
    case GPU_FEATURE_SUBGROUPS:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_Subgroups);
    case GPU_FEATURE_INDIRECT_DRAW:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_IndirectFirstInstance);
    case GPU_FEATURE_MULTI_DRAW:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_IndirectFirstInstance) &&
             wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_MultiDrawIndirect);
    default:
      return false;
  }
}

static bool
webgpu_supportsSubgroupOperations(
  const GPUAdapter                 * __restrict adapter,
  GPUShaderStageFlags                           stage,
  GPUBackendSubgroupOperationFlags              operations) {
  const GPUShaderStageFlags supportedStages =
    GPU_SHADER_STAGE_FRAGMENT_BIT |
    GPU_SHADER_STAGE_COMPUTE_BIT;
  const GPUBackendSubgroupOperationFlags supportedOperations =
    GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;

  return webgpu_hasAdapterFeature(adapter, WGPUFeatureName_Subgroups) &&
         (supportedStages & stage) == stage &&
         (supportedOperations & operations) == operations;
}

static void
webgpu_getLimits(const GPUAdapter *adapter, GPULimits *limits) {
  GPUAdapterWebGPU *native;
  WGPUAdapterInfo   info = WGPU_ADAPTER_INFO_INIT;
  WGPULimits        webLimits = WGPU_LIMITS_INIT;

  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter || !limits) {
    return;
  }
  if (wgpuAdapterGetLimits(native->adapter, &webLimits) ==
      WGPUStatus_Success) {
    limits->maxBindGroups            = webLimits.maxBindGroups;
    limits->maxBindingsPerGroup      = webLimits.maxBindingsPerBindGroup;
    limits->maxDynamicUniformBuffers =
      webLimits.maxDynamicUniformBuffersPerPipelineLayout;
    limits->maxDynamicStorageBuffers =
      webLimits.maxDynamicStorageBuffersPerPipelineLayout;
    limits->minUniformBufferOffsetAlignment =
      webLimits.minUniformBufferOffsetAlignment;
    limits->minStorageBufferOffsetAlignment =
      webLimits.minStorageBufferOffsetAlignment;
    limits->maxColorAttachments      = webLimits.maxColorAttachments;
    limits->maxComputeWorkgroupSizeX = webLimits.maxComputeWorkgroupSizeX;
    limits->maxComputeWorkgroupSizeY = webLimits.maxComputeWorkgroupSizeY;
    limits->maxComputeWorkgroupSizeZ = webLimits.maxComputeWorkgroupSizeZ;
    limits->maxPushConstantSizeBytes = 256u;
  }
  if (wgpuAdapterGetInfo(native->adapter, &info) == WGPUStatus_Success) {
    limits->minSubgroupSize = info.subgroupMinSize;
    limits->maxSubgroupSize = info.subgroupMaxSize;
    wgpuAdapterInfoFreeMembers(info);
  }
}

static void
webgpu_deviceReady(WGPURequestDeviceStatus status,
                   WGPUDevice              nativeDevice,
                   WGPUStringView           message,
                   void                    *userData,
                   void                    *unused) {
  WebGPUDeviceRequest *request;
  GPUDeviceWebGPU     *native;
  GPUDevice           *device;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  request = userData;
  device  = request->device;
  native  = request->native;

  if (status == WGPURequestDeviceStatus_Success && nativeDevice) {
    if (device && native) {
      native->device = nativeDevice;
      native->queue  = wgpuDeviceGetQueue(nativeDevice);
      if (native->queue &&
          gpu_webgpuInitPushConstants(native) == GPU_OK) {
        native->queueHandle._priv   = native->queue;
        native->queueHandle._device = device;
        native->queueHandle.bits    = GPU_QUEUE_GRAPHICS_BIT |
                                      GPU_QUEUE_COMPUTE_BIT |
                                      GPU_QUEUE_TRANSFER_BIT;
        for (uint32_t i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
          native->commands[i].command._priv = &native->commands[i];
        }
        device->_priv         = native;
        device->queueFamilies = native->queueHandle.bits;
      } else {
        if (native->queue) {
          wgpuQueueRelease(native->queue);
          native->queue = NULL;
        }
        gpu_webgpuDestroyPushConstants(native);
        device = NULL;
      }
    }
  } else {
    device = NULL;
  }

  if (!device && nativeDevice) {
    wgpuDeviceRelease(nativeDevice);
  }
  if (!device) {
    free(request->native);
    free(request->device);
  } else {
    native->errorContext = request;
    request->ready       = true;
  }
  request->callback(device ? GPU_OK : GPU_ERROR_BACKEND_FAILURE,
                    device,
                    request->userData);
  if (!device) {
    free(request);
  }
}

static GPUResult
webgpu_requestDevice(GPUAdapter                     *adapter,
                     const GPUQueueCreateInfo        queueInfos[],
                     uint32_t                        queueInfoCount,
                     uint64_t                        enabledFeatureMask,
                     GPUBackendDeviceRequestCallback callback,
                     void                           *userData) {
  WGPURequestDeviceCallbackInfo callbackInfo =
    WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
  static const WGPUFeatureName formatFeatures[] = {
    WGPUFeatureName_Depth32FloatStencil8,
    WGPUFeatureName_TextureCompressionBC,
    WGPUFeatureName_TextureCompressionBCSliced3D,
    WGPUFeatureName_TextureCompressionETC2,
    WGPUFeatureName_TextureCompressionASTC,
    WGPUFeatureName_TextureCompressionASTCSliced3D,
    WGPUFeatureName_RG11B10UfloatRenderable,
    WGPUFeatureName_BGRA8UnormStorage,
    WGPUFeatureName_Float32Filterable,
    WGPUFeatureName_Float32Blendable,
    WGPUFeatureName_TextureFormatsTier1,
    WGPUFeatureName_TextureFormatsTier2,
    WGPUFeatureName_Unorm16TextureFormats
  };
  WGPUFeatureName       requiredFeatures[20];
  GPUAdapterWebGPU    *native;
  WebGPUDeviceRequest *request;
  uint64_t             supportedMask;

  _Static_assert(GPU_ARRAY_LEN(formatFeatures) + 5u <=
                   GPU_ARRAY_LEN(requiredFeatures),
                 "WebGPU device feature storage is too small");

  GPU__UNUSED(queueInfos);
  GPU__UNUSED(queueInfoCount);
  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  supportedMask = 1ull << GPU_FEATURE_COMPUTE;
#if !defined(__EMSCRIPTEN__)
  if (webgpu_supportsFeature(adapter, GPU_FEATURE_TIMESTAMPS)) {
    supportedMask |= 1ull << GPU_FEATURE_TIMESTAMPS;
  }
#endif
  if (wgpuAdapterHasFeature(native->adapter, WGPUFeatureName_ShaderF16)) {
    supportedMask |= 1ull << GPU_FEATURE_SHADER_F16;
  }
  if (wgpuAdapterHasFeature(native->adapter, WGPUFeatureName_Subgroups)) {
    supportedMask |= 1ull << GPU_FEATURE_SUBGROUPS;
  }
  if (wgpuAdapterHasFeature(native->adapter,
                            WGPUFeatureName_IndirectFirstInstance)) {
    supportedMask |= 1ull << GPU_FEATURE_INDIRECT_DRAW;
    if (wgpuAdapterHasFeature(native->adapter,
                              WGPUFeatureName_MultiDrawIndirect)) {
      supportedMask |= 1ull << GPU_FEATURE_MULTI_DRAW;
    }
  }
  if ((enabledFeatureMask & ~supportedMask) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  request = calloc(1, sizeof(*request));
  if (!request ||
      !(request->device = calloc(1, sizeof(*request->device))) ||
      !(request->native = calloc(1, sizeof(*request->native)))) {
    free(request ? request->native : NULL);
    free(request ? request->device : NULL);
    free(request);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  request->callback = callback;
  request->userData = userData;

  descriptor.label = gpu_webgpuString("gpu-webgpu-device");
#if !defined(__EMSCRIPTEN__)
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_TIMESTAMPS)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] =
      WGPUFeatureName_TimestampQuery;
  }
#endif
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SHADER_F16)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] =
      WGPUFeatureName_ShaderF16;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SUBGROUPS)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] =
      WGPUFeatureName_Subgroups;
  }
  if ((enabledFeatureMask &
       ((1ull << GPU_FEATURE_INDIRECT_DRAW) |
        (1ull << GPU_FEATURE_MULTI_DRAW))) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] =
      WGPUFeatureName_IndirectFirstInstance;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MULTI_DRAW)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] =
      WGPUFeatureName_MultiDrawIndirect;
  }
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(formatFeatures); i++) {
    if (wgpuAdapterHasFeature(native->adapter, formatFeatures[i])) {
      requiredFeatures[descriptor.requiredFeatureCount++] = formatFeatures[i];
    }
  }
  descriptor.requiredFeatures = descriptor.requiredFeatureCount
                                  ? requiredFeatures
                                  : NULL;
  descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  descriptor.deviceLostCallbackInfo.callback = webgpu_deviceLost;
  descriptor.deviceLostCallbackInfo.userdata1 = request;
  descriptor.uncapturedErrorCallbackInfo.callback = webgpu_uncapturedError;
  descriptor.uncapturedErrorCallbackInfo.userdata1 = request;
  callbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
  callbackInfo.callback  = webgpu_deviceReady;
  callbackInfo.userdata1 = request;
  wgpuAdapterRequestDevice(native->adapter, &descriptor, callbackInfo);
  return GPU_OK;
}

static void
webgpu_destroyDevice(GPUDevice *device) {
  GPUDeviceWebGPU *native;

  native = gpu_webgpuDevice(device);
  if (native) {
    WebGPUDeviceRequest *request;

    request = native->errorContext;
    if (request) {
      request->device = NULL;
      request->ready  = false;
    }
    if (native->queue) {
      wgpuQueueRelease(native->queue);
    }
    gpu_webgpuDestroyPushConstants(native);
    for (uint32_t i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
      if (native->commands[i].queryResolveScratch) {
        wgpuBufferDestroy(native->commands[i].queryResolveScratch);
        wgpuBufferRelease(native->commands[i].queryResolveScratch);
      }
    }
    if (native->device) {
      wgpuDeviceRelease(native->device);
    }
    free(request);
    free(native);
  }
  free(device);
}

void
webgpu_initDevice(GPUApiDevice *api) {
  api->requestAdapter              = webgpu_requestAdapter;
  api->destroyAdapter              = webgpu_destroyAdapter;
  api->getAdapterProperties        = webgpu_getAdapterProperties;
  api->supportsFeature             = webgpu_supportsFeature;
  api->supportsSubgroupOperations = webgpu_supportsSubgroupOperations;
  api->getLimits                   = webgpu_getLimits;
  api->getFormatCapabilities       = webgpu_getFormatCapabilities;
  api->requestDevice               = webgpu_requestDevice;
  api->destroyDevice               = webgpu_destroyDevice;
}
