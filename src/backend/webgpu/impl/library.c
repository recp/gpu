/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUShaderLibrary *
webgpu_newLibraryWithSource(GPUDevice *device,
                            const char *source,
                            uint64_t    sourceSize) {
  WGPUShaderSourceWGSL sourceInfo = WGPU_SHADER_SOURCE_WGSL_INIT;
  WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  GPUDeviceWebGPU    *native;
  GPUShaderLibrary   *library;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !source || sourceSize == 0u ||
      sourceSize > (uint64_t)SIZE_MAX) {
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  if (!library) {
    return NULL;
  }

  sourceInfo.code       = gpu_webgpuStringSize(source, sourceSize);
  descriptor.nextInChain = &sourceInfo.chain;
  library->_priv = wgpuDeviceCreateShaderModule(native->device, &descriptor);
  if (!library->_priv) {
    free(library);
    return NULL;
  }
  return library;
}

static void
webgpu_destroyLibrary(GPUShaderLibrary *library) {
  if (!library) {
    return;
  }
  if (library->_priv) {
    wgpuShaderModuleRelease(library->_priv);
  }
  free(library);
}

void
webgpu_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithSource = webgpu_newLibraryWithSource;
  api->destroyLibrary       = webgpu_destroyLibrary;
}
