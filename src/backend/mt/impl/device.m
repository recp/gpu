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
GPUAdapter *
mt_getAvailableAdapters(GPUInstance * __restrict inst,
                        uint32_t                 maxNumberOfItems) {
  NSArray<id<MTLDevice>> *devices;
  GPUAdapter             *firstAdapter, *lastAdapter, *adapter;
  uint32_t                i;

  i            = 0;
  firstAdapter = lastAdapter = NULL;
  devices      = MTLCopyAllDevices();

  for (id<MTLDevice> device in devices) {
    adapter = calloc(1, sizeof(*adapter));
    if (!adapter) {
      break;
    }
    adapter->separatePresentQueue       = 1;
    adapter->supportsDisplayTiming      = 1;
    adapter->supportsIncrementalPresent = 1; /* TODO: */
    adapter->supportsSwapchain          = 1;
    adapter->inst                       = inst;
    adapter->_priv                      = [device retain];

    if (lastAdapter) { lastAdapter->next = adapter; }
    else             { firstAdapter      = adapter; }
    lastAdapter = adapter;

    if (++i >= maxNumberOfItems) { break; }
  }

  [devices release];

  return firstAdapter;
}

GPU_HIDE
GPUAdapter *
mt_selectAdapter(GPUInstance * __restrict inst,
                 GPUAdapter  * __restrict adapters) {
  /* TODO: implement this later */
  GPU__UNUSED(inst);
  return adapters;
}

GPU_HIDE
void
mt_destroyAdapter(GPUAdapter * __restrict adapter) {
  if (!adapter) {
    return;
  }
  [(id<MTLDevice>)adapter->_priv release];
  free(adapter);
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

static bool
mt_isFloat32Format(GPUFormat format) {
  return format == GPU_FORMAT_R32_FLOAT ||
         format == GPU_FORMAT_RG32_FLOAT ||
         format == GPU_FORMAT_RGBA32_FLOAT;
}

static bool
mt_isTier1StorageFormat(GPUFormat format) {
  return format == GPU_FORMAT_R32_UINT ||
         format == GPU_FORMAT_R32_SINT ||
         format == GPU_FORMAT_R32_FLOAT;
}

static bool
mt_isTier2StorageFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R8_UNORM:
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_R16_FLOAT:
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA16_FLOAT:
    case GPU_FORMAT_RGBA32_UINT:
    case GPU_FORMAT_RGBA32_SINT:
    case GPU_FORMAT_RGBA32_FLOAT:
      return true;
    default:
      return mt_isTier1StorageFormat(format);
  }
}

static bool
mt_isBCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_BC1_RGBA_UNORM &&
         format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB;
}

static bool
mt_isETCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_EAC_R11_UNORM &&
         format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB;
}

static bool
mt_isASTCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_ASTC_4X4_UNORM &&
         format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB;
}

static void
mt_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps) {
  id<MTLDevice>           device;
  MTLReadWriteTextureTier storageTier;
  bool                    depthSupported;
  bool                    float32Filterable;
  bool                    appleFamily1;
  bool                    appleFamily2;
  bool                    bcSupported;

  device = adapter ? (id<MTLDevice>)adapter->_priv : nil;
  if (!device || !outCaps) {
    return;
  }

  storageTier       = MTLReadWriteTextureTierNone;
  float32Filterable = false;
  appleFamily1      = false;
  appleFamily2      = false;
  bcSupported       = false;
  if (@available(macOS 10.13, iOS 11.0, *)) {
    storageTier = device.readWriteTextureSupport;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    float32Filterable = device.supports32BitFloatFiltering;
  }
  if (@available(macOS 11.0, iOS 16.4, *)) {
    bcSupported = device.supportsBCTextureCompression;
  }
  if (@available(macOS 10.15, iOS 13.0, *)) {
    appleFamily1 = [device supportsFamily:MTLGPUFamilyApple1];
    appleFamily2 = [device supportsFamily:MTLGPUFamilyApple2];
  }

  if (mt_isBCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = bcSupported;
    outCaps->filterable = bcSupported;
    return;
  }
  if (mt_isETCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = appleFamily1;
    outCaps->filterable = appleFamily1;
    return;
  }
  if (mt_isASTCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = appleFamily2;
    outCaps->filterable = appleFamily2;
    return;
  }

  depthSupported = true;
  if (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8) {
#if TARGET_OS_IOS
    depthSupported = false;
#else
    depthSupported = device.depth24Stencil8PixelFormatSupported;
#endif
  }
  if (outCaps->depthStencil) {
    if (!depthSupported) {
      memset(outCaps, 0, sizeof(*outCaps));
      return;
    }
    outCaps->sampled    = true;
    outCaps->filterable = false;
    return;
  }

  outCaps->storage =
    (storageTier >= MTLReadWriteTextureTier1 &&
     mt_isTier1StorageFormat(format)) ||
    (storageTier >= MTLReadWriteTextureTier2 &&
     mt_isTier2StorageFormat(format));
  if (mt_isFloat32Format(format)) {
    outCaps->filterable = float32Filterable;
    outCaps->blendable  = false;
  }
}

extern
GPU_HIDE
GPUQueue*
mt_newCommandQueue(GPUDevice * __restrict device);

GPU_HIDE
void
mt_destroyCommandQueue(GPUQueue * __restrict queue);

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
mt_createDevice(GPUAdapter        * __restrict adapter,
                GPUQueueCreateInfo queCI[],
                uint32_t           nQueCI) {
  GPUDevice     *device;
  GPUDeviceMT   *deviceMT;
  MTCommandMode  commandMode;
  uint32_t       i, j, queueIndex, queueCount;

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI)

  if (!adapter || !adapter->_priv ||
      !mt_selectCommandMode((id<MTLDevice>)adapter->_priv, &commandMode)) {
    return NULL;
  }

  device   = calloc(1, sizeof(*device));
  deviceMT = calloc(1, sizeof(*deviceMT));
  if (!device || !deviceMT) {
    free(deviceMT);
    free(device);
    return NULL;
  }

  deviceMT->device      = adapter->_priv;
  deviceMT->commandMode = commandMode;
  queueCount            = 0;
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
  device->inst             = adapter->inst;
  device->adapter          = adapter;

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

static GPUResult
mt_waitDeviceIdle(GPUDevice * __restrict device) {
  GPUDeviceMT *deviceMT;

  if (!device || !(deviceMT = device->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t i = 0u; i < deviceMT->nCreatedQueues; i++) {
    GPUQueue        *commandQueue;
    MTCommandQueue *queue;

    commandQueue = deviceMT->createdQueues[i];
    queue        = mt_commandQueue(commandQueue);
    if (queue && mt_flushTransfers(commandQueue, true) != GPU_OK) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (queue && queue->inFlightGroup) {
      dispatch_group_wait(queue->inFlightGroup, DISPATCH_TIME_FOREVER);
    }
  }
  return GPU_OK;
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
  apiDevice->getAvailableAdapters      = mt_getAvailableAdapters;
  apiDevice->selectAdapter             = mt_selectAdapter;
  apiDevice->destroyAdapter            = mt_destroyAdapter;
  apiDevice->getAdapterProperties      = mt_getAdapterProperties;
  apiDevice->supportsFeature           = mt_supportsFeature;
  apiDevice->getLimits                 = mt_getLimits;
  apiDevice->getFormatCapabilities     = mt_getFormatCapabilities;
  apiDevice->createDevice              = mt_createDevice;
  apiDevice->waitIdle                  = mt_waitDeviceIdle;
  apiDevice->destroyDevice             = mt_destroyDevice;
}
