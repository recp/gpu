/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUInstance *
cuda_createInstance(GPUApi                      *api,
                    const GPUInstanceCreateInfo *info) {
  GPUInstance *instance;

  GPU__UNUSED(api);
  instance = calloc(1, sizeof(*instance));
  if (instance && info) {
    instance->createInfo = *info;
  }
  return instance;
}

static void
cuda_destroyInstance(GPUApi *api, GPUInstance *instance) {
  GPU__UNUSED(api);
  free(instance);
}

void
cuda_initInstance(GPUApiInstance *api) {
  api->createInstance  = cuda_createInstance;
  api->destroyInstance = cuda_destroyInstance;
}
