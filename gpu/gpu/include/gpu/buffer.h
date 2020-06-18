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

#ifndef gpu_buffer_h
#define gpu_buffer_h

#include "common.h"
#include "resource.h"
#include "cmdqueue.h"

typedef struct GPUBuffer GPUBuffer;

GPU_EXPORT
GPUBuffer*
gpuNewBuffer(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options);

GPU_EXPORT
void
gpuPresent(GPUCommandBuffer *cmdb, void *drawable);

#endif /* gpu_buffer_h */
