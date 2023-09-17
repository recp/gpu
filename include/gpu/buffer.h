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
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "resource.h"
#include "cmdqueue.h"

typedef struct GPUBuffer GPUBuffer;

GPU_EXPORT
GPUBuffer*
GPUNewBuffer(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options);

GPU_EXPORT
size_t
gpuBufferLength(GPUBuffer * __restrict buff);

GPU_EXPORT
GPUBuffer*
gpuBufferContents(GPUBuffer * __restrict buff);

GPU_EXPORT
void
GPUPresent(GPUCommandBuffer *cmdb, void *drawable);

#ifdef __cplusplus
}
#endif
#endif /* gpu_buffer_h */
