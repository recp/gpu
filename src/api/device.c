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
gpu_validQueueCreateInfos(const GPUCommandQueueCreateInfo queCI[],
                          uint32_t                        nQueCI) {
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

static bool
gpu_validQueueRequestType(GPUQueueFlagBits type) {
  return type == GPU_QUEUE_GRAPHICS ||
         type == GPU_QUEUE_COMPUTE ||
         type == GPU_QUEUE_TRANSFER;
}

static bool
gpu_validFeatureSet(const GPUFeatureSet *set) {
  return set->featureCount == 0 || set->pFeatures;
}

static bool
gpu_knownFeature(GPUFeature feature) {
  return feature >= GPU_FEATURE_COMPUTE &&
         feature <= GPU_FEATURE_VARIABLE_RATE_SHADING;
}

static bool
gpu_supportedFeature(GPUFeature feature) {
  return feature == GPU_FEATURE_COMPUTE;
}

static GPUResult
gpu_validateFeatureSet(const GPUFeatureSet *set, bool required) {
  if (!gpu_validFeatureSet(set)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t i = 0; i < set->featureCount; i++) {
    if (!gpu_knownFeature(set->pFeatures[i])) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (required && !gpu_supportedFeature(set->pFeatures[i])) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  return GPU_OK;
}

static GPUResult
gpu_buildQueueCreateInfos(const GPUDeviceCreateInfo  *info,
                          GPUCommandQueueCreateInfo  *stackInfos,
                          uint32_t                    stackInfoCount,
                          GPUCommandQueueCreateInfo **outInfos,
                          uint32_t                   *outInfoCount) {
  const GPUDeviceQueueCreateInfo *queueInfo;
  GPUCommandQueueCreateInfo *infos;
  uint32_t requestCount;

  *outInfos = NULL;
  *outInfoCount = 0;

  if (!info) {
    return GPU_OK;
  }
  if (info->queues.chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->queues.chain.sType != GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->queues.chain.structSize != 0 &&
      info->queues.chain.structSize < sizeof(info->queues)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  queueInfo = &info->queues;
  requestCount = queueInfo->requestCount;
  if (requestCount == 0) {
    return queueInfo->pRequests ? GPU_ERROR_INVALID_ARGUMENT : GPU_OK;
  }
  if (!queueInfo->pRequests) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  infos = stackInfos;
  if (requestCount > stackInfoCount) {
    infos = calloc(requestCount, sizeof(*infos));
    if (!infos) {
      return GPU_ERROR_OUT_OF_MEMORY;
    }
  }

  for (uint32_t i = 0; i < requestCount; i++) {
    if (!gpu_validQueueRequestType(queueInfo->pRequests[i].type) ||
        queueInfo->pRequests[i].count == 0) {
      if (infos != stackInfos) {
        free(infos);
      }
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    infos[i].flags = queueInfo->pRequests[i].type;
    infos[i].count = queueInfo->pRequests[i].count;
  }

  *outInfos = infos;
  *outInfoCount = requestCount;
  return GPU_OK;
}

static GPUAdapter*
gpu_getAvailableAdapters(GPUInstance *inst, uint32_t maxNumberOfItems) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->device.getAvailableAdapters(inst, maxNumberOfItems);
}

GPU_EXPORT
GPUResult
GPUEnumerateAdapters(GPUInstance *inst,
                     uint32_t    *inoutAdapterCount,
                     GPUAdapter **outAdapters) {
  GPUAdapter *deviceList;
  GPUAdapter *item;
  uint32_t capacity;
  uint32_t count;

  if (!inoutAdapterCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity = *inoutAdapterCount;
  deviceList = gpu_getAvailableAdapters(inst, UINT32_MAX);
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
GPUResult
GPUCreateDevice(GPUAdapter                *adapter,
                const GPUDeviceCreateInfo *info,
                GPUDevice                **outDevice) {
  GPUCommandQueueCreateInfo stackQueueInfos[8];
  GPUCommandQueueCreateInfo *queueInfos;
  uint32_t queueInfoCount;
  GPUResult result;
  GPUResult featureResult;
  GPUApi *api;

  if (!outDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outDevice = NULL;

  if (!adapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info) {
    if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
        info->chain.sType != GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    featureResult = gpu_validateFeatureSet(&info->required, true);
    if (featureResult != GPU_OK) {
      return featureResult;
    }
    featureResult = gpu_validateFeatureSet(&info->optional, false);
    if (featureResult != GPU_OK) {
      return featureResult;
    }
  }
  if (!(api = gpuActiveGPUApi()))
    return GPU_ERROR_BACKEND_FAILURE;

  queueInfos = NULL;
  queueInfoCount = 0;
  result = gpu_buildQueueCreateInfos(info,
                                     stackQueueInfos,
                                     (uint32_t)GPU_ARRAY_LEN(stackQueueInfos),
                                     &queueInfos,
                                     &queueInfoCount);
  if (result != GPU_OK) {
    return result;
  }
  if (!gpu_validQueueCreateInfos(queueInfos, queueInfoCount)) {
    if (queueInfos != stackQueueInfos) {
      free(queueInfos);
    }
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outDevice = api->device.createDevice(adapter, queueInfos, queueInfoCount);
  if (queueInfos != stackQueueInfos) {
    free(queueInfos);
  }
  if (!*outDevice) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return GPU_OK;
}

GPU_EXPORT
GPUDevice *
GPUCreateDeviceWithDefaultQueues(GPUAdapter *adapter) {
  GPUDevice *device;

  if (GPUCreateDevice(adapter, NULL, &device) != GPU_OK) {
    return NULL;
  }

  return device;
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
