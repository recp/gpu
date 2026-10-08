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

#ifndef gpu_webgpu_impl_h
#define gpu_webgpu_impl_h

#include "../api/gpudef.h"

void
webgpu_initDevice(ApiDevice    *api);

void
webgpu_initInstance(ApiInstance    *api);

void
webgpu_initSurface(ApiSurface    *api);

void
webgpu_initSwapchain(ApiSwapchain    *api);

void
webgpu_initFrame(ApiFrame    *api);

void
webgpu_initCommandQueue(ApiCommandQueue    *api);

void
webgpu_initCommandBuffer(ApiCommandBuffer    *api);

void
webgpu_initBuffer(ApiBuffer    *api);

void
webgpu_initTexture(ApiTexture    *api);

void
webgpu_initSampler(ApiSampler    *api);

void
webgpu_initLibrary(ApiLibrary    *api);

void
webgpu_initDescriptor(ApiDescriptor    *api);

void
webgpu_initPipeline(ApiRender    *api);

void
webgpu_initCompute(ApiCompute    *api);

void
webgpu_initQuery(ApiCommandBuffer    *api);

void
webgpu_initRenderPass(ApiRenderPass    *api);

void
webgpu_blitTexture(GPUCommandBuffer         *cmdb,
                   const GPUTextureBlitInfo *info);

void
webgpu_initRenderEncoder(ApiRCE    *api);

#endif /* gpu_webgpu_impl_h */
