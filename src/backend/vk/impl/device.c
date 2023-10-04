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

GPU_HIDE
GPUDevice*
vk_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUDevice     *device;

  device       = calloc(1, sizeof(*device));
  device->priv = NULL;

  return device;
}

GPU_HIDE
GPUPhysicalDevice*
vk_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                 GPUInstance * __restrict inst) {
  GPUInstanceVk     *instVk;
  GPUPhysicalDevice *device;
  VkInstance        instRaw;
  VkPhysicalDevice  *phyDevices;
  VkResult           err;
  uint32_t           gpu_count;

  instVk       = inst->_priv;
  instRaw      = instVk->inst;
  device       = calloc(1, sizeof(*device));
  device->priv = NULL;

  gpu_count = 0;
  err       = vkEnumeratePhysicalDevices(instRaw, &gpu_count, NULL);
  assert(!err);

  if (gpu_count <= 0) {
    ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n",
             "vkEnumeratePhysicalDevices Failure");
  }

  phyDevices = malloc(sizeof(VkPhysicalDevice) * gpu_count);
  err        = vkEnumeratePhysicalDevices(instRaw, &gpu_count, phyDevices);
  assert(!err);

  return device;
}

GPU_HIDE
void
vk_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->createSystemDefaultDevice     = vk_createSystemDefaultDevice;
  apiDevice->getAvailablePhysicalDevicesBy = vk_getAvailablePhysicalDevicesBy;
}
