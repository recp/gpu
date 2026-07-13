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

#ifndef backend_backends_h
#define backend_backends_h

#include "common.h"

#if defined(__APPLE__) && !GPU_BACKEND_VULKAN_ONLY
GPU_HIDE
GPUApi*
backend_metal(void);
#endif

#if (defined(_WIN32) || defined(WIN32)) && !GPU_BACKEND_VULKAN_ONLY
GPU_HIDE
GPUApi*
backend_dx12(void);
#endif

#if !GPU_BACKEND_METAL_ONLY && \
    !GPU_BACKEND_VULKAN_ONLY && \
    !GPU_BACKEND_DX12_ONLY
GPU_HIDE
GPUApi*
backend_gl(void);
#endif

#if defined(GPU_ENABLE_VULKAN) && \
    !GPU_BACKEND_METAL_ONLY && \
    !GPU_BACKEND_DX12_ONLY
GPU_HIDE
GPUApi*
backend_vk(void);
#endif

#endif /* backend_backends_h */
