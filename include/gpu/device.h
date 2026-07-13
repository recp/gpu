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

#ifndef gpu_device_h
#define gpu_device_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "instance.h"
#include "cmdqueue.h"
#include "format.h"
#include "buffer.h"

typedef struct GPUAdapter GPUAdapter;

typedef enum GPUAdapterType {
  GPU_ADAPTER_TYPE_UNKNOWN = 0,
  GPU_ADAPTER_TYPE_INTEGRATED = 1,
  GPU_ADAPTER_TYPE_DISCRETE = 2,
  GPU_ADAPTER_TYPE_SOFTWARE = 3
} GPUAdapterType;

typedef struct GPUAdapterProperties {
  const char    *name;
  GPUBackend     backend;
  GPUAdapterType type;
} GPUAdapterProperties;

typedef struct GPUDevice GPUDevice;

typedef enum GPUDeviceErrorType {
  GPU_DEVICE_ERROR_VALIDATION = 0,
  GPU_DEVICE_ERROR_OUT_OF_MEMORY,
  GPU_DEVICE_ERROR_BACKEND,
  GPU_DEVICE_ERROR_LOST
} GPUDeviceErrorType;

typedef enum GPUDeviceLostReason {
  GPU_DEVICE_LOST_REASON_UNKNOWN = 0,
  GPU_DEVICE_LOST_REASON_REMOVED,
  GPU_DEVICE_LOST_REASON_RESET,
  GPU_DEVICE_LOST_REASON_HUNG,
  GPU_DEVICE_LOST_REASON_DRIVER_ERROR
} GPUDeviceLostReason;

typedef struct GPUDeviceErrorInfo {
  const char          *message;
  GPUResult            result;
  GPUDeviceErrorType   type;
  GPUDeviceLostReason  lostReason;
} GPUDeviceErrorInfo;

typedef void (*GPUDeviceErrorCallback)(GPUDevice                *device,
                                       const GPUDeviceErrorInfo *error,
                                       void                     *userData);

typedef void (*GPUProc)(void);

typedef enum GPUFeature {
  GPU_FEATURE_COMPUTE = 0,
  GPU_FEATURE_TIMESTAMPS = 1,
  GPU_FEATURE_PIPELINE_STATISTICS = 2,
  GPU_FEATURE_INDIRECT_DRAW = 3,
  GPU_FEATURE_MULTI_DRAW = 4,
  GPU_FEATURE_SUBGROUPS = 5,
  GPU_FEATURE_SHADER_F16 = 6,
  GPU_FEATURE_DESCRIPTOR_INDEXING = 7,
  GPU_FEATURE_MESH_SHADER = 8,
  GPU_FEATURE_RAY_TRACING = 9,
  GPU_FEATURE_VARIABLE_RATE_SHADING = 10
} GPUFeature;

typedef struct GPUFeatureSet {
  uint32_t          featureCount;
  const GPUFeature *pFeatures;
} GPUFeatureSet;

typedef struct GPUQueueRequest {
  GPUQueueFlagBits type;
  uint32_t         count;
} GPUQueueRequest;

typedef struct GPUDeviceQueueCreateInfo {
  GPUChainedStruct      chain;
  uint32_t              requestCount;
  const GPUQueueRequest *pRequests;
} GPUDeviceQueueCreateInfo;

typedef struct GPUDeviceCreateInfo {
  GPUChainedStruct          chain;
  const char               *label;
  GPUFeatureSet             required;
  GPUFeatureSet             optional;
  GPUDeviceQueueCreateInfo  queues;
} GPUDeviceCreateInfo;

typedef struct GPULimits {
  uint32_t maxBindGroups;
  uint32_t maxBindingsPerGroup;
  uint32_t maxDynamicUniformBuffers;
  uint32_t maxDynamicStorageBuffers;
  uint64_t minUniformBufferOffsetAlignment;
  uint64_t minStorageBufferOffsetAlignment;
  uint32_t maxColorAttachments;
  uint32_t maxComputeWorkgroupSizeX;
  uint32_t maxComputeWorkgroupSizeY;
  uint32_t maxComputeWorkgroupSizeZ;
  uint32_t minSubgroupSize;
  uint32_t maxSubgroupSize;
} GPULimits;

typedef struct GPUAdapterCapabilities {
  GPUFeatureSet supported;
  GPULimits     limits;
} GPUAdapterCapabilities;

typedef struct GPUDeviceCapabilities {
  GPUFeatureSet enabled;
  GPULimits     limits;
} GPUDeviceCapabilities;

typedef struct GPUFormatCapabilities {
  bool sampled;
  bool filterable;
  bool storage;
  bool colorAttachment;
  bool blendable;
  bool depthStencil;
} GPUFormatCapabilities;

typedef struct GPUCacheStats {
  uint64_t bindGroupHits;
  uint64_t bindGroupMisses;
  uint64_t bindGroupCollisions;
  uint64_t pipelineHits;
  uint64_t pipelineMisses;
  uint64_t pipelineCompiles;
} GPUCacheStats;

