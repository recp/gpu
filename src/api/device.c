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
#include "buffer_internal.h"
#include "descr/descriptor_internal.h"
#include "device_internal.h"
#include "pipeline_cache_internal.h"

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

static uint64_t
gpu_featureBit(GPUFeature feature) {
  return 1ull << (uint32_t)feature;
}

static bool
gpu_builtinSupportedFeature(const GPUApi *api, GPUFeature feature) {
  bool hasComputePipeline;

  if (!api) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      hasComputePipeline = api->compute.createPipeline ||
                           (api->compute.newComputePipeline &&
                            api->compute.setFunction &&
                            api->compute.newComputeState);
      return hasComputePipeline &&
             api->compute.computeCommandEncoder &&
             api->compute.setComputePipelineState &&
             api->compute.dispatch &&
             api->compute.endEncoding;
    case GPU_FEATURE_INDIRECT_DRAW:
      return api->rce.drawPrimitivesIndirect &&
             api->rce.drawIndexedPrimsIndirect;
    case GPU_FEATURE_MULTI_DRAW:
      return api->rce.multiDrawPrimitivesIndirect &&
             api->rce.multiDrawIndexedPrimsIndirect;
    default:
      return false;
  }
}

static bool
gpu_adapterSupportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  GPUApi *api;

  if (!gpu_knownFeature(feature)) {
    return false;
  }
  if ((api = gpuAdapterApi(adapter)) && api->device.supportsFeature) {
    return api->device.supportsFeature(adapter, feature);
  }

  return gpu_builtinSupportedFeature(api, feature);
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
gpu_defaultEnabledFeatureMask(const GPUAdapter *adapter) {
  static const GPUFeature defaultFeatures[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_INDIRECT_DRAW,
    GPU_FEATURE_MULTI_DRAW
  };
  GPUApi *api;
  uint64_t mask;

  api  = gpuAdapterApi(adapter);
  mask = 0;
  for (uint32_t i = 0; i < GPU_ARRAY_LEN(defaultFeatures); i++) {
    bool supported;

    supported = adapter ?
      gpu_adapterSupportsFeature(adapter, defaultFeatures[i]) :
      gpu_builtinSupportedFeature(api, defaultFeatures[i]);
    if (supported) {
      mask |= gpu_featureBit(defaultFeatures[i]);
    }
  }

  return mask;
}

static uint64_t
gpu_enabledFeatureMaskForCreateInfo(const GPUAdapter *adapter,
                                    const GPUDeviceCreateInfo *info) {
  uint64_t mask;

  if (!info) {
    return gpu_defaultEnabledFeatureMask(adapter);
  }

  mask = gpu_collectEnabledFeatures(adapter, &info->required);
  mask |= gpu_collectEnabledFeatures(adapter, &info->optional);
  return mask;
}

static uint64_t
gpu_supportedFeatureMask(const GPUAdapter *adapter) {
  static const GPUFeature queryableFeatures[] = {
    GPU_FEATURE_COMPUTE,
    GPU_FEATURE_TIMESTAMPS,
    GPU_FEATURE_PIPELINE_STATISTICS,
    GPU_FEATURE_INDIRECT_DRAW,
    GPU_FEATURE_MULTI_DRAW
  };
  uint64_t mask;

  mask = 0;
  for (uint32_t i = 0; i < GPU_ARRAY_LEN(queryableFeatures); i++) {
    if (gpu_adapterSupportsFeature(adapter, queryableFeatures[i])) {
      mask |= gpu_featureBit(queryableFeatures[i]);
    }
  }

  return mask;
}

static void
gpu_fillFeatureSet(uint64_t       mask,
                   GPUFeature    *storage,
                   uint32_t       capacity,
                   GPUFeatureSet *outSet) {
  uint32_t count;

  count = 0u;
  for (GPUFeature feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_VARIABLE_RATE_SHADING && count < capacity;
       feature = (GPUFeature)(feature + 1)) {
    if (mask & gpu_featureBit(feature)) {
      storage[count++] = feature;
    }
  }

  outSet->featureCount = count;
  outSet->pFeatures    = count ? storage : NULL;
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

  limits->maxBindGroups                     = GPU_ENCODER_MAX_BIND_GROUPS;
  limits->maxBindingsPerGroup               = 64u;
  limits->maxDynamicUniformBuffers          = 8u;
  limits->maxDynamicStorageBuffers          = 4u;
  limits->minUniformBufferOffsetAlignment   = 256u;
  limits->minStorageBufferOffsetAlignment   = 256u;
  limits->maxColorAttachments               =
    GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS;
  limits->maxComputeWorkgroupSizeX          = 1024u;
  limits->maxComputeWorkgroupSizeY          = 1024u;
  limits->maxComputeWorkgroupSizeZ          = 64u;
}

