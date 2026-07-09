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
