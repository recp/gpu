/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef gpu_backend_surface_apple_h
#define gpu_backend_surface_apple_h

#include "../api/surface_internal.h"

GPU_HIDE
void *
gpuCreateMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale);

GPU_HIDE
void
gpuResizeMetalLayer(void *metalLayer,
                    uint32_t width,
                    uint32_t height,
                    float scale);

GPU_HIDE
void
gpuDestroyMetalLayer(void *metalLayer);

#endif /* gpu_backend_surface_apple_h */
