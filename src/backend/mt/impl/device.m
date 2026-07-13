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

GPU_HIDE
GPUResult
mt_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                        GPUAdapterProperties * __restrict outProps) {
  id<MTLDevice> device;

  if (!adapter || !outProps || !adapter->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device = (id<MTLDevice>)adapter->_priv;
  memset(outProps, 0, sizeof(*outProps));
  outProps->backend = GPU_BACKEND_METAL;
  outProps->name = device.name.UTF8String;
  outProps->type = device.isLowPower ?
    GPU_ADAPTER_TYPE_INTEGRATED :
    GPU_ADAPTER_TYPE_DISCRETE;

  return GPU_OK;
}

static bool
mt_hasCounterSet(id<MTLDevice> device, MTLCommonCounterSet name) {
  if (!device || !name) {
    return false;
  }

  if (@available(macOS 10.15, iOS 14.0, *)) {
    for (id<MTLCounterSet> counterSet in device.counterSets) {
      if ([counterSet.name isEqualToString:name]) {
        return true;
      }
    }
  }

  return false;
}

static bool
mt_supportsBlitCounterSampling(id<MTLDevice> device) {
  if (!device) {
    return false;
  }

  if (@available(macOS 11.0, iOS 14.0, *)) {
    return [device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];
  }

  return false;
}

GPU_HIDE
bool
mt_supportsFeature(const GPUAdapter * __restrict adapter, GPUFeature feature) {
  id<MTLDevice> device;

  if (!adapter) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
    case GPU_FEATURE_INDIRECT_DRAW:
      return true;
    case GPU_FEATURE_TIMESTAMPS:
      device = (id<MTLDevice>)adapter->_priv;
      return mt_hasCounterSet(device, MTLCommonCounterSetTimestamp) &&
             mt_supportsBlitCounterSampling(device);
    default:
      return false;
  }
}

static void
mt_getLimits(const GPUAdapter * __restrict adapter,
             GPULimits       * __restrict outLimits) {
  id<MTLDevice> device;
  MTLSize       threads;

  device = adapter ? (id<MTLDevice>)adapter->_priv : nil;
  if (!device || !outLimits) {
    return;
  }

  threads = device.maxThreadsPerThreadgroup;
  outLimits->maxComputeWorkgroupSizeX = (uint32_t)threads.width;
  outLimits->maxComputeWorkgroupSizeY = (uint32_t)threads.height;
  outLimits->maxComputeWorkgroupSizeZ = (uint32_t)threads.depth;
}

extern
GPU_HIDE
GPUCommandQueue*
mt_newCommandQueue(GPUDevice * __restrict device);

GPU_HIDE
void
mt_destroyCommandQueue(GPUCommandQueue * __restrict queue);

static bool
mt_supportsMetal4(id<MTLDevice> device) {
  if (@available(macOS 26.0, iOS 26.0, *)) {
    return device &&
           [device respondsToSelector:@selector(newMTL4CommandQueue)] &&
           [device respondsToSelector:@selector(newCommandAllocator)] &&
           [device respondsToSelector:@selector(newArgumentTableWithDescriptor:error:)];
  }

  return false;
}

static bool
mt_selectCommandMode(id<MTLDevice> device, MTCommandMode *outMode) {
  const char *mode;
  bool supportsMetal4;

  if (!outMode) {
    return false;
  }

  mode = getenv("GPU_METAL_MODE");
  supportsMetal4 = mt_supportsMetal4(device);
  if (mode && strcmp(mode, "classic") == 0) {
    *outMode = MTCommandModeClassic;
    return true;
  }
  if (mode && strcmp(mode, "metal4") == 0) {
    if (!supportsMetal4) {
      NSLog(@"GPU_METAL_MODE=metal4 requested on an unsupported device or OS");
      return false;
    }
    *outMode = MTCommandMode4;
    return true;
  }
  if (mode && strcmp(mode, "auto") != 0) {
    NSLog(@"Unknown GPU_METAL_MODE '%s'; expected auto, classic, or metal4", mode);
    return false;
  }

  *outMode = supportsMetal4 ? MTCommandMode4 : MTCommandModeClassic;
  return true;
}

GPU_HIDE
GPUDevice *
mt_createDevice(GPUPhysicalDevice        *phyDevice,
                GPUCommandQueueCreateInfo queCI[],
                uint32_t                  nQueCI) {
  GPUDevice   *device;
  GPUDeviceMT *deviceMT;
  MTCommandMode commandMode;
  uint32_t     i, j, queueIndex, queueCount;

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI)

  if (!phyDevice || !phyDevice->_priv ||
      !mt_selectCommandMode((id<MTLDevice>)phyDevice->_priv, &commandMode)) {
    return NULL;
  }

  device = calloc(1, sizeof(*device));
  deviceMT = calloc(1, sizeof(*deviceMT));
  if (!device || !deviceMT) {
    free(deviceMT);
    free(device);
    return NULL;
  }

  deviceMT->device = phyDevice->_priv;
  deviceMT->commandMode = commandMode;
  queueCount               = 0;
  for (i = 0; i < nQueCI; i++) {
    queueCount += queCI[i].count;
  }
  deviceMT->nCreatedQueues = queueCount;

  if (queueCount) {
    deviceMT->createdQueues = calloc(queueCount, sizeof(void*));
    if (!deviceMT->createdQueues) {
      free(deviceMT);
      free(device);
      return NULL;
    }
  }

  device->_priv            = deviceMT;
  device->inst             = phyDevice->inst;
  device->phyDevice        = phyDevice;

  queueIndex = 0;
  for (i = 0; i < nQueCI; i++) {
    for (j = 0; j < queCI[i].count; j++) {
      deviceMT->createdQueues[queueIndex] = mt_newCommandQueue(device);
      if (!deviceMT->createdQueues[queueIndex]) {
        for (uint32_t k = 0; k < queueIndex; k++) {
          mt_destroyCommandQueue(deviceMT->createdQueues[k]);
        }
        free(deviceMT->createdQueues);
        free(deviceMT);
        free(device);
        return NULL;
      }
      deviceMT->createdQueues[queueIndex]->bits = queCI[i].flags;
      device->queueFamilies |= queCI[i].flags;
      queueIndex++;
    }
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
mt_destroyDevice(GPUDevice * __restrict device) {
  GPUDeviceMT *deviceMT;

  if (!device) {
    return;
  }

  deviceMT = device->_priv;
  if (deviceMT) {
    if (deviceMT->createdQueues) {
      for (uint32_t i = 0; i < deviceMT->nCreatedQueues; i++) {
        mt_destroyCommandQueue(deviceMT->createdQueues[i]);
      }
      free(deviceMT->createdQueues);
    }
    free(deviceMT);
  }

  free(device);
}

GPU_HIDE
void
mt_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailableAdapters      = mt_getAvailablePhysicalDevicesBy;
  apiDevice->getAdapterProperties      = mt_getAdapterProperties;
  apiDevice->supportsFeature           = mt_supportsFeature;
  apiDevice->getLimits                 = mt_getLimits;
  apiDevice->createDevice              = mt_createDevice;
  apiDevice->createSystemDefaultDevice = mt_createSystemDefaultDevice;
  apiDevice->destroyDevice             = mt_destroyDevice;
}
