/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../common.h"

GPU_INLINE
MTLTextureUsage
mt_textureUsage(GPUTextureUsageFlags usage) {
  MTLTextureUsage mtUsage;

  mtUsage = MTLTextureUsageUnknown;
  if ((usage & GPU_TEXTURE_USAGE_SAMPLED) != 0) {
    mtUsage |= MTLTextureUsageShaderRead;
  }
  if ((usage & GPU_TEXTURE_USAGE_STORAGE) != 0) {
    mtUsage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  }
  if ((usage & (GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_DEPTH_STENCIL)) != 0) {
    mtUsage |= MTLTextureUsageRenderTarget;
  }
  return mtUsage;
}

GPU_INLINE
MTLTextureType
mt_textureType(GPUTextureDimension dimension, uint32_t depthOrLayers) {
  switch (dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      return depthOrLayers > 1 ? MTLTextureType1DArray : MTLTextureType1D;
    case GPU_TEXTURE_DIMENSION_3D:
      return MTLTextureType3D;
    case GPU_TEXTURE_DIMENSION_2D:
    default:
      return depthOrLayers > 1 ? MTLTextureType2DArray : MTLTextureType2D;
  }
}

GPU_INLINE
MTLTextureType
mt_textureViewType(GPUTextureViewType viewType) {
  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      return MTLTextureType1D;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      return MTLTextureType2DArray;
    case GPU_TEXTURE_VIEW_CUBE:
      return MTLTextureTypeCube;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      return MTLTextureTypeCubeArray;
    case GPU_TEXTURE_VIEW_3D:
      return MTLTextureType3D;
    case GPU_TEXTURE_VIEW_2D:
    default:
      return MTLTextureType2D;
  }
}

