/*
 * Copyright (C) 2020 Recep Aslantas
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
#include "device_internal.h"

static const char*
gpu_backendName(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return "Metal";
    case GPU_BACKEND_VULKAN:
      return "Vulkan";
    case GPU_BACKEND_DIRECTX12:
      return "Direct3D 12";
    case GPU_BACKEND_OPENGL:
      return "OpenGL";
    case GPU_BACKEND_NULL:
    default:
      return "Unknown GPU";
  }
}

static bool
gpu_validQueueCreateInfos(const GPUCommandQueueCreateInfo queCI[],
                          uint32_t                        nQueCI) {
  if (!queCI) {
    return nQueCI == 0;
  }
  if (nQueCI == 0) {
    return false;
  }

  for (uint32_t i = 0; i < nQueCI; i++) {
    if (queCI[i].flags == 0 || queCI[i].count == 0) {
      return false;
    }
  }

  return true;
}

static bool
gpu_validQueueRequestType(GPUQueueFlagBits type) {
  return type == GPU_QUEUE_GRAPHICS ||
         type == GPU_QUEUE_COMPUTE ||
         type == GPU_QUEUE_TRANSFER;
}

static bool
gpu_validFeatureSet(const GPUFeatureSet *set) {
  return set->featureCount == 0 || set->pFeatures;
}

static bool
gpu_validValidationMode(GPUValidationMode mode) {
  return mode == GPU_VALIDATION_OFF ||
         mode == GPU_VALIDATION_BASIC ||
         mode == GPU_VALIDATION_FULL;
}

static bool
gpu_knownFeature(GPUFeature feature) {
  return feature >= GPU_FEATURE_COMPUTE &&
         feature <= GPU_FEATURE_VARIABLE_RATE_SHADING;
}

static bool
gpu_builtinSupportedFeature(GPUFeature feature) {
  return feature == GPU_FEATURE_COMPUTE;
}

static bool
gpu_adapterSupportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  GPUApi *api;

  if (!gpu_knownFeature(feature)) {
    return false;
  }
  if ((api = gpuActiveGPUApi()) && api->device.supportsFeature) {
    return api->device.supportsFeature(adapter, feature);
  }

  return gpu_builtinSupportedFeature(feature);
}

static uint64_t
gpu_featureBit(GPUFeature feature) {
  return 1ull << (uint32_t)feature;
}

static uint64_t
gpu_collectEnabledFeatures(const GPUAdapter *adapter, const GPUFeatureSet *set) {
  uint64_t mask;

  mask = 0;
  if (!set || !gpu_validFeatureSet(set)) {
    return 0;
  }

  for (uint32_t i = 0; i < set->featureCount; i++) {
    if (gpu_knownFeature(set->pFeatures[i]) &&
        gpu_adapterSupportsFeature(adapter, set->pFeatures[i])) {
      mask |= gpu_featureBit(set->pFeatures[i]);
    }
  }

  return mask;
}

static uint64_t
gpu_defaultEnabledFeatureMask(void) {
  return gpu_featureBit(GPU_FEATURE_COMPUTE);
}

static uint64_t
gpu_enabledFeatureMaskForCreateInfo(const GPUAdapter *adapter,
                                    const GPUDeviceCreateInfo *info) {
  uint64_t mask;

  if (!info) {
    return gpu_defaultEnabledFeatureMask();
  }

  mask = gpu_collectEnabledFeatures(adapter, &info->required);
  mask |= gpu_collectEnabledFeatures(adapter, &info->optional);
  return mask;
}

static GPUResult
gpu_validateFeatureSet(const GPUAdapter *adapter,
                       const GPUFeatureSet *set,
                       bool required) {
  if (!gpu_validFeatureSet(set)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t i = 0; i < set->featureCount; i++) {
    if (!gpu_knownFeature(set->pFeatures[i])) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (required && !gpu_adapterSupportsFeature(adapter, set->pFeatures[i])) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  return GPU_OK;
}

static void
gpu_fillDefaultLimits(GPULimits *limits) {
  memset(limits, 0, sizeof(*limits));

  limits->maxBindGroups = 4;
  limits->maxBindingsPerGroup = 64;
  limits->maxDynamicUniformBuffers = 8;
  limits->maxDynamicStorageBuffers = 4;
  limits->minUniformBufferOffsetAlignment = 256;
  limits->minStorageBufferOffsetAlignment = 256;
  limits->maxColorAttachments = 4;
  limits->maxComputeWorkgroupSizeX = 1024;
  limits->maxComputeWorkgroupSizeY = 1024;
  limits->maxComputeWorkgroupSizeZ = 64;
}

static bool
gpu_u64MulOverflow(uint64_t a, uint64_t b, uint64_t *out) {
  if (a != 0 && b > UINT64_MAX / a) {
    return true;
  }

  *out = a * b;
  return false;
}

static bool
gpu_u64AddOverflow(uint64_t a, uint64_t b, uint64_t *out) {
  if (b > UINT64_MAX - a) {
    return true;
  }

  *out = a + b;
  return false;
}

static bool
gpu_isPowerOfTwo(uint64_t value) {
  return value != 0 && (value & (value - 1u)) == 0;
}

static bool
gpu_alignUp(uint64_t value, uint64_t alignment, uint64_t *out) {
  uint64_t mask;
  uint64_t biased;

  if (!gpu_isPowerOfTwo(alignment)) {
    return false;
  }

  mask = alignment - 1u;
  if (gpu_u64AddOverflow(value, mask, &biased)) {
    return false;
  }

  *out = biased & ~mask;
  return true;
}

static GPUBufferUsageFlags
gpu_knownTransientBufferUsageMask(void) {
  return GPU_BUFFER_USAGE_VERTEX |
         GPU_BUFFER_USAGE_INDEX |
         GPU_BUFFER_USAGE_UNIFORM |
         GPU_BUFFER_USAGE_STORAGE |
         GPU_BUFFER_USAGE_COPY_SRC |
         GPU_BUFFER_USAGE_COPY_DST |
         GPU_BUFFER_USAGE_INDIRECT;
}

static bool
gpu_validTransientBufferUsage(GPUBufferUsageFlags usage) {
  return usage != 0u &&
         (usage & ~gpu_knownTransientBufferUsageMask()) == 0u;
}

static void*
gpu_bufferContents(GPUBuffer *buffer) {
  GPUApi *api;

  if (!buffer) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()) || !api->buf.contents) {
    return NULL;
  }

  return api->buf.contents(buffer);
}

static void
gpu_destroyTransientChunks(GPUDevice *device) {
  GPUTransientChunk *chunk;

  if (!device) {
    return;
  }

  chunk = device->transientChunks;
  while (chunk) {
    GPUTransientChunk *next = chunk->next;

    GPUDestroyBuffer(chunk->buffer);
    free(chunk);
    chunk = next;
  }

  device->transientChunks = NULL;
}

static void
gpu_destroyTransientAllocator(GPUDevice *device) {
  if (!device) {
    return;
  }

  gpu_destroyTransientChunks(device);
  GPUDestroyBuffer(device->transientBuffer);
  device->transientBuffer = NULL;
  device->transientCpuPtr = NULL;
  device->transientFrameOffset = 0;
  device->transientFrameIndex = 0;
  device->transientConfigured = false;
  device->transientFrameBegun = false;
  memset(&device->transientConfig, 0, sizeof(device->transientConfig));
  memset(&device->allocatorStats, 0, sizeof(device->allocatorStats));
}

static GPUResult
gpu_createTransientBuffer(GPUDevice *device,
                          GPUBufferUsageFlags usage,
                          uint64_t sizeBytes,
                          GPUBuffer **outBuffer,
                          void **outCpuPtr) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer *buffer;
  void *cpuPtr;
  GPUResult result;

  if (!device || !outBuffer || !outCpuPtr || sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuActiveGPUApi() || !gpuActiveGPUApi()->buf.contents) {
    return GPU_ERROR_UNSUPPORTED;
  }

  info.chain.sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label = "transient-buffer";
  info.sizeBytes = sizeBytes;
  info.usage = usage;

  buffer = NULL;
  result = GPUCreateBuffer(device, &info, &buffer);
  if (result != GPU_OK) {
    return result;
  }

  cpuPtr = gpu_bufferContents(buffer);
  if (!cpuPtr) {
    GPUDestroyBuffer(buffer);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outBuffer = buffer;
  *outCpuPtr = cpuPtr;
  return GPU_OK;
}

static bool
gpu_validRuntimeConfig(const GPURuntimeConfig *config) {
  if (!config) {
    return false;
  }
  if (config->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      config->chain.sType != GPU_STRUCTURE_TYPE_RUNTIME_CONFIG) {
    return false;
  }
  if (config->chain.structSize != 0 &&
      config->chain.structSize < sizeof(*config)) {
    return false;
  }

  return gpu_validValidationMode(config->validationMode);
}

static bool
gpu_validTransientAllocatorConfig(const GPUTransientAllocatorConfig *config,
                                  uint64_t *outCapacityBytes) {
  if (!config || !outCapacityBytes) {
    return false;
  }
  if (config->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      config->chain.sType != GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG) {
    return false;
  }
  if (config->chain.structSize != 0 &&
      config->chain.structSize < sizeof(*config)) {
    return false;
  }
  if (config->ringBytesPerFrame == 0u || config->framesInFlight == 0u) {
    return false;
  }
  if (config->allowChunkFallback && config->chunkBytes == 0u) {
    return false;
  }

  return !gpu_u64MulOverflow(config->ringBytesPerFrame,
                             config->framesInFlight,
                             outCapacityBytes);
}

static GPUResult
gpu_allocateTransientChunk(GPUDevice *device,
                           GPUBufferUsageFlags usage,
                           uint64_t sizeBytes,
                           GPUTransientBufferSlice *outSlice) {
  GPUTransientChunk *chunk;
  GPUBuffer *buffer;
  void *cpuPtr;
  uint64_t chunkBytes;
  GPUResult result;

  if (!device->transientConfig.allowChunkFallback) {
    device->allocatorStats.uploadStallCount++;
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  chunkBytes = device->transientConfig.chunkBytes;
  if (chunkBytes < sizeBytes) {
    chunkBytes = sizeBytes;
  }

  chunk = calloc(1, sizeof(*chunk));
  if (!chunk) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  buffer = NULL;
  cpuPtr = NULL;
  result = gpu_createTransientBuffer(device, usage, chunkBytes, &buffer, &cpuPtr);
  if (result != GPU_OK) {
    free(chunk);
    return result;
  }

  chunk->buffer = buffer;
  chunk->cpuPtr = cpuPtr;
  chunk->sizeBytes = chunkBytes;
  chunk->next = device->transientChunks;
  device->transientChunks = chunk;

  device->allocatorStats.uploadStallCount++;
  device->currentFrameStats.hotPathAllocCount++;
  device->currentFrameStats.hotPathAllocBytes += chunkBytes;

  outSlice->buffer = buffer;
  outSlice->offset = 0;
  outSlice->sizeBytes = sizeBytes;
  outSlice->cpuPtr = cpuPtr;
  return GPU_OK;
}

static bool
gpu_formatIsDepthStencil(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
    case GPU_FORMAT_DEPTH32_FLOAT:
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return true;
    default:
      return false;
  }
}

static bool
gpu_formatIsInteger(GPUFormat format) {
  switch (format) {
    case GPUPixelFormatR8Uint:
    case GPUPixelFormatR8Sint:
    case GPUPixelFormatR16Uint:
    case GPUPixelFormatR16Sint:
    case GPUPixelFormatRG8Uint:
    case GPUPixelFormatRG8Sint:
    case GPUPixelFormatR32Uint:
    case GPUPixelFormatR32Sint:
    case GPUPixelFormatRG16Uint:
    case GPUPixelFormatRG16Sint:
    case GPUPixelFormatRGBA8Uint:
    case GPUPixelFormatRGBA8Sint:
    case GPUPixelFormatRGB10A2Uint:
    case GPUPixelFormatRG32Uint:
    case GPUPixelFormatRG32Sint:
    case GPUPixelFormatRGBA16Uint:
    case GPUPixelFormatRGBA16Sint:
    case GPUPixelFormatRGBA32Uint:
    case GPUPixelFormatRGBA32Sint:
      return true;
    default:
      return false;
  }
}

static bool
gpu_formatIsKnownColor(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UNORM_SRGB:
    case GPU_FORMAT_BGRA8_UNORM:
    case GPU_FORMAT_BGRA8_UNORM_SRGB:
    case GPU_FORMAT_RGBA16_FLOAT:
    case GPU_FORMAT_RGBA32_FLOAT:
    case GPU_FORMAT_RG11B10_UFLOAT:
    case GPUPixelFormatR8Unorm:
    case GPUPixelFormatR8Unorm_sRGB:
    case GPUPixelFormatR8Snorm:
    case GPUPixelFormatR8Uint:
    case GPUPixelFormatR8Sint:
    case GPUPixelFormatR16Unorm:
    case GPUPixelFormatR16Snorm:
    case GPUPixelFormatR16Uint:
    case GPUPixelFormatR16Sint:
    case GPUPixelFormatR16Float:
    case GPUPixelFormatRG8Unorm:
    case GPUPixelFormatRG8Unorm_sRGB:
    case GPUPixelFormatRG8Snorm:
    case GPUPixelFormatRG8Uint:
    case GPUPixelFormatRG8Sint:
    case GPUPixelFormatR32Uint:
    case GPUPixelFormatR32Sint:
    case GPUPixelFormatR32Float:
    case GPUPixelFormatRG16Unorm:
    case GPUPixelFormatRG16Snorm:
    case GPUPixelFormatRG16Uint:
    case GPUPixelFormatRG16Sint:
    case GPUPixelFormatRG16Float:
    case GPUPixelFormatRGBA8Snorm:
    case GPUPixelFormatRGBA8Uint:
    case GPUPixelFormatRGBA8Sint:
    case GPUPixelFormatBGRX8Unorm:
    case GPUPixelFormatRGB10A2Unorm:
    case GPUPixelFormatRGB10A2Uint:
    case GPUPixelFormatRG32Uint:
    case GPUPixelFormatRG32Sint:
    case GPUPixelFormatRG32Float:
    case GPUPixelFormatRGBA16Unorm:
    case GPUPixelFormatRGBA16Snorm:
    case GPUPixelFormatRGBA16Uint:
    case GPUPixelFormatRGBA16Sint:
    case GPUPixelFormatRGBA32Uint:
    case GPUPixelFormatRGBA32Sint:
      return true;
    default:
      return false;
  }
}

static GPUResult
gpu_buildQueueCreateInfos(const GPUDeviceCreateInfo  *info,
                          GPUCommandQueueCreateInfo  *stackInfos,
                          uint32_t                    stackInfoCount,
                          GPUCommandQueueCreateInfo **outInfos,
                          uint32_t                   *outInfoCount) {
  const GPUDeviceQueueCreateInfo *queueInfo;
  GPUCommandQueueCreateInfo *infos;
  uint32_t requestCount;

  *outInfos = NULL;
  *outInfoCount = 0;

  if (!info) {
    return GPU_OK;
  }
  if (info->queues.chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->queues.chain.sType != GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->queues.chain.structSize != 0 &&
      info->queues.chain.structSize < sizeof(info->queues)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  queueInfo = &info->queues;
  requestCount = queueInfo->requestCount;
  if (requestCount == 0) {
    return queueInfo->pRequests ? GPU_ERROR_INVALID_ARGUMENT : GPU_OK;
  }
  if (!queueInfo->pRequests) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  infos = stackInfos;
  if (requestCount > stackInfoCount) {
    infos = calloc(requestCount, sizeof(*infos));
    if (!infos) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }

  for (uint32_t i = 0; i < requestCount; i++) {
    if (!gpu_validQueueRequestType(queueInfo->pRequests[i].type) ||
        queueInfo->pRequests[i].count == 0) {
      if (infos != stackInfos) {
        free(infos);
      }
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    infos[i].flags = queueInfo->pRequests[i].type;
    infos[i].count = queueInfo->pRequests[i].count;
  }

  *outInfos = infos;
  *outInfoCount = requestCount;
  return GPU_OK;
}

static GPUAdapter*
gpu_getAvailableAdapters(GPUInstance *inst, uint32_t maxNumberOfItems) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.getAvailableAdapters(inst, maxNumberOfItems);
}

GPU_EXPORT
GPUResult
GPUEnumerateAdapters(GPUInstance *inst,
                     uint32_t    *inoutAdapterCount,
                     GPUAdapter **outAdapters) {
  GPUAdapter *deviceList;
  GPUAdapter *item;
  uint32_t capacity;
  uint32_t count;

  if (!inoutAdapterCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity = *inoutAdapterCount;
  deviceList = gpu_getAvailableAdapters(inst, UINT32_MAX);
  count = 0;

  for (item = deviceList; item; item = item->next) {
    if (outAdapters && count < capacity) {
      outAdapters[count] = item;
    }
    count++;
  }

  *inoutAdapterCount = count;
  if (outAdapters && capacity < count) {
    return GPU_ERROR_INSUFFICIENT_CAPACITY;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAdapterProperties(const GPUAdapter     *adapter,
                        GPUAdapterProperties *outProps) {
  GPUApi *api;
  GPUBackend backend;

  if (!adapter || !outProps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outProps, 0, sizeof(*outProps));
  api = gpuActiveGPUApi();
  if (api && api->device.getAdapterProperties) {
    return api->device.getAdapterProperties(adapter, outProps);
  }

  backend = api ? api->backend : GPU_BACKEND_NULL;

  outProps->backend = backend;
  outProps->type = GPU_ADAPTER_TYPE_UNKNOWN;
  outProps->name = gpu_backendName(backend);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAdapterCapabilities(const GPUAdapter       *adapter,
                          GPUAdapterCapabilities *outCaps) {
  static const GPUFeature supportedCompute[] = {
    GPU_FEATURE_COMPUTE
  };
  static const GPUFeature supportedComputeTimestamp[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_TIMESTAMPS
  };

  if (!adapter || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (gpu_adapterSupportsFeature(adapter, GPU_FEATURE_TIMESTAMPS)) {
    outCaps->supported.featureCount = (uint32_t)GPU_ARRAY_LEN(supportedComputeTimestamp);
    outCaps->supported.pFeatures = supportedComputeTimestamp;
  } else {
    outCaps->supported.featureCount = (uint32_t)GPU_ARRAY_LEN(supportedCompute);
    outCaps->supported.pFeatures = supportedCompute;
  }
  gpu_fillDefaultLimits(&outCaps->limits);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetDeviceCapabilities(const GPUDevice       *device,
                         GPUDeviceCapabilities *outCaps) {
  static const GPUFeature enabledCompute[] = {
    GPU_FEATURE_COMPUTE
  };
  static const GPUFeature enabledTimestamp[] = {
    GPU_FEATURE_TIMESTAMPS
  };
  static const GPUFeature enabledComputeTimestamp[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_TIMESTAMPS
  };
  bool computeEnabled;
  bool timestampEnabled;

  if (!device || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  computeEnabled = (device->enabledFeatureMask & gpu_featureBit(GPU_FEATURE_COMPUTE)) != 0;
  timestampEnabled = (device->enabledFeatureMask & gpu_featureBit(GPU_FEATURE_TIMESTAMPS)) != 0;
  if (computeEnabled && timestampEnabled) {
    outCaps->enabled.featureCount = (uint32_t)GPU_ARRAY_LEN(enabledComputeTimestamp);
    outCaps->enabled.pFeatures = enabledComputeTimestamp;
  } else if (computeEnabled) {
    outCaps->enabled.featureCount = (uint32_t)GPU_ARRAY_LEN(enabledCompute);
    outCaps->enabled.pFeatures = enabledCompute;
  } else if (timestampEnabled) {
    outCaps->enabled.featureCount = (uint32_t)GPU_ARRAY_LEN(enabledTimestamp);
    outCaps->enabled.pFeatures = enabledTimestamp;
  }
  gpu_fillDefaultLimits(&outCaps->limits);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetFormatCapabilities(const GPUAdapter      *adapter,
                         GPUFormat              format,
                         GPUFormatCapabilities *outCaps) {
  bool color;
  bool integerFormat;

  if (!adapter || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (format == GPU_FORMAT_UNDEFINED) {
    return GPU_OK;
  }

  if (gpu_formatIsDepthStencil(format)) {
    outCaps->depthStencil = true;
    return GPU_OK;
  }

  color = gpu_formatIsKnownColor(format);
  if (!color) {
    return GPU_OK;
  }

  integerFormat = gpu_formatIsInteger(format);
  outCaps->sampled = true;
  outCaps->filterable = !integerFormat;
  outCaps->storage = !integerFormat;
  outCaps->colorAttachment = true;
  outCaps->blendable = !integerFormat;

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetCacheStats(GPUDevice *device, GPUCacheStats *outStats) {
  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStats = device->cacheStats;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUConfigureRuntime(GPUDevice *device, const GPURuntimeConfig *config) {
  if (!device || !gpu_validRuntimeConfig(config)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device->runtimeConfig = *config;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUConfigureTransientAllocator(GPUDevice *device,
                               const GPUTransientAllocatorConfig *config) {
  GPUBuffer *buffer;
  void *cpuPtr;
  uint64_t capacityBytes;
  GPUResult result;

  if (!device || !gpu_validTransientAllocatorConfig(config, &capacityBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer = NULL;
  cpuPtr = NULL;
  result = gpu_createTransientBuffer(device,
                                     gpu_knownTransientBufferUsageMask(),
                                     capacityBytes,
                                     &buffer,
                                     &cpuPtr);
  if (result != GPU_OK) {
    return result;
  }

  gpu_destroyTransientAllocator(device);
  device->transientBuffer = buffer;
  device->transientCpuPtr = cpuPtr;
  device->transientConfig = *config;
  device->transientConfigured = true;
  device->transientFrameIndex = 0;
  device->allocatorStats.ringCapacityBytes = capacityBytes;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUAllocateTransientBuffer(GPUDevice *device,
                           GPUBufferUsageFlags usage,
                           uint64_t sizeBytes,
                           uint64_t alignment,
                           GPUTransientBufferSlice *outSlice) {
  uint64_t alignedOffset;
  uint64_t endOffset;
  uint64_t frameBaseOffset;

  if (!outSlice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(outSlice, 0, sizeof(*outSlice));

  if (!device ||
      !device->transientConfigured ||
      !gpu_validTransientBufferUsage(usage) ||
      sizeBytes == 0u ||
      !gpu_isPowerOfTwo(alignment)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!gpu_alignUp(device->transientFrameOffset, alignment, &alignedOffset) ||
      gpu_u64AddOverflow(alignedOffset, sizeBytes, &endOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (endOffset > device->transientConfig.ringBytesPerFrame) {
    return gpu_allocateTransientChunk(device, usage, sizeBytes, outSlice);
  }

  frameBaseOffset = (uint64_t)device->transientFrameIndex *
                    device->transientConfig.ringBytesPerFrame;
  outSlice->buffer = device->transientBuffer;
  outSlice->offset = frameBaseOffset + alignedOffset;
  outSlice->sizeBytes = sizeBytes;
  outSlice->cpuPtr = (uint8_t *)device->transientCpuPtr + outSlice->offset;

  device->transientFrameOffset = endOffset;
  device->allocatorStats.ringUsedBytes = endOffset;
  if (endOffset > device->allocatorStats.ringHighWaterBytes) {
    device->allocatorStats.ringHighWaterBytes = endOffset;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetLastFrameStats(GPUDevice *device, GPUFrameStats *outStats) {
  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStats = device->lastFrameStats;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAllocatorStats(GPUDevice *device, GPUAllocatorStats *outStats) {
  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStats = device->allocatorStats;
  return GPU_OK;
}

GPU_EXPORT
void
GPUResetStats(GPUDevice *device) {
  if (!device) {
    return;
  }

  memset(&device->cacheStats, 0, sizeof(device->cacheStats));
  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  memset(&device->lastFrameStats, 0, sizeof(device->lastFrameStats));
  device->allocatorStats.ringUsedBytes = device->transientFrameOffset;
  device->allocatorStats.ringHighWaterBytes = device->transientFrameOffset;
  device->allocatorStats.ringWrapCount = 0;
  device->allocatorStats.uploadStallCount = 0;
}

GPU_HIDE
void
gpuDeviceBeginFrame(GPUDevice *device) {
  uint32_t nextFrameIndex;

  if (!device) {
    return;
  }

  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  if (!device->transientConfigured) {
    return;
  }

  nextFrameIndex = device->transientFrameIndex + 1u;
  if (nextFrameIndex >= device->transientConfig.framesInFlight) {
    nextFrameIndex = 0;
  }
  if (device->transientFrameBegun &&
      nextFrameIndex <= device->transientFrameIndex) {
    device->allocatorStats.ringWrapCount++;
  }

  device->transientFrameIndex = nextFrameIndex;
  device->transientFrameOffset = 0;
  device->transientFrameBegun = true;
  device->allocatorStats.ringUsedBytes = 0;
}

GPU_HIDE
void
gpuDeviceEndFrame(GPUDevice *device) {
  if (!device) {
    return;
  }

  device->lastFrameStats = device->currentFrameStats;
}

GPU_HIDE
void
gpuDeviceRecordValidationError(GPUDevice *device, const char *message) {
  if (!device ||
      device->runtimeConfig.validationMode == GPU_VALIDATION_OFF ||
      !device->runtimeConfig.enableVerboseLogs) {
    return;
  }

  fprintf(stderr, "GPU validation: %s\n", message ? message : "validation error");
}

GPU_EXPORT
bool
GPUIsFeatureSupported(const GPUAdapter *adapter, GPUFeature feature) {
  if (!adapter || !gpu_knownFeature(feature)) {
    return false;
  }

  return gpu_adapterSupportsFeature(adapter, feature);
}

GPU_EXPORT
bool
GPUIsFeatureEnabled(const GPUDevice *device, GPUFeature feature) {
  if (!device || !gpu_knownFeature(feature)) {
    return false;
  }

  return (device->enabledFeatureMask & gpu_featureBit(feature)) != 0;
}

GPU_EXPORT
GPUDevice*
GPUCreateSystemDefaultDevice(GPUInstance *inst) {
  GPUApi *api;
  GPUDevice *device;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  device = api->device.createSystemDefaultDevice(inst);
  if (device) {
    device->enabledFeatureMask = gpu_defaultEnabledFeatureMask();
  }

  return device;
}

GPU_EXPORT
GPUResult
GPUCreateDevice(GPUAdapter                *adapter,
                const GPUDeviceCreateInfo *info,
                GPUDevice                **outDevice) {
  GPUCommandQueueCreateInfo stackQueueInfos[8];
  GPUCommandQueueCreateInfo *queueInfos;
  uint32_t queueInfoCount;
  GPUResult result;
  GPUResult featureResult;
  GPUApi *api;

  if (!outDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outDevice = NULL;

  if (!adapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info) {
    if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
        info->chain.sType != GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    featureResult = gpu_validateFeatureSet(adapter, &info->required, true);
    if (featureResult != GPU_OK) {
      return featureResult;
    }
    featureResult = gpu_validateFeatureSet(adapter, &info->optional, false);
    if (featureResult != GPU_OK) {
      return featureResult;
    }
  }
  if (!(api = gpuActiveGPUApi()))
    return GPU_ERROR_BACKEND_FAILURE;

  queueInfos = NULL;
  queueInfoCount = 0;
  result = gpu_buildQueueCreateInfos(info,
                                     stackQueueInfos,
                                     (uint32_t)GPU_ARRAY_LEN(stackQueueInfos),
                                     &queueInfos,
                                     &queueInfoCount);
  if (result != GPU_OK) {
    return result;
  }
  if (!gpu_validQueueCreateInfos(queueInfos, queueInfoCount)) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outDevice = api->device.createDevice(adapter, queueInfos, queueInfoCount);
  if (queueInfos != stackQueueInfos) {
    free(queueInfos);
  }
  if (!*outDevice) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  (*outDevice)->enabledFeatureMask = gpu_enabledFeatureMaskForCreateInfo(adapter, info);

  return GPU_OK;
}

GPU_EXPORT
GPUDevice *
GPUCreateDeviceWithDefaultQueues(GPUAdapter *adapter) {
  GPUDevice *device;

  if (GPUCreateDevice(adapter, NULL, &device) != GPU_OK) {
    return NULL;
  }

  return device;
}

GPU_EXPORT
GPUQueueFlagBits
GPUGetAvailableQueueBits(GPUDevice * __restrict device) {
  if (!device) {
    return 0;
  }

  return device->queueFamilies;
}

GPU_EXPORT
void
GPUDestroyDevice(GPUDevice * __restrict device) {
  GPUApi *api;

  if (!device) {
    return;
  }

  if (!(api = gpuActiveGPUApi())) {
    return;
  }

  gpu_destroyTransientAllocator(device);
  if (api->device.destroyDevice) {
    api->device.destroyDevice(device);
  }
}
