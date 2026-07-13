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

#include "backends.h"

static GPUApi*
gpu__selectDefaultBackend(void) {
#if GPU_BACKEND_METAL_ONLY
  return backend_metal();
#elif GPU_BACKEND_VULKAN_ONLY
  return backend_vk();
#elif GPU_BACKEND_DX12_ONLY
  return backend_dx12();
#elif defined(__APPLE__)
  return backend_metal();
#elif defined(_WIN32) || defined(WIN32)
  return backend_dx12();
#elif defined(GPU_ENABLE_VULKAN)
  return backend_vk();
#else
  return NULL;
#endif
}

GPU_HIDE
GPUApi*
gpuApiForBackend(GPUBackend backend) {
#if GPU_BACKEND_METAL_ONLY
  if (backend == GPU_BACKEND_DEFAULT ||
      backend == GPU_BACKEND_NULL ||
      backend == GPU_BACKEND_METAL) {
    return gpu__selectDefaultBackend();
  }
  return NULL;
#elif GPU_BACKEND_VULKAN_ONLY
  if (backend == GPU_BACKEND_DEFAULT ||
      backend == GPU_BACKEND_NULL ||
      backend == GPU_BACKEND_VULKAN) {
    return gpu__selectDefaultBackend();
  }
  return NULL;
#elif GPU_BACKEND_DX12_ONLY
  if (backend == GPU_BACKEND_DEFAULT ||
      backend == GPU_BACKEND_NULL ||
      backend == GPU_BACKEND_DX12) {
    return gpu__selectDefaultBackend();
  }
  return NULL;
#else
  GPUApi *api;

  if (backend == GPU_BACKEND_DEFAULT || backend == GPU_BACKEND_NULL) {
    return gpu__selectDefaultBackend();
  }

  api = NULL;
  switch (backend) {
#ifdef __APPLE__
    case GPU_BACKEND_METAL:
      api = backend_metal();
      break;
#endif
#if defined(GPU_ENABLE_VULKAN)
    case GPU_BACKEND_VULKAN:
      api = backend_vk();
      break;
#endif
#if defined(_WIN32) || defined(WIN32)
    case GPU_BACKEND_DX12:
      api = backend_dx12();
      break;
#endif
    case GPU_BACKEND_OPENGL:
      api = backend_gl();
      break;
    default:
      return NULL;
  }

  return api;
#endif
}
