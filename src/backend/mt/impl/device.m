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
mt_getAvailablePhysicalDevicesBy(GPUInstance * __restrict inst,
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
    item->_priv                      = device;

    /* add to linked list of devices */
    if (lastDevice) { lastDevice->next = item; }
    else            { firstDevice      = item; }
    lastDevice = item;

    if (++i >= maxNumberOfItems) { break; }
  }

  return firstDevice;
}

GPU_EXPORT
GPUPhysicalDevice*
mt_autoSelectPhysicalDeviceIn(GPUInstance       * __restrict inst,
                              GPUPhysicalDevice * __restrict deviceList) {
  /* TODO: implement this later */
  return deviceList;
}

GPU_HIDE
GPUPhysicalDevice*
mt_getAutoSelectedPhysicalDevice(GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;
  id<MTLDevice>      mtlDevice;

  mtlDevice = MTLCreateSystemDefaultDevice();
  phyDevice = calloc(1, sizeof(*phyDevice));

  phyDevice->separatePresentQueue       = 1;
  phyDevice->supportsDisplayTiming      = 1;
  phyDevice->supportsIncrementalPresent = 1; /* TODO: */
  phyDevice->supportsSwapchain          = 1;
  phyDevice->inst                       = inst;
  phyDevice->_priv                      = mtlDevice;

  return phyDevice;
}

extern
GPU_HIDE
GPUCommandQueue*
mt_newCommandQueue(GPUDevice * __restrict device);

GPU_HIDE
GPUDevice *
mt_createDevice(GPUPhysicalDevice        *phyDevice,
                GPUCommandQueueCreateInfo queCI[],
                uint32_t                  nQueCI) {
  GPUDevice   *device;
  GPUDeviceMT *deviceMT;
  uint32_t     i;

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI)

  device                   = calloc(1, sizeof(*device));
  deviceMT                 = calloc(1, sizeof(*deviceMT));

  deviceMT->device         = phyDevice->_priv;
  deviceMT->nCreatedQueues = nQueCI;

  if (nQueCI) {
    deviceMT->createdQueues = calloc(nQueCI, sizeof(void*));
  }

  device->_priv            = deviceMT;
  device->inst             = phyDevice->inst;
  device->phyDevice        = phyDevice;

  for (i = 0; i < nQueCI; i++) {
    deviceMT->createdQueues[i] = mt_newCommandQueue(device);
  }

  return device;
}

GPU_HIDE
GPUDevice*
mt_createSystemDefaultDevice(GPUInstance * __restrict inst) {
  GPUPhysicalDevice *phyDevice;

  /* TODO: keep global reference of phyDevice for mem management */
  if (!(phyDevice = mt_getAutoSelectedPhysicalDevice(inst))) {
    return NULL;
  }

  return mt_createDevice(phyDevice, NULL, 0);
}

GPU_HIDE
void
mt_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailablePhysicalDevicesBy = mt_getAvailablePhysicalDevicesBy;
  apiDevice->autoSelectPhysicalDeviceIn    = mt_autoSelectPhysicalDeviceIn;
  apiDevice->getAutoSelectedPhysicalDevice = mt_getAutoSelectedPhysicalDevice;
  apiDevice->createDevice                  = mt_createDevice;
  apiDevice->createSystemDefaultDevice     = mt_createSystemDefaultDevice;
}
