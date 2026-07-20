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
webgpu_supportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  GPU__UNUSED(adapter);
  GPU__UNUSED(feature);
  return false;
}

static void
webgpu_getLimits(const GPUAdapter *adapter, GPULimits *limits) {
  GPUAdapterWebGPU *native;
  WGPULimits        webLimits = WGPU_LIMITS_INIT;

  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter || !limits ||
      wgpuAdapterGetLimits(native->adapter, &webLimits) != WGPUStatus_Success) {
    return;
  }

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
      if (native->queue) {
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
  GPUAdapterWebGPU    *native;
  WebGPUDeviceRequest *request;

  GPU__UNUSED(queueInfos);
  GPU__UNUSED(queueInfoCount);
  if (enabledFeatureMask != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  native = gpu_webgpuAdapter(adapter);
  if (!native || !native->adapter || !callback) {
    return GPU_ERROR_INVALID_ARGUMENT;
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
  api->requestAdapter       = webgpu_requestAdapter;
  api->destroyAdapter       = webgpu_destroyAdapter;
  api->getAdapterProperties = webgpu_getAdapterProperties;
  api->supportsFeature      = webgpu_supportsFeature;
  api->getLimits            = webgpu_getLimits;
  api->requestDevice        = webgpu_requestDevice;
  api->destroyDevice        = webgpu_destroyDevice;
}
