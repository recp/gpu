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
typedef GPUAdapter GPUPhysicalDevice;

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
GPUPhysicalDevice*
GPUGetAvailablePhysicalDevicesBy(GPUInstance *inst, uint32_t maxNumberOfItems);

GPU_EXPORT
GPUPhysicalDevice*
GPUGetAutoSelectedPhysicalDevice(GPUInstance *inst);

GPU_EXPORT
GPUPhysicalDevice*
GPUAutoSelectPhysicalDeviceIn(GPUInstance       * __restrict inst,
                              GPUPhysicalDevice * __restrict deviceList);

GPU_INLINE
GPUPhysicalDevice *
GPUGetFirstPhysicalDevice(GPUInstance *inst) {
  return GPUGetAvailablePhysicalDevicesBy(inst, 1);
}

#define GPUDefaultQueuesParam NULL, 0

GPU_EXPORT
GPUDevice *
GPUCreateDevice(GPUAdapter               *adapter,
                GPUCommandQueueCreateInfo queCI[],
                uint32_t                  nQueCI);

#define GPUCreateDeviceWithDefaultQueues(adapter)                             \
  GPUCreateDevice(adapter, NULL, 0)

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
