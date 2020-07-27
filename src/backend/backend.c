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

#include "mt/api.h"

typedef struct GPUApiList {
  struct GPUApiList *next;
  GPUApi            *api;
} GPUApiList;

static GPUApi     *gpu__api     = NULL;
static GPUApiList *gpu__apis    = NULL;

GPU_EXPORT
void
gpuRegisterCustomGPUApi(GPUApi * __restrict gpuApi) {
  GPUApiList *item;
  
  item = calloc(1, sizeof(*item));
  item->api  = gpuApi;
  item->next = gpu__apis;
  gpu__apis  = item;
}

GPU_EXPORT
void
gpuSwitchGPUApi(GPUBackend backend) {
  
}

GPU_EXPORT
void
gpuSwitchGPUApiAuto() {
#ifdef __APPLE__
  gpu__api = metal();
#endif
}

GPU_EXPORT
GPUBackend
gpuActiveGPUBackend() {
  return gpu__api ? gpu__api->backend : GPU_BACKEND_NULL;
}

GPU_EXPORT
GPUApi*
gpuActiveGPUApi(void) {
  return gpu__api;
}
