/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static bool
webgpu_configureSwapchain(GPUSwapchain                 *swapchain,
                          const GPUSwapchainCreateInfo *info) {
  WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
  GPUSwapchainWebGPU      *native;
  GPUSurfaceWebGPU        *surface;

  native  = gpu_webgpuSwapchain(swapchain);
  surface = gpu_webgpuSurface(info->surface);
  if (!native || !surface || !surface->surface) {
    return false;
  }

  native->format = gpu_webgpuFormat(info->format);
  if (native->format == WGPUTextureFormat_Undefined) {
    return false;
  }
  native->presentMode = gpu_webgpuPresentMode(info->presentMode);
  native->surface     = surface->surface;

  configuration.device      = native->device;
  configuration.format      = native->format;
  configuration.usage       = WGPUTextureUsage_RenderAttachment;
  configuration.width       = info->width;
  configuration.height      = info->height;
  configuration.alphaMode   = WGPUCompositeAlphaMode_Auto;
  configuration.presentMode = native->presentMode;
  wgpuSurfaceConfigure(native->surface, &configuration);
  return true;
}

static GPUSwapchain *
webgpu_createSwapchain(GPUApi                         *api,
                       GPUDevice                      *device,
                       GPUQueue                       *queue,
                       const GPUSwapchainCreateInfo  *info) {
  GPUDeviceWebGPU    *deviceNative;
  GPUSwapchainWebGPU *native;
  GPUSwapchain       *swapchain;

  GPU__UNUSED(api);
  GPU__UNUSED(queue);
  deviceNative = gpu_webgpuDevice(device);
  if (!deviceNative || !deviceNative->device || !info) {
    return NULL;
  }

  swapchain = calloc(1, sizeof(*swapchain));
  native    = calloc(1, sizeof(*native));
  if (!swapchain || !native) {
    free(native);
    free(swapchain);
    return NULL;
  }

  swapchain->_priv = native;
  native->device   = deviceNative->device;
  if (!webgpu_configureSwapchain(swapchain, info)) {
    free(native);
    free(swapchain);
    return NULL;
  }
  return swapchain;
}

static GPUResult
webgpu_resizeSwapchain(GPUSwapchain *swapchain, GPUExtent2D size) {
  WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
  GPUSwapchainWebGPU      *native;

  native = gpu_webgpuSwapchain(swapchain);
  if (!native || !native->surface || native->acquired) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  configuration.device      = native->device;
  configuration.format      = native->format;
  configuration.usage       = WGPUTextureUsage_RenderAttachment;
  configuration.width       = size.width;
  configuration.height      = size.height;
  configuration.alphaMode   = WGPUCompositeAlphaMode_Auto;
  configuration.presentMode = native->presentMode;
  wgpuSurfaceConfigure(native->surface, &configuration);
  return GPU_OK;
}

static void
webgpu_destroySwapchain(GPUSwapchain *swapchain) {
  GPUSwapchainWebGPU *native;

  native = gpu_webgpuSwapchain(swapchain);
  if (native) {
    if (native->currentView) {
      wgpuTextureViewRelease(native->currentView);
    }
    if (native->currentTexture) {
      wgpuTextureRelease(native->currentTexture);
    }
    if (native->surface) {
      wgpuSurfaceUnconfigure(native->surface);
    }
    free(native);
  }
  free(swapchain);
}

void
webgpu_initSwapchain(GPUApiSwapchain *api) {
  api->createSwapchain  = webgpu_createSwapchain;
  api->resizeSwapchain  = webgpu_resizeSwapchain;
  api->destroySwapchain = webgpu_destroySwapchain;
}
