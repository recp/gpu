/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPUTextureDimension
webgpu_textureDimension(GPUTextureDimension dimension) {
  static const WGPUTextureDimension dimensions[] = {
    [GPU_TEXTURE_DIMENSION_1D] = WGPUTextureDimension_1D,
    [GPU_TEXTURE_DIMENSION_2D] = WGPUTextureDimension_2D,
    [GPU_TEXTURE_DIMENSION_3D] = WGPUTextureDimension_3D
  };

  return (uint32_t)dimension < GPU_ARRAY_LEN(dimensions)
           ? dimensions[dimension]
           : WGPUTextureDimension_Undefined;
}

static WGPUTextureViewDimension
webgpu_textureViewDimension(GPUTextureViewType type) {
  static const WGPUTextureViewDimension dimensions[] = {
    [GPU_TEXTURE_VIEW_1D]         = WGPUTextureViewDimension_1D,
    [GPU_TEXTURE_VIEW_2D]         = WGPUTextureViewDimension_2D,
    [GPU_TEXTURE_VIEW_2D_ARRAY]   = WGPUTextureViewDimension_2DArray,
    [GPU_TEXTURE_VIEW_CUBE]       = WGPUTextureViewDimension_Cube,
    [GPU_TEXTURE_VIEW_CUBE_ARRAY] = WGPUTextureViewDimension_CubeArray,
    [GPU_TEXTURE_VIEW_3D]         = WGPUTextureViewDimension_3D
  };

  return (uint32_t)type < GPU_ARRAY_LEN(dimensions)
           ? dimensions[type]
           : WGPUTextureViewDimension_Undefined;
}

static WGPUTextureAspect
webgpu_textureAspect(GPUTextureAspect aspect) {
  static const WGPUTextureAspect aspects[] = {
    [GPU_TEXTURE_ASPECT_ALL]          = WGPUTextureAspect_All,
    [GPU_TEXTURE_ASPECT_DEPTH_ONLY]   = WGPUTextureAspect_DepthOnly,
    [GPU_TEXTURE_ASPECT_STENCIL_ONLY] = WGPUTextureAspect_StencilOnly
  };

  return (uint32_t)aspect < GPU_ARRAY_LEN(aspects)
           ? aspects[aspect]
           : WGPUTextureAspect_Undefined;
}

static WGPUTextureUsage
webgpu_textureUsage(GPUTextureUsageFlags usage) {
  WGPUTextureUsage result;

  result = WGPUTextureUsage_None;
  if (usage & GPU_TEXTURE_USAGE_SAMPLED)
    result |= WGPUTextureUsage_TextureBinding;
  if (usage & GPU_TEXTURE_USAGE_STORAGE)
    result |= WGPUTextureUsage_StorageBinding;
  if (usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
               GPU_TEXTURE_USAGE_DEPTH_STENCIL))
    result |= WGPUTextureUsage_RenderAttachment;
  if (usage & GPU_TEXTURE_USAGE_COPY_SRC)
    result |= WGPUTextureUsage_CopySrc;
  if (usage & GPU_TEXTURE_USAGE_COPY_DST)
    result |= WGPUTextureUsage_CopyDst;
  return result;
}

