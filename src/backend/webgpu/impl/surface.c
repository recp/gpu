/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#  include "../../surface_apple.h"
#endif

WGPUTextureFormat
gpu_webgpuFormat(GPUFormat format) {
  static const WGPUTextureFormat formats[] = {
    [GPU_FORMAT_R8_UNORM]              = WGPUTextureFormat_R8Unorm,
    [GPU_FORMAT_R8_SNORM]              = WGPUTextureFormat_R8Snorm,
    [GPU_FORMAT_R8_UINT]               = WGPUTextureFormat_R8Uint,
    [GPU_FORMAT_R8_SINT]               = WGPUTextureFormat_R8Sint,
    [GPU_FORMAT_R16_UNORM]             = WGPUTextureFormat_R16Unorm,
    [GPU_FORMAT_R16_SNORM]             = WGPUTextureFormat_R16Snorm,
    [GPU_FORMAT_R16_UINT]              = WGPUTextureFormat_R16Uint,
    [GPU_FORMAT_R16_SINT]              = WGPUTextureFormat_R16Sint,
    [GPU_FORMAT_R16_FLOAT]             = WGPUTextureFormat_R16Float,
    [GPU_FORMAT_RG8_UNORM]             = WGPUTextureFormat_RG8Unorm,
    [GPU_FORMAT_RG8_SNORM]             = WGPUTextureFormat_RG8Snorm,
    [GPU_FORMAT_RG8_UINT]              = WGPUTextureFormat_RG8Uint,
    [GPU_FORMAT_RG8_SINT]              = WGPUTextureFormat_RG8Sint,
    [GPU_FORMAT_R32_UINT]              = WGPUTextureFormat_R32Uint,
    [GPU_FORMAT_R32_SINT]              = WGPUTextureFormat_R32Sint,
    [GPU_FORMAT_R32_FLOAT]             = WGPUTextureFormat_R32Float,
    [GPU_FORMAT_RG16_UNORM]            = WGPUTextureFormat_RG16Unorm,
    [GPU_FORMAT_RG16_SNORM]            = WGPUTextureFormat_RG16Snorm,
    [GPU_FORMAT_RG16_UINT]             = WGPUTextureFormat_RG16Uint,
    [GPU_FORMAT_RG16_SINT]             = WGPUTextureFormat_RG16Sint,
    [GPU_FORMAT_RG16_FLOAT]            = WGPUTextureFormat_RG16Float,
    [GPU_FORMAT_RGBA8_UNORM]           = WGPUTextureFormat_RGBA8Unorm,
    [GPU_FORMAT_RGBA8_UNORM_SRGB]      = WGPUTextureFormat_RGBA8UnormSrgb,
    [GPU_FORMAT_RGBA8_SNORM]           = WGPUTextureFormat_RGBA8Snorm,
    [GPU_FORMAT_RGBA8_UINT]            = WGPUTextureFormat_RGBA8Uint,
    [GPU_FORMAT_RGBA8_SINT]            = WGPUTextureFormat_RGBA8Sint,
    [GPU_FORMAT_BGRA8_UNORM]           = WGPUTextureFormat_BGRA8Unorm,
    [GPU_FORMAT_BGRA8_UNORM_SRGB]      = WGPUTextureFormat_BGRA8UnormSrgb,
    [GPU_FORMAT_RGB10A2_UNORM]         = WGPUTextureFormat_RGB10A2Unorm,
    [GPU_FORMAT_RGB10A2_UINT]          = WGPUTextureFormat_RGB10A2Uint,
    [GPU_FORMAT_RG11B10_UFLOAT]        = WGPUTextureFormat_RG11B10Ufloat,
    [GPU_FORMAT_RGB9E5_UFLOAT]         = WGPUTextureFormat_RGB9E5Ufloat,
    [GPU_FORMAT_RG32_UINT]             = WGPUTextureFormat_RG32Uint,
    [GPU_FORMAT_RG32_SINT]             = WGPUTextureFormat_RG32Sint,
    [GPU_FORMAT_RG32_FLOAT]            = WGPUTextureFormat_RG32Float,
    [GPU_FORMAT_RGBA16_UNORM]          = WGPUTextureFormat_RGBA16Unorm,
    [GPU_FORMAT_RGBA16_SNORM]          = WGPUTextureFormat_RGBA16Snorm,
    [GPU_FORMAT_RGBA16_UINT]           = WGPUTextureFormat_RGBA16Uint,
    [GPU_FORMAT_RGBA16_SINT]           = WGPUTextureFormat_RGBA16Sint,
    [GPU_FORMAT_RGBA16_FLOAT]          = WGPUTextureFormat_RGBA16Float,
    [GPU_FORMAT_RGBA32_UINT]           = WGPUTextureFormat_RGBA32Uint,
    [GPU_FORMAT_RGBA32_SINT]           = WGPUTextureFormat_RGBA32Sint,
    [GPU_FORMAT_RGBA32_FLOAT]          = WGPUTextureFormat_RGBA32Float,
    [GPU_FORMAT_BC1_RGBA_UNORM]        = WGPUTextureFormat_BC1RGBAUnorm,
    [GPU_FORMAT_BC1_RGBA_UNORM_SRGB]   = WGPUTextureFormat_BC1RGBAUnormSrgb,
    [GPU_FORMAT_BC2_RGBA_UNORM]        = WGPUTextureFormat_BC2RGBAUnorm,
    [GPU_FORMAT_BC2_RGBA_UNORM_SRGB]   = WGPUTextureFormat_BC2RGBAUnormSrgb,
    [GPU_FORMAT_BC3_RGBA_UNORM]        = WGPUTextureFormat_BC3RGBAUnorm,
    [GPU_FORMAT_BC3_RGBA_UNORM_SRGB]   = WGPUTextureFormat_BC3RGBAUnormSrgb,
    [GPU_FORMAT_BC4_R_UNORM]           = WGPUTextureFormat_BC4RUnorm,
    [GPU_FORMAT_BC4_R_SNORM]           = WGPUTextureFormat_BC4RSnorm,
    [GPU_FORMAT_BC5_RG_UNORM]          = WGPUTextureFormat_BC5RGUnorm,
    [GPU_FORMAT_BC5_RG_SNORM]          = WGPUTextureFormat_BC5RGSnorm,
    [GPU_FORMAT_BC6H_RGB_FLOAT]        = WGPUTextureFormat_BC6HRGBFloat,
    [GPU_FORMAT_BC6H_RGB_UFLOAT]       = WGPUTextureFormat_BC6HRGBUfloat,
    [GPU_FORMAT_BC7_RGBA_UNORM]        = WGPUTextureFormat_BC7RGBAUnorm,
    [GPU_FORMAT_BC7_RGBA_UNORM_SRGB]   = WGPUTextureFormat_BC7RGBAUnormSrgb,
    [GPU_FORMAT_EAC_R11_UNORM]         = WGPUTextureFormat_EACR11Unorm,
    [GPU_FORMAT_EAC_R11_SNORM]         = WGPUTextureFormat_EACR11Snorm,
    [GPU_FORMAT_EAC_RG11_UNORM]        = WGPUTextureFormat_EACRG11Unorm,
    [GPU_FORMAT_EAC_RG11_SNORM]        = WGPUTextureFormat_EACRG11Snorm,
    [GPU_FORMAT_ETC2_RGBA8_UNORM]      = WGPUTextureFormat_ETC2RGBA8Unorm,
    [GPU_FORMAT_ETC2_RGBA8_UNORM_SRGB] = WGPUTextureFormat_ETC2RGBA8UnormSrgb,
    [GPU_FORMAT_ETC2_RGB8_UNORM]       = WGPUTextureFormat_ETC2RGB8Unorm,
    [GPU_FORMAT_ETC2_RGB8_UNORM_SRGB]  = WGPUTextureFormat_ETC2RGB8UnormSrgb,
    [GPU_FORMAT_ETC2_RGB8A1_UNORM]     = WGPUTextureFormat_ETC2RGB8A1Unorm,
    [GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB] =
      WGPUTextureFormat_ETC2RGB8A1UnormSrgb,
    [GPU_FORMAT_ASTC_4X4_UNORM]         = WGPUTextureFormat_ASTC4x4Unorm,
    [GPU_FORMAT_ASTC_4X4_UNORM_SRGB]    = WGPUTextureFormat_ASTC4x4UnormSrgb,
    [GPU_FORMAT_ASTC_5X4_UNORM]         = WGPUTextureFormat_ASTC5x4Unorm,
    [GPU_FORMAT_ASTC_5X4_UNORM_SRGB]    = WGPUTextureFormat_ASTC5x4UnormSrgb,
    [GPU_FORMAT_ASTC_5X5_UNORM]         = WGPUTextureFormat_ASTC5x5Unorm,
    [GPU_FORMAT_ASTC_5X5_UNORM_SRGB]    = WGPUTextureFormat_ASTC5x5UnormSrgb,
    [GPU_FORMAT_ASTC_6X5_UNORM]         = WGPUTextureFormat_ASTC6x5Unorm,
    [GPU_FORMAT_ASTC_6X5_UNORM_SRGB]    = WGPUTextureFormat_ASTC6x5UnormSrgb,
    [GPU_FORMAT_ASTC_6X6_UNORM]         = WGPUTextureFormat_ASTC6x6Unorm,
    [GPU_FORMAT_ASTC_6X6_UNORM_SRGB]    = WGPUTextureFormat_ASTC6x6UnormSrgb,
    [GPU_FORMAT_ASTC_8X5_UNORM]         = WGPUTextureFormat_ASTC8x5Unorm,
    [GPU_FORMAT_ASTC_8X5_UNORM_SRGB]    = WGPUTextureFormat_ASTC8x5UnormSrgb,
    [GPU_FORMAT_ASTC_8X6_UNORM]         = WGPUTextureFormat_ASTC8x6Unorm,
    [GPU_FORMAT_ASTC_8X6_UNORM_SRGB]    = WGPUTextureFormat_ASTC8x6UnormSrgb,
    [GPU_FORMAT_ASTC_8X8_UNORM]         = WGPUTextureFormat_ASTC8x8Unorm,
    [GPU_FORMAT_ASTC_8X8_UNORM_SRGB]    = WGPUTextureFormat_ASTC8x8UnormSrgb,
    [GPU_FORMAT_ASTC_10X5_UNORM]        = WGPUTextureFormat_ASTC10x5Unorm,
    [GPU_FORMAT_ASTC_10X5_UNORM_SRGB]   = WGPUTextureFormat_ASTC10x5UnormSrgb,
    [GPU_FORMAT_ASTC_10X6_UNORM]        = WGPUTextureFormat_ASTC10x6Unorm,
    [GPU_FORMAT_ASTC_10X6_UNORM_SRGB]   = WGPUTextureFormat_ASTC10x6UnormSrgb,
    [GPU_FORMAT_ASTC_10X8_UNORM]        = WGPUTextureFormat_ASTC10x8Unorm,
    [GPU_FORMAT_ASTC_10X8_UNORM_SRGB]   = WGPUTextureFormat_ASTC10x8UnormSrgb,
    [GPU_FORMAT_ASTC_10X10_UNORM]       = WGPUTextureFormat_ASTC10x10Unorm,
    [GPU_FORMAT_ASTC_10X10_UNORM_SRGB]  = WGPUTextureFormat_ASTC10x10UnormSrgb,
    [GPU_FORMAT_ASTC_12X10_UNORM]       = WGPUTextureFormat_ASTC12x10Unorm,
    [GPU_FORMAT_ASTC_12X10_UNORM_SRGB]  = WGPUTextureFormat_ASTC12x10UnormSrgb,
    [GPU_FORMAT_ASTC_12X12_UNORM]       = WGPUTextureFormat_ASTC12x12Unorm,
    [GPU_FORMAT_ASTC_12X12_UNORM_SRGB]  = WGPUTextureFormat_ASTC12x12UnormSrgb,
    [GPU_FORMAT_DEPTH16_UNORM]           = WGPUTextureFormat_Depth16Unorm,
    [GPU_FORMAT_STENCIL8]                = WGPUTextureFormat_Stencil8,
    [GPU_FORMAT_DEPTH24_UNORM_STENCIL8] =
      WGPUTextureFormat_Depth24PlusStencil8,
    [GPU_FORMAT_DEPTH32_FLOAT] = WGPUTextureFormat_Depth32Float,
    [GPU_FORMAT_DEPTH32_FLOAT_STENCIL8] =
      WGPUTextureFormat_Depth32FloatStencil8
  };

  _Static_assert(GPU_ARRAY_LEN(formats) == GPU_FORMAT_COUNT,
                 "WebGPU format table must cover GPUFormat");

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
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  WGPUSurfaceSourceMetalLayer metalLayer =
    WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
#elif defined(_WIN32) || defined(WIN32)
  WGPUSurfaceSourceWindowsHWND window =
    WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
#endif

  GPU__UNUSED(api);
  GPU__UNUSED(adapter);
  instanceNative = gpu_webgpuInstance(instance);
  if (!instanceNative || !instanceNative->instance || !nativeHandle) {
    return NULL;
  }

#if defined(__EMSCRIPTEN__)
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas =
    WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;

  if (type != GPU_SURFACE_WEB_CANVAS) {
    return NULL;
  }
  canvas.selector          = gpu_webgpuString(nativeHandle);
  descriptor.nextInChain   = &canvas.chain;
#elif defined(__APPLE__)
  if (type != GPU_SURFACE_APPLE_NSVIEW &&
      type != GPU_SURFACE_APPLE_UIVIEW) {
    return NULL;
  }
  metalLayer.layer = gpuCreateMetalLayer(nativeHandle, type, scale);
  if (!metalLayer.layer) {
    return NULL;
  }
  descriptor.nextInChain = &metalLayer.chain;
#elif defined(_WIN32) || defined(WIN32)
  if (type != GPU_SURFACE_WINDOWS_HWND) {
    return NULL;
  }
  window.hinstance       = GetModuleHandleW(NULL);
  window.hwnd            = nativeHandle;
  descriptor.nextInChain = &window.chain;
#else
  return NULL;
#endif

  surface = calloc(1, sizeof(*surface));
  native  = calloc(1, sizeof(*native));
  if (!surface || !native) {
    free(native);
    free(surface);
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    gpuDestroyMetalLayer(metalLayer.layer);
#endif
    return NULL;
  }

  native->surface = wgpuInstanceCreateSurface(instanceNative->instance,
                                               &descriptor);
  if (!native->surface) {
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    gpuDestroyMetalLayer(metalLayer.layer);
#endif
    free(native);
    free(surface);
    return NULL;
  }
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  native->ownedPlatformHandle = metalLayer.layer;
#endif

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
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    gpuDestroyMetalLayer(native->ownedPlatformHandle);
#endif
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
