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
gpu_validQueueCreateInfos(GPUCommandQueueCreateInfo queCI[], uint32_t nQueCI) {
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

GPU_EXPORT
GPUResult
GPUEnumerateAdapters(GPUInstance *inst,
                     uint32_t    *inoutAdapterCount,
                     GPUAdapter **outAdapters) {
  GPUPhysicalDevice *deviceList;
  GPUPhysicalDevice *item;
  uint32_t capacity;
  uint32_t count;

  if (!inoutAdapterCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity = *inoutAdapterCount;
  deviceList = GPUGetAvailablePhysicalDevicesBy(inst, UINT32_MAX);
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
GPUDevice*
GPUCreateSystemDefaultDevice(GPUInstance *inst) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.createSystemDefaultDevice(inst);
}

GPU_EXPORT
GPUPhysicalDevice*
GPUGetAvailablePhysicalDevicesBy(GPUInstance *inst, uint32_t maxNumberOfItems) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.getAvailablePhysicalDevicesBy(inst, maxNumberOfItems);
}

GPU_EXPORT
GPUPhysicalDevice*
GPUGetAutoSelectedPhysicalDevice(GPUInstance *inst) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.getAutoSelectedPhysicalDevice(inst);
}

GPU_EXPORT
GPUPhysicalDevice*
GPUAutoSelectPhysicalDeviceIn(GPUInstance       * __restrict inst,
                              GPUPhysicalDevice * __restrict deviceList) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.autoSelectPhysicalDeviceIn(inst, deviceList);
}

GPU_EXPORT
GPUDevice *
GPUCreateDevice(GPUPhysicalDevice        *phyDevice,
                GPUCommandQueueCreateInfo queCI[],
                uint32_t                  nQueCI) {
  GPUApi *api;

  if (!phyDevice || !gpu_validQueueCreateInfos(queCI, nQueCI)) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.createDevice(phyDevice, queCI, nQueCI);
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

  if (api->device.destroyDevice) {
    api->device.destroyDevice(device);
  }
}
