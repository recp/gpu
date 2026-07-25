/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUInstance *
webgpu_createInstance(GPUApi                     *api,
                      const GPUInstanceCreateInfo *info) {
  WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
#if GPU_WEBGPU_PROVIDER_DAWN && !defined(__EMSCRIPTEN__)
  WGPUInstanceFeatureName requiredFeature =
    WGPUInstanceFeatureName_TimedWaitAny;
  WGPUInstanceLimits      requiredLimits = WGPU_INSTANCE_LIMITS_INIT;
#endif
  GPUInstanceWebGPU     *native;
  GPUInstance           *instance;

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
  native->timedWaitAny =
    wgpuHasInstanceFeature(WGPUInstanceFeatureName_TimedWaitAny);
  if (native->timedWaitAny) {
    requiredLimits.timedWaitAnyMaxCount = 1u;
    descriptor.requiredFeatureCount    = 1u;
    descriptor.requiredFeatures        = &requiredFeature;
    descriptor.requiredLimits          = &requiredLimits;
  }
  native->instance = wgpuCreateInstance(&descriptor);
#else
  native->instance = wgpuCreateInstance(&descriptor);
#endif
  if (!native->instance) {
    free(native);
    free(instance);
    return NULL;
  }

  instance->_priv = native;
  instance->createInfo = *info;
  return instance;
}

static void
webgpu_destroyInstance(GPUApi *api, GPUInstance *instance) {
  GPUInstanceWebGPU *native;

  GPU__UNUSED(api);
  native = gpu_webgpuInstance(instance);
  if (native) {
    if (native->instance) {
      wgpuInstanceRelease(native->instance);
    }
    free(native);
  }
  free(instance);
}

void
webgpu_initInstance(GPUApiInstance *api) {
  api->createInstance  = webgpu_createInstance;
  api->destroyInstance = webgpu_destroyInstance;
}
