/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

WGPUTextureFormat
gpu_webgpuFormat(GPUFormat format) {
  static const WGPUTextureFormat formats[] = {
    [GPU_FORMAT_RGBA8_UNORM]      = WGPUTextureFormat_RGBA8Unorm,
    [GPU_FORMAT_RGBA8_UNORM_SRGB] = WGPUTextureFormat_RGBA8UnormSrgb,
    [GPU_FORMAT_BGRA8_UNORM]      = WGPUTextureFormat_BGRA8Unorm,
    [GPU_FORMAT_BGRA8_UNORM_SRGB] = WGPUTextureFormat_BGRA8UnormSrgb,
    [GPU_FORMAT_DEPTH16_UNORM]    = WGPUTextureFormat_Depth16Unorm,
    [GPU_FORMAT_STENCIL8]         = WGPUTextureFormat_Stencil8,
    [GPU_FORMAT_DEPTH24_UNORM_STENCIL8] =
      WGPUTextureFormat_Depth24PlusStencil8,
    [GPU_FORMAT_DEPTH32_FLOAT] = WGPUTextureFormat_Depth32Float,
    [GPU_FORMAT_DEPTH32_FLOAT_STENCIL8] =
      WGPUTextureFormat_Depth32FloatStencil8
  };

  return (uint32_t)format < GPU_ARRAY_LEN(formats)
           ? formats[format]
           : WGPUTextureFormat_Undefined;
}

GPUFormat
gpu_webgpuGPUFormat(WGPUTextureFormat format) {
  switch (format) {
    case WGPUTextureFormat_RGBA8Unorm:
      return GPU_FORMAT_RGBA8_UNORM;
    case WGPUTextureFormat_RGBA8UnormSrgb:
      return GPU_FORMAT_RGBA8_UNORM_SRGB;
    case WGPUTextureFormat_BGRA8Unorm:
      return GPU_FORMAT_BGRA8_UNORM;
    case WGPUTextureFormat_BGRA8UnormSrgb:
      return GPU_FORMAT_BGRA8_UNORM_SRGB;
    default:
      return GPU_FORMAT_UNDEFINED;
  }
}

WGPUPresentMode
gpu_webgpuPresentMode(GPUPresentMode mode) {
  switch (mode) {
    case GPU_PRESENT_MODE_MAILBOX:
      return WGPUPresentMode_Mailbox;
    case GPU_PRESENT_MODE_IMMEDIATE:
      return WGPUPresentMode_Immediate;
    case GPU_PRESENT_MODE_FIFO:
    default:
      return WGPUPresentMode_Fifo;
  }
}

static GPUResult
webgpu_getCapabilities(const GPUAdapter      *adapter,
                       GPUSurface            *surface,
                       GPUSurfaceCapabilities *outCaps) {
  WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
  GPUAdapterWebGPU       *adapterNative;
  GPUSurfaceWebGPU       *surfaceNative;

  adapterNative = gpu_webgpuAdapter(adapter);
  surfaceNative = gpu_webgpuSurface(surface);
  if (!adapterNative || !adapterNative->adapter ||
      !surfaceNative || !surfaceNative->surface || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (wgpuSurfaceGetCapabilities(surfaceNative->surface,
                                 adapterNative->adapter,
                                 &capabilities) != WGPUStatus_Success) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  surfaceNative->formatCount = 0u;
  for (size_t i = 0u;
       i < capabilities.formatCount &&
       surfaceNative->formatCount < GPU_WEBGPU_MAX_SURFACE_FORMATS;
       i++) {
    GPUFormat format;

    format = gpu_webgpuGPUFormat(capabilities.formats[i]);
    if (format != GPU_FORMAT_UNDEFINED) {
      surfaceNative->formats[surfaceNative->formatCount++] = format;
    }
  }

  surfaceNative->presentModeCount = 0u;
  for (size_t i = 0u;
       i < capabilities.presentModeCount &&
       surfaceNative->presentModeCount < GPU_WEBGPU_MAX_PRESENT_MODES;
       i++) {
    switch (capabilities.presentModes[i]) {
      case WGPUPresentMode_Fifo:
        surfaceNative->presentModes[surfaceNative->presentModeCount++] =
          GPU_PRESENT_MODE_FIFO;
        break;
      case WGPUPresentMode_Mailbox:
        surfaceNative->presentModes[surfaceNative->presentModeCount++] =
          GPU_PRESENT_MODE_MAILBOX;
        break;
      case WGPUPresentMode_Immediate:
        surfaceNative->presentModes[surfaceNative->presentModeCount++] =
          GPU_PRESENT_MODE_IMMEDIATE;
        break;
      default:
        break;
    }
  }
  wgpuSurfaceCapabilitiesFreeMembers(capabilities);

  if (surfaceNative->formatCount == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (surfaceNative->presentModeCount == 0u) {
    surfaceNative->presentModes[0] = GPU_PRESENT_MODE_FIFO;
    surfaceNative->presentModeCount = 1u;
  }

  outCaps->pFormats         = surfaceNative->formats;
  outCaps->pPresentModes    = surfaceNative->presentModes;
  outCaps->minImageCount    = 2u;
  outCaps->maxImageCount    = 3u;
  outCaps->formatCount      = surfaceNative->formatCount;
  outCaps->presentModeCount = surfaceNative->presentModeCount;
  return GPU_OK;
}

static GPUSurface *
webgpu_createSurface(GPUApi        *api,
                     GPUInstance   *instance,
                     GPUAdapter    *adapter,
                     void          *nativeHandle,
                     GPUSurfaceType type,
                     float          scale) {
  WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
  GPUInstanceWebGPU    *instanceNative;
  GPUSurfaceWebGPU     *native;
  GPUSurface           *surface;

  GPU__UNUSED(api);
  GPU__UNUSED(adapter);
  instanceNative = gpu_webgpuInstance(instance);
  if (!instanceNative || !instanceNative->instance || !nativeHandle ||
      type != GPU_SURFACE_WEB_CANVAS) {
    return NULL;
  }

#if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas =
    WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;

  canvas.selector          = gpu_webgpuString(nativeHandle);
  descriptor.nextInChain   = &canvas.chain;
#else
  return NULL;
#endif

  surface = calloc(1, sizeof(*surface));
  native  = calloc(1, sizeof(*native));
  if (!surface || !native) {
    free(native);
    free(surface);
    return NULL;
  }

  native->surface = wgpuInstanceCreateSurface(instanceNative->instance,
                                               &descriptor);
  if (!native->surface) {
    free(native);
    free(surface);
    return NULL;
  }

  surface->_priv = native;
  surface->type  = type;
  surface->scale = scale;
  return surface;
}

static void
webgpu_destroySurface(GPUSurface *surface) {
  GPUSurfaceWebGPU *native;

  native = gpu_webgpuSurface(surface);
  if (native) {
    if (native->surface) {
      wgpuSurfaceRelease(native->surface);
    }
    free(native);
  }
  free(surface);
}

void
webgpu_initSurface(GPUApiSurface *api) {
  api->createSurface   = webgpu_createSurface;
  api->getCapabilities = webgpu_getCapabilities;
  api->destroySurface  = webgpu_destroySurface;
}
