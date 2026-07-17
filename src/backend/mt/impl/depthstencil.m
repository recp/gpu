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
#include "texture_view_pool.h"
#include <string.h>

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
#if !TARGET_OS_IOS
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      return MTLPixelFormatX24_Stencil8;
#endif
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
mt_textureViewType(GPUTextureViewType viewType, uint32_t sampleCount) {
  switch (viewType) {
    case GPU_TEXTURE_VIEW_1D:
      return MTLTextureType1D;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      return MTLTextureType1DArray;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      return sampleCount > 1u
               ? MTLTextureType2DMultisampleArray
               : MTLTextureType2DArray;
    case GPU_TEXTURE_VIEW_CUBE:
      return MTLTextureTypeCube;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      return MTLTextureTypeCubeArray;
    case GPU_TEXTURE_VIEW_3D:
      return MTLTextureType3D;
    case GPU_TEXTURE_VIEW_2D:
    default:
      return sampleCount > 1u
               ? MTLTextureType2DMultisample
               : MTLTextureType2D;
  }
}

GPU_HIDE
GPUResult
mt_createTextureDescriptor(GPUDevice                  *device,
                           const GPUTextureCreateInfo *info,
                           MTLStorageMode              storageMode,
                           MTLTextureDescriptor      **outDesc,
                           MTLPixelFormat             *outStencilCopyFormat) {
  GPUDeviceMT          *deviceMT;
  MTLTextureDescriptor *desc;
  MTLPixelFormat        stencilCopyFormat;
  uint32_t              sampleCount;

  if (!device || !device->_priv || !info || !outDesc ||
      !outStencilCopyFormat) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outDesc              = nil;
  *outStencilCopyFormat = MTLPixelFormatInvalid;

  deviceMT    = device->_priv;
  sampleCount = info->sampleCount ? info->sampleCount : 1u;
  if ((info->usage & GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (sampleCount > 1u &&
      ![deviceMT->device supportsTextureSampleCount:sampleCount]) {
    return GPU_ERROR_UNSUPPORTED;
  }

  desc                  = [MTLTextureDescriptor new];
  desc.textureType      = sampleCount > 1u
                            ? MTLTextureType2DMultisample
                            : mt_textureType(info->dimension,
                                             info->depthOrLayers);
  desc.pixelFormat      = mt_format(info->format);
  desc.width            = info->width;
  desc.height           = info->height;
  desc.depth            = info->dimension == GPU_TEXTURE_DIMENSION_3D
                            ? info->depthOrLayers
                            : 1u;
  desc.arrayLength      = info->dimension == GPU_TEXTURE_DIMENSION_3D
                            ? 1u
                            : info->depthOrLayers;
  desc.mipmapLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  desc.sampleCount      = sampleCount;
  desc.usage            = mt_textureUsage(info->usage);
  desc.storageMode      = storageMode;

  stencilCopyFormat = mt_stencilCopyFormat(info->format);
  if (stencilCopyFormat != MTLPixelFormatInvalid &&
      (info->usage & (GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST)) != 0u) {
    desc.usage |= MTLTextureUsagePixelFormatView;
  }

  *outDesc              = desc;
  *outStencilCopyFormat = stencilCopyFormat;
  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_wrapTexture(GPUDevice                  *device,
               const GPUTextureCreateInfo *info,
               id<MTLTexture>              nativeTexture,
               MTLPixelFormat              stencilCopyFormat,
               GPUTexture                **outTexture) {
  id<MTLTexture> stencilCopyView;
  GPUTexture    *texture;
  GPUTextureMT  *native;

  if (!device || !info || !nativeTexture || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  stencilCopyView = nil;
  if (stencilCopyFormat != MTLPixelFormatInvalid &&
      (info->usage & (GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST)) != 0u) {
    stencilCopyView = [nativeTexture
      newTextureViewWithPixelFormat:stencilCopyFormat];
    if (!stencilCopyView) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    nativeTexture.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  texture = calloc(1, sizeof(*texture) + sizeof(*native));
  if (!texture) {
    [stencilCopyView release];
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native                  = (GPUTextureMT *)(texture + 1);
  native->texture         = nativeTexture;
  native->stencilCopyView = stencilCopyView;
  texture->_priv          = native;
  texture->device         = device;
  texture->format         = info->format;
  texture->dimension      = info->dimension;
  texture->width          = info->width;
  texture->height         = info->height;
  texture->depthOrLayers  = info->depthOrLayers;
  texture->mipLevelCount  = info->mipLevelCount ? info->mipLevelCount : 1u;
  texture->sampleCount    = info->sampleCount ? info->sampleCount : 1u;
  texture->usage          = info->usage;
  texture->_ownsNative    = true;
  *outTexture             = texture;
  return GPU_OK;
}

GPU_HIDE
GPUResult
mt_createTexture(GPUDevice                  * __restrict device,
                 const GPUTextureCreateInfo * __restrict info,
                 GPUTexture                ** __restrict outTexture) {
  GPUDeviceMT          *deviceMT;
  MTLTextureDescriptor *desc;
  id<MTLTexture>        nativeTexture;
  MTLPixelFormat        stencilCopyFormat;
  MTLStorageMode        storageMode;
  GPUResult             result;

  if (!device || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;

  deviceMT    = device->_priv;
  storageMode = MTLStorageModePrivate;
  if ((info->usage & GPU_TEXTURE_USAGE_COPY_DST) != 0) {
#if TARGET_OS_OSX
    storageMode = MTLStorageModeManaged;
#else
    storageMode = MTLStorageModeShared;
#endif
  }

  result = mt_createTextureDescriptor(device,
                                      info,
                                      storageMode,
                                      &desc,
                                      &stencilCopyFormat);
  if (result != GPU_OK) {
    return result;
  }

  nativeTexture = [deviceMT->device newTextureWithDescriptor:desc];
  [desc release];
  if (!nativeTexture) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = mt_wrapTexture(device,
                          info,
                          nativeTexture,
                          stencilCopyFormat,
                          outTexture);
  if (result != GPU_OK) {
    [nativeTexture release];
    return result;
  }
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

static GPUResult
mt_writeTextureBlit(GPUQueue                    *queue,
                    GPUTexture                  *texture,
                    const GPUTextureWriteRegion *region,
                    const void                  *data,
                    const GPUFormatDataLayout   *dataLayout,
                    MTLBlitOption                 option) {
  id<MTLBlitCommandEncoder> blit;
  id<MTLBuffer>             upload;
  id<MTLTexture>            nativeTexture;
  uint8_t                  *contents;
  MTLSize                   size;
  uint64_t                  uploadOffset;
  GPUResult                 result;

  nativeTexture = mt_nativeTexture(texture);
  if (!nativeTexture || dataLayout->requiredBytes > NSUIntegerMax) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = mt_beginTransfer(queue,
                            dataLayout->requiredBytes,
                            &blit,
                            &upload,
                            &uploadOffset);
  if (result != GPU_OK) {
    return result;
  }
  contents = (uint8_t *)[upload contents];
  if (!contents) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  memcpy(contents + uploadOffset,
         data,
         (size_t)dataLayout->requiredBytes);

  size = MTLSizeMake(region->width,
                     region->height,
                     texture->dimension == GPU_TEXTURE_DIMENSION_3D
                       ? region->depth
                       : 1u);
  if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
    [blit copyFromBuffer:upload
            sourceOffset:(NSUInteger)uploadOffset
       sourceBytesPerRow:region->bytesPerRow
     sourceBytesPerImage:(NSUInteger)dataLayout->bytesPerImage
              sourceSize:size
               toTexture:nativeTexture
        destinationSlice:0u
        destinationLevel:region->mipLevel
       destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                 options:option];
    return GPU_OK;
  }
  for (uint32_t i = 0u; i < region->layerCount; i++) {
    [blit copyFromBuffer:upload
            sourceOffset:(NSUInteger)(uploadOffset +
                                      (uint64_t)i * dataLayout->bytesPerImage)
       sourceBytesPerRow:region->bytesPerRow
     sourceBytesPerImage:(NSUInteger)dataLayout->bytesPerImage
              sourceSize:size
               toTexture:nativeTexture
        destinationSlice:region->baseArrayLayer + i
        destinationLevel:region->mipLevel
       destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                 options:option];
  }
  return GPU_OK;
}

#if MT_HAS_METAL4
static GPUResult
mt_writeSparseTexture4(GPUQueue                    *queue,
                       GPUTexture                  *texture,
                       const GPUTextureWriteRegion *region,
                       const void                  *data,
                       const GPUFormatDataLayout   *dataLayout) {
  GPUCommandBuffer             *cmdb;
  GPUCommandBuffer             *submitList[1];
  GPUQueueSubmitInfo            submitInfo = {0};
  MTCommandQueue               *nativeQueue;
  GPUHeapMT                    *nativeHeap;
  id<MTL4ComputeCommandEncoder> encoder;
  id<MTLTexture>                nativeTexture;
  id<MTLBuffer>                 upload;
  MTLSize                       size;
  uint64_t                      uploadOffset;
  GPUResult                     result;

  nativeQueue   = mt_commandQueue(queue);
  nativeHeap    = texture && texture->_heap ? texture->_heap->_priv : NULL;
  nativeTexture = mt_nativeTexture(texture);
  cmdb          = NULL;
  if (!nativeQueue || nativeQueue->mode != MTCommandMode4 ||
      !nativeTexture || !nativeHeap || !nativeHeap->heap) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  result = GPUAcquireCommandBuffer(queue, "sparse texture upload", &cmdb);
  if (result != GPU_OK || !cmdb ||
      !mt_reserveUpload(cmdb,
                        dataLayout->requiredBytes,
                        256u,
                        &upload,
                        &uploadOffset)) {
    if (cmdb) {
      GPUCommit(cmdb);
    }
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  memcpy((uint8_t *)upload.contents + uploadOffset,
         data,
         (size_t)dataLayout->requiredBytes);

  if (@available(macOS 26.0, iOS 26.0, *)) {
    encoder = [(id<MTL4CommandBuffer>)mt_modernCommandBuffer(cmdb)
      computeCommandEncoder];
    if (!encoder) {
      GPUCommit(cmdb);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    mt_applyPendingBarrier(cmdb, encoder);
    mt_useAllocation(cmdb, nativeHeap->heap);
    mt_useAllocation(cmdb, nativeTexture);
    size = MTLSizeMake(region->width,
                       region->height,
                       texture->dimension == GPU_TEXTURE_DIMENSION_3D
                         ? region->depth
                         : 1u);
    if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
      [encoder copyFromBuffer:upload
                  sourceOffset:(NSUInteger)uploadOffset
             sourceBytesPerRow:region->bytesPerRow
           sourceBytesPerImage:(NSUInteger)dataLayout->bytesPerImage
                  sourceSize:size
                   toTexture:nativeTexture
            destinationSlice:0u
            destinationLevel:region->mipLevel
           destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                     options:MTLBlitOptionNone];
    } else {
      for (uint32_t i = 0u; i < region->layerCount; i++) {
        [encoder copyFromBuffer:upload
                    sourceOffset:(NSUInteger)(uploadOffset +
                                               (uint64_t)i *
                                                 dataLayout->bytesPerImage)
               sourceBytesPerRow:region->bytesPerRow
             sourceBytesPerImage:(NSUInteger)dataLayout->bytesPerImage
                    sourceSize:size
                     toTexture:nativeTexture
              destinationSlice:region->baseArrayLayer + i
              destinationLevel:region->mipLevel
             destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                       options:MTLBlitOptionNone];
      }
    }
    [encoder endEncoding];

    submitList[0]                  = cmdb;
    submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize   = sizeof(submitInfo);
    submitInfo.ppCommandBuffers   = submitList;
    submitInfo.commandBufferCount = 1u;
    return GPUQueueSubmit(queue, &submitInfo);
  }

  GPUCommit(cmdb);
  return GPU_ERROR_UNSUPPORTED;
}
#endif

GPU_HIDE
GPUResult
mt_createTextureView(GPUTexture                      * __restrict texture,
                     const GPUTextureViewCreateInfo  * __restrict info,
                     GPUTextureView                 ** __restrict outView) {
  id<MTLTexture> nativeTexture;
  id<MTLTexture> nativeView;
  MTTextureViewSlot *slot;
  GPUTextureView *view;
  MTLTextureType nativeViewType;
  NSRange levels;
  NSRange slices;
  bool fullView;

  if (!texture || !texture->_priv || !info || !outView ||
      info->format != texture->format) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outView = NULL;

  nativeTexture = mt_nativeTexture(texture);
  nativeViewType = mt_textureViewType(info->viewType, texture->sampleCount);
  nativeView     = nil;
  fullView = info->format == texture->format &&
             nativeTexture.textureType == nativeViewType &&
             info->baseMipLevel == 0 &&
             info->mipLevelCount == texture->mipLevelCount &&
             info->baseArrayLayer == 0 &&
             info->arrayLayerCount == gpuTextureArrayLayerCount(texture);
  view = calloc(1, sizeof(*view) + sizeof(*slot));
  if (!view) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  slot = (MTTextureViewSlot *)(view + 1);

#if MT_HAS_METAL4
  if (!fullView &&
      (texture->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                         GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                         GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT)) == 0u) {
    GPUDeviceMT             *deviceMT;
    MTLTextureViewDescriptor *descriptor;
    GPUResult                result;

    deviceMT = texture->device->_priv;
    if (deviceMT && deviceMT->commandMode == MTCommandMode4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        descriptor             = [MTLTextureViewDescriptor new];
        descriptor.pixelFormat = mt_format(info->format);
        descriptor.textureType = nativeViewType;
        descriptor.levelRange  = NSMakeRange(info->baseMipLevel,
                                             info->mipLevelCount);
        descriptor.sliceRange  = NSMakeRange(info->baseArrayLayer,
                                             info->arrayLayerCount);
        result = mt_acquireTextureView(deviceMT,
                                       nativeTexture,
                                       descriptor,
                                       slot,
                                       &view->_gpuResourceID);
        [descriptor release];
        if (result != GPU_OK) {
          free(view);
          return result;
        }
        nativeView = [nativeTexture retain];
      }
    }
  }
#endif

  if (!nativeView && fullView) {
    nativeView = nativeTexture;
    [nativeView retain];
  } else if (!nativeView) {
    levels = NSMakeRange(info->baseMipLevel, info->mipLevelCount);
    slices = NSMakeRange(info->baseArrayLayer, info->arrayLayerCount);
    nativeView = [nativeTexture newTextureViewWithPixelFormat:mt_format(info->format)
                                                  textureType:nativeViewType
                                                       levels:levels
                                                       slices:slices];
  }
  if (!nativeView) {
    if (slot->page) {
      mt_releaseTextureView(texture->device->_priv, slot);
    }
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  view->_priv       = nativeView;
  view->_texture    = texture;
  view->_ownsNative = true;
  if (view->_gpuResourceID == 0u) {
    if (@available(macOS 13.0, iOS 16.0, *)) {
      view->_gpuResourceID = nativeView.gpuResourceID._impl;
    }
  }
  *outView = view;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyTextureView(GPUTextureView * __restrict view) {
  MTTextureViewSlot *slot;

  if (!view) {
    return;
  }

  slot = (MTTextureViewSlot *)(view + 1);
  if (slot->page && view->_texture && view->_texture->device) {
    mt_releaseTextureView(view->_texture->device->_priv, slot);
  }
  if (view->_ownsNative && view->_priv) {
    [(id<MTLTexture>)view->_priv release];
  }
  free(view);
}

GPU_HIDE
GPUResult
mt_writeTexture(GPUQueue                    * __restrict queue,
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
    MTLBlitOption option;

    option = mt_copyOption(texture->format, region->aspect);
    if (option == MTLBlitOptionNone) {
      return GPU_ERROR_UNSUPPORTED;
    }
    return mt_writeTextureBlit(queue,
                               texture,
                               region,
                               data,
                               &dataLayout,
                               option);
  }
  if (texture->_sparse) {
#if MT_HAS_METAL4
    MTCommandQueue *nativeQueue;

    nativeQueue = mt_commandQueue(queue);
    if (nativeQueue && nativeQueue->mode == MTCommandMode4) {
      return mt_writeSparseTexture4(queue,
                                    texture,
                                    region,
                                    data,
                                    &dataLayout);
    }
#endif
    return mt_writeTextureBlit(queue,
                               texture,
                               region,
                               data,
                               &dataLayout,
                               MTLBlitOptionNone);
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
