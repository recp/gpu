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

static MTLPixelFormat
mt_stencilCopyFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      return MTLPixelFormatX24_Stencil8;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return MTLPixelFormatX32_Stencil8;
    default:
      return MTLPixelFormatInvalid;
  }
}

GPU_HIDE
id<MTLTexture>
mt_nativeTexture(GPUTexture *texture) {
  GPUTextureMT *native;

  if (!texture || !texture->_priv) {
    return nil;
  }
  if (!texture->_ownsNative) {
    return (id<MTLTexture>)texture->_priv;
  }

  native = texture->_priv;
  return native->texture;
}

GPU_HIDE
id<MTLTexture>
mt_copyTexture(GPUTexture *texture, GPUTextureAspect aspect) {
  GPUTextureAspect resolved;
  GPUTextureMT    *native;

  if (!texture || !texture->_priv ||
      !gpuFormatResolveCopyAspect(texture->format, aspect, &resolved)) {
    return nil;
  }
  if (resolved == GPU_TEXTURE_ASPECT_ALL ||
      texture->format == GPU_FORMAT_DEPTH16_UNORM ||
      texture->format == GPU_FORMAT_DEPTH32_FLOAT ||
      texture->format == GPU_FORMAT_STENCIL8) {
    return mt_nativeTexture(texture);
  }
  if (!texture->_ownsNative) {
    return nil;
  }

  native = texture->_priv;
  if (resolved == GPU_TEXTURE_ASPECT_DEPTH_ONLY) {
    return native->texture;
  }
  return native->stencilCopyView;
}

