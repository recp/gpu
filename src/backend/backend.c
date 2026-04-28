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

#include "common.h"
#include "../../include/gpu/gpu.h"
#include "../../include/gpu/api/gpudef.h"

#include "backends.h"

/* LEGACY / CONVENIENCE RUNTIME MODEL:
 * This file owns the older process-global active backend switch.
 * The canonical core direction is instance/device-scoped backend ownership.
 * Do not build new core v1 work on this path. A simplified convenience layer
 * may still exist in final form above the canonical core.
 */

typedef struct GPUApiList {
  struct GPUApiList *next;
  GPUApi            *api;
} GPUApiList;

static GPUApi     *gpu__api     = NULL;
static GPUApiList *gpu__apis    = NULL;

GPUInitParams gpu__defaultInitParams = {
  .requiredFeatures    = GPU_FEATURE_REQUIRED_DEFAULT,
  .optionalFeatures    = GPU_FEATURE_OPTIONAL_DEFAULT,
  .validation          = false,
  .validation_usebreak = false
};

static GPUApi*
gpu__selectDefaultBackend(void) {
#ifdef __APPLE__
  return backend_metal();
#elif defined(_WIN32) || defined(WIN32)
  return backend_dx12();
#else
  return NULL;
#endif
}

static void
gpu__ensureActiveBackend(void) {
  if (!gpu__api) {
    gpu__api = gpu__selectDefaultBackend();
  }
}

GPU_EXPORT
void
gpuRegisterCustomGPUApi(GPUApi * __restrict gpuApi) {
  GPUApiList *item;
  
  item       = calloc(1, sizeof(*item));
  item->api  = gpuApi;
  item->next = gpu__apis;
  gpu__apis  = item;
}

GPU_EXPORT
GPUApi*
gpuActiveGPUApi(void) {
  gpu__ensureActiveBackend();
  return gpu__api;
}
