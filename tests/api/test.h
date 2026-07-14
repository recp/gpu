/*
 * Copyright (C) 2020 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef gpu_tests_api_test_h
#define gpu_tests_api_test_h

#include <gpu/bindgroup.h>
#include "../../src/backend/api/compute.h"
#include "../../src/backend/api/pass.h"
#include "../../src/backend/api/rce.h"
#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GPUApiTestContext {
  GPUInstance *instance;
  GPUAdapter  *adapter;
  GPUDevice   *device;
  const char  *uslBytecodePath;
  const char  *mrtBytecodePath;
  const char  *computeBytecodePath;
  const char  *sourceSamplerBytecodePath;
  const char  *storageTextureBytecodePath;
  const char  *cubeTextureBytecodePath;
  const char  *lineTextureBytecodePath;
  const char  *volumeTextureBytecodePath;
  const char  *descriptorArrayBytecodePath;
  const char  *descriptorIndexingBytecodePath;
  const char  *subgroupBytecodePath;
  const char  *shaderF16BytecodePath;
} GPUApiTestContext;

typedef int (*GPUApiTestRunFn)(void *ctx);

typedef struct GPUApiTest {
  const char      *name;
  GPUApiTestRunFn run;
  void           *ctx;
} GPUApiTest;

int gpu_run_api_tests(const GPUApiTest *tests, uint32_t count);
void *gpu_test_read_file(const char *path, uint64_t *outSize);

int gpu_test_queue(GPUInstance *instance,
                   GPUAdapter  *adapter,
                   GPUDevice   *device);
int gpu_test_sampler(GPUDevice *device);
int gpu_test_bindgroup(GPUDevice *device);
int gpu_test_resources(GPUDevice *device);
int gpu_test_copy(GPUDevice *device);
int gpu_test_texture_transfer(GPUDevice *device);
int gpu_test_texture_view_render(GPUDevice *device);
int gpu_test_texture_integer_clear(GPUDevice *device);
int gpu_test_texture_view_depth(GPUDevice *device);
int gpu_test_texture_view_depth_stencil(GPUDevice *device);
int gpu_test_render(GPUDevice *device, const char *mrtBytecodePath);
int gpu_test_metal_vertex_slots(GPUDevice *device, const char *bytecodePath);
int gpu_test_compute(GPUDevice *device, const char *bytecodePath);
int gpu_test_query(GPUAdapter *adapter,
                   GPUDevice  *device,
                   const char *computeBytecodePath);
int gpu_test_barrier(GPUDevice *device);
int gpu_test_runtime(GPUDevice *device);
int gpu_test_threading(GPUDevice *device, const char *artifactPath);
int gpu_test_shader(GPUDevice *device,
                    const char *bytecodePath,
                    const char *descriptorArrayBytecodePath);
int gpu_test_descriptor_array(GPUDevice *device, const char *bytecodePath);
int gpu_test_descriptor_indexing(GPUAdapter *adapter, const char *bytecodePath);
int gpu_test_source_sampler_draw(GPUDevice *device, const char *bytecodePath);
int gpu_test_storage_texture_view(GPUDevice *device, const char *bytecodePath);
int gpu_test_cube_texture_view(GPUDevice *device, const char *bytecodePath);
int gpu_test_line_texture_view(GPUDevice *device, const char *bytecodePath);
int gpu_test_volume_texture_view(GPUDevice *device, const char *bytecodePath);
int gpu_test_subgroup(GPUAdapter *adapter, const char *bytecodePath);
int gpu_test_shader_f16(GPUAdapter *adapter, const char *bytecodePath);

#endif /* gpu_tests_api_test_h */
