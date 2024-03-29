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

#ifdef __APPLE__
GPU_HIDE
GPUApi*
backend_metal(void);
#endif

#if defined(_WIN32) || defined(WIN32)
GPU_HIDE
GPUApi*
backend_dx12(void);
#endif

GPU_HIDE
GPUApi*
backend_gl(void);

GPU_HIDE
GPUApi*
backend_vk(void);

#endif /* backend_backends_h */
