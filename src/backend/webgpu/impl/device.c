/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
  DeviceWebGPU                    *native;
  void                            *userData;
  GPUQueueFlagBits                 queueBits;
  bool                             ready;
} WebGPUDeviceRequest;

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
#  if defined(_MSC_VER)
static __declspec(thread) WebGPUPipelineError    *webgpu_pipelineError;
#  else
static _Thread_local WebGPUPipelineError    *webgpu_pipelineError;
#  endif
#endif

static const WGPUFeatureName webgpu_optionalFeatures[] = {
  WGPUFeatureName_CoreFeaturesAndLimits,
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
  GPU_WEBGPU_FEATURE_NORM16,
  WGPUFeatureName_IndirectFirstInstance,
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  (WGPUFeatureName)WGPUNativeFeature_TextureAdapterSpecificFormatFeatures,
  (WGPUFeatureName)WGPUNativeFeature_VertexWritableStorage,
#endif
};

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
                       WGPUErrorType     type,
                       WGPUStringView    message,
                       void             *userData,
                       void             *unused) {
  char                 text[512];
  WebGPUDeviceRequest *request;
  GPUDeviceErrorType   errorType;
  GPUResult            result;

  GPU__UNUSED(nativeDevice);
  GPU__UNUSED(unused);
  request = userData;

  if (!request || !request->ready || !request->device
      || type == WGPUErrorType_NoError) {
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

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  /* native pipeline errors arrive inline; keep capture local to this call/thread. */

  if (webgpu_pipelineError && webgpu_pipelineError->device == request->device) {
    if (webgpu_pipelineError->result == GPU_OK) {
      webgpu_pipelineError->type   = errorType;
      webgpu_pipelineError->result = result;
      webgpu_copyString(webgpu_pipelineError->message, sizeof(webgpu_pipelineError->message), message);
    }

    return;
  }
#endif

  webgpu_copyString(text, sizeof(text), message);
  deviceReportError(request->device,
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
  char                 text[512];
  WebGPUDeviceRequest *request;
  GPUDeviceLostReason  lostReason;

  GPU__UNUSED(nativeDevice);
  GPU__UNUSED(unused);
  request = userData;

  if (!request || !request->ready || !request->device
      || reason == WGPUDeviceLostReason_Destroyed
      || reason == WGPUDeviceLostReason_CallbackCancelled) {
    return;
  }

  lostReason = reason == WGPUDeviceLostReason_FailedCreation
                 ? GPU_DEVICE_LOST_REASON_DRIVER_ERROR
                 : GPU_DEVICE_LOST_REASON_UNKNOWN;
  webgpu_copyString(text, sizeof(text), message);
  deviceReportError(request->device,
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
  WGPUAdapterInfo       info = WGPU_ADAPTER_INFO_INIT;
  WebGPUAdapterRequest *request;
  AdapterWebGPU        *native;
  GPUAdapter           *adapter;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  request = userData;
  adapter = NULL;
  native  = NULL;

  if (status == WGPURequestAdapterStatus_Success && nativeAdapter) {
    adapter = calloc(1, sizeof(*adapter));
    native  = calloc(1, sizeof(*native));

    if (adapter && native) {
      native->adapter            = nativeAdapter;
      adapter->_priv             = native;
      adapter->inst              = request->instance;
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
webgpu_requestAdapter(GPUInstance                     *instance,
                      GPUPowerPreference               powerPreference,
                      GPUBackendAdapterRequestCallback callback,
                      void                            *userData) {
  WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  WGPURequestAdapterOptions      options      = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
  InstanceWebGPU                *native;
  WebGPUAdapterRequest          *request;

  native = webgpuInstance(instance);

  if (!native || !native->instance || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(request = calloc(1, sizeof(*request)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  request->instance = instance;
  request->callback = callback;
  request->userData = userData;

  options.featureLevel = WGPUFeatureLevel_Core;

  switch (powerPreference) {
    case GPU_POWER_PREFERENCE_LOW_POWER:
      options.powerPreference = WGPUPowerPreference_LowPower;
      break;
    case GPU_POWER_PREFERENCE_HIGH_PERFORMANCE:
      options.powerPreference = WGPUPowerPreference_HighPerformance;
      break;
    case GPU_POWER_PREFERENCE_DEFAULT:
    default:
      options.powerPreference = WGPUPowerPreference_Undefined;
      break;
  }

  callbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
  callbackInfo.callback  = webgpu_adapterReady;
  callbackInfo.userdata1 = request;
  wgpuInstanceRequestAdapter(native->instance, &options, callbackInfo);

  return GPU_OK;
}

static void
webgpu_destroyAdapter(GPUAdapter *adapter) {
  AdapterWebGPU    *native;

  native = webgpuAdapter(adapter);

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
  WGPUAdapterInfo   info = WGPU_ADAPTER_INFO_INIT;
  AdapterWebGPU    *native;

  native = webgpuAdapter(adapter);

  if (!native || !native->adapter || !properties) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(properties, 0, sizeof(*properties));
  properties->backend = GPU_BACKEND_WEBGPU;
  properties->name    = native->name[0] ? native->name : "WebGPU adapter";
  properties->executionFlags = GPU_EXECUTION_GRAPHICS_BIT |
                               GPU_EXECUTION_COMPUTE_BIT;

  if (wgpuAdapterGetInfo(native->adapter, &info) == WGPUStatus_Success) {
    properties->type = webgpu_adapterType(info.adapterType);
    wgpuAdapterInfoFreeMembers(info);
  }

  return GPU_OK;
}

static bool
webgpu_isBCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_BC1_RGBA_UNORM
         && format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB;
}

static bool
webgpu_isETC2Format(GPUFormat format) {
  return format >= GPU_FORMAT_EAC_R11_UNORM
         && format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB;
}

static bool
webgpu_isASTCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_ASTC_4X4_UNORM
         && format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB;
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
webgpu_isUnorm16Format(GPUFormat format) {
  return format == GPU_FORMAT_R16_UNORM
         || format == GPU_FORMAT_RG16_UNORM
         || format == GPU_FORMAT_RGBA16_UNORM;
}

static bool
webgpu_isSRGBFormat(GPUFormat format) {
  return format == GPU_FORMAT_RGBA8_UNORM_SRGB
         || format == GPU_FORMAT_BGRA8_UNORM_SRGB;
}

static bool
webgpu_isFloat32Format(GPUFormat format) {
  return format == GPU_FORMAT_R32_FLOAT
         || format == GPU_FORMAT_RG32_FLOAT
         || format == GPU_FORMAT_RGBA32_FLOAT;
}

static bool
webgpu_supportsMultisampling(GPUFormat format, bool coreFeatures) {
  switch (format) {
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_RG8_UINT:
    case GPU_FORMAT_RG8_SINT:
    case GPU_FORMAT_R32_FLOAT:
    case GPU_FORMAT_RG16_UINT:
    case GPU_FORMAT_RG16_SINT:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RGB10A2_UINT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA16_FLOAT:
      return coreFeatures;
    case GPU_FORMAT_R32_UINT:
    case GPU_FORMAT_R32_SINT:
    case GPU_FORMAT_RG32_UINT:
    case GPU_FORMAT_RG32_SINT:
    case GPU_FORMAT_RG32_FLOAT:
    case GPU_FORMAT_RGBA32_UINT:
    case GPU_FORMAT_RGBA32_SINT:
    case GPU_FORMAT_RGBA32_FLOAT:
      return false;
    default:
      return true;
  }
}

static bool
webgpu_hasCoreStorage(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R32_UINT:
    case GPU_FORMAT_R32_SINT:
    case GPU_FORMAT_R32_FLOAT:
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_SNORM:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
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
webgpu_hasCoreFeaturesStorage(GPUFormat format) {
  return format == GPU_FORMAT_RG32_UINT
         || format == GPU_FORMAT_RG32_SINT
         || format == GPU_FORMAT_RG32_FLOAT;
}

static bool
webgpu_hasTier1Storage(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R8_UNORM:
    case GPU_FORMAT_R8_SNORM:
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_RG8_UNORM:
    case GPU_FORMAT_RG8_SNORM:
    case GPU_FORMAT_RG8_UINT:
    case GPU_FORMAT_RG8_SINT:
    case GPU_FORMAT_R16_UNORM:
    case GPU_FORMAT_R16_SNORM:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_R16_FLOAT:
    case GPU_FORMAT_RG16_UNORM:
    case GPU_FORMAT_RG16_SNORM:
    case GPU_FORMAT_RG16_UINT:
    case GPU_FORMAT_RG16_SINT:
    case GPU_FORMAT_RG16_FLOAT:
    case GPU_FORMAT_RGBA16_UNORM:
    case GPU_FORMAT_RGBA16_SNORM:
    case GPU_FORMAT_RGB10A2_UNORM:
    case GPU_FORMAT_RGB10A2_UINT:
    case GPU_FORMAT_RG11B10_UFLOAT:
      return true;
    default:
      return false;
  }
}

static bool
webgpu_hasAdapterFeature(const GPUAdapter *adapter, WGPUFeatureName feature) {
  AdapterWebGPU    *native;

  native = webgpuAdapter(adapter);

  return native && native->adapter
         && wgpuAdapterHasFeature(native->adapter, feature);
}

#if GPU_WEBGPU_PROVIDER_DAWN && defined(WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT)
static bool
webgpu_hasWGSLLanguageFeature(const GPUAdapter           *adapter,
                              WGPUWGSLLanguageFeatureName feature) {
  InstanceWebGPU    *native;

  native = adapter ? webgpuInstance(adapter->inst) : NULL;

  return native && native->instance
         && wgpuInstanceHasWGSLLanguageFeature(native->instance, feature);
}
#endif

static void
webgpu_getFormatCapabilities(const GPUAdapter      *__restrict adapter,
                             GPUFormat                         format,
                             GPUFormatCapabilities *__restrict outCaps) {
  bool float32Blendable;
  bool float32Filterable;
  bool coreFeatures;
  bool legacyUnorm16;
  bool norm16Filterable;
  bool tier1;
  bool wideNorm;

  if (!outCaps) {
    return;
  }

  memset(outCaps, 0, sizeof(*outCaps));

  if (webgpuFormat(format) == WGPUTextureFormat_Undefined) {
    return;
  }

  if (format == GPU_FORMAT_BGRA8_UNORM_SRGB
      && !webgpu_hasAdapterFeature(adapter,
                                   WGPUFeatureName_CoreFeaturesAndLimits)) {
    return;
  }

  if (webgpu_isBCFormat(format)) {
    outCaps->sampled = outCaps->filterable = webgpu_hasAdapterFeature(adapter, WGPUFeatureName_TextureCompressionBC);
    return;
  }

  if (webgpu_isETC2Format(format)) {
    outCaps->sampled = outCaps->filterable = webgpu_hasAdapterFeature(adapter,
                                                                      WGPUFeatureName_TextureCompressionETC2);
    return;
  }

  if (webgpu_isASTCFormat(format)) {
    outCaps->sampled = outCaps->filterable = webgpu_hasAdapterFeature(adapter,
                                                                      WGPUFeatureName_TextureCompressionASTC);
    return;
  }

  switch (format) {
    case GPU_FORMAT_DEPTH16_UNORM:
    case GPU_FORMAT_STENCIL8:
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
    case GPU_FORMAT_DEPTH32_FLOAT:
      outCaps->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT;
      outCaps->sampled               = true;
      outCaps->depthStencil          = true;
      return;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      outCaps->sampled = outCaps->depthStencil = webgpu_hasAdapterFeature(adapter,
                                                                          WGPUFeatureName_Depth32FloatStencil8);

      if (outCaps->depthStencil) {
        outCaps->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT;
      }

      return;
    case GPU_FORMAT_RGB9E5_UFLOAT:
      outCaps->sampled    = true;
      outCaps->filterable = true;
      return;
    default:
      break;
  }

  coreFeatures     = webgpu_hasAdapterFeature(adapter,
                                              WGPUFeatureName_CoreFeaturesAndLimits);
  tier1            = webgpu_hasAdapterFeature(adapter,
                                              WGPUFeatureName_TextureFormatsTier1);
  wideNorm         = webgpu_isWideNormFormat(format);
  norm16Filterable = webgpu_hasAdapterFeature(adapter,
                                              GPU_WEBGPU_FEATURE_NORM16);
  legacyUnorm16    =
    !tier1
    && webgpu_isUnorm16Format(format)
    && norm16Filterable;

  if (wideNorm && !tier1 && !legacyUnorm16) {
    return;
  }

  float32Filterable   = webgpu_hasAdapterFeature(adapter,
                                                 WGPUFeatureName_Float32Filterable);
  float32Blendable    = webgpu_hasAdapterFeature(adapter,
                                                 WGPUFeatureName_Float32Blendable);
  outCaps->sampled    = true;
  outCaps->filterable =
    formatNumericType(format) == GPU_FORMAT_NUMERIC_FLOAT
    && (!wideNorm || norm16Filterable)
    && (!webgpu_isFloat32Format(format) || float32Filterable);

  outCaps->colorAttachment = !wideNorm || tier1 || legacyUnorm16;

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
  if (legacyUnorm16
      && !webgpu_hasAdapterFeature(adapter,
                                    (WGPUFeatureName)WGPUNativeFeature_TextureAdapterSpecificFormatFeatures)) {
    outCaps->colorAttachment = false;
  }
#endif

  if (format == GPU_FORMAT_R8_SNORM
      || format == GPU_FORMAT_RG8_SNORM
      || format == GPU_FORMAT_RGBA8_SNORM) {
    outCaps->colorAttachment = tier1;
  } else if (format == GPU_FORMAT_RG11B10_UFLOAT) {
    outCaps->colorAttachment = webgpu_hasAdapterFeature(adapter,
                                                        WGPUFeatureName_RG11B10UfloatRenderable);
  }

  outCaps->blendable = outCaps->colorAttachment
                       && formatNumericType(format) == GPU_FORMAT_NUMERIC_FLOAT
                       && (!wideNorm || tier1)
                       && (!webgpu_isFloat32Format(format)
                           || float32Blendable);
  outCaps->storage   =
    !webgpu_isSRGBFormat(format)
    && (webgpu_hasCoreStorage(format)
        || (coreFeatures && webgpu_hasCoreFeaturesStorage(format))
        || (tier1 && webgpu_hasTier1Storage(format)));

  if (format == GPU_FORMAT_BGRA8_UNORM) {
    outCaps->storage = webgpu_hasAdapterFeature(adapter,
                                                WGPUFeatureName_BGRA8UnormStorage);
  }

  if (outCaps->colorAttachment) {
    outCaps->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT;

    if (webgpu_supportsMultisampling(format, coreFeatures)) {
      outCaps->supportedSampleCounts |= GPU_SAMPLE_COUNT_4_BIT;
    }
  }
}

static bool
webgpu_supportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  AdapterWebGPU    *native;

  native = webgpuAdapter(adapter);

  if (!native || !native->adapter) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      return true;
    case GPU_FEATURE_TIMESTAMPS:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_TimestampQuery);
    case GPU_FEATURE_SHADER_F16:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_ShaderF16);
    case GPU_FEATURE_SUBGROUPS:
      return wgpuAdapterHasFeature(native->adapter,
                                   WGPUFeatureName_Subgroups);
    case GPU_FEATURE_INDIRECT_DRAW:
      return true;
    case GPU_FEATURE_MULTI_DRAW:
      return wgpuAdapterHasFeature(native->adapter,
                                   GPU_WEBGPU_FEATURE_MULTI_DRAW);
    case GPU_FEATURE_DESCRIPTOR_INDEXING:
      return true;
    default:
      return false;
  }
}

static bool
webgpu_supportsSubgroupOperations(const GPUAdapter     *__restrict adapter,
                                  GPUShaderStageFlags              stage,
                                  BackendSubgroupOperationFlags    operations) {
  const GPUShaderStageFlags              supportedStages     = GPU_SHADER_STAGE_FRAGMENT_BIT |
                                                               GPU_SHADER_STAGE_COMPUTE_BIT;
  const BackendSubgroupOperationFlags    supportedOperations = GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT |
                                                               GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT |
                                                               GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;

  return webgpu_hasAdapterFeature(adapter, WGPUFeatureName_Subgroups)
         && (supportedStages & stage) == stage
         && (supportedOperations & operations) == operations;
}

static void
webgpu_getLimits(const GPUAdapter *adapter, GPULimits *limits) {
  WGPUAdapterInfo   info      = WGPU_ADAPTER_INFO_INIT;
  WGPULimits        webLimits = WGPU_LIMITS_INIT;
  AdapterWebGPU    *native;

  native = webgpuAdapter(adapter);

  if (!native || !native->adapter || !limits) {
    return;
  }

  if (wgpuAdapterGetLimits(native->adapter, &webLimits) == WGPUStatus_Success) {
    limits->maxBindGroups                   = webLimits.maxBindGroups;
    limits->maxBindingsPerGroup             = webLimits.maxBindingsPerBindGroup;
    limits->maxDynamicUniformBuffers        = webLimits.maxDynamicUniformBuffersPerPipelineLayout;
    limits->maxDynamicStorageBuffers        = webLimits.maxDynamicStorageBuffersPerPipelineLayout;
    limits->minUniformBufferOffsetAlignment = webLimits.minUniformBufferOffsetAlignment;
    limits->minStorageBufferOffsetAlignment = webLimits.minStorageBufferOffsetAlignment;
    limits->maxColorAttachments             = webLimits.maxColorAttachments;
    limits->maxComputeWorkgroupSizeX        = webLimits.maxComputeWorkgroupSizeX;
    limits->maxComputeWorkgroupSizeY        = webLimits.maxComputeWorkgroupSizeY;
    limits->maxComputeWorkgroupSizeZ        = webLimits.maxComputeWorkgroupSizeZ;
    limits->maxPushConstantSizeBytes        = 256u;
    limits->maxSamplerAnisotropy            = 16u;
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
                   WGPUStringView          message,
                   void                   *userData,
                   void                   *unused) {
  WebGPUDeviceRequest *request;
  DeviceWebGPU        *native;
  GPUDevice           *device;
  uint32_t             i;
  bool                 usable;

  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  request = userData;
  device  = request->device;
  native  = request->native;

  if (status == WGPURequestDeviceStatus_Success && nativeDevice) {
    if (device && native) {
      native->device = nativeDevice;
      native->queue  = wgpuDeviceGetQueue(nativeDevice);
      usable         = native->queue
                       && webgpuInitPushConstants(native) == GPU_OK;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
      if (usable) {
        usable = webgpuStartCompletionWorker(native);
      }
#endif
      if (usable) {
        native->queueHandle._priv   = native->queue;
        native->queueHandle._device = device;
        native->queueHandle.bits    = request->queueBits;

        for (i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
          native->commands[i].command._priv = &native->commands[i];
        }

        device->_priv         = native;
        device->queueFamilies = native->queueHandle.bits;
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
        device->uslStorageExtAccess = wgpuDeviceHasFeature(nativeDevice,
                                                           (WGPUFeatureName)WGPUNativeFeature_TextureAdapterSpecificFormatFeatures);
#endif
      } else {
        if (native->queue) {
          wgpuQueueRelease(native->queue);
          native->queue = NULL;
        }

        webgpuDestroyPushConstants(native);
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
                     const QueueCreateInfo           queueInfos[],
                     uint32_t                        queueInfoCount,
                     uint64_t                        enabledFeatureMask,
                     GPUBackendDeviceRequestCallback callback,
                     void                           *userData) {
  WGPURequestDeviceCallbackInfo callbackInfo   = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  WGPUDeviceDescriptor          descriptor     = WGPU_DEVICE_DESCRIPTOR_INIT;
  WGPULimits                    requiredLimits = WGPU_LIMITS_INIT;
  WGPUFeatureName               requiredFeatures[22];
  AdapterWebGPU                *native;
  WebGPUDeviceRequest          *request;
  uint64_t                      supportedMask;
  GPUQueueFlagBits              queueBits;
  uint32_t                      graphicsCount;
  uint32_t                      computeCount;
  uint32_t                      transferCount;
  uint32_t                      queueIndex;
  uint32_t                      featureIndex;

  _Static_assert(GPU_ARRAY_LEN(webgpu_optionalFeatures) + 4u <=
                   GPU_ARRAY_LEN(requiredFeatures),
                 "WebGPU device feature storage is too small");

  native = webgpuAdapter(adapter);

  if (!native || !native->adapter || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((!queueInfos && queueInfoCount != 0u)
      || (queueInfos && queueInfoCount == 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  queueBits     = GPU_QUEUE_GRAPHICS_BIT | GPU_QUEUE_COMPUTE_BIT;
  graphicsCount = 1u;
  computeCount  = 1u;
  transferCount = 0u;

  if (queueInfos) {
    queueBits     = 0u;
    graphicsCount = 0u;
    computeCount  = 0u;

    for (queueIndex = 0u; queueIndex < queueInfoCount; queueIndex++) {
      queueBits |= queueInfos[queueIndex].flags;

      if ((queueInfos[queueIndex].flags & GPU_QUEUE_GRAPHICS_BIT) != 0u) {
        graphicsCount += queueInfos[queueIndex].count;
      }

      if ((queueInfos[queueIndex].flags & GPU_QUEUE_COMPUTE_BIT) != 0u) {
        computeCount += queueInfos[queueIndex].count;
      }

      if ((queueInfos[queueIndex].flags & GPU_QUEUE_TRANSFER_BIT) != 0u) {
        transferCount += queueInfos[queueIndex].count;
      }
    }
  }

  if (graphicsCount > 1u || computeCount > 1u || transferCount > 1u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (wgpuAdapterGetLimits(native->adapter, &requiredLimits) != WGPUStatus_Success) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  supportedMask = (1ull << GPU_FEATURE_COMPUTE) |
                  (1ull << GPU_FEATURE_INDIRECT_DRAW) |
                  (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING);

  if (webgpu_supportsFeature(adapter, GPU_FEATURE_TIMESTAMPS)) {
    supportedMask |= 1ull << GPU_FEATURE_TIMESTAMPS;
  }

  if (wgpuAdapterHasFeature(native->adapter, WGPUFeatureName_ShaderF16)) {
    supportedMask |= 1ull << GPU_FEATURE_SHADER_F16;
  }

  if (wgpuAdapterHasFeature(native->adapter, WGPUFeatureName_Subgroups)) {
    supportedMask |= 1ull << GPU_FEATURE_SUBGROUPS;
  }

  if (wgpuAdapterHasFeature(native->adapter,
                            GPU_WEBGPU_FEATURE_MULTI_DRAW)) {
    supportedMask |= 1ull << GPU_FEATURE_MULTI_DRAW;
  }

  if ((enabledFeatureMask & ~supportedMask) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!(request = calloc(1, sizeof(*request)))
      || !(request->device = calloc(1, sizeof(*request->device)))
      || !(request->native = calloc(1, sizeof(*request->native)))) {
    free(request ? request->native : NULL);
    free(request ? request->device : NULL);
    free(request);
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  request->callback       = callback;
  request->userData       = userData;
  request->queueBits      = queueBits;
  request->native->limits = requiredLimits;
#if GPU_WEBGPU_PROVIDER_DAWN && defined(WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT)
  request->device->uslStorageExtAccess  =
    wgpuAdapterHasFeature(native->adapter,
                          WGPUFeatureName_TextureFormatsTier2)
    && webgpu_hasWGSLLanguageFeature(adapter,
                                     WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures);
  request->device->uslStorageExtFormats =
    wgpuAdapterHasFeature(native->adapter,
                          WGPUFeatureName_TextureFormatsTier1)
    && webgpu_hasWGSLLanguageFeature(adapter,
                                     WGPUWGSLLanguageFeatureName_TextureFormatsTier1);
#endif

  descriptor.label = webgpuString("gpu-webgpu-device");

  if ((enabledFeatureMask & (1ull << GPU_FEATURE_TIMESTAMPS)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] = WGPUFeatureName_TimestampQuery;
  }

  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SHADER_F16)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] = WGPUFeatureName_ShaderF16;
  }

  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SUBGROUPS)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] = WGPUFeatureName_Subgroups;
  }

  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MULTI_DRAW)) != 0u) {
    requiredFeatures[descriptor.requiredFeatureCount++] = GPU_WEBGPU_FEATURE_MULTI_DRAW;
  }

  for (featureIndex = 0u; featureIndex < GPU_ARRAY_LEN(webgpu_optionalFeatures); featureIndex++) {
    if (wgpuAdapterHasFeature(native->adapter, webgpu_optionalFeatures[featureIndex])) {
      requiredFeatures[descriptor.requiredFeatureCount++] = webgpu_optionalFeatures[featureIndex];
    }
  }

  descriptor.requiredFeatures = descriptor.requiredFeatureCount
                                  ? requiredFeatures
                                  : NULL;
  descriptor.requiredLimits = &requiredLimits;
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
  DeviceWebGPU        *native;
  WebGPUDeviceRequest *request;
  uint32_t             i;

  native = webgpuDevice(device);

  if (native) {
    request = native->errorContext;

    if (request) {
      request->device = NULL;
      request->ready  = false;
    }

    if (native->queue) {
#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
      webgpuStopCompletionWorker(native);
#endif
      wgpuQueueRelease(native->queue);
    }

    webgpuDestroyPushConstants(native);

    for (i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
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

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
GPU_HIDE
void
webgpuBeginPipelineError(GPUDevice              *device,
                            WebGPUPipelineError    *error) {
  error->previous      = webgpu_pipelineError;
  error->device        = device;
  error->result        = GPU_OK;
  webgpu_pipelineError = error;
}

GPU_HIDE
GPUResult
webgpuEndPipelineError(WebGPUPipelineError    *error) {
  webgpu_pipelineError = error->previous;

  /* report after the native call releases its error-sink lock; callbacks may reenter. */

  if (error->result != GPU_OK) {
    deviceReportError(error->device,
                      error->type,
                      GPU_DEVICE_LOST_REASON_UNKNOWN,
                      error->result,
                      error->message[0] ? error->message : "WebGPU pipeline creation failed");
  }

  return error->result;
}
#endif

void
webgpu_initDevice(ApiDevice    *api) {
  api->requestAdapter             = webgpu_requestAdapter;
  api->destroyAdapter             = webgpu_destroyAdapter;
  api->getAdapterProperties       = webgpu_getAdapterProperties;
  api->supportsFeature            = webgpu_supportsFeature;
  api->supportsSubgroupOperations = webgpu_supportsSubgroupOperations;
  api->getLimits                  = webgpu_getLimits;
  api->getFormatCapabilities      = webgpu_getFormatCapabilities;
  api->requestDevice              = webgpu_requestDevice;
  api->destroyDevice              = webgpu_destroyDevice;
}
