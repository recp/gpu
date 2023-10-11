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
GPUPhysicalDevice *
mt_getAvailablePhysicalDevicesBy(GPUApi      * __restrict api,
                                 GPUInstance * __restrict inst,
                                 uint32_t                 maxNumberOfItems) {
  NSArray<id<MTLDevice>> *devices;
  GPUPhysicalDevice      *firstDevice, *lastDevice, *item;
  uint32_t                i;

  i           = 0;
  firstDevice = lastDevice = NULL;
  devices     = MTLCopyAllDevices();

  for (id<MTLDevice> device in devices) {
    item = calloc(1, sizeof(*item));
    item->separatePresentQueue       = 1;
    item->supportsDisplayTiming      = 1;
    item->supportsIncrementalPresent = 1; /* TODO: */
    item->supportsSwapchain          = 1;
    item->inst                       = inst;
    item->priv                       = device;

    /* add to linked list of devices */
    if (lastDevice) { lastDevice->next = item; }
    else            { firstDevice      = item; }
    lastDevice = item;

    if (++i >= maxNumberOfItems) { break; }
  }

  return firstDevice;
}

GPU_HIDE
GPUDevice *
mt_createDevice(GPUPhysicalDevice *phyDevice) {
  GPUDevice *device;
  device  = calloc(1, sizeof(*device));

  device->priv      = phyDevice->priv;
  device->inst      = phyDevice->inst;
  device->phyDevice = phyDevice;

  return device;
}

GPU_HIDE
GPUDevice*
mt_createSystemDefaultDevice(GPUApi *api, GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;
  GPUDevice         *device;
  id<MTLDevice>      mtlDevice;

  mtlDevice = MTLCreateSystemDefaultDevice();
  phyDevice = calloc(1, sizeof(*phyDevice));
  device    = calloc(1, sizeof(*device));

  phyDevice->separatePresentQueue       = 1;
  phyDevice->supportsDisplayTiming      = 1;
  phyDevice->supportsIncrementalPresent = 1; /* TODO: */
  phyDevice->supportsSwapchain          = 1;
  phyDevice->inst                       = inst;

  phyDevice->priv   = mtlDevice;
  device->priv      = mtlDevice;
  device->inst      = inst;
  device->phyDevice = phyDevice;

  return device;
}

GPU_HIDE
void
mt_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = mt_getAvailablePhysicalDevicesBy;
  apiDevice->createDevice                  = mt_createDevice;
  apiDevice->createSystemDefaultDevice     = mt_createSystemDefaultDevice;
}
