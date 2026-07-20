/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "common.h"
#include "impl.h"

static GPUApi webgpu = {
  .backend = GPU_BACKEND_WEBGPU
};

GPU_HIDE
GPUApi *
backend_webgpu(void) {
  if (!webgpu.initialized) {
    webgpu_initDevice(&webgpu.device);
    webgpu_initInstance(&webgpu.instance);
    webgpu_initSurface(&webgpu.surface);
    webgpu_initSwapchain(&webgpu.swapchain);
    webgpu_initFrame(&webgpu.frame);
    webgpu_initCommandQueue(&webgpu.cmdque);
    webgpu_initCommandBuffer(&webgpu.cmdbuf);
    webgpu_initLibrary(&webgpu.library);
    webgpu_initDescriptor(&webgpu.descriptor);
    webgpu_initPipeline(&webgpu.render);
    webgpu_initRenderPass(&webgpu.renderPass);
    webgpu_initRenderEncoder(&webgpu.rce);
    webgpu.initialized = true;
  }
  return &webgpu;
}
