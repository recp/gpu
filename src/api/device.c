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

GPU_EXPORT
GPUDevice*
GPUCreateSystemDefaultDevice(GPUInstance *inst) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.createSystemDefaultDevice(api, inst);
}

GPU_EXPORT
GPUPhysicalDevice*
GPUGetAvailablePhysicalDevicesBy(GPUInstance *inst, uint32_t maxNumberOfItems) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.getAvailablePhysicalDevicesBy(api, inst, maxNumberOfItems);
}

GPU_EXPORT
GPUDevice *
GPUCreateDevice(GPUPhysicalDevice *phyDevice) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.createDevice(phyDevice);
}