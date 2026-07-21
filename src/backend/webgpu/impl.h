/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef gpu_webgpu_impl_h
#define gpu_webgpu_impl_h

#include "../api/gpudef.h"

void webgpu_initDevice(GPUApiDevice *api);
void webgpu_initInstance(GPUApiInstance *api);
void webgpu_initSurface(GPUApiSurface *api);
void webgpu_initSwapchain(GPUApiSwapchain *api);
void webgpu_initFrame(GPUApiFrame *api);
void webgpu_initCommandQueue(GPUApiCommandQueue *api);
void webgpu_initCommandBuffer(GPUApiCommandBuffer *api);
void webgpu_initBuffer(GPUApiBuffer *api);
void webgpu_initTexture(GPUApiTexture *api);
void webgpu_initSampler(GPUApiSampler *api);
void webgpu_initLibrary(GPUApiLibrary *api);
void webgpu_initDescriptor(GPUApiDescriptor *api);
void webgpu_initPipeline(GPUApiRender *api);
void webgpu_initCompute(GPUApiCompute *api);
void webgpu_initQuery(GPUApiCommandBuffer *api);
void webgpu_initRenderPass(GPUApiRenderPass *api);
void webgpu_initRenderEncoder(GPUApiRCE *api);

#endif /* gpu_webgpu_impl_h */
