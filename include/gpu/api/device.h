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

#ifndef gpu_gpudef_device_h
#define gpu_gpudef_device_h
#ifdef __cplusplus
extern "C" {
#endif

#include "../common.h"
#include "../gpu.h"

struct GPUApi;
struct GPUInstance;

typedef struct GPUApiDevice {
  GPUDevice* (*createSystemDefaultDevice)(struct GPUApi*, GPUInstance * __restrict inst);
  GPUPhysicalDevice* (*getAvailablePhysicalDevicesBy)(GPUInstance   * __restrict inst,
                                                      uint32_t maxNumberOfItems);
  GPUDevice* (*createDevice)(GPUPhysicalDevice * __restrict phyDevice,
                             GPUCommandQueueCreateInfo      queCI[],
                             uint32_t                       nQueCI);
  GPUPhysicalDevice* (*getAutoSelectedPhysicalDevice)(GPUInstance *inst);
  GPUPhysicalDevice*
  (*autoSelectPhysicalDeviceIn)(GPUInstance       * __restrict inst,
                                GPUPhysicalDevice * __restrict deviceList);
} GPUApiDevice;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_device_h */