GPU_HIDE
GPUResult
mt_createTexture(GPUDevice                  * __restrict device,
                 const GPUTextureCreateInfo * __restrict info,
                 GPUTexture                ** __restrict outTexture) {
  GPUDeviceMT          *deviceMT;
  MTLTextureDescriptor *desc;
  id<MTLTexture>        nativeTexture;
  GPUTexture           *texture;
  uint32_t              sampleCount;

  if (!device || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;

  deviceMT    = device->_priv;
  sampleCount = info->sampleCount ? info->sampleCount : 1u;
  if (sampleCount > 1u &&
      ![deviceMT->device supportsTextureSampleCount:sampleCount]) {
    return GPU_ERROR_UNSUPPORTED;
  }
  desc = [MTLTextureDescriptor new];
  desc.textureType = sampleCount > 1u
                       ? MTLTextureType2DMultisample
                       : mt_textureType(info->dimension, info->depthOrLayers);
  desc.pixelFormat = mt_format(info->format);
  desc.width = info->width;
  desc.height = info->height;
  desc.depth = info->dimension == GPU_TEXTURE_DIMENSION_3D ? info->depthOrLayers : 1u;
  desc.arrayLength = info->dimension == GPU_TEXTURE_DIMENSION_3D ? 1u : info->depthOrLayers;
  desc.mipmapLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  desc.sampleCount = sampleCount;
  desc.usage = mt_textureUsage(info->usage);
  desc.storageMode = MTLStorageModePrivate;
  if ((info->usage & GPU_TEXTURE_USAGE_COPY_DST) != 0) {
#if TARGET_OS_OSX
    desc.storageMode = MTLStorageModeManaged;
#else
    desc.storageMode = MTLStorageModeShared;
#endif
  }

  nativeTexture = [deviceMT->device newTextureWithDescriptor:desc];
  [desc release];
  if (!nativeTexture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  texture = calloc(1, sizeof(*texture));
  if (!texture) {
    [nativeTexture release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  texture->_priv = nativeTexture;
  texture->format = info->format;
  texture->dimension = info->dimension;
  texture->width = info->width;
  texture->height = info->height;
  texture->depthOrLayers = info->depthOrLayers;
  texture->mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  texture->sampleCount = info->sampleCount ? info->sampleCount : 1u;
  texture->usage = info->usage;
  texture->_ownsNative = true;

  *outTexture = texture;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyTexture(GPUTexture * __restrict texture) {
  if (!texture) {
    return;
  }

  if (texture->_ownsNative && texture->_priv) {
    [(id<MTLTexture>)texture->_priv release];
  }
  free(texture);
}

GPU_HIDE
GPUResult
mt_createTextureView(GPUTexture                      * __restrict texture,
                     const GPUTextureViewCreateInfo  * __restrict info,
                     GPUTextureView                 ** __restrict outView) {
  id<MTLTexture> nativeTexture;
  id<MTLTexture> nativeView;
  GPUTextureView *view;
  NSRange levels;
  NSRange slices;
  bool fullView;

  if (!texture || !texture->_priv || !info || !outView) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outView = NULL;

  nativeTexture = (id<MTLTexture>)texture->_priv;
  fullView = info->format == texture->format &&
             info->baseMipLevel == 0 &&
             info->mipLevelCount == texture->mipLevelCount &&
             info->baseArrayLayer == 0 &&
             info->arrayLayerCount == texture->depthOrLayers;
  if (fullView) {
    nativeView = nativeTexture;
    [nativeView retain];
  } else {
    levels = NSMakeRange(info->baseMipLevel, info->mipLevelCount);
    slices = NSMakeRange(info->baseArrayLayer, info->arrayLayerCount);
    nativeView = [nativeTexture newTextureViewWithPixelFormat:mt_format(info->format)
                                                  textureType:mt_textureViewType(info->viewType)
                                                       levels:levels
                                                       slices:slices];
  }
  if (!nativeView) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  view = calloc(1, sizeof(*view));
  if (!view) {
    [nativeView release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  view->_priv = nativeView;
  view->_texture = texture;
  view->_ownsNative = true;
  *outView = view;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyTextureView(GPUTextureView * __restrict view) {
  if (!view) {
    return;
  }

  if (view->_ownsNative && view->_priv) {
    [(id<MTLTexture>)view->_priv release];
  }
  free(view);
}

GPU_HIDE
GPUResult
mt_writeTexture(GPUCommandQueue             * __restrict queue,
                GPUTexture                  * __restrict texture,
                const GPUTextureWriteRegion * __restrict region,
                const void                  * __restrict data,
                uint64_t                                 sizeBytes) {
  GPUFormatDataLayout dataLayout;
  id<MTLTexture> nativeTexture;
  MTLRegion mtRegion;
  const uint8_t *bytes;

  (void)queue;

  if (!texture || !texture->_priv || !region || !data) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuFormatDataLayout(texture->format,
                           region->width,
                           region->height,
                           region->depth,
                           region->layerCount,
                           region->bytesPerRow,
                           region->rowsPerImage,
                           &dataLayout) ||
      sizeBytes < dataLayout.requiredBytes ||
      dataLayout.bytesPerImage > NSUIntegerMax) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  nativeTexture = (id<MTLTexture>)texture->_priv;
  bytes = data;
  if (nativeTexture.textureType == MTLTextureType3D) {
    mtRegion = MTLRegionMake3D(0, 0, 0, region->width, region->height, region->depth);
    [nativeTexture replaceRegion:mtRegion
                      mipmapLevel:region->mipLevel
                            slice:0
                        withBytes:bytes
                      bytesPerRow:region->bytesPerRow
                    bytesPerImage:(NSUInteger)dataLayout.bytesPerImage];
    return GPU_OK;
  }

  mtRegion = MTLRegionMake2D(0, 0, region->width, region->height);
  for (uint32_t i = 0; i < region->layerCount; i++) {
    [nativeTexture replaceRegion:mtRegion
                      mipmapLevel:region->mipLevel
                            slice:region->baseArrayLayer + i
                        withBytes:bytes +
                                  ((NSUInteger)i *
                                   (NSUInteger)dataLayout.bytesPerImage)
                      bytesPerRow:region->bytesPerRow
                    bytesPerImage:(NSUInteger)dataLayout.bytesPerImage];
  }
  return GPU_OK;
}

GPU_HIDE
void
mt_initDepthStencil(GPUApiDepthStencil *api) {
  api->reserved = NULL;
}

GPU_HIDE
void
mt_initTexture(GPUApiTexture *api) {
  api->create = mt_createTexture;
  api->destroy = mt_destroyTexture;
  api->createView = mt_createTextureView;
  api->destroyView = mt_destroyTextureView;
  api->write = mt_writeTexture;
}
