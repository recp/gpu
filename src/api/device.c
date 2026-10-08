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
#include "cmdqueue_internal.h"
#include "descr/descriptor_internal.h"
#include "device_internal.h"
#include "pass/blit_internal.h"
#include "pipeline_cache_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <sched.h>
#endif

typedef struct AdapterRequestContext {
  GPUInstance              *instance;
  GPUAdapterRequestCallback callback;
  void                     *userData;
  uint64_t                  requiredFeatureMask;
  GPUWorkload               workload;
} AdapterRequestContext;

typedef struct DeviceRequestContext {
  GPUAdapter              *adapter;
  GPUDeviceRequestCallback callback;
  void                    *userData;
  uint64_t                 enabledFeatureMask;
} DeviceRequestContext;

static const GPUFeature gpu_defaultFeatures[] = {
  GPU_FEATURE_COMPUTE,
  GPU_FEATURE_INDIRECT_DRAW,
  GPU_FEATURE_MULTI_DRAW
};

static const GPUExecutionFlags gpu_workloadFlags[] = {
  [GPU_WORKLOAD_DEFAULT]  = 0u,
  [GPU_WORKLOAD_GRAPHICS] = GPU_EXECUTION_GRAPHICS_BIT,
  [GPU_WORKLOAD_COMPUTE]  = GPU_EXECUTION_COMPUTE_BIT,
  [GPU_WORKLOAD_HYBRID]   = GPU_EXECUTION_GRAPHICS_BIT |
                            GPU_EXECUTION_COMPUTE_BIT
};

static const char*
gpu_backendName(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return "Metal";
    case GPU_BACKEND_VULKAN:
      return "Vulkan";
    case GPU_BACKEND_DX12:
      return "Direct3D 12";
    case GPU_BACKEND_WEBGPU:
      return "WebGPU";
    case GPU_BACKEND_CUDA:
      return "CUDA";
    default:
      return "Unknown GPU";
  }
}

static bool
validQueueCreateInfos(const QueueCreateInfo    queCI[],
                      uint32_t                 nQueCI) {
  uint32_t i;

  if (!queCI) {
    return nQueCI == 0;
  }

  if (nQueCI == 0) {
    return false;
  }

  for (i = 0; i < nQueCI; i++) {
    if (queCI[i].flags == 0 || queCI[i].count == 0) {
      return false;
    }
  }

  return true;
}

static bool
validQueueRequestType(GPUQueueFlagBits type) {
  return type == GPU_QUEUE_GRAPHICS
         || type == GPU_QUEUE_COMPUTE
         || type == GPU_QUEUE_TRANSFER;
}

static bool
validFeatureSet(const GPUFeatureSet *set) {
  return set->featureCount == 0 || set->pFeatures;
}

static bool
validValidationMode(GPUValidationMode mode) {
  return mode == GPU_VALIDATION_OFF
         || mode == GPU_VALIDATION_BASIC
         || mode == GPU_VALIDATION_FULL;
}

static bool
reportDeviceLostOnce(GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedCompareExchange((volatile LONG *)&device->deviceLostReported,
                                    1,
                                    0) == 0;
#else
  uint32_t expected;

  expected = 0u;

  return __atomic_compare_exchange_n(&device->deviceLostReported,
                                     &expected,
                                     1u,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE);
#endif
}

static bool
knownFeature(GPUFeature feature) {
  return feature >= GPU_FEATURE_COMPUTE
         && feature <= GPU_FEATURE_ML_MODEL_EXT;
}

static bool
validPowerPreference(GPUPowerPreference preference) {
  return preference == GPU_POWER_PREFERENCE_DEFAULT
         || preference == GPU_POWER_PREFERENCE_LOW_POWER
         || preference == GPU_POWER_PREFERENCE_HIGH_PERFORMANCE;
}

static bool
validWorkload(GPUWorkload workload) {
  return workload == GPU_WORKLOAD_DEFAULT
         || workload == GPU_WORKLOAD_GRAPHICS
         || workload == GPU_WORKLOAD_COMPUTE
         || workload == GPU_WORKLOAD_HYBRID;
}

static uint64_t
featureBit(GPUFeature feature) {
  return 1ull << (uint32_t)feature;
}