typedef enum GPUValidationMode {
  GPU_VALIDATION_OFF = 0,
  GPU_VALIDATION_BASIC = 1,
  GPU_VALIDATION_FULL = 2
} GPUValidationMode;

typedef struct GPURuntimeConfig {
  GPUChainedStruct chain;
  GPUValidationMode validationMode;
  bool enableDebugMarkers;
  bool enableVerboseLogs;
  bool enableStats;
} GPURuntimeConfig;

typedef struct GPUTransientAllocatorConfig {
  GPUChainedStruct chain;
  uint64_t ringBytesPerFrame;
  uint32_t framesInFlight;
  uint64_t chunkBytes;
  bool allowChunkFallback;
} GPUTransientAllocatorConfig;

typedef struct GPUTransientBufferSlice {
  GPUBuffer *buffer;
  uint64_t offset;
  uint64_t sizeBytes;
  void *cpuPtr;
} GPUTransientBufferSlice;

typedef struct GPUFrameStats {
  double cpuEncodeMs;
  double gpuFrameMs;
  uint32_t drawCalls;
  uint32_t requestedStateCalls;
  uint32_t emittedStateCalls;
  uint32_t requestedBindCalls;
  uint32_t emittedBindCalls;
  uint64_t hotPathAllocCount;
  uint64_t hotPathAllocBytes;
  uint64_t hotPathFreeCount;
  uint64_t hotPathFreeBytes;
} GPUFrameStats;

typedef struct GPUAllocatorStats {
  uint64_t ringCapacityBytes;
  uint64_t ringUsedBytes;
  uint64_t ringHighWaterBytes;
  uint64_t ringWrapCount;
  uint64_t uploadStallCount;
} GPUAllocatorStats;

GPU_EXPORT
GPUResult
GPUEnumerateAdapters(GPUInstance *inst,
                     uint32_t    *inoutAdapterCount,
                     GPUAdapter **outAdapters);

GPU_EXPORT
GPUResult
GPUGetAdapterProperties(const GPUAdapter     *adapter,
                        GPUAdapterProperties *outProps);

GPU_EXPORT
GPUResult
GPUGetAdapterCapabilities(const GPUAdapter       *adapter,
                          GPUAdapterCapabilities *outCaps);

GPU_EXPORT
GPUResult
GPUGetDeviceCapabilities(const GPUDevice       *device,
                         GPUDeviceCapabilities *outCaps);

GPU_EXPORT
GPUResult
GPUGetFormatCapabilities(const GPUAdapter      *adapter,
                         GPUFormat              format,
                         GPUFormatCapabilities *outCaps);

GPU_EXPORT
GPUResult
GPUGetCacheStats(GPUDevice *device, GPUCacheStats *outStats);

GPU_EXPORT
GPUResult
GPUConfigureRuntime(GPUDevice *device, const GPURuntimeConfig *config);

GPU_EXPORT
GPUResult
GPUSetDeviceErrorCallback(GPUDevice              *device,
                          GPUDeviceErrorCallback  callback,
                          void                   *userData);

GPU_EXPORT
GPUResult
GPUConfigureTransientAllocator(GPUDevice *device,
                               const GPUTransientAllocatorConfig *config);

GPU_EXPORT
GPUResult
GPUAllocateTransientBuffer(GPUDevice *device,
                           GPUBufferUsageFlags usage,
                           uint64_t sizeBytes,
                           uint64_t alignment,
                           GPUTransientBufferSlice *outSlice);

GPU_EXPORT
GPUResult
GPUGetLastFrameStats(GPUDevice *device, GPUFrameStats *outStats);

GPU_EXPORT
GPUResult
GPUGetAllocatorStats(GPUDevice *device, GPUAllocatorStats *outStats);

GPU_EXPORT
void
GPUResetStats(GPUDevice *device);

GPU_EXPORT
bool
GPUIsFeatureSupported(const GPUAdapter *adapter, GPUFeature feature);

GPU_EXPORT
bool
GPUIsFeatureEnabled(const GPUDevice *device, GPUFeature feature);

GPU_EXPORT
GPUProc
GPUGetProcAddr(GPUDevice *device, const char *name);

GPU_EXPORT
GPUResult
GPUCreateDevice(GPUAdapter               *adapter,
                const GPUDeviceCreateInfo *info,
                GPUDevice                **outDevice);

GPU_EXPORT
GPUDevice *
GPUCreateDeviceWithDefaultQueues(GPUAdapter *adapter);

GPU_EXPORT
GPUDevice *
GPUCreateSystemDefaultDevice(GPUInstance *inst);

/*! Returns queue bits created and usable on this device. */
GPU_EXPORT
GPUQueueFlagBits
GPUGetAvailableQueueBits(GPUDevice * __restrict device);

GPU_EXPORT
void
GPUDestroyDevice(GPUDevice * __restrict device);

#ifdef __cplusplus
}
#endif
#endif /* gpu_device_h */
