/*
 * Copyright (C) 2026 Recep Aslantas
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
#include "../impl.h"

static GPUInstance*
webgpu_createInstance(GPUApi                      *api,
                      const GPUInstanceCreateInfo *info) {
  WGPUInstanceDescriptor  descriptor     = WGPU_INSTANCE_DESCRIPTOR_INIT;
#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
  WGPUInstanceLimits      requiredLimits = WGPU_INSTANCE_LIMITS_INIT;
  WGPUInstanceFeatureName requiredFeatures[2];
#endif
  GPUInstanceWebGPU      *native;
  GPUInstance            *instance;

  GPU__UNUSED(api);
  GPU__UNUSED(info);

  instance = calloc(1, sizeof(*instance));
  native   = calloc(1, sizeof(*native));

  if (!instance || !native) {
    free(native);
    free(instance);
    return NULL;
  }

#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
  native->timedWaitAny = wgpuHasInstanceFeature(WGPUInstanceFeatureName_TimedWaitAny);

  if (native->timedWaitAny) {
    requiredLimits.timedWaitAnyMaxCount = 1u;
    requiredFeatures[descriptor.requiredFeatureCount++] = WGPUInstanceFeatureName_TimedWaitAny;
    descriptor.requiredLimits           = &requiredLimits;
  }

  if (wgpuHasInstanceFeature(WGPUInstanceFeatureName_MultipleDevicesPerAdapter)) {
    requiredFeatures[descriptor.requiredFeatureCount++] = WGPUInstanceFeatureName_MultipleDevicesPerAdapter;
  }

  descriptor.requiredFeatures = descriptor.requiredFeatureCount
                                  ? requiredFeatures
                                  : NULL;

  native->instance = wgpuCreateInstance(&descriptor);
#else
  native->instance = wgpuCreateInstance(&descriptor);
#endif
  if (!native->instance) {
    free(native);
    free(instance);
    return NULL;
  }

  instance->_priv      = native;
  instance->createInfo = *info;

  return instance;
}

static void
webgpu_destroyInstance(GPUApi *api, GPUInstance *instance) {
  GPUInstanceWebGPU *native;

  GPU__UNUSED(api);
  native = webgpuInstance(instance);

  if (native) {
    if (native->instance) {
      wgpuInstanceRelease(native->instance);
    }

    free(native);
  }

  free(instance);
}

void
webgpu_initInstance(GPUInstanceApi *api) {
  api->createInstance  = webgpu_createInstance;
  api->destroyInstance = webgpu_destroyInstance;
}