static GPUResult
adapterRequestMask(const GPUAdapterRequestOptions *options,
                   GPUPowerPreference             *outPreference,
                   GPUWorkload                    *outWorkload,
                   uint64_t                       *outRequiredMask) {
  uint64_t           mask;
  GPUPowerPreference preference;
  GPUWorkload        workload;
  uint32_t           i;
  GPUFeature         feature;

  if (!outPreference || !outWorkload || !outRequiredMask) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  preference = GPU_POWER_PREFERENCE_DEFAULT;
  workload   = GPU_WORKLOAD_DEFAULT;
  mask       = 0u;

  if (options) {
    if ((options->chain.sType != GPU_STRUCTURE_TYPE_NONE
         && options->chain.sType != GPU_STRUCTURE_TYPE_ADAPTER_REQUEST_OPTIONS)
        || (options->chain.structSize != 0u
            && options->chain.structSize < sizeof(*options))
        || !validPowerPreference(options->powerPreference)
        || !validWorkload(options->workload)
        || (options->requiredFeatureCount > 0u
            && !options->pRequiredFeatures)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    preference = options->powerPreference;
    workload   = options->workload;

    for (i = 0u; i < options->requiredFeatureCount; i++) {
      feature = options->pRequiredFeatures[i];

      if (!knownFeature(feature)) {
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      mask |= featureBit(feature);
    }
  }

  *outPreference   = preference;
  *outWorkload     = workload;
  *outRequiredMask = mask;

  return GPU_OK;
}

static bool
builtinSupportedFeature(const Api    *api, GPUFeature feature) {
  bool hasComputePipeline;

  if (!api) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
      hasComputePipeline = api->compute.createPipeline
                           || (api->compute.newComputePipeline
                               && api->compute.setFunction
                               && api->compute.newComputeState);
      return hasComputePipeline
             && api->compute.computeCommandEncoder
             && api->compute.setComputePipelineState
             && api->compute.dispatch
             && api->compute.endEncoding;
    case GPU_FEATURE_INDIRECT_DRAW:
      return api->rce.drawPrimitivesIndirect
             && api->rce.drawIndexedPrimsIndirect;
    case GPU_FEATURE_MULTI_DRAW:
      return api->rce.multiDrawPrimitivesIndirect
             && api->rce.multiDrawIndexedPrimsIndirect;
    default:
      return false;
  }
}

static bool
adapterSupportsFeature(const GPUAdapter *adapter, GPUFeature feature) {
  Api    *api;

  if (!knownFeature(feature)) {
    return false;
  }

  if ((api = adapterApi(adapter)) && api->device.supportsFeature) {
    return api->device.supportsFeature(adapter, feature);
  }

  return builtinSupportedFeature(api, feature);
}

static bool
adapterSupportsMask(const GPUAdapter *adapter, uint64_t requiredMask) {
  uint64_t   bit;
  GPUFeature feature;

  for (feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_ML_MODEL_EXT;
       feature = (GPUFeature)(feature + 1)) {
    bit = featureBit(feature);

    if ((requiredMask & bit) != 0u
        && !adapterSupportsFeature(adapter, feature)) {
      return false;
    }
  }

  return true;
}

static uint64_t
collectEnabledFeatures(const GPUAdapter *adapter, const GPUFeatureSet *set) {
  uint64_t mask;
  uint32_t i;

  mask = 0;

  if (!set || !validFeatureSet(set)) {
    return 0;
  }

  for (i = 0; i < set->featureCount; i++) {
    if (knownFeature(set->pFeatures[i])
        && adapterSupportsFeature(adapter, set->pFeatures[i])) {
      mask |= featureBit(set->pFeatures[i]);
    }
  }

  return mask;
}

static uint64_t
defaultEnabledFeatureMask(const GPUAdapter *adapter) {
  Api     *api;
  uint64_t mask;
  uint32_t i;
  bool     supported;

  api  = adapterApi(adapter);
  mask = 0;

  for (i = 0; i < GPU_ARRAY_LEN(gpu_defaultFeatures); i++) {
    supported = adapter ?
      adapterSupportsFeature(adapter, gpu_defaultFeatures[i]) :
      builtinSupportedFeature(api, gpu_defaultFeatures[i]);

    if (supported) {
      mask |= featureBit(gpu_defaultFeatures[i]);
    }
  }

  return mask;
}

static uint64_t
enabledFeatureMaskForCreateInfo(const GPUAdapter          *adapter,
                                const GPUDeviceCreateInfo *info) {
  uint64_t mask;

  if (!info) {
    return defaultEnabledFeatureMask(adapter);
  }

  mask = collectEnabledFeatures(adapter, &info->required);
  mask |= collectEnabledFeatures(adapter, &info->optional);

  if ((mask & featureBit(GPU_FEATURE_BINDLESS)) != 0u) {
    mask |= featureBit(GPU_FEATURE_DESCRIPTOR_INDEXING);
  }

  if ((mask & featureBit(GPU_FEATURE_SUBGROUP_MATRIX)) != 0u) {
    mask |= featureBit(GPU_FEATURE_SUBGROUPS);
  }

  if ((mask & featureBit(GPU_FEATURE_RAY_TRACING_PIPELINE)) != 0u) {
    mask |= featureBit(GPU_FEATURE_RAY_QUERY);
  }

  if ((mask & featureBit(GPU_FEATURE_INTERSECTION_FUNCTION_TABLE)) != 0u) {
    mask |= featureBit(GPU_FEATURE_RAY_QUERY);
  }

  if ((mask & (featureBit(GPU_FEATURE_INDIRECT_MEMORY_COPY) |
               featureBit(GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY))) != 0u) {
    mask |= featureBit(GPU_FEATURE_BUFFER_DEVICE_ADDRESS);
  }

  if ((mask & featureBit(GPU_FEATURE_EXECUTION_GRAPH)) != 0u) {
    mask |= featureBit(GPU_FEATURE_BUFFER_DEVICE_ADDRESS);
  }

  if ((mask & featureBit(GPU_FEATURE_SPARSE_TEXTURES)) != 0u
      && GPUIsFeatureSupported((GPUAdapter *)adapter,
                               GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)) {
    mask |= featureBit(GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT);
  }

  if ((mask & featureBit(GPU_FEATURE_SPARSE_BUFFERS)) != 0u) {
    mask |= featureBit(GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT);
  }

  return mask;
}

static uint64_t
supportedFeatureMask(const GPUAdapter *adapter) {
  uint64_t   mask;
  GPUFeature feature;

  mask = 0;

  for (feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_ML_MODEL_EXT;
       feature = (GPUFeature)(feature + 1)) {
    if (adapterSupportsFeature(adapter, feature)) {
      mask |= featureBit(feature);
    }
  }

  return mask;
}

static void
fillFeatureSet(uint64_t       mask,
               GPUFeature    *storage,
               uint32_t       capacity,
               GPUFeatureSet *outSet) {
  uint32_t   count;
  GPUFeature feature;

  count = 0u;

  for (feature = GPU_FEATURE_COMPUTE;
       feature <= GPU_FEATURE_ML_MODEL_EXT
         && count < capacity;
       feature = (GPUFeature)(feature + 1)) {
    if (mask & featureBit(feature)) {
      storage[count++] = feature;
    }
  }

  outSet->featureCount = count;
  outSet->pFeatures    = count ? storage : NULL;
}

static void
ensureAdapterFeatureSet(GPUAdapter *adapter) {
  uint64_t mask;

  if (!adapter || adapterFeatureStateLoad(adapter) == 2u) {
    return;
  }

  if (adapterFeatureStateBegin(adapter)) {
    mask = supportedFeatureMask(adapter);
    fillFeatureSet(mask,
                   adapter->supportedFeatureStorage,
                   (uint32_t)GPU_ARRAY_LEN(adapter->supportedFeatureStorage),
                   &adapter->supportedFeatures);
    adapterFeatureStateComplete(adapter);
    return;
  }

  while (adapterFeatureStateLoad(adapter) != 2u) {
#if defined(_WIN32) || defined(WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
  }
}

static GPUResult
validateFeatureSet(const GPUAdapter    *adapter,
                   const GPUFeatureSet *set,
                   bool                 required) {
  uint32_t i;

  if (!validFeatureSet(set)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (i = 0; i < set->featureCount; i++) {
    if (!knownFeature(set->pFeatures[i])) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (required && !adapterSupportsFeature(adapter, set->pFeatures[i])) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  return GPU_OK;
}

static void
fillDefaultLimits(GPULimits *limits) {
  memset(limits, 0, sizeof(*limits));

  limits->maxBindGroups                   = GPU_ENCODER_MAX_BIND_GROUPS;
  limits->maxBindingsPerGroup             = 64u;
  limits->maxDynamicUniformBuffers        = 8u;
  limits->maxDynamicStorageBuffers        = 4u;
  limits->minUniformBufferOffsetAlignment = 256u;
  limits->minStorageBufferOffsetAlignment = 256u;
  limits->maxColorAttachments             = GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS;
  limits->maxComputeWorkgroupSizeX        = 1024u;
  limits->maxComputeWorkgroupSizeY        = 1024u;
  limits->maxComputeWorkgroupSizeZ        = 64u;
  limits->maxPushConstantSizeBytes        = 256u;
  limits->maxSamplerAnisotropy            = 1u;
}

static void
fillAdapterLimits(const GPUAdapter *adapter, GPULimits *limits) {
  Api    *api;

  fillDefaultLimits(limits);
  if ((api = adapterApi(adapter)) && api->device.getLimits) {
    api->device.getLimits(adapter, limits);
  }
}

static bool
u64MulOverflow(uint64_t a, uint64_t b, uint64_t *out) {
  if (a != 0 && b > UINT64_MAX / a) {
    return true;
  }

  *out = a * b;

  return false;
}

static bool
u64AddOverflow(uint64_t a, uint64_t b, uint64_t *out) {
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
alignUp(uint64_t value, uint64_t alignment, uint64_t *out) {
  uint64_t mask;
  uint64_t biased;

  if (!gpu_isPowerOfTwo(alignment)) {
    return false;
  }

  mask = alignment - 1u;

  if (u64AddOverflow(value, mask, &biased)) {
    return false;
  }

  *out = biased & ~mask;

  return true;
}

static GPUBufferUsageFlags
knownTransientBufferUsageMask(void) {
  return GPU_BUFFER_USAGE_VERTEX |
         GPU_BUFFER_USAGE_INDEX |
         GPU_BUFFER_USAGE_UNIFORM |
         GPU_BUFFER_USAGE_STORAGE |
         GPU_BUFFER_USAGE_COPY_SRC |
         GPU_BUFFER_USAGE_COPY_DST |
         GPU_BUFFER_USAGE_INDIRECT;
}

static GPUBufferUsageFlags
transientUploadUsageMask(void) {
  return GPU_BUFFER_USAGE_VERTEX |
         GPU_BUFFER_USAGE_INDEX |
         GPU_BUFFER_USAGE_UNIFORM |
         GPU_BUFFER_USAGE_COPY_SRC |
         GPU_BUFFER_USAGE_INDIRECT;
}

static void*
bufferContents(GPUBuffer *buffer) {
  Api    *api;

  if (!buffer) {
    return NULL;
  }

  if (!(api = deviceApi(buffer->device)) || !api->buf.contents) {
    return NULL;
  }

  return api->buf.contents(buffer);
}

static void
destroyTransientChunks(GPUDevice *device) {
  TransientChunk    *chunk;
  TransientChunk    *next;

  if (!device) {
    return;
  }

  chunk = device->transientChunks;

  while (chunk) {
    next = chunk->next;

    if (chunk->cpuPtrOwned) {
      free(chunk->cpuPtr);
    }

    GPUDestroyBuffer(chunk->buffer);
    free(chunk);
    chunk = next;
  }

  device->transientChunks = NULL;
}

static void
destroyTransientFrameFences(GPUDevice *device) {
  GPUFence *fence;
  uint32_t  i;

  if (!device || !device->transientFrameFences) {
    return;
  }

  for (i = 0u; i < device->transientConfig.framesInFlight; i++) {
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
destroyTransientAllocator(GPUDevice *device) {
  if (!device) {
    return;
  }

  destroyTransientFrameFences(device);
  destroyTransientChunks(device);

  if (device->transientCpuPtrOwned) {
    free(device->transientCpuPtr);
  }

  GPUDestroyBuffer(device->transientBuffer);
  device->transientBuffer      = NULL;
  device->transientCpuPtr      = NULL;
  device->transientFrameOffset = 0u;
  device->transientFrameStride = 0u;
  device->transientBufferUsage = 0u;
  device->transientFrameIndex  = 0u;
  device->transientConfigured  = false;
  device->transientFrameBegun  = false;
  device->transientCpuPtrOwned = false;
  memset(&device->transientConfig, 0, sizeof(device->transientConfig));
  memset(&device->allocatorStats, 0, sizeof(device->allocatorStats));
}

static GPUResult
createTransientFrameFences(GPUDevice  *device,
                           uint32_t    framesInFlight,
                           GPUFence ***outFences) {
  GPUFenceCreateInfo info;
  GPUFence         **fences;
  uint32_t           i;
  GPUResult          result;
  uint32_t           j;

  if (!device || framesInFlight == 0u || !outFences) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outFences = NULL;

  if (!(fences = calloc(framesInFlight, sizeof(*fences)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  memset(&info, 0, sizeof(info));
  info.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "transient-frame";
  info.signaled         = true;

  for (i = 0u; i < framesInFlight; i++) {
    result = GPUCreateFence(device, &info, &fences[i]);

    if (result != GPU_OK) {
      for (j = 0u; j < i; j++) {
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
createTransientBuffer(GPUDevice          *device,
                      GPUBufferUsageFlags usage,
                      uint64_t            sizeBytes,
                      GPUBuffer         **outBuffer,
                      void              **outCpuPtr,
                      bool               *outCpuPtrOwned) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer          *buffer;
  Api                *api;
  void               *cpuPtr;
  GPUResult           result;

  if (!device || !outBuffer || !outCpuPtr || !outCpuPtrOwned
      || sizeBytes == 0u || sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = deviceApi(device)) || !api->buf.create) {
    return GPU_ERROR_UNSUPPORTED;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "transient-buffer";
  info.sizeBytes        = sizeBytes;
  info.usage            = usage | GPU_BUFFER_USAGE_COPY_DST;

  buffer = NULL;
  result = GPUCreateBuffer(device, &info, &buffer);

  if (result != GPU_OK) {
    return result;
  }

  if (!(cpuPtr = api->buf.contents ? bufferContents(buffer) : NULL)) {
    if (!api->buf.write) {
      GPUDestroyBuffer(buffer);
      return GPU_ERROR_UNSUPPORTED;
    }

    if (!(cpuPtr = calloc(1u, (size_t)sizeBytes))) {
      GPUDestroyBuffer(buffer);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    *outCpuPtrOwned = true;
  } else {
    *outCpuPtrOwned = false;
  }

  *outBuffer = buffer;
  *outCpuPtr = cpuPtr;

  return GPU_OK;
}

static bool
validRuntimeConfig(const GPURuntimeConfig *config) {
  if (!config) {
    return false;
  }

  if (config->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && config->chain.sType != GPU_STRUCTURE_TYPE_RUNTIME_CONFIG) {
    return false;
  }

  if (config->chain.structSize != 0
      && config->chain.structSize < sizeof(*config)) {
    return false;
  }

  return validValidationMode(config->validationMode);
}

static bool
validTransientAllocatorConfig(const GPUTransientAllocatorConfig *config,
                              uint64_t                          *outCapacityBytes) {
  uint64_t frameStride;

  if (!config || !outCapacityBytes) {
    return false;
  }

  if (config->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && config->chain.sType != GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG) {
    return false;
  }

  if (config->chain.structSize != 0
      && config->chain.structSize < sizeof(*config)) {
    return false;
  }

  if (config->ringBytesPerFrame == 0u || config->framesInFlight == 0u) {
    return false;
  }

  if (config->allowChunkFallback && config->chunkBytes == 0u) {
    return false;
  }

  return alignUp(config->ringBytesPerFrame, 4u, &frameStride)
         && !u64MulOverflow(frameStride,
                            config->framesInFlight,
                            outCapacityBytes);
}

static GPUResult
allocateTransientChunk(GPUDevice               *device,
                       GPUBufferUsageFlags      usage,
                       uint64_t                 sizeBytes,
                       uint64_t                 alignment,
                       GPUTransientBufferSlice *outSlice) {
  TransientChunk    *chunk;
  GPUBuffer         *buffer;
  void              *cpuPtr;
  uint64_t           alignedOffset;
  uint64_t           endOffset;
  uint64_t           chunkBytes;
  GPUResult          result;
  bool               cpuPtrOwned;

  if (!device->transientConfig.allowChunkFallback) {
    device->allocatorStats.uploadStallCount++;
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  for (chunk = device->transientChunks; chunk; chunk = chunk->next) {
    if (chunk->frameIndex != device->transientFrameIndex
        || (usage & ~chunk->usage) != 0u
        || !alignUp(chunk->offset, alignment, &alignedOffset)
        || u64AddOverflow(alignedOffset, sizeBytes, &endOffset)
        || endOffset > chunk->sizeBytes) {
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

  if (!alignUp(sizeBytes, alignment, &endOffset)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  chunkBytes = device->transientConfig.chunkBytes;

  if (chunkBytes < endOffset) {
    chunkBytes = endOffset;
  }

  if (!alignUp(chunkBytes, 4u, &chunkBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(chunk = calloc(1, sizeof(*chunk)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  buffer      = NULL;
  cpuPtr      = NULL;
  cpuPtrOwned = false;
  result      = createTransientBuffer(device,
                                      usage,
                                      chunkBytes,
                                      &buffer,
                                      &cpuPtr,
                                      &cpuPtrOwned);

  if (result != GPU_OK) {
    free(chunk);
    return result;
  }

  chunk->buffer           = buffer;
  chunk->cpuPtr           = cpuPtr;
  chunk->next             = device->transientChunks;
  chunk->sizeBytes        = chunkBytes;
  chunk->offset           = sizeBytes;
  chunk->usage            = usage;
  chunk->frameIndex       = device->transientFrameIndex;
  chunk->cpuPtrOwned      = cpuPtrOwned;
  device->transientChunks = chunk;

  device->allocatorStats.uploadStallCount++;
  device->currentFrameStats.hotPathAllocCount++;
  device->currentFrameStats.hotPathAllocBytes += chunkBytes;

  outSlice->buffer    = buffer;
  outSlice->offset    = 0;
  outSlice->sizeBytes = sizeBytes;
  outSlice->cpuPtr    = cpuPtr;

  return GPU_OK;
}

static bool
formatIsDepthStencil(GPUFormat format) {
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
formatIsCompressed(GPUFormat format) {
  return (format >= GPU_FORMAT_BC1_RGBA_UNORM
          && format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB)
         || (format >= GPU_FORMAT_EAC_R11_UNORM
             && format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB)
         || (format >= GPU_FORMAT_ASTC_4X4_UNORM
             && format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB);
}

static bool
formatIsInteger(GPUFormat format) {
  return formatNumericType(format) != GPU_FORMAT_NUMERIC_FLOAT;
}

static bool
formatIsKnownColor(GPUFormat format) {
  return format > GPU_FORMAT_UNDEFINED && format < GPU_FORMAT_COUNT
         && !formatIsDepthStencil(format)
         && !formatIsCompressed(format);
}

static GPUResult
buildQueueCreateInfos(const GPUDeviceCreateInfo *info,
                      QueueCreateInfo           *stackInfos,
                      uint32_t                   stackInfoCount,
                      QueueCreateInfo          **outInfos,
                      uint32_t                  *outInfoCount) {
  const GPUDeviceQueueCreateInfo *queueInfo;
  QueueCreateInfo                *infos;
  uint32_t                        requestCount;
  uint32_t                        i;

  *outInfos     = NULL;
  *outInfoCount = 0;

  if (!info) {
    return GPU_OK;
  }

  if (info->queues.chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->queues.chain.sType != GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->queues.chain.structSize != 0
      && info->queues.chain.structSize < sizeof(info->queues)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  queueInfo    = &info->queues;
  requestCount = queueInfo->requestCount;

  if (requestCount == 0) {
    return queueInfo->pRequests ? GPU_ERROR_INVALID_ARGUMENT : GPU_OK;
  }

  if (!queueInfo->pRequests) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  infos = stackInfos;

  if (requestCount > stackInfoCount) {
    if (!(infos = calloc(requestCount, sizeof(*infos)))) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }

  for (i = 0; i < requestCount; i++) {
    if (!validQueueRequestType(queueInfo->pRequests[i].type)
        || queueInfo->pRequests[i].count == 0) {
      if (infos != stackInfos) {
        free(infos);
      }
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    infos[i].flags = queueInfo->pRequests[i].type;
    infos[i].count = queueInfo->pRequests[i].count;
  }

  *outInfos     = infos;
  *outInfoCount = requestCount;

  return GPU_OK;
}

static GPUAdapter*
getInstanceAdapters(GPUInstance *inst) {
  GPUAdapter *adapters;
  GPUAdapter *item;
  Api        *api;
  uint32_t    count;

  if (inst->_adaptersEnumerated) {
    return inst->_adapters;
  }

  if (!(api = instanceApi(inst)) || !api->device.getAvailableAdapters) {
    return NULL;
  }

  if (!(adapters = api->device.getAvailableAdapters(inst, UINT32_MAX))) {
    return NULL;
  }

  inst->_adapters = adapters;
  count           = 0u;

  for (item = inst->_adapters; item; item = item->next) {
    item->inst = inst;
    count++;
  }

  inst->_adapterCount       = count;
  inst->_adaptersEnumerated = true;

  return inst->_adapters;
}

static uint32_t
adapterPreferenceRank(const GPUAdapter  *adapter,
                      GPUPowerPreference preference) {
  GPUAdapterProperties properties;

  if (GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    return 3u;
  }

  if (preference == GPU_POWER_PREFERENCE_LOW_POWER) {
    switch (properties.type) {
      case GPU_ADAPTER_TYPE_INTEGRATED:
        return 0u;
      case GPU_ADAPTER_TYPE_DISCRETE:
        return 1u;
      case GPU_ADAPTER_TYPE_UNKNOWN:
        return 2u;
      case GPU_ADAPTER_TYPE_SOFTWARE:
        return 3u;
    }
  }

  switch (properties.type) {
    case GPU_ADAPTER_TYPE_DISCRETE:
      return 0u;
    case GPU_ADAPTER_TYPE_INTEGRATED:
      return 1u;
    case GPU_ADAPTER_TYPE_UNKNOWN:
      return 2u;
    case GPU_ADAPTER_TYPE_SOFTWARE:
      return 3u;
  }

  return 3u;
}

static GPUExecutionFlags
workloadExecutionFlags(GPUWorkload workload) {
  return gpu_workloadFlags[workload];
}

static bool
adapterSupportsWorkload(const GPUAdapter *adapter,
                        GPUWorkload       workload) {
  GPUAdapterProperties properties;
  GPUExecutionFlags    required;

  required = workloadExecutionFlags(workload);

  if (required == 0u) {
    return true;
  }

  if (GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    return false;
  }

  return (properties.executionFlags & required) == required;
}

static GPUAdapter*
selectRequestedAdapter(GPUInstance       *inst,
                       GPUPowerPreference preference,
                       GPUWorkload        workload,
                       uint64_t           requiredFeatureMask) {
  GPUAdapter *adapters;
  GPUAdapter *preferred;
  GPUAdapter *best;
  Api        *api;
  GPUAdapter *adapter;
  uint32_t    bestRank;
  uint32_t    rank;

  if (!(api = instanceApi(inst))
      || !(adapters = getInstanceAdapters(inst))) {
    return NULL;
  }

  preferred = adapters;

  if (api->device.selectAdapter) {
    preferred = api->device.selectAdapter(inst, adapters, preference);
  }

  if (preferred
      && adapterSupportsWorkload(preferred, workload)
      && adapterSupportsMask(preferred, requiredFeatureMask)) {
    return preferred;
  }

  best     = NULL;
  bestRank = UINT32_MAX;

  for (adapter = adapters; adapter; adapter = adapter->next) {
    if (adapter == preferred
        || !adapterSupportsWorkload(adapter, workload)
        || !adapterSupportsMask(adapter, requiredFeatureMask)) {
      continue;
    }

    if (preference == GPU_POWER_PREFERENCE_DEFAULT) {
      return adapter;
    }

    rank = adapterPreferenceRank(adapter, preference);

    if (rank < bestRank) {
      best     = adapter;
      bestRank = rank;
    }
  }

  return best;
}

static void
completeAdapterRequest(GPUResult   result,
                       GPUAdapter *adapter,
                       void       *userData) {
  AdapterRequestContext    *request;
  GPUInstance              *instance;
  Api                      *api;

  request  = userData;
  instance = request->instance;

  if (result == GPU_OK && adapter) {
    adapter->inst = instance;
  }

  if (result == GPU_OK && adapter
      && (!adapterSupportsWorkload(adapter, request->workload)
          || !adapterSupportsMask(adapter, request->requiredFeatureMask))) {
    api = instanceApi(instance);

    if (api && api->device.destroyAdapter) {
      api->device.destroyAdapter(adapter);
    }

    adapter = NULL;
    result  = GPU_ERROR_UNSUPPORTED;
  }

  if (result == GPU_OK && adapter) {
    adapter->next       = instance->_adapters;
    instance->_adapters = adapter;
    instance->_adapterCount++;
    instance->_adaptersEnumerated = true;
  } else {
    adapter = NULL;

    if (result == GPU_OK) {
      result = GPU_ERROR_BACKEND_FAILURE;
    }
  }

  request->callback(result, adapter, request->userData);
  free(request);
}

static GPUResult
finalizeDevice(GPUAdapter *adapter,
               GPUDevice  *device,
               uint64_t    enabledFeatureMask) {
  Api      *api;
  GPUResult result;

  if (!(api = adapterApi(adapter)) || !device) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  device->inst    = adapter->inst;
  device->adapter = adapter;
  device->_api    = api;

  if ((enabledFeatureMask & (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) != 0u
      && GPUGetVRSCapabilitiesEXT(adapter, &device->vrsCapabilities) != GPU_OK) {
    api->device.destroyDevice(device);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = initPipelineCacheDevice(device);

  if (result != GPU_OK) {
    api->device.destroyDevice(device);
    return result;
  }

  result = initBindGroupCacheDevice(device);

  if (result != GPU_OK) {
    destroyPipelineCacheDevice(device);
    api->device.destroyDevice(device);
    return result;
  }

  result = initBlitDevice(device);

  if (result != GPU_OK) {
    destroyBindGroupCacheDevice(device);
    destroyPipelineCacheDevice(device);
    api->device.destroyDevice(device);
    return result;
  }

  device->enabledFeatureMask = enabledFeatureMask;
  fillFeatureSet(device->enabledFeatureMask,
                 device->enabledFeatureStorage,
                 (uint32_t)GPU_ARRAY_LEN(device->enabledFeatureStorage),
                 &device->enabledFeatures);

  return GPU_OK;
}

static void
completeDeviceRequest(GPUResult  result,
                      GPUDevice *device,
                      void      *userData) {
  DeviceRequestContext    *request;

  request = userData;

  if (result == GPU_OK && device) {
    result = finalizeDevice(request->adapter,
                            device,
                            request->enabledFeatureMask);
  } else if (result == GPU_OK) {
    result = GPU_ERROR_BACKEND_FAILURE;
  }

  if (result != GPU_OK) {
    device = NULL;
  }

  request->callback(result, device, request->userData);
  free(request);
}

GPU_HIDE
GPUResult
deviceFlushTransientUploads(GPUQueue *queue, uint32_t frameIndex) {
  TransientChunk    *chunk;
  GPUDevice         *device;
  uint64_t           baseOffset;
  uint64_t           flushBytes;
  GPUResult          result;

  if (!(device = commandQueueDevice(queue))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!device->transientConfigured) {
    return GPU_OK;
  }

  if (frameIndex >= device->transientConfig.framesInFlight) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (device->transientCpuPtrOwned
      && device->transientFrameOffset != 0u) {
    if (frameIndex != device->transientFrameIndex
        || !alignUp(device->transientFrameOffset, 4u, &flushBytes)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    baseOffset = (uint64_t)frameIndex * device->transientFrameStride;
    result     = GPUQueueWriteBuffer(queue,
                                     device->transientBuffer,
                                     baseOffset,
                                     (const uint8_t *)device->transientCpuPtr + baseOffset,
                                     flushBytes);

    if (result != GPU_OK) {
      return result;
    }
  }

  for (chunk = device->transientChunks; chunk; chunk = chunk->next) {
    if (!chunk->cpuPtrOwned || chunk->frameIndex != frameIndex
        || chunk->offset == 0u) {
      continue;
    }

    if (!alignUp(chunk->offset, 4u, &flushBytes)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    result = GPUQueueWriteBuffer(queue,
                                 chunk->buffer,
                                 0u,
                                 chunk->cpuPtr,
                                 flushBytes);

    if (result != GPU_OK) {
      return result;
    }
  }

  return GPU_OK;
}

GPU_HIDE
GPUResult
devicePrepareFrame(GPUDevice *device, uint32_t *outFrameIndex) {
  if (!device || !outFrameIndex) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return devicePrepareFrameSlot(device, outFrameIndex);
}

GPU_HIDE
void
deviceActivateFrame(GPUDevice *device, uint32_t frameIndex) {
  if (!device) {
    return;
  }

  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  deviceActivateFrameSlot(device, frameIndex);
}

GPU_HIDE
void
deviceEndFrame(GPUDevice *device) {
  if (!device) {
    return;
  }

  device->lastFrameStats = device->currentFrameStats;
}

GPU_HIDE
void
deviceRecordHotPathAlloc(GPUDevice *device, uint64_t sizeBytes) {
  if (!device) {
    return;
  }

  device->currentFrameStats.hotPathAllocCount++;
  device->currentFrameStats.hotPathAllocBytes += sizeBytes;
}

GPU_HIDE
void
deviceRecordHotPathFree(GPUDevice *device, uint64_t sizeBytes) {
  if (!device) {
    return;
  }

  device->currentFrameStats.hotPathFreeCount++;
  device->currentFrameStats.hotPathFreeBytes += sizeBytes;
}

GPU_HIDE
void
deviceRecordGPUFrameTime(GPUDevice *device, double milliseconds) {
  uint64_t bits;

  if (!device || !device->runtimeConfig.enableStats
      || !(milliseconds > 0.0)) {
    return;
  }

  memcpy(&bits, &milliseconds, sizeof(bits));
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange64((volatile LONG64 *)&device->_completedGPUFrameTimeBits,
                        (LONG64)bits);
#else
  __atomic_store_n(&device->_completedGPUFrameTimeBits,
                   bits,
                   __ATOMIC_RELEASE);
#endif
}

GPU_HIDE
void
deviceReportError(GPUDevice          *device,
                  GPUDeviceErrorType  type,
                  GPUDeviceLostReason lostReason,
                  GPUResult           result,
                  const char         *message) {
  GPUDeviceErrorInfo     info;
  GPUDeviceErrorCallback callback;

  if (!device || type < GPU_DEVICE_ERROR_VALIDATION
      || type > GPU_DEVICE_ERROR_LOST) {
    return;
  }

  if (type == GPU_DEVICE_ERROR_LOST && !reportDeviceLostOnce(device)) {
    return;
  }

  callback        = device->errorCallback;
  info.message    = message;
  info.result     = result;
  info.type       = type;
  info.lostReason = type == GPU_DEVICE_ERROR_LOST
                      ? lostReason
                      : GPU_DEVICE_LOST_REASON_UNKNOWN;

  if (callback) {
    callback(device, &info, device->errorUserData);
    return;
  }

#if GPU_BUILD_WITH_VALIDATION
  if (!device->runtimeConfig.enableVerboseLogs) {
    return;
  }

  fprintf(stderr,
          type == GPU_DEVICE_ERROR_VALIDATION ? "GPU validation: %s\n" : "GPU device error: %s\n",
          message ? message : "unknown error");
#endif
}

#if GPU_BUILD_WITH_VALIDATION
GPU_HIDE
void
deviceRecordValidationError(GPUDevice *device, const char *message) {
  if (!device
      || device->runtimeConfig.validationMode == GPU_VALIDATION_OFF) {
    return;
  }

  deviceReportError(device,
                    GPU_DEVICE_ERROR_VALIDATION,
                    GPU_DEVICE_LOST_REASON_UNKNOWN,
                    GPU_ERROR_INVALID_ARGUMENT,
                    message ? message : "validation error");
}
#endif

GPU_EXPORT
GPUResult
GPUEnumerateAdapters(GPUInstance *inst,
                     uint32_t    *inoutAdapterCount,
                     GPUAdapter **outAdapters) {
  GPUAdapter *deviceList;
  GPUAdapter *item;
  uint32_t    capacity;
  uint32_t    count;
  uint32_t    i;

  if (!inst || !inoutAdapterCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity   = *inoutAdapterCount;
  deviceList = getInstanceAdapters(inst);
  count      = inst->_adapterCount;
  i          = 0u;

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
GPURequestAdapter(GPUInstance                    *inst,
                  const GPUAdapterRequestOptions *options,
                  GPUAdapterRequestCallback       callback,
                  void                           *userData) {
  AdapterRequestContext    *request;
  GPUAdapter               *adapter;
  Api                      *api;
  uint64_t                  requiredFeatureMask;
  GPUResult                 result;
  GPUPowerPreference        preference;
  GPUWorkload               workload;

  if (!inst || !callback || !(api = instanceApi(inst))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = adapterRequestMask(options,
                              &preference,
                              &workload,
                              &requiredFeatureMask);

  if (result != GPU_OK) {
    return result;
  }

  if (inst->_adapters) {
    adapter = selectRequestedAdapter(inst,
                                     preference,
                                     workload,
                                     requiredFeatureMask);
    result = adapter ? GPU_OK : GPU_ERROR_UNSUPPORTED;
    callback(result, adapter, userData);
    return result;
  }

  if (!api->device.requestAdapter) {
    adapter = selectRequestedAdapter(inst,
                                     preference,
                                     workload,
                                     requiredFeatureMask);
    result  = adapter ? GPU_OK : GPU_ERROR_UNSUPPORTED;
    callback(result, adapter, userData);
    return result;
  }

  if (!(request = calloc(1, sizeof(*request)))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  request->instance            = inst;
  request->callback            = callback;
  request->userData            = userData;
  request->requiredFeatureMask = requiredFeatureMask;
  request->workload            = workload;

  result = api->device.requestAdapter(inst,
                                      preference,
                                      completeAdapterRequest,
                                      request);

  if (result != GPU_OK) {
    free(request);
  }

  return result;
}

GPU_EXPORT
GPUResult
GPUGetAdapterProperties(const GPUAdapter     *adapter,
                        GPUAdapterProperties *outProps) {
  Api       *api;
  GPUBackend backend;

  if (!adapter || !outProps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outProps, 0, sizeof(*outProps));
  if ((api = adapterApi(adapter)) && api->device.getAdapterProperties) {
    return api->device.getAdapterProperties(adapter, outProps);
  }

  backend = api ? api->backend : GPU_BACKEND_DEFAULT;

  outProps->backend = backend;
  outProps->type    = GPU_ADAPTER_TYPE_UNKNOWN;
  outProps->name    = gpu_backendName(backend);

  if (adapter->supportsSwapchain) {
    outProps->executionFlags |= GPU_EXECUTION_GRAPHICS_BIT;
  }

  if (api && api->device.supportsFeature
      && api->device.supportsFeature(adapter, GPU_FEATURE_COMPUTE)) {
    outProps->executionFlags |= GPU_EXECUTION_COMPUTE_BIT;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAdapterIdentity(const GPUAdapter   *adapter,
                      GPUAdapterIdentity *outIdentity) {
  Api                    *api;
  GPUAdapterIdentityFlags knownFlags;
  GPUResult               result;

  if (!adapter || !outIdentity) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outIdentity, 0, sizeof(*outIdentity));
  if (!(api = adapterApi(adapter)) || !api->device.getAdapterIdentity) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->device.getAdapterIdentity(adapter, outIdentity);

  if (result != GPU_OK) {
    memset(outIdentity, 0, sizeof(*outIdentity));
    return result;
  }

  knownFlags = GPU_ADAPTER_IDENTITY_UUID_BIT |
               GPU_ADAPTER_IDENTITY_LUID_BIT |
               GPU_ADAPTER_IDENTITY_REGISTRY_ID_BIT;
  outIdentity->validFlags &= knownFlags;

  if (outIdentity->validFlags == 0u) {
    memset(outIdentity, 0, sizeof(*outIdentity));
    return GPU_ERROR_UNSUPPORTED;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUAdaptersSharePhysicalDevice(const GPUAdapter *first,
                               const GPUAdapter *second,
                               bool             *outSameDevice) {
  GPUAdapterIdentity      firstIdentity, secondIdentity;
  GPUAdapterIdentityFlags commonFlags;
  GPUResult               result;

  if (!first || !second || !outSameDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSameDevice = false;

  if (first == second) {
    *outSameDevice = true;
    return GPU_OK;
  }

  result = GPUGetAdapterIdentity(first, &firstIdentity);

  if (result != GPU_OK) {
    return result;
  }

  result = GPUGetAdapterIdentity(second, &secondIdentity);

  if (result != GPU_OK) {
    return result;
  }

  commonFlags = firstIdentity.validFlags & secondIdentity.validFlags;

  if (commonFlags == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if ((commonFlags & GPU_ADAPTER_IDENTITY_UUID_BIT) != 0u
      && memcmp(firstIdentity.deviceUUID,
                secondIdentity.deviceUUID,
                sizeof(firstIdentity.deviceUUID)) != 0) {
    return GPU_OK;
  }

  if ((commonFlags & GPU_ADAPTER_IDENTITY_REGISTRY_ID_BIT) != 0u
      && firstIdentity.registryID != secondIdentity.registryID) {
    return GPU_OK;
  }

  if ((commonFlags & GPU_ADAPTER_IDENTITY_LUID_BIT) != 0u) {
    if (firstIdentity.luid != secondIdentity.luid) {
      return GPU_OK;
    }

    if (firstIdentity.luidNodeMask != 0u
        && secondIdentity.luidNodeMask != 0u
        && (firstIdentity.luidNodeMask & secondIdentity.luidNodeMask) == 0u) {
      return GPU_OK;
    }
  }

  *outSameDevice = true;

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
  ensureAdapterFeatureSet((GPUAdapter *)adapter);

  outCaps->supported = adapter->supportedFeatures;
  fillAdapterLimits(adapter, &outCaps->limits);

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
  fillAdapterLimits(device->adapter, &outCaps->limits);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetFormatCapabilities(const GPUAdapter      *adapter,
                         GPUFormat              format,
                         GPUFormatCapabilities *outCaps) {
  Api                      *api;
  const GPUSampleCountFlags knownSampleCounts = GPU_SAMPLE_COUNT_1_BIT |
                                                GPU_SAMPLE_COUNT_2_BIT |
                                                GPU_SAMPLE_COUNT_4_BIT |
                                                GPU_SAMPLE_COUNT_8_BIT;
  bool                      color;
  bool                      integerFormat;

  if (!adapter || !outCaps
      || format <= GPU_FORMAT_UNDEFINED || format >= GPU_FORMAT_COUNT) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outCaps, 0, sizeof(*outCaps));

  if (formatIsDepthStencil(format)) {
    outCaps->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT;
    outCaps->depthStencil          = true;
  } else {
    color = formatIsKnownColor(format);

    if (color) {
      integerFormat = formatIsInteger(format);

      outCaps->supportedSampleCounts = GPU_SAMPLE_COUNT_1_BIT;
      outCaps->sampled               = true;
      outCaps->filterable            = !integerFormat;
      outCaps->storage               = !integerFormat;
      outCaps->colorAttachment       = true;
      outCaps->blendable             = !integerFormat;
    }
  }

  if ((api = adapterApi(adapter)) && api->device.getFormatCapabilities) {
    api->device.getFormatCapabilities(adapter, format, outCaps);
  }

  if (outCaps->colorAttachment || outCaps->depthStencil) {
    outCaps->supportedSampleCounts &= knownSampleCounts;
    outCaps->supportedSampleCounts |= GPU_SAMPLE_COUNT_1_BIT;
  } else {
    outCaps->supportedSampleCounts = 0u;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetCacheStats(GPUDevice *device, GPUCacheStats *outStats) {
  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceGetCacheStats(device, outStats);

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUConfigureRuntime(GPUDevice *device, const GPURuntimeConfig *config) {
  if (!device || !validRuntimeConfig(config)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device->runtimeConfig = *config;

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUConfigureTransientAllocator(GPUDevice                         *device,
                               const GPUTransientAllocatorConfig *config) {
  GPUBuffer          *buffer;
  GPUFence          **frameFences;
  void               *cpuPtr;
  uint64_t            capacityBytes;
  GPUBufferUsageFlags usage;
  GPUResult           result;
  bool                cpuPtrOwned;

  if (!device || !validTransientAllocatorConfig(config, &capacityBytes)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  buffer      = NULL;
  frameFences = NULL;
  cpuPtr      = NULL;
  cpuPtrOwned = false;
  usage       = knownTransientBufferUsageMask();
  result      = createTransientBuffer(device,
                                      usage,
                                      capacityBytes,
                                      &buffer,
                                      &cpuPtr,
                                      &cpuPtrOwned);

  if (result != GPU_OK) {
    usage  = transientUploadUsageMask();
    result = createTransientBuffer(device,
                                   usage,
                                   capacityBytes,
                                   &buffer,
                                   &cpuPtr,
                                   &cpuPtrOwned);
  }

  if (result != GPU_OK) {
    return result;
  }

  result = createTransientFrameFences(device,
                                      config->framesInFlight,
                                      &frameFences);

  if (result != GPU_OK) {
    if (cpuPtrOwned) {
      free(cpuPtr);
    }

    GPUDestroyBuffer(buffer);
    return result;
  }

  destroyTransientAllocator(device);

  device->transientBuffer      = buffer;
  device->transientFrameFences = frameFences;
  device->transientCpuPtr      = cpuPtr;
  device->transientBufferUsage = usage;
  device->transientConfig      = *config;
  device->transientConfigured  = true;
  device->transientCpuPtrOwned = cpuPtrOwned;
  device->transientFrameIndex  = 0u;
  device->transientFrameStride = capacityBytes / config->framesInFlight;

  device->allocatorStats.ringCapacityBytes = capacityBytes;

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUAllocateTransientBuffer(GPUDevice               *device,
                           GPUBufferUsageFlags      usage,
                           uint64_t                 sizeBytes,
                           uint64_t                 alignment,
                           GPUTransientBufferSlice *outSlice) {
  uint64_t alignedOffset;
  uint64_t endOffset;
  uint64_t frameBaseOffset;

  if (!outSlice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!device
      || !device->transientConfigured
      || usage == 0u
      || sizeBytes == 0u
      || alignment == 0u) {
    memset(outSlice, 0, sizeof(*outSlice));
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!device->transientBuffer
      || (usage & ~device->transientBufferUsage) != 0u) {
    memset(outSlice, 0, sizeof(*outSlice));
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!alignUp(device->transientFrameOffset, alignment, &alignedOffset)
      || u64AddOverflow(alignedOffset, sizeBytes, &endOffset)) {
    memset(outSlice, 0, sizeof(*outSlice));
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (endOffset > device->transientConfig.ringBytesPerFrame) {
    memset(outSlice, 0, sizeof(*outSlice));
    return allocateTransientChunk(device,
                                  usage,
                                  sizeBytes,
                                  alignment,
                                  outSlice);
  }

  frameBaseOffset = (uint64_t)device->transientFrameIndex *
                    device->transientFrameStride;
  outSlice->buffer    = device->transientBuffer;
  outSlice->offset    = frameBaseOffset + alignedOffset;
  outSlice->sizeBytes = sizeBytes;
  outSlice->cpuPtr    = (uint8_t *)device->transientCpuPtr + outSlice->offset;

  device->transientFrameOffset = endOffset;

  if (endOffset > device->allocatorStats.ringHighWaterBytes) {
    device->allocatorStats.ringHighWaterBytes = endOffset;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetLastFrameStats(GPUDevice *device, GPUFrameStats *outStats) {
  uint64_t gpuFrameTimeBits;

  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStats = device->lastFrameStats;

  if (device->runtimeConfig.enableStats) {
#if defined(_WIN32) || defined(WIN32)
    gpuFrameTimeBits = (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)&device->_completedGPUFrameTimeBits,
                                                              0,
                                                              0);
#else
    gpuFrameTimeBits = __atomic_load_n(&device->_completedGPUFrameTimeBits,
                                       __ATOMIC_ACQUIRE);
#endif
    memcpy(&outStats->gpuFrameMs,
           &gpuFrameTimeBits,
           sizeof(outStats->gpuFrameMs));
  }

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetAllocatorStats(GPUDevice *device, GPUAllocatorStats *outStats) {
  if (!device || !outStats) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outStats               = device->allocatorStats;
  outStats->ringUsedBytes = device->transientFrameOffset;

  return GPU_OK;
}

GPU_EXPORT
void
GPUResetStats(GPUDevice *device) {
  if (!device) {
    return;
  }

  deviceResetCacheStats(device);
  memset(&device->currentFrameStats, 0, sizeof(device->currentFrameStats));
  memset(&device->lastFrameStats, 0, sizeof(device->lastFrameStats));
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange64((volatile LONG64 *)&device->_completedGPUFrameTimeBits,
                        0);
#else
  __atomic_store_n(&device->_completedGPUFrameTimeBits,
                   0u,
                   __ATOMIC_RELEASE);
#endif
  device->allocatorStats.ringUsedBytes      = device->transientFrameOffset;
  device->allocatorStats.ringHighWaterBytes = device->transientFrameOffset;
  device->allocatorStats.ringWrapCount      = 0;
  device->allocatorStats.uploadStallCount   = 0;
}

GPU_EXPORT
GPUResult
GPUSetDeviceErrorCallback(GPUDevice             *device,
                          GPUDeviceErrorCallback callback,
                          void                  *userData) {
  if (!device) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device->errorUserData = callback ? userData : NULL;
  device->errorCallback = callback;

  return GPU_OK;
}

GPU_EXPORT
bool
GPUIsFeatureSupported(const GPUAdapter *adapter, GPUFeature feature) {
  if (!adapter || !knownFeature(feature)) {
    return false;
  }

  return adapterSupportsFeature(adapter, feature);
}

GPU_EXPORT
bool
GPUIsFeatureEnabled(const GPUDevice *device, GPUFeature feature) {
  if (!device || !knownFeature(feature)) {
    return false;
  }

  return (device->enabledFeatureMask & featureBit(feature)) != 0;
}

GPU_EXPORT
GPUProc
GPUGetProcAddr(GPUDevice *device, const char *name) {
  Api    *api;

  if (!(api = deviceApi(device)) || !name || name[0] == '\0') {
    return NULL;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_ML_MODEL_EXT)
      && api->ml.createModel && api->ml.destroyModel && api->ml.createPipeline
      && api->ml.createBindings && api->ml.destroyBindings && api->ml.encode) {
    if (strcmp(name, "GPUCreateMLModelEXT") == 0) {
      return (GPUProc)GPUCreateMLModelEXT;
    }

    if (strcmp(name, "GPUDestroyMLModelEXT") == 0) {
      return (GPUProc)GPUDestroyMLModelEXT;
    }

    if (strcmp(name, "GPUCreateMLPipelineEXT") == 0) {
      return (GPUProc)GPUCreateMLPipelineEXT;
    }

    if (strcmp(name, "GPUGetMLPipelineInfoEXT") == 0) {
      return (GPUProc)GPUGetMLPipelineInfoEXT;
    }

    if (strcmp(name, "GPUDestroyMLPipelineEXT") == 0) {
      return (GPUProc)GPUDestroyMLPipelineEXT;
    }

    if (strcmp(name, "GPUCreateMLBindingsEXT") == 0) {
      return (GPUProc)GPUCreateMLBindingsEXT;
    }

    if (strcmp(name, "GPUDestroyMLBindingsEXT") == 0) {
      return (GPUProc)GPUDestroyMLBindingsEXT;
    }

    if (strcmp(name, "GPUEncodeMLEXT") == 0) {
      return (GPUProc)GPUEncodeMLEXT;
    }
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_TENSOR_RESOURCES_EXT)
      && api->tensor.getBufferRequirements && api->tensor.createView && api->tensor.destroy) {
    if (strcmp(name, "GPUGetTensorBufferRequirementsEXT") == 0) {
      return (GPUProc)GPUGetTensorBufferRequirementsEXT;
    }

    if (strcmp(name, "GPUCreateTensorViewEXT") == 0) {
      return (GPUProc)GPUCreateTensorViewEXT;
    }

    if (strcmp(name, "GPUGetTensorDescEXT") == 0) {
      return (GPUProc)GPUGetTensorDescEXT;
    }

    if (strcmp(name, "GPUGetTensorBufferEXT") == 0) {
      return (GPUProc)GPUGetTensorBufferEXT;
    }

    if (strcmp(name, "GPUDestroyTensorEXT") == 0) {
      return (GPUProc)GPUDestroyTensorEXT;
    }
  }

  if (api->multigpu.createInterop && api->multigpu.destroyInterop) {
    if (strcmp(name, "GPUCreateDeviceInteropEXT") == 0) {
      return (GPUProc)GPUCreateDeviceInteropEXT;
    }

    if (strcmp(name, "GPUDestroyDeviceInteropEXT") == 0) {
      return (GPUProc)GPUDestroyDeviceInteropEXT;
    }
  }

  if (api->multigpu.getBufferRequirements
      && strcmp(name, "GPUGetSharedBufferMemoryRequirementsEXT") == 0) {
    return (GPUProc)GPUGetSharedBufferMemoryRequirementsEXT;
  }

  if (api->multigpu.createBuffer
      && strcmp(name, "GPUCreateSharedBufferEXT") == 0) {
    return (GPUProc)GPUCreateSharedBufferEXT;
  }

  if (api->multigpu.getTextureRequirements
      && strcmp(name, "GPUGetSharedTextureMemoryRequirementsEXT") == 0) {
    return (GPUProc)GPUGetSharedTextureMemoryRequirementsEXT;
  }

  if (api->multigpu.createTexture
      && strcmp(name, "GPUCreateSharedTextureEXT") == 0) {
    return (GPUProc)GPUCreateSharedTextureEXT;
  }

  if (api->multigpu.createSemaphore
      && strcmp(name, "GPUCreateSharedSemaphoreEXT") == 0) {
    return (GPUProc)GPUCreateSharedSemaphoreEXT;
  }

  if (api->multigpu.encodeRelease
      && strcmp(name, "GPUEncodeSharedReleaseEXT") == 0) {
    return (GPUProc)GPUEncodeSharedReleaseEXT;
  }

  if (api->multigpu.encodeAcquire
      && strcmp(name, "GPUEncodeSharedAcquireEXT") == 0) {
    return (GPUProc)GPUEncodeSharedAcquireEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_BINDLESS)
      && strcmp(name, "GPUUpdateBindGroupEXT") == 0) {
    return (GPUProc)GPUUpdateBindGroupEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_MESH_SHADER)
      && strcmp(name, "GPUDrawMeshEXT") == 0) {
    return (GPUProc)GPUDrawMeshEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_SUBGROUP_MATRIX)
      && strcmp(name, "GPUGetSubgroupMatrixPropertiesEXT") == 0) {
    return (GPUProc)GPUGetSubgroupMatrixPropertiesEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_DEVICE_ADDRESS)
      && strcmp(name, "GPUGetBufferDeviceAddressEXT") == 0) {
    return (GPUProc)GPUGetBufferDeviceAddressEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_MEMORY_COPY)
      && strcmp(name, "GPUCopyMemoryIndirectEXT") == 0) {
    return (GPUProc)GPUCopyMemoryIndirectEXT;
  }

  if (GPUIsFeatureEnabled(device,
                          GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY)
      && strcmp(name, "GPUCopyMemoryToTextureIndirectEXT") == 0) {
    return (GPUProc)GPUCopyMemoryToTextureIndirectEXT;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_VARIABLE_RATE_SHADING)) {
    if (strcmp(name, "GPUGetVRSCapabilitiesEXT") == 0) {
      return (GPUProc)GPUGetVRSCapabilitiesEXT;
    }

    if (strcmp(name, "GPUCreateRasterizationRateMapEXT") == 0) {
      return (GPUProc)GPUCreateRasterizationRateMapEXT;
    }

    if (strcmp(name, "GPUDestroyRasterizationRateMapEXT") == 0) {
      return (GPUProc)GPUDestroyRasterizationRateMapEXT;
    }

    if (strcmp(name, "GPUGetRasterizationRateMapPhysicalSizeEXT") == 0) {
      return (GPUProc)GPUGetRasterizationRateMapPhysicalSizeEXT;
    }

    if (strcmp(name, "GPUMapRasterizationRateScreenToPhysicalEXT") == 0) {
      return (GPUProc)GPUMapRasterizationRateScreenToPhysicalEXT;
    }

    if (strcmp(name, "GPUMapRasterizationRatePhysicalToScreenEXT") == 0) {
      return (GPUProc)GPUMapRasterizationRatePhysicalToScreenEXT;
    }

    if (strcmp(name, "GPUGetRasterizationRateMapParameterInfoEXT") == 0) {
      return (GPUProc)GPUGetRasterizationRateMapParameterInfoEXT;
    }

    if (strcmp(name, "GPUCopyRasterizationRateMapParametersEXT") == 0) {
      return (GPUProc)GPUCopyRasterizationRateMapParametersEXT;
    }

    if (strcmp(name, "GPUSetFragmentShadingRateEXT") == 0) {
      return (GPUProc)GPUSetFragmentShadingRateEXT;
    }
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_QUERY)) {
    if (strcmp(name, "GPUGetAccelerationStructureSizesEXT") == 0) {
      return (GPUProc)GPUGetAccelerationStructureSizesEXT;
    }

    if (strcmp(name, "GPUCreateAccelerationStructureEXT") == 0) {
      return (GPUProc)GPUCreateAccelerationStructureEXT;
    }

    if (strcmp(name, "GPUDestroyAccelerationStructureEXT") == 0) {
      return (GPUProc)GPUDestroyAccelerationStructureEXT;
    }

    if (strcmp(name, "GPUBeginAccelerationStructurePassEXT") == 0) {
      return (GPUProc)GPUBeginAccelerationStructurePassEXT;
    }

    if (strcmp(name, "GPUBuildAccelerationStructureEXT") == 0) {
      return (GPUProc)GPUBuildAccelerationStructureEXT;
    }

    if (strcmp(name, "GPUEndAccelerationStructurePassEXT") == 0) {
      return (GPUProc)GPUEndAccelerationStructurePassEXT;
    }
  }

  if (GPUIsFeatureEnabled(device,
                          GPU_FEATURE_INTERSECTION_FUNCTION_TABLE)) {
    if (strcmp(name, "GPUCreateIntersectionFunctionTableEXT") == 0) {
      return (GPUProc)GPUCreateIntersectionFunctionTableEXT;
    }

    if (strcmp(name, "GPUDestroyIntersectionFunctionTableEXT") == 0) {
      return (GPUProc)GPUDestroyIntersectionFunctionTableEXT;
    }

    if (strcmp(name, "GPUSetIntersectionFunctionTableBufferEXT") == 0) {
      return (GPUProc)GPUSetIntersectionFunctionTableBufferEXT;
    }

    if (strcmp(name, "GPUBindComputeIntersectionFunctionTableEXT") == 0) {
      return (GPUProc)GPUBindComputeIntersectionFunctionTableEXT;
    }

    if (strcmp(name, "GPUBindRenderIntersectionFunctionTableEXT") == 0) {
      return (GPUProc)GPUBindRenderIntersectionFunctionTableEXT;
    }
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_RAY_TRACING_PIPELINE)) {
    if (strcmp(name, "GPUCreateRayTracingPipelineEXT") == 0) {
      return (GPUProc)GPUCreateRayTracingPipelineEXT;
    }

    if (strcmp(name, "GPUDestroyRayTracingPipelineEXT") == 0) {
      return (GPUProc)GPUDestroyRayTracingPipelineEXT;
    }

    if (strcmp(name, "GPUCreateShaderTableEXT") == 0) {
      return (GPUProc)GPUCreateShaderTableEXT;
    }

    if (strcmp(name, "GPUDestroyShaderTableEXT") == 0) {
      return (GPUProc)GPUDestroyShaderTableEXT;
    }

    if (strcmp(name, "GPUBeginRayTracingPassEXT") == 0) {
      return (GPUProc)GPUBeginRayTracingPassEXT;
    }

    if (strcmp(name, "GPUBindRayTracingPipelineEXT") == 0) {
      return (GPUProc)GPUBindRayTracingPipelineEXT;
    }

    if (strcmp(name, "GPUBindRayTracingGroupEXT") == 0) {
      return (GPUProc)GPUBindRayTracingGroupEXT;
    }

    if (strcmp(name, "GPUDispatchRaysEXT") == 0) {
      return (GPUProc)GPUDispatchRaysEXT;
    }

    if (strcmp(name, "GPUEndRayTracingPassEXT") == 0) {
      return (GPUProc)GPUEndRayTracingPassEXT;
    }
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_EXECUTION_GRAPH)) {
    if (strcmp(name, "GPUCreateExecutionGraphEXT") == 0) {
      return (GPUProc)GPUCreateExecutionGraphEXT;
    }

    if (strcmp(name, "GPUDestroyExecutionGraphEXT") == 0) {
      return (GPUProc)GPUDestroyExecutionGraphEXT;
    }

    if (strcmp(name, "GPUGetExecutionGraphMemoryRequirementsEXT") == 0) {
      return (GPUProc)GPUGetExecutionGraphMemoryRequirementsEXT;
    }

    if (strcmp(name, "GPUCreateExecutionGraphInstanceEXT") == 0) {
      return (GPUProc)GPUCreateExecutionGraphInstanceEXT;
    }

    if (strcmp(name, "GPUDestroyExecutionGraphInstanceEXT") == 0) {
      return (GPUProc)GPUDestroyExecutionGraphInstanceEXT;
    }

    if (strcmp(name, "GPUGetExecutionGraphEntryEXT") == 0) {
      return (GPUProc)GPUGetExecutionGraphEntryEXT;
    }

    if (strcmp(name, "GPUBindExecutionGraphEXT") == 0) {
      return (GPUProc)GPUBindExecutionGraphEXT;
    }

    if (strcmp(name, "GPUDispatchExecutionGraphEXT") == 0) {
      return (GPUProc)GPUDispatchExecutionGraphEXT;
    }

    if (strcmp(name, "GPUDispatchExecutionGraphBufferEXT") == 0) {
      return (GPUProc)GPUDispatchExecutionGraphBufferEXT;
    }
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_SAMPLER_FEEDBACK)) {
    if (strcmp(name, "GPUCreateSamplerFeedbackMapEXT") == 0) {
      return (GPUProc)GPUCreateSamplerFeedbackMapEXT;
    }

    if (strcmp(name, "GPUDestroySamplerFeedbackMapEXT") == 0) {
      return (GPUProc)GPUDestroySamplerFeedbackMapEXT;
    }

    if (strcmp(name, "GPUGetSamplerFeedbackDecodeInfoEXT") == 0) {
      return (GPUProc)GPUGetSamplerFeedbackDecodeInfoEXT;
    }

    if (strcmp(name, "GPUClearSamplerFeedbackEXT") == 0) {
      return (GPUProc)GPUClearSamplerFeedbackEXT;
    }

    if (strcmp(name, "GPUDecodeSamplerFeedbackEXT") == 0) {
      return (GPUProc)GPUDecodeSamplerFeedbackEXT;
    }

    if (strcmp(name, "GPUEncodeSamplerFeedbackEXT") == 0) {
      return (GPUProc)GPUEncodeSamplerFeedbackEXT;
    }
  }

  return NULL;
}

GPU_EXPORT
GPUAdapter*
GPUGetAutoSelectedAdapter(GPUInstance *inst) {
  return selectRequestedAdapter(inst,
                                GPU_POWER_PREFERENCE_DEFAULT,
                                GPU_WORKLOAD_DEFAULT,
                                0u);
}

GPU_EXPORT
GPUResult
GPUCreateDevice(GPUAdapter                *adapter,
                const GPUDeviceCreateInfo *info,
                GPUDevice                **outDevice) {
  QueueCreateInfo     stackQueueInfos[8];
  QueueCreateInfo    *queueInfos;
  Api                *api;
  uint64_t            enabledFeatureMask;
  uint32_t            queueInfoCount;
  GPUResult           result;
  GPUResult           featureResult;

  if (!outDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outDevice = NULL;

  if (!adapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info) {
    if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
        && info->chain.sType != GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    featureResult = validateFeatureSet(adapter, &info->required, true);

    if (featureResult != GPU_OK) {
      return featureResult;
    }

    featureResult = validateFeatureSet(adapter, &info->optional, false);

    if (featureResult != GPU_OK) {
      return featureResult;
    }
  }

  enabledFeatureMask = enabledFeatureMaskForCreateInfo(adapter, info);
  queueInfos         = NULL;
  queueInfoCount     = 0;
  result             = buildQueueCreateInfos(info,
                                             stackQueueInfos,
                                             (uint32_t)GPU_ARRAY_LEN(stackQueueInfos),
                                             &queueInfos,
                                             &queueInfoCount);

  if (result != GPU_OK) {
    return result;
  }

  if (!validQueueCreateInfos(queueInfos, queueInfoCount)) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = adapterApi(adapter)) || !api->device.createDevice) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_BACKEND_FAILURE;
  }

  *outDevice = api->device.createDevice(adapter,
                                        queueInfos,
                                        queueInfoCount,
                                        enabledFeatureMask);

  if (queueInfos != stackQueueInfos) {
    free(queueInfos);
  }

  if (!*outDevice) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = finalizeDevice(adapter, *outDevice, enabledFeatureMask);

  if (result != GPU_OK) {
    *outDevice = NULL;
  }

  return result;
}

GPU_EXPORT
GPUResult
GPURequestDevice(GPUAdapter                *adapter,
                 const GPUDeviceCreateInfo *info,
                 GPUDeviceRequestCallback   callback,
                 void                      *userData) {
  QueueCreateInfo          stackQueueInfos[8];
  QueueCreateInfo         *queueInfos;
  DeviceRequestContext    *request;
  GPUDevice               *device;
  Api                     *api;
  uint64_t                 enabledFeatureMask;
  GPUResult                result;
  uint32_t                 queueInfoCount;

  if (!adapter || !callback || !(api = adapterApi(adapter))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!api->device.requestDevice) {
    device = NULL;
    result = GPUCreateDevice(adapter, info, &device);
    callback(result, device, userData);
    return result;
  }

  if (info) {
    if ((info->chain.sType != GPU_STRUCTURE_TYPE_NONE
         && info->chain.sType != GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO)
        || (info->chain.structSize != 0u
            && info->chain.structSize < sizeof(*info))) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    result = validateFeatureSet(adapter, &info->required, true);

    if (result != GPU_OK) {
      return result;
    }

    result = validateFeatureSet(adapter, &info->optional, false);

    if (result != GPU_OK) {
      return result;
    }
  }

  enabledFeatureMask = enabledFeatureMaskForCreateInfo(adapter, info);
  queueInfos         = NULL;
  queueInfoCount     = 0u;
  result             = buildQueueCreateInfos(info,
                                             stackQueueInfos,
                                             (uint32_t)GPU_ARRAY_LEN(stackQueueInfos),
                                             &queueInfos,
                                             &queueInfoCount);

  if (result != GPU_OK) {
    return result;
  }

  if (!validQueueCreateInfos(queueInfos, queueInfoCount)) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(request = calloc(1, sizeof(*request)))) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  request->adapter            = adapter;
  request->callback           = callback;
  request->userData           = userData;
  request->enabledFeatureMask = enabledFeatureMask;

  result = api->device.requestDevice(adapter,
                                     queueInfos,
                                     queueInfoCount,
                                     enabledFeatureMask,
                                     completeDeviceRequest,
                                     request);

  if (queueInfos != stackQueueInfos) {
    free(queueInfos);
  }

  if (result != GPU_OK) {
    free(request);
  }

  return result;
}

GPU_EXPORT
GPUDevice*
GPUCreateDeviceWithDefaultQueues(GPUAdapter *adapter) {
  GPUDevice *device;

  if (GPUCreateDevice(adapter, NULL, &device) != GPU_OK) {
    return NULL;
  }

  return device;
}

GPU_EXPORT
GPUQueueFlagBits
GPUGetAvailableQueueBits(GPUDevice *__restrict device) {
  if (!device) {
    return 0;
  }

  return device->queueFamilies;
}

GPU_EXPORT
void
GPUDestroyDevice(GPUDevice *__restrict device) {
  Api    *api;

  if (!device) {
    return;
  }

  if (!(api = deviceApi(device))) {
    return;
  }

  if (api->device.waitIdle) {
    (void)api->device.waitIdle(device);
  }

  destroyTransientAllocator(device);
  destroyBlitDevice(device);
  destroyBindGroupCacheDevice(device);
  destroyPipelineCacheDevice(device);

  if (api->device.destroyDevice) {
    api->device.destroyDevice(device);
  }
}