GPU_HIDE
MTLBlitOption
mt_copyOption(GPUFormat format, GPUTextureAspect aspect) {
  GPUTextureAspect resolved;

  if ((format != GPU_FORMAT_DEPTH24_UNORM_STENCIL8 &&
       format != GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) ||
      !gpuFormatResolveCopyAspect(format, aspect, &resolved)) {
    return MTLBlitOptionNone;
  }
  return resolved == GPU_TEXTURE_ASPECT_DEPTH_ONLY
           ? MTLBlitOptionDepthFromDepthStencil
           : MTLBlitOptionStencilFromDepthStencil;
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
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      return MTLTextureType1DArray;
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
  id<MTLTexture>        stencilCopyView;
  GPUTexture           *texture;
  GPUTextureMT         *native;
  MTLPixelFormat        stencilCopyFormat;
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
  stencilCopyFormat = mt_stencilCopyFormat(info->format);
  if (stencilCopyFormat != MTLPixelFormatInvalid &&
      (info->usage & (GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST)) != 0u) {
    desc.usage |= MTLTextureUsagePixelFormatView;
  }
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

  stencilCopyView = nil;
  if (stencilCopyFormat != MTLPixelFormatInvalid &&
      (info->usage & (GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST)) != 0u) {
    stencilCopyView = [nativeTexture
      newTextureViewWithPixelFormat:stencilCopyFormat];
    if (!stencilCopyView) {
      [nativeTexture release];
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

  texture = calloc(1, sizeof(*texture) + sizeof(*native));
  if (!texture) {
    [stencilCopyView release];
    [nativeTexture release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native                  = (GPUTextureMT *)(texture + 1);
  native->texture         = nativeTexture;
  native->stencilCopyView = stencilCopyView;
  texture->_priv          = native;
  texture->format         = info->format;
  texture->dimension      = info->dimension;
  texture->width          = info->width;
  texture->height         = info->height;
  texture->depthOrLayers  = info->depthOrLayers;
  texture->mipLevelCount  = info->mipLevelCount ? info->mipLevelCount : 1u;
  texture->sampleCount    = info->sampleCount ? info->sampleCount : 1u;
  texture->usage          = info->usage;
  texture->_ownsNative    = true;

  *outTexture = texture;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyTexture(GPUTexture * __restrict texture) {
  GPUTextureMT *native;

  if (!texture) {
    return;
  }

  if (texture->_ownsNative && texture->_priv) {
    native = texture->_priv;
    [native->stencilCopyView release];
    [native->texture release];
  }
  free(texture);
}

static id<MTLCommandQueue>
mt_uploadCommandQueue(GPUCommandQueue *queue) {
  GPUDeviceMT        *device;
  MTCommandQueue     *native;
  id<MTLCommandQueue> upload;

  native = mt_commandQueue(queue);
  if (!native) {
    return nil;
  }
  if (native->classic) {
    return native->classic;
  }

  device = queue && queue->_device ? queue->_device->_priv : NULL;
  if (!device || !device->device) {
    return nil;
  }

  os_unfair_lock_lock(&native->poolLock);
  if (!native->upload) {
    native->upload = [device->device newCommandQueue];
  }
  upload = native->upload;
  os_unfair_lock_unlock(&native->poolLock);
  return upload;
}

static GPUResult
mt_writeDepthStencilPlane(GPUCommandQueue             *queue,
                          GPUTexture                  *texture,
                          const GPUTextureWriteRegion *region,
                          const void                  *data,
                          const GPUFormatDataLayout   *dataLayout) {
  id<MTLCommandQueue>       commandQueue;
  id<MTLCommandBuffer>      command;
  id<MTLBlitCommandEncoder> blit;
  id<MTLBuffer>             upload;
  id<MTLTexture>            nativeTexture;
  MTLBlitOption             option;
  MTLSize                   size;

  commandQueue  = mt_uploadCommandQueue(queue);
  nativeTexture = mt_nativeTexture(texture);
  option        = mt_copyOption(texture->format, region->aspect);
  if (!commandQueue || !nativeTexture || option == MTLBlitOptionNone ||
      dataLayout->requiredBytes > NSUIntegerMax) {
    return GPU_ERROR_UNSUPPORTED;
  }

  upload = [nativeTexture.device
    newBufferWithBytes:data
                length:(NSUInteger)dataLayout->requiredBytes
               options:MTLResourceStorageModeShared];
  command = [commandQueue commandBuffer];
  blit    = [command blitCommandEncoder];
  if (!upload || !command || !blit) {
    [upload release];
    return GPU_ERROR_BACKEND_FAILURE;
  }

  size = MTLSizeMake(region->width, region->height, 1u);
  for (uint32_t i = 0u; i < region->layerCount; i++) {
    [blit copyFromBuffer:upload
            sourceOffset:(NSUInteger)((uint64_t)i * dataLayout->bytesPerImage)
       sourceBytesPerRow:region->bytesPerRow
     sourceBytesPerImage:(NSUInteger)dataLayout->bytesPerImage
              sourceSize:size
               toTexture:nativeTexture
        destinationSlice:region->baseArrayLayer + i
        destinationLevel:region->mipLevel
       destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                 options:option];
  }
  [blit endEncoding];
  [command commit];
  [command waitUntilCompleted];
  [upload release];
  return command.status == MTLCommandBufferStatusCompleted
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
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

  if (!texture || !texture->_priv || !info || !outView ||
      info->format != texture->format) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outView = NULL;

  nativeTexture = mt_nativeTexture(texture);
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
  id<MTLTexture>      nativeTexture;
  const uint8_t      *bytes;
  GPUFormatDataLayout dataLayout;
  MTLRegion           mtRegion;
  GPUTextureAspect    resolved;

  if (!texture || !texture->_priv || !region || !data ||
      !gpuFormatResolveCopyAspect(texture->format,
                                  region->aspect,
                                  &resolved)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuFormatAspectDataLayout(texture->format,
                                 region->aspect,
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

  if (texture->format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
      texture->format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
    return mt_writeDepthStencilPlane(queue,
                                     texture,
                                     region,
                                     data,
                                     &dataLayout);
  }

  nativeTexture = mt_copyTexture(texture, region->aspect);
  if (!nativeTexture) {
    return GPU_ERROR_UNSUPPORTED;
  }
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
