/*
 * Copyright (C) 2026 Recep Aslantas
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

#ifndef gpu_blit_internal_h
#define gpu_blit_internal_h

#include "../../common.h"

typedef struct GPUBlitShaderData {
  const void *data;
  uint64_t    size;
  bool        binary;
} GPUBlitShaderData;

typedef struct GPUBlitShaderSet {
  GPUBlitShaderData filteringFloat;
  GPUBlitShaderData filteringFloatArray;
  GPUBlitShaderData unfilterableFloat;
  GPUBlitShaderData unsignedInteger;
  GPUBlitShaderData signedInteger;
} GPUBlitShaderSet;

GPU_HIDE
GPUResult
gpuInitBlitDevice(GPUDevice *device);

GPU_HIDE
void
gpuDestroyBlitDevice(GPUDevice *device);

GPU_HIDE
void
gpuDestroyTextureBlitViews(GPUTexture *texture);

GPU_HIDE
void
gpuBlitTextureRenderFallback(GPUCommandBuffer         *cmdb,
                             const GPUTextureBlitInfo *info,
                             const GPUBlitShaderSet   *shaders);

#endif /* gpu_blit_internal_h */