static void
gpu_fillAdapterLimits(const GPUAdapter *adapter, GPULimits *limits) {
  GPUApi *api;

  gpu_fillDefaultLimits(limits);
  api = gpuAdapterApi(adapter);
  if (api && api->device.getLimits) {
    api->device.getLimits(adapter, limits);
  }
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

static GPUBufferUsageFlags
gpu_transientUploadUsageMask(void) {
  return GPU_BUFFER_USAGE_VERTEX |
         GPU_BUFFER_USAGE_INDEX |
         GPU_BUFFER_USAGE_UNIFORM |
         GPU_BUFFER_USAGE_COPY_SRC |
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
  if (!(api = gpuDeviceApi(buffer->device)) || !api->buf.contents) {
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
gpu_destroyTransientFrameFences(GPUDevice *device) {
  if (!device || !device->transientFrameFences) {
    return;
  }

  for (uint32_t i = 0u; i < device->transientConfig.framesInFlight; i++) {
    GPUFence *fence;

    fence = device->transientFrameFences[i];
    if (fence) {
      (void)GPUWaitFence(fence, UINT64_MAX);
      GPUDestroyFence(fence);
    }
  }
  free(device->transientFrameFences);
  device->transientFrameFences = NULL;
}

static void
gpu_destroyTransientAllocator(GPUDevice *device) {
  if (!device) {
    return;
  }

  gpu_destroyTransientFrameFences(device);
  gpu_destroyTransientChunks(device);
  GPUDestroyBuffer(device->transientBuffer);
  device->transientBuffer       = NULL;
  device->transientCpuPtr       = NULL;
  device->transientFrameOffset  = 0;
  device->transientBufferUsage  = 0;
  device->transientFrameIndex   = 0;
  device->transientConfigured   = false;
  device->transientFrameBegun   = false;
  memset(&device->transientConfig, 0, sizeof(device->transientConfig));
  memset(&device->allocatorStats, 0, sizeof(device->allocatorStats));
}

static GPUResult
gpu_createTransientFrameFences(GPUDevice *device,
                               uint32_t framesInFlight,
                               GPUFence ***outFences) {
  GPUFenceCreateInfo info;
  GPUFence         **fences;

  if (!device || framesInFlight == 0u || !outFences) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outFences = NULL;

  fences = calloc(framesInFlight, sizeof(*fences));
  if (!fences) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "transient-frame";
  info.signaled         = true;
  for (uint32_t i = 0u; i < framesInFlight; i++) {
    GPUResult result;

    result = GPUCreateFence(device, &info, &fences[i]);
    if (result != GPU_OK) {
      for (uint32_t j = 0u; j < i; j++) {
        GPUDestroyFence(fences[j]);
      }
      free(fences);
      return result;
    }
  }

  *outFences = fences;
  return GPU_OK;
}

static GPUResult
gpu_createTransientBuffer(GPUDevice *device,
                          GPUBufferUsageFlags usage,
                          uint64_t sizeBytes,
                          GPUBuffer **outBuffer,
                          void **outCpuPtr) {
  GPUBuffer           *buffer;
  GPUApi              *api;
  void                *cpuPtr;
  GPUBufferCreateInfo  info = {0};
  GPUResult            result;

  if (!device || !outBuffer || !outCpuPtr || sizeBytes == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!(api = gpuDeviceApi(device)) || !api->buf.contents) {
    return GPU_ERROR_UNSUPPORTED;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "transient-buffer";
  info.sizeBytes        = sizeBytes;
  info.usage            = usage;

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
                           uint64_t alignment,
                           GPUTransientBufferSlice *outSlice) {
  GPUTransientChunk *chunk;
  GPUBuffer         *buffer;
  void              *cpuPtr;
  uint64_t           alignedOffset;
  uint64_t           endOffset;
  uint64_t           chunkBytes;
  GPUResult          result;

  if (!device->transientConfig.allowChunkFallback) {
    device->allocatorStats.uploadStallCount++;
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  for (chunk = device->transientChunks; chunk; chunk = chunk->next) {
    if (chunk->frameIndex != device->transientFrameIndex ||
        (usage & ~chunk->usage) != 0u ||
        !gpu_alignUp(chunk->offset, alignment, &alignedOffset) ||
        gpu_u64AddOverflow(alignedOffset, sizeBytes, &endOffset) ||
        endOffset > chunk->sizeBytes) {
      continue;
    }

    chunk->offset       = endOffset;
    outSlice->buffer    = chunk->buffer;
    outSlice->offset    = alignedOffset;
    outSlice->sizeBytes = sizeBytes;
    outSlice->cpuPtr    = (uint8_t *)chunk->cpuPtr + alignedOffset;
    device->allocatorStats.uploadStallCount++;
    return GPU_OK;
  }

  if (!gpu_alignUp(sizeBytes, alignment, &endOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  chunkBytes = device->transientConfig.chunkBytes;
  if (chunkBytes < endOffset) {
    chunkBytes = endOffset;
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

  chunk->buffer     = buffer;
  chunk->cpuPtr     = cpuPtr;
  chunk->next       = device->transientChunks;
  chunk->sizeBytes  = chunkBytes;
  chunk->offset     = sizeBytes;
  chunk->usage      = usage;
  chunk->frameIndex = device->transientFrameIndex;
  device->transientChunks  = chunk;

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
    case GPU_FORMAT_DEPTH16_UNORM:
    case GPU_FORMAT_STENCIL8:
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
    case GPU_FORMAT_DEPTH32_FLOAT:
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return true;
    default:
      return false;
  }
}

static bool
gpu_formatIsCompressed(GPUFormat format) {
  return (format >= GPU_FORMAT_BC1_RGBA_UNORM &&
          format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB) ||
         (format >= GPU_FORMAT_EAC_R11_UNORM &&
          format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB) ||
         (format >= GPU_FORMAT_ASTC_4X4_UNORM &&
          format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB);
}

static bool
gpu_formatIsInteger(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_RG8_UINT:
    case GPU_FORMAT_RG8_SINT:
    case GPU_FORMAT_R32_UINT:
    case GPU_FORMAT_R32_SINT:
    case GPU_FORMAT_RG16_UINT:
    case GPU_FORMAT_RG16_SINT:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RGB10A2_UINT:
    case GPU_FORMAT_RG32_UINT:
    case GPU_FORMAT_RG32_SINT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA32_UINT:
    case GPU_FORMAT_RGBA32_SINT:
      return true;
    default:
      return false;
  }
}

static bool
gpu_formatIsKnownColor(GPUFormat format) {
  return format > GPU_FORMAT_UNDEFINED && format < GPU_FORMAT_COUNT &&
         !gpu_formatIsDepthStencil(format) &&
         !gpu_formatIsCompressed(format);
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
gpu_getInstanceAdapters(GPUInstance *inst) {
  GPUAdapter *item;
  GPUApi     *api;
  uint32_t    count;

  if (!(api = gpuInstanceApi(inst)) || !api->device.getAvailableAdapters) {
    return NULL;
  }
  if (inst->_adaptersEnumerated) {
    return inst->_adapters;
  }

  inst->_adaptersEnumerated = true;
  inst->_adapters = api->device.getAvailableAdapters(inst, UINT32_MAX);
  count = 0u;
  for (item = inst->_adapters; item; item = item->next) {
    item->inst = inst;
    gpu_fillFeatureSet(gpu_supportedFeatureMask(item),
                       item->supportedFeatureStorage,
                       (uint32_t)GPU_ARRAY_LEN(item->supportedFeatureStorage),
                       &item->supportedFeatures);
    count++;
  }
  inst->_adapterCount = count;

  return inst->_adapters;
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
  uint32_t i;

  if (!inst || !inoutAdapterCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity = *inoutAdapterCount;
  deviceList = gpu_getInstanceAdapters(inst);
  count = inst->_adapterCount;
  i = 0u;

  if (outAdapters) {
    for (item = deviceList; item && i < capacity; item = item->next) {
      outAdapters[i++] = item;
    }
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
  api = gpuAdapterApi(adapter);
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
  if (!adapter || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  outCaps->supported = adapter->supportedFeatures;
  gpu_fillAdapterLimits(adapter, &outCaps->limits);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetDeviceCapabilities(const GPUDevice       *device,
                         GPUDeviceCapabilities *outCaps) {
  if (!device || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  outCaps->enabled = device->enabledFeatures;
  gpu_fillAdapterLimits(device->phyDevice, &outCaps->limits);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetFormatCapabilities(const GPUAdapter      *adapter,
                         GPUFormat              format,
                         GPUFormatCapabilities *outCaps) {
  GPUApi *api;
  bool color;
  bool integerFormat;

  if (!adapter || !outCaps ||
      format <= GPU_FORMAT_UNDEFINED || format >= GPU_FORMAT_COUNT) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));
  if (gpu_formatIsDepthStencil(format)) {
    outCaps->depthStencil = true;
  } else {
    color = gpu_formatIsKnownColor(format);
    if (color) {
      integerFormat = gpu_formatIsInteger(format);
      outCaps->sampled         = true;
      outCaps->filterable      = !integerFormat;
      outCaps->storage         = !integerFormat;
      outCaps->colorAttachment = true;
      outCaps->blendable       = !integerFormat;
    }
  }

  api = gpuAdapterApi(adapter);
  if (api && api->device.getFormatCapabilities) {
    api->device.getFormatCapabilities(adapter, format, outCaps);
  }

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
  GPUBuffer           *buffer;
  GPUFence           **frameFences;
  void                *cpuPtr;
  uint64_t             capacityBytes;
  GPUBufferUsageFlags  usage;
  GPUResult            result;

  if (!device || !gpu_validTransientAllocatorConfig(config, &capacityBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer      = NULL;
  frameFences = NULL;
  cpuPtr      = NULL;
  usage       = gpu_knownTransientBufferUsageMask();
  result = gpu_createTransientBuffer(device,
                                     usage,
                                     capacityBytes,
                                     &buffer,
                                     &cpuPtr);
  if (result != GPU_OK) {
    usage  = gpu_transientUploadUsageMask();
    result = gpu_createTransientBuffer(device,
                                       usage,
                                       capacityBytes,
                                       &buffer,
                                       &cpuPtr);
  }
  if (result != GPU_OK) {
    return result;
  }
  result = gpu_createTransientFrameFences(device,
                                          config->framesInFlight,
                                          &frameFences);
  if (result != GPU_OK) {
    GPUDestroyBuffer(buffer);
    return result;
  }

  gpu_destroyTransientAllocator(device);
  device->transientBuffer           = buffer;
  device->transientFrameFences      = frameFences;
  device->transientCpuPtr           = cpuPtr;
  device->transientBufferUsage      = usage;
  device->transientConfig           = *config;
  device->transientConfigured       = true;
  device->transientFrameIndex       = 0u;
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
  if (!device->transientBuffer ||
      (usage & ~device->transientBufferUsage) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!gpu_alignUp(device->transientFrameOffset, alignment, &alignedOffset) ||
      gpu_u64AddOverflow(alignedOffset, sizeBytes, &endOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (endOffset > device->transientConfig.ringBytesPerFrame) {
    return gpu_allocateTransientChunk(device,
                                      usage,
                                      sizeBytes,
                                      alignment,
                                      outSlice);
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
GPUResult
gpuDeviceBeginFrame(GPUDevice *device) {
  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  return gpuDeviceAdvanceFrameSlot(device);
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
gpuDeviceRecordHotPathAlloc(GPUDevice *device, uint64_t sizeBytes) {
  if (!device) {
    return;
  }

  device->currentFrameStats.hotPathAllocCount++;
  device->currentFrameStats.hotPathAllocBytes += sizeBytes;
}

GPU_HIDE
void
gpuDeviceRecordHotPathFree(GPUDevice *device, uint64_t sizeBytes) {
  if (!device) {
    return;
  }

  device->currentFrameStats.hotPathFreeCount++;
  device->currentFrameStats.hotPathFreeBytes += sizeBytes;
}

#if GPU_BUILD_WITH_VALIDATION
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
#endif

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
  GPUAdapter *adapter;
  GPUDevice *device;
  GPUApi     *api;

  if (!(api = gpuInstanceApi(inst))) {
    return NULL;
  }

  adapter = gpu_getInstanceAdapters(inst);
  if (api->device.selectAdapter) {
    adapter = api->device.selectAdapter(inst, adapter);
  }
  if (!adapter || GPUCreateDevice(adapter, NULL, &device) != GPU_OK) {
    return NULL;
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
  if (!(api = gpuAdapterApi(adapter)) || !api->device.createDevice)
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
  (*outDevice)->_api = api;
  result = gpuInitPipelineCacheDevice(*outDevice);
  if (result != GPU_OK) {
    api->device.destroyDevice(*outDevice);
    *outDevice = NULL;
    return result;
  }
  result = gpuInitBindGroupCacheDevice(*outDevice);
  if (result != GPU_OK) {
    gpuDestroyPipelineCacheDevice(*outDevice);
    api->device.destroyDevice(*outDevice);
    *outDevice = NULL;
    return result;
  }
  (*outDevice)->enabledFeatureMask = gpu_enabledFeatureMaskForCreateInfo(adapter, info);
  gpu_fillFeatureSet((*outDevice)->enabledFeatureMask,
                     (*outDevice)->enabledFeatureStorage,
                     (uint32_t)GPU_ARRAY_LEN((*outDevice)->enabledFeatureStorage),
                     &(*outDevice)->enabledFeatures);

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

  if (!(api = gpuDeviceApi(device))) {
    return;
  }

  gpu_destroyTransientAllocator(device);
  gpuDestroyBindGroupCacheDevice(device);
  gpuDestroyPipelineCacheDevice(device);
  if (api->device.destroyDevice) {
    api->device.destroyDevice(device);
  }
}
