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
webgpu_initDevice(GPUDeviceApi *api);

void
webgpu_initInstance(GPUInstanceApi *api);

void
webgpu_initSurface(GPUSurfaceApi *api);

void
webgpu_initSwapchain(GPUSwapchainApi *api);

void
webgpu_initFrame(GPUFrameApi *api);

void
webgpu_initCommandQueue(GPUCommandQueueApi *api);

void
webgpu_initCommandBuffer(GPUCommandBufferApi *api);

void
webgpu_initBuffer(GPUBufferApi *api);

void
webgpu_initTexture(GPUTextureApi *api);

void
webgpu_initSampler(GPUSamplerApi *api);

void
webgpu_initLibrary(GPULibraryApi *api);

void
webgpu_initDescriptor(GPUDescriptorApi *api);

void
webgpu_initPipeline(GPURenderApi *api);

void
webgpu_initCompute(GPUComputeApi *api);

void
webgpu_initQuery(GPUCommandBufferApi *api);

void
webgpu_initRenderPass(GPURenderPassApi *api);

void
webgpu_blitTexture(GPUCommandBuffer         *cmdb,
                   const GPUTextureBlitInfo *info);

void
webgpu_initRenderEncoder(GPURCEApi *api);

#endif /* gpu_webgpu_impl_h */