static GPUResult
webgpu_createTexture(GPUDevice                  * __restrict device,
                     const GPUTextureCreateInfo * __restrict info,
                     GPUTexture                ** __restrict outTexture) {
  WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
  GPUDeviceWebGPU      *native;
  GPUTexture           *texture;

  native = gpu_webgpuDevice(device);
  if (!native || !native->device || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->usage & GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  descriptor.dimension = webgpu_textureDimension(info->dimension);
  descriptor.format    = gpu_webgpuFormat(info->format);
  descriptor.usage     = webgpu_textureUsage(info->usage);
  if (descriptor.dimension == WGPUTextureDimension_Undefined ||
      descriptor.format == WGPUTextureFormat_Undefined ||
      descriptor.usage == WGPUTextureUsage_None) {
    return GPU_ERROR_UNSUPPORTED;
  }

  texture = calloc(1, sizeof(*texture));
  if (!texture) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label                   = gpu_webgpuString(info->label);
  descriptor.size.width              = info->width;
  descriptor.size.height             = info->height;
  descriptor.size.depthOrArrayLayers = info->depthOrLayers;
  descriptor.mipLevelCount           = info->mipLevelCount
                                         ? info->mipLevelCount
                                         : 1u;
  descriptor.sampleCount             = info->sampleCount
                                         ? info->sampleCount
                                         : 1u;
  texture->_priv = wgpuDeviceCreateTexture(native->device, &descriptor);
  if (!texture->_priv) {
    free(texture);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  texture->format          = info->format;
  texture->dimension       = info->dimension;
  texture->width           = info->width;
  texture->height          = info->height;
  texture->depthOrLayers   = info->depthOrLayers;
  texture->mipLevelCount   = descriptor.mipLevelCount;
  texture->sampleCount     = descriptor.sampleCount;
  texture->usage           = info->usage;
  texture->_ownsNative     = true;
  *outTexture = texture;
  return GPU_OK;
}

static void
webgpu_destroyTexture(GPUTexture * __restrict texture) {
  if (!texture) {
    return;
  }
  if (texture->_priv && texture->_ownsNative) {
    wgpuTextureDestroy(texture->_priv);
    wgpuTextureRelease(texture->_priv);
  }
  free(texture);
}

static GPUResult
webgpu_createTextureView(
  GPUTexture                     * __restrict texture,
  const GPUTextureViewCreateInfo * __restrict info,
  GPUTextureView                ** __restrict outView) {
  WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  GPUTextureView           *view;

  if (!texture || !texture->_priv || !info || !outView) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  descriptor.format    = gpu_webgpuFormat(info->format);
  descriptor.dimension = webgpu_textureViewDimension(info->viewType);
  if (descriptor.format == WGPUTextureFormat_Undefined ||
      descriptor.dimension == WGPUTextureViewDimension_Undefined) {
    return GPU_ERROR_UNSUPPORTED;
  }

  view = calloc(1, sizeof(*view));
  if (!view) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  descriptor.label           = gpu_webgpuString(info->label);
  descriptor.baseMipLevel    = info->baseMipLevel;
  descriptor.mipLevelCount   = info->mipLevelCount;
  descriptor.baseArrayLayer  = info->baseArrayLayer;
  descriptor.arrayLayerCount = info->arrayLayerCount;
  descriptor.aspect          = WGPUTextureAspect_All;
  view->_priv = wgpuTextureCreateView(texture->_priv, &descriptor);
  if (!view->_priv) {
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  view->_ownsNative = true;
  *outView = view;
  return GPU_OK;
}

static void
webgpu_destroyTextureView(GPUTextureView * __restrict view) {
  if (!view) {
    return;
  }
  if (view->_priv && view->_ownsNative) {
    wgpuTextureViewRelease(view->_priv);
  }
  free(view);
}

static GPUResult
webgpu_writeTexture(GPUQueue             * __restrict queue,
                    GPUTexture           * __restrict texture,
                    const GPUTextureWriteRegion * __restrict region,
                    const void           * __restrict data,
                    uint64_t                          sizeBytes) {
  WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  WGPUTexelCopyTextureInfo  destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUExtent3D              extent = WGPU_EXTENT_3D_INIT;
  GPUDeviceWebGPU          *native;

  native = gpu_webgpuDevice(gpuCommandQueueDevice(queue));
  if (!native || !native->queue || !texture || !texture->_priv ||
      !region || !data || sizeBytes > SIZE_MAX) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  destination.texture  = texture->_priv;
  destination.mipLevel = region->mipLevel;
  destination.aspect   = webgpu_textureAspect(region->aspect);
  destination.origin.z = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                           ? 0u
                           : region->baseArrayLayer;
  if (destination.aspect == WGPUTextureAspect_Undefined) {
    return GPU_ERROR_UNSUPPORTED;
  }

  layout.bytesPerRow  = region->bytesPerRow;
  layout.rowsPerImage = region->rowsPerImage;
  extent.width        = region->width;
  extent.height       = region->height;
  extent.depthOrArrayLayers = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                ? region->depth
                                : region->layerCount;
  wgpuQueueWriteTexture(native->queue,
                        &destination,
                        data,
                        (size_t)sizeBytes,
                        &layout,
                        &extent);
  return GPU_OK;
}

void
webgpu_initTexture(GPUApiTexture *api) {
  api->create      = webgpu_createTexture;
  api->destroy     = webgpu_destroyTexture;
  api->createView  = webgpu_createTextureView;
  api->destroyView = webgpu_destroyTextureView;
  api->write       = webgpu_writeTexture;
}
