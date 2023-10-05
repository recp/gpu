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
  
  item       = calloc(1, sizeof(*item));
  item->api  = gpuApi;
  item->next = gpu__apis;
  gpu__apis  = item;
}

GPU_EXPORT
void
GPUSwitchGPUApi(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
#ifdef __APPLE__
      gpu__api = backend_metal();
#endif
      break;
    case GPU_BACKEND_VULKAN:
//#ifdef GPU_BACKEND_VULKAN

#ifndef _WIN32
      gpu__api = backend_vk();
#endif // !_WIN32

//#endif
      break;
    case GPU_BACKEND_DIRECTX12:
#if defined(_WIN32) || defined(WIN32)
      gpu__api = backend_dx12(); /* check DX version support */
#endif
      break;
    case GPU_BACKEND_OPENGL:
//      gpu__api = backend_gl();
      break;
    default:
      break;
  }
}

GPU_EXPORT
void
gpuSwitchGPUApiAuto(void) {
#ifdef __APPLE__
  gpu__api = backend_metal();
#elif defined(_WIN32) || defined(WIN32)
  gpu__api = backend_dx12(); /* check DX version support */
#endif
}

GPU_EXPORT
GPUBackend
gpuActiveGPUBackend(void) {
  return gpu__api ? gpu__api->backend : GPU_BACKEND_NULL;
}

GPU_EXPORT
GPUApi*
gpuActiveGPUApi(void) {
  return gpu__api;
}
