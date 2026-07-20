/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUFrame *
webgpu_beginFrame(GPUApi *api, GPUSwapchain *swapchain) {
  WGPUSurfaceTexture   surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  native = gpu_webgpuSwapchain(swapchain);
  if (!native || native->acquired) {
    return NULL;
  }

  wgpuSurfaceGetCurrentTexture(native->surface, &surfaceTexture);
  if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    if (surfaceTexture.texture) {
      wgpuTextureRelease(surfaceTexture.texture);
    }
    switch (surfaceTexture.status) {
      case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        gpuSwapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_OUT_OF_DATE);
        break;
      case WGPUSurfaceGetCurrentTextureStatus_Lost:
        gpuSwapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SURFACE_LOST);
        break;
      default:
        gpuSwapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
        break;
    }
    return NULL;
  }

  native->currentTexture = surfaceTexture.texture;
  native->currentView    = wgpuTextureCreateView(native->currentTexture, NULL);
  if (!native->currentView) {
    wgpuTextureRelease(native->currentTexture);
    native->currentTexture = NULL;
    return NULL;
  }

  memset(&native->frame, 0, sizeof(native->frame));
  memset(&native->texture, 0, sizeof(native->texture));
  memset(&native->view, 0, sizeof(native->view));

  native->texture._priv         = native->currentTexture;
  native->texture.format        = swapchain->format;
  native->texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
  native->texture.width         = swapchain->width;
  native->texture.height        = swapchain->height;
  native->texture.depthOrLayers = 1u;
  native->texture.mipLevelCount = 1u;
  native->texture.sampleCount   = 1u;
  native->texture.usage         = GPU_TEXTURE_USAGE_COLOR_TARGET;

  native->view._priv           = native->currentView;
  native->view._texture        = &native->texture;
  native->view.format          = swapchain->format;
  native->view.viewType        = GPU_TEXTURE_VIEW_2D;
  native->view.mipLevelCount   = 1u;
  native->view.arrayLayerCount = 1u;

  native->frame._priv      = native;
  native->frame.target     = &native->texture;
  native->frame.targetView = &native->view;
  native->frame.drawable   = native->currentTexture;
  native->acquired         = true;
  if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    gpuSwapchainSetStatus(swapchain, GPU_SWAPCHAIN_STATUS_SUBOPTIMAL);
  }
  return &native->frame;
}

static void
webgpu_endFrame(GPUApi *api, GPUFrame *frame) {
  GPUSwapchainWebGPU *native;

  GPU__UNUSED(api);
  native = frame ? frame->_priv : NULL;
  if (!native) {
    return;
  }
  if (native->currentView) {
    wgpuTextureViewRelease(native->currentView);
  }
  if (native->currentTexture) {
    wgpuTextureRelease(native->currentTexture);
  }
  native->currentView    = NULL;
  native->currentTexture = NULL;
  native->acquired       = false;
}

void
webgpu_initFrame(GPUApiFrame *api) {
  api->beginFrame = webgpu_beginFrame;
  api->endFrame   = webgpu_endFrame;
}
