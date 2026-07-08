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
#include <gpu/api/compute.h>
#include <gpu/api/pass.h>
#include <gpu/api/rce.h>
#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gpu_test_queue(GPUPhysicalDevice *physicalDevice, GPUDevice *device);
int gpu_test_sampler(GPUDevice *device);
int gpu_test_bindgroup(GPUDevice *device);
int gpu_test_resources(GPUDevice *device);
int gpu_test_copy(GPUDevice *device);
int gpu_test_render(void);
int gpu_test_compute(void);

#endif /* gpu_tests_api_test_h */
