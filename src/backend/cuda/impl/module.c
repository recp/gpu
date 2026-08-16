/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

GPUCudaModule *
cuda_createModule(GPUDevice  *device,
                  const void *image,
                  uint64_t    imageSize) {
  GPUDeviceCuda *deviceNative;
  GPUCudaModule *module;
  void          *terminatedImage;
  CUresult       result;

  deviceNative = cuda_device(device);
  if (!deviceNative || !image || imageSize == 0u ||
      imageSize > (uint64_t)SIZE_MAX - 1u) {
    return NULL;
  }

  module = calloc(1, sizeof(*module));
  terminatedImage = malloc((size_t)imageSize + 1u);
  if (!module || !terminatedImage) {
    free(terminatedImage);
    free(module);
    return NULL;
  }
  memcpy(terminatedImage, image, (size_t)imageSize);
  ((char *)terminatedImage)[imageSize] = '\0';

  module->driver   = deviceNative->driver;
  module->context  = deviceNative->context;
  module->refCount = 1u;
  if (cuda_push(module->driver, module->context) != GPU_OK) {
    free(terminatedImage);
    free(module);
    return NULL;
  }
  result = module->driver->moduleLoadData(&module->module,
                                           terminatedImage,
                                           0u,
                                           NULL,
                                           NULL);
  cuda_pop(module->driver);
  free(terminatedImage);
  if (result != CUDA_SUCCESS) {
    cuda_report(device, result, "PTX module creation");
    free(module);
    return NULL;
  }
  return module;
}

void
cuda_retainModule(GPUCudaModule *module) {
  if (!module) return;
#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&module->refCount);
#else
  __atomic_add_fetch(&module->refCount, 1u, __ATOMIC_RELAXED);
#endif
}

void
cuda_releaseModule(GPUCudaModule *module) {
  bool destroy;

  if (!module) return;
#if defined(_WIN32) || defined(WIN32)
  destroy = InterlockedDecrement((volatile LONG *)&module->refCount) == 0;
#else
  destroy = __atomic_sub_fetch(&module->refCount, 1u, __ATOMIC_ACQ_REL) == 0u;
#endif
  if (!destroy) return;

  if (module->module && cuda_push(module->driver, module->context) == GPU_OK) {
    (void)module->driver->moduleUnload(module->module);
    cuda_pop(module->driver);
  }
  free(module);
}

CUresult
cuda_getModuleFunction(GPUCudaModule *module,
                       const char    *name,
                       CUfunction    *outFunction) {
  CUresult result;

  if (!module || !module->module || !name || !name[0] || !outFunction ||
      cuda_push(module->driver, module->context) != GPU_OK) {
    return CUDA_ERROR_INVALID_VALUE;
  }
  result = module->driver->moduleGetFunction(outFunction,
                                              module->module,
                                              name);
  cuda_pop(module->driver);
  return result;
}
