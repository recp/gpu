/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../common.h"

static GPUShaderLibrary *
cuda_newLibraryWithSource(GPUDevice *device,
                          const char *source,
                          uint64_t    sourceSize) {
  GPUShaderLibraryCuda *native;
  GPUShaderLibrary     *library;

  if (!device || !source || sourceSize == 0u) {
    return NULL;
  }
  library = calloc(1, sizeof(*library));
  native  = calloc(1, sizeof(*native));
  if (!library || !native) {
    free(native);
    free(library);
    return NULL;
  }

  native->module = cuda_createModule(device, source, sourceSize);
  if (!native->module) {
    free(native);
    free(library);
    return NULL;
  }
  library->_priv = native;
  return library;
}

static void
cuda_destroyLibrary(GPUShaderLibrary *library) {
  GPUShaderLibraryCuda *native;

  native = library ? library->_priv : NULL;
  if (native) {
    cuda_releaseModule(native->module);
    free(native);
  }
  free(library);
}

void
cuda_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithSource = cuda_newLibraryWithSource;
  api->destroyLibrary       = cuda_destroyLibrary;
}
