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

#include "../../common.h"
#include "../buffer_internal.h"
#include "../cmdqueue_internal.h"
#include "../device_internal.h"
#include "../query_internal.h"
#include "../texture_internal.h"
#include "../vrs_internal.h"

#define GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS 8u

static GPUApi*
transferPassApi(const GPUTransferPassEncoder *pass) {
  return pass ? commandBufferApi(pass->_cmdb) : NULL;
}

#if GPU_BUILD_WITH_VALIDATION
static bool
validLoadOp(GPULoadOp op) {
  return op == GPU_LOAD_OP_LOAD
         || op == GPU_LOAD_OP_CLEAR
         || op == GPU_LOAD_OP_DONT_CARE;
}

static bool
validStoreOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE
         || op == GPU_STORE_OP_DONT_CARE;
}
#endif

static bool
validIndirectCommandRange(const GPUIndirectCommandRangeEXT *range,
                          GPUDevice                        *device,
                          uint32_t                          commandCount,
                          uint64_t                          commandSize) {
  uint64_t address;

  if (!range || !range->buffer || !device || commandCount == 0u
      || range->buffer->device != device
      || !bufferHasUsage(range->buffer,
                         GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT)
      || range->buffer->_gpuAddress == 0u
      || range->offset > UINT64_MAX - range->buffer->_gpuAddress
      || !bufferRangeValid(range->buffer, range->offset, range->sizeBytes)
      || range->strideBytes < commandSize
      || (range->strideBytes & 3u) != 0u
      || commandCount > range->sizeBytes / range->strideBytes) {
    return false;
  }

  address = range->buffer->_gpuAddress + range->offset;

  return (address & 3u) == 0u;
}

static bool
validAddressCopyFlags(GPUAddressCopyFlagsEXT flags) {
  const GPUAddressCopyFlagsEXT known = GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT
                                      | GPU_ADDRESS_COPY_SPARSE_BIT_EXT
                                      | GPU_ADDRESS_COPY_PROTECTED_BIT_EXT;

  return (flags & ~known) == 0u
         && (flags & GPU_ADDRESS_COPY_PROTECTED_BIT_EXT) == 0u;
}

static bool
indirectTextureAspect(GPUIndirectTextureAspectFlagsEXT aspect,
                      GPUTextureAspect                *outAspect) {
  if (!outAspect || aspect == 0u || (aspect & (aspect - 1u)) != 0u) {
    return false;
  }

  switch (aspect) {
    case GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT:
      *outAspect = GPU_TEXTURE_ASPECT_ALL;
      return true;
    case GPU_INDIRECT_TEXTURE_ASPECT_DEPTH_BIT_EXT:
      *outAspect = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
      return true;
    case GPU_INDIRECT_TEXTURE_ASPECT_STENCIL_BIT_EXT:
      *outAspect = GPU_TEXTURE_ASPECT_STENCIL_ONLY;
      return true;
    default:
      return false;
  }
}

static bool
validIndirectTextureSubresource(const GPUIndirectTextureSubresourceEXT *subresource,
                                const GPUTexture                       *texture) {
  GPUTextureAspect resolved;
  GPUTextureAspect aspect;
  uint32_t         layers;

  if (!subresource || !texture || subresource->layerCount == 0u
      || subresource->mipLevel >= texture->mipLevelCount
      || !indirectTextureAspect(subresource->aspectMask, &aspect)
      || !formatResolveCopyAspect(texture->format, aspect, &resolved)) {
    return false;
  }

  layers = texture->dimension == GPU_TEXTURE_DIMENSION_3D
             ? texture->depthOrLayers >> subresource->mipLevel
             : texture->depthOrLayers;

  if (layers == 0u) {
    layers = 1u;
  }

  return subresource->baseArrayLayer < layers
         && subresource->layerCount <= layers - subresource->baseArrayLayer;
}

#if GPU_BUILD_WITH_VALIDATION
static bool
textureViewHasUsage(const GPUTextureView *view, GPUTextureUsageFlags usage) {
  const GPUTexture *texture;

  texture = view ? view->_texture : NULL;

  return texture && (texture->usage & usage) == usage;
}

static uint32_t
textureViewSampleCount(const GPUTextureView *view) {
  return view && view->_texture ? view->_texture->sampleCount : 0u;
}

static uint32_t
textureViewExtent(uint32_t extent, const GPUTextureView *view) {
  uint32_t result;

  if (!view) {
    return 0u;
  }

  if (view->baseMipLevel >= 32u) {
    return 1u;
  }

  result = extent >> view->baseMipLevel;

  return result ? result : 1u;
}

static bool
formatIsDepthStencil(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH16_UNORM
         || format == GPU_FORMAT_STENCIL8
         || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
         || format == GPU_FORMAT_DEPTH32_FLOAT
         || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static bool
formatHasDepth(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH16_UNORM
         || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
         || format == GPU_FORMAT_DEPTH32_FLOAT
         || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static bool
formatHasStencil(GPUFormat format) {
  return format == GPU_FORMAT_STENCIL8
         || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
         || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}
#endif

static bool
textureCopySubresourcesOverlap(const GPUTexture                    *texture,
                               const GPUTextureToTextureCopyRegion *region) {
  uint32_t srcLayerEnd;
  uint32_t dstLayerEnd;

  if (region->src.mipLevel != region->dst.mipLevel) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
    return true;
  }

  srcLayerEnd = region->src.baseArrayLayer + region->layerCount;
  dstLayerEnd = region->dst.baseArrayLayer + region->layerCount;

  return region->src.baseArrayLayer < dstLayerEnd
         && region->dst.baseArrayLayer < srcLayerEnd;
}

static bool
validRenderPassCreateInfo(const GPURenderPassCreateInfo *info,
                          const GPUDevice               *device) {
#if GPU_BUILD_WITH_VALIDATION
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;
  const GPURenderPassDepthStencilAttachment  *depthStencil;
  const GPURenderPassColorAttachment         *color;
  uint32_t                                    sampleCount;
  uint32_t                                    i;

  if (!info) {
    return false;
  }

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO) {
    return false;
  }

  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return false;
  }

  if (info->colorAttachmentCount > GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS) {
    return false;
  }

  if (info->colorAttachmentCount > 0 && !info->pColorAttachments) {
    return false;
  }

  if (info->occlusionQuerySet
      && (info->occlusionQuerySet->device != device
          || info->occlusionQuerySet->type != GPU_QUERY_OCCLUSION)) {
    return false;
  }

  if (!validPassTimestampWrites(info->timestampWrites, device)) {
    return false;
  }

  if (!renderPassVRSExtensions(info, &shadingRate, &rateMap)) {
    return false;
  }

  if ((shadingRate || rateMap)
      && !GPUIsFeatureEnabled(device, GPU_FEATURE_VARIABLE_RATE_SHADING)) {
    return false;
  }

  if ((shadingRate
       && (device->vrsCapabilities.modes & GPU_VRS_ATTACHMENT_BIT_EXT) == 0u)
      || (rateMap
          && (device->vrsCapabilities.modes & GPU_VRS_RATE_MAP_BIT_EXT) == 0u)) {
    return false;
  }

  if (shadingRate
      && (!shadingRate->view
          || !textureViewHasUsage(shadingRate->view,
                                  GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT)
          || shadingRate->view->_texture->device != device
          || shadingRate->view->format != GPU_FORMAT_R8_UINT
          || shadingRate->view->viewType != GPU_TEXTURE_VIEW_2D
          || shadingRate->view->mipLevelCount != 1u
          || shadingRate->view->arrayLayerCount != 1u
          || shadingRate->view->_texture->sampleCount != 1u
          || shadingRate->texelSize.width == 0u
          || shadingRate->texelSize.height == 0u)) {
    return false;
  }

  if (shadingRate
      && (shadingRate->texelSize.width < device->vrsCapabilities.minAttachmentTexelSize.width
          || shadingRate->texelSize.height < device->vrsCapabilities.minAttachmentTexelSize.height
          || shadingRate->texelSize.width > device->vrsCapabilities.maxAttachmentTexelSize.width
          || shadingRate->texelSize.height > device->vrsCapabilities.maxAttachmentTexelSize.height)) {
    return false;
  }

  if (rateMap && (!rateMap->map || rateMap->map->device != device)) {
    return false;
  }

  sampleCount = 0u;

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];

    if (!color->view
        || !textureViewHasUsage(color->view, GPU_TEXTURE_USAGE_COLOR_TARGET)
        || color->view->_texture->device != device
        || formatIsDepthStencil(color->view->format)
        || (color->resolveView
            && (!textureViewHasUsage(color->resolveView, GPU_TEXTURE_USAGE_COLOR_TARGET)
                || color->resolveView->_texture->device != device
                || formatIsDepthStencil(color->resolveView->format)
                || color->resolveView->format != color->view->format
                || textureViewExtent(color->resolveView->_texture->width,
                                     color->resolveView) !=
                   textureViewExtent(color->view->_texture->width, color->view)
                || textureViewExtent(color->resolveView->_texture->height,
                                     color->resolveView) !=
                   textureViewExtent(color->view->_texture->height, color->view)
                || color->resolveView->arrayLayerCount != color->view->arrayLayerCount
                || textureViewSampleCount(color->view) <= 1u
                || textureViewSampleCount(color->resolveView) != 1u))
        || !validLoadOp(color->loadOp)
        || !validStoreOp(color->storeOp)) {
      return false;
    }

    if (sampleCount != 0u
        && sampleCount != textureViewSampleCount(color->view)) {
      return false;
    }

    sampleCount = textureViewSampleCount(color->view);
  }

  depthStencil = info->pDepthStencilAttachment;

  if (depthStencil) {
    if (!depthStencil->view
        || !textureViewHasUsage(depthStencil->view, GPU_TEXTURE_USAGE_DEPTH_STENCIL)
        || depthStencil->view->_texture->device != device
        || !formatIsDepthStencil(depthStencil->view->format)
        || !validLoadOp(depthStencil->depthLoadOp)
        || !validStoreOp(depthStencil->depthStoreOp)
        || !validLoadOp(depthStencil->stencilLoadOp)
        || !validStoreOp(depthStencil->stencilStoreOp)
        || (formatHasDepth(depthStencil->view->format)
           ? (depthStencil->depthLoadOp == GPU_LOAD_OP_CLEAR
              && (depthStencil->clearDepth < 0.0f
                  || depthStencil->clearDepth > 1.0f))
           : (depthStencil->depthLoadOp != GPU_LOAD_OP_DONT_CARE
              || depthStencil->depthStoreOp != GPU_STORE_OP_DONT_CARE))
        || (formatHasStencil(depthStencil->view->format)
           ? (depthStencil->stencilLoadOp == GPU_LOAD_OP_CLEAR
              && depthStencil->clearStencil > UINT8_MAX)
           : (depthStencil->stencilLoadOp != GPU_LOAD_OP_DONT_CARE
              || depthStencil->stencilStoreOp != GPU_STORE_OP_DONT_CARE))) {
      return false;
    }

    if (sampleCount != 0u
        && sampleCount != textureViewSampleCount(depthStencil->view)) {
      return false;
    }
  }

  return info->colorAttachmentCount > 0 || depthStencil != NULL;
#else
  return info
         && (info->chain.sType == GPU_STRUCTURE_TYPE_NONE
             || info->chain.sType == GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)
         && (info->chain.structSize == 0u
             || info->chain.structSize >= sizeof(*info))
         && info->colorAttachmentCount <= GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS
         && (info->colorAttachmentCount == 0u || info->pColorAttachments)
         && (info->colorAttachmentCount > 0u || info->pDepthStencilAttachment)
         && validPassTimestampWrites(info->timestampWrites, device);
#endif
}

static void
setRenderPassEncoderInfo(GPURenderPassEncoder          *encoder,
                         const GPURenderPassCreateInfo *info) {
#if GPU_BUILD_WITH_VALIDATION
  const GPURenderPassDepthStencilAttachment *depthStencil;
  const GPURenderPassColorAttachment        *color;
  uint32_t                                   i;
#endif

  encoder->_occlusionQuerySet = info->occlusionQuerySet;
  encoder->_timestampQuerySet = info->timestampWrites
                                  ? info->timestampWrites->querySet
                                  : NULL;
  encoder->_timestampEndIndex = info->timestampWrites
                                  ? info->timestampWrites->endIndex
                                  : 0u;
#if GPU_BUILD_WITH_VALIDATION
  encoder->_colorAttachmentCount = info->colorAttachmentCount;

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];

    encoder->_colorAttachmentFormats[i]      = color->view->format;
    encoder->_colorAttachmentSampleCounts[i] = textureViewSampleCount(color->view);
    encoder->_colorAttachmentHasResolve[i]   = color->resolveView != NULL;
  }

  depthStencil = info->pDepthStencilAttachment;

  if (depthStencil) {
    encoder->_depthStencilFormat      = depthStencil->view->format;
    encoder->_depthStencilSampleCount = textureViewSampleCount(depthStencil->view);
  } else {
    encoder->_depthStencilFormat      = GPU_FORMAT_UNDEFINED;
    encoder->_depthStencilSampleCount = 0u;
  }
#endif
}

static uint32_t
copyMipExtent(uint32_t extent, uint32_t mipLevel) {
  uint32_t mipExtent;

  mipExtent = extent >> mipLevel;

  return mipExtent > 0 ? mipExtent : 1u;
}

static bool
validTextureCopyRegion(const GPUTextureSubresourceRegion *region,
                       const GPUTexture                  *texture) {
  const GPUTextureLocation *location;
  GPUTextureAspect          aspect;
  uint32_t                  mipWidth;
  uint32_t                  mipHeight;
  uint32_t                  mipDepth;

  if (!region
      || !texture
      || region->width == 0
      || region->height == 0
      || region->depth == 0
      || region->layerCount == 0) {
    return false;
  }

  location = &region->texture;

  if (location->mipLevel >= texture->mipLevelCount
      || !formatResolveCopyAspect(texture->format,
                                  location->aspect,
                                  &aspect)) {
    return false;
  }

  mipWidth  = copyMipExtent(texture->width, location->mipLevel);
  mipHeight = copyMipExtent(texture->height, location->mipLevel);

  if (location->x > mipWidth
      || region->width > mipWidth - location->x
      || location->y > mipHeight
      || region->height > mipHeight - location->y) {
    return false;
  }

  if (!formatCopyAligned(texture->format,
                         location->x,
                         location->y,
                         region->width,
                         region->height,
                         mipWidth,
                         mipHeight)) {
    return false;
  }

  if (aspect != GPU_TEXTURE_ASPECT_ALL
      && (location->x != 0u || location->y != 0u || location->z != 0u
          || region->width != mipWidth || region->height != mipHeight
          || region->depth != 1u)) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_1D
      && (location->y != 0 || region->height != 1)) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
    mipDepth = copyMipExtent(texture->depthOrLayers, location->mipLevel);
    return location->baseArrayLayer == 0
           && region->layerCount == 1
           && location->z <= mipDepth
           && region->depth <= mipDepth - location->z;
  }

  return location->z == 0
         && region->depth == 1
         && location->baseArrayLayer < texture->depthOrLayers
         && region->layerCount <= texture->depthOrLayers - location->baseArrayLayer;
}

static bool
validBufferTextureCopy(const GPUBufferTextureCopyRegion *region,
                       const GPUTexture                 *texture,
                       uint64_t                         *outBytes) {
  GPUFormatDataLayout layout;

  if (!region || !texture || !outBytes
      || !validTextureCopyRegion(&region->texture, texture)
      || texture->sampleCount != 1u
      || !formatAspectDataLayout(texture->format,
                                 region->texture.texture.aspect,
                                 region->texture.width,
                                 region->texture.height,
                                 region->texture.depth,
                                 region->texture.layerCount,
                                 region->bytesPerRow,
                                 region->rowsPerImage,
                                 &layout)) {
    return false;
  }

  *outBytes = layout.requiredBytes;

  return true;
}

static void
gpu_destroyRenderPass(GPUApi *api, GPURenderPassDesc *pass) {
  if (!pass) {
    return;
  }

  if (api && api->renderPass.destroyRenderPass) {
    api->renderPass.destroyRenderPass(pass);
    return;
  }

  free(pass);
}

GPU_EXPORT
GPURenderPassEncoder*
GPUBeginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  GPURenderPassDesc    *desc;
  GPURenderPassEncoder *encoder;
  GPUDevice            *device;
  GPUApi               *api;
  bool                  wroteBeginTimestamp;

  device = commandBufferDevice(cmdb);

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder
      || !validRenderPassCreateInfo(info, device))
    return NULL;

  if (!(api = deviceApi(device)))
    return NULL;

  if (!api->renderPass.beginRenderPass || !api->rce.renderCommandEncoder)
    return NULL;

  wroteBeginTimestamp = false;

  if (info->timestampWrites && api->cmdbuf.writeTimestamp) {
    api->cmdbuf.writeTimestamp(cmdb,
                               info->timestampWrites->querySet,
                               info->timestampWrites->beginIndex,
                               true);
    wroteBeginTimestamp = true;
  }

  if (!(desc = api->renderPass.beginRenderPass(cmdb, info))) {
    if (wroteBeginTimestamp) {
      api->cmdbuf.writeTimestamp(cmdb,
                                 info->timestampWrites->querySet,
                                 info->timestampWrites->endIndex,
                                 false);
    }
    return NULL;
  }

  encoder = api->rce.renderCommandEncoder(cmdb, desc);
  gpu_destroyRenderPass(api, desc);

  if (encoder) {
    encoder->_api               = api;
    encoder->_device            = device;
    encoder->_cmdb              = cmdb;
    encoder->_drawPrimitives    = encoder->_drawPrimitives
                                   ? encoder->_drawPrimitives
                                   : api->rce.drawPrimitives;
    encoder->_drawIndexedPrims  = encoder->_drawIndexedPrims
                                   ? encoder->_drawIndexedPrims
                                   : api->rce.drawIndexedPrims;
    encoder->_vertexInputBuffer = api->rce.vertexInputBuffer;
    encoder->_bindRenderGroup   = api->descriptor.bindRenderGroup;
    encoder->_stats             = device->runtimeConfig.enableStats
                                   ? &device->currentFrameStats
                                   : NULL;
    setRenderPassEncoderInfo(encoder, info);
    cmdb->_activeEncoder = true;
  } else if (wroteBeginTimestamp) {
    api->cmdbuf.writeTimestamp(cmdb,
                               info->timestampWrites->querySet,
                               info->timestampWrites->endIndex,
                               false);
  }

  return encoder;
}

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassEncoder *pass) {
  GPUApi *api;
#if GPU_BUILD_WITH_VALIDATION
  GPUDevice *device;
#endif

  if (!pass || pass->_ended)
    return;

  if (pass->_occlusionQueryActive) {
#if GPU_BUILD_WITH_VALIDATION
    device = pass->_cmdb && pass->_cmdb->_queue
               ? pass->_cmdb->_queue->_device
               : NULL;
    deviceRecordValidationError(device,
                                "GPUEndRenderPass requires ending the active occlusion query");
#endif
    return;
  }

  pass->_ended = true;

  if (pass->_cmdb) {
    pass->_cmdb->_activeEncoder = false;
  }

  if (!(api = commandBufferApi(pass->_cmdb)) || !api->rce.endEncoding)
    return;

  api->rce.endEncoding(pass);

  if (pass->_timestampQuerySet && api->cmdbuf.writeTimestamp) {
    api->cmdbuf.writeTimestamp(pass->_cmdb,
                               pass->_timestampQuerySet,
                               pass->_timestampEndIndex,
                               false);
  }
}

GPU_EXPORT
GPUTransferPassEncoder*
GPUBeginTransferPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUTransferPassEncoder *pass;
  GPUDevice              *device;
  GPUApi                 *api;

  if (!cmdb || cmdb->_submitted || cmdb->_activeEncoder) {
    return NULL;
  }

  device = commandBufferDevice(cmdb);

  if (!(api = deviceApi(device)) || !api->renderPass.beginTransferPass) {
    return NULL;
  }

  label = deviceDebugLabel(device, label);

  if ((pass = api->renderPass.beginTransferPass(cmdb, label))) {
    pass->_cmdb          = cmdb;
    cmdb->_activeEncoder = true;
  }

  return pass;
}

GPU_EXPORT
void
GPUCopyBufferToBuffer(GPUTransferPassEncoder    *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region) {
  GPUApi *api;

  if (!pass || pass->_ended
      || !src || !dst || !region || region->sizeBytes == 0
      || !bufferHasUsage(src, GPU_BUFFER_USAGE_COPY_SRC)
      || !bufferHasUsage(dst, GPU_BUFFER_USAGE_COPY_DST)
      || !bufferRangeValid(src, region->srcOffset, region->sizeBytes)
      || !bufferRangeValid(dst, region->dstOffset, region->sizeBytes)) {
    return;
  }

  if (!(api = transferPassApi(pass)) || !api->renderPass.copyBufferToBuffer) {
    return;
  }

  api->renderPass.copyBufferToBuffer(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyBufferToTexture(GPUTransferPassEncoder           *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUApi  *api;
  uint64_t copyBytes;

  if (!pass || pass->_ended
      || !src || !dst || !validBufferTextureCopy(region, dst, &copyBytes)
      || !bufferHasUsage(src, GPU_BUFFER_USAGE_COPY_SRC)
      || (dst->usage & GPU_TEXTURE_USAGE_COPY_DST) == 0
      || !bufferRangeValid(src, region->bufferOffset, copyBytes)) {
    return;
  }

  if (!(api = transferPassApi(pass)) || !api->renderPass.copyBufferToTexture) {
    return;
  }

  api->renderPass.copyBufferToTexture(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyTextureToBuffer(GPUTransferPassEncoder           *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUApi  *api;
  uint64_t copyBytes;

  if (!pass || pass->_ended
      || !src || !dst || !validBufferTextureCopy(region, src, &copyBytes)
      || (src->usage & GPU_TEXTURE_USAGE_COPY_SRC) == 0
      || !bufferHasUsage(dst, GPU_BUFFER_USAGE_COPY_DST)
      || !bufferRangeValid(dst, region->bufferOffset, copyBytes)) {
    return;
  }

  if (!(api = transferPassApi(pass)) || !api->renderPass.copyTextureToBuffer) {
    return;
  }

  api->renderPass.copyTextureToBuffer(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyTextureToTexture(GPUTransferPassEncoder              *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  GPUTextureSubresourceRegion srcRegion;
  GPUTextureSubresourceRegion dstRegion;
  GPUApi                     *api;
  GPUTextureAspect            srcAspect;
  GPUTextureAspect            dstAspect;

  if (!pass || pass->_ended || !src || !dst || !region
      || src->dimension != dst->dimension
      || src->format != dst->format
      || src->sampleCount != dst->sampleCount
      || (src->usage & GPU_TEXTURE_USAGE_COPY_SRC) == 0
      || (dst->usage & GPU_TEXTURE_USAGE_COPY_DST) == 0
      || region->width == 0
      || region->height == 0
      || region->depth == 0
      || region->layerCount == 0) {
    return;
  }

  memset(&srcRegion, 0, sizeof(srcRegion));
  memset(&dstRegion, 0, sizeof(dstRegion));
  srcRegion.texture    = region->src;
  srcRegion.width      = region->width;
  srcRegion.height     = region->height;
  srcRegion.depth      = region->depth;
  srcRegion.layerCount = region->layerCount;
  dstRegion.texture    = region->dst;
  dstRegion.width      = region->width;
  dstRegion.height     = region->height;
  dstRegion.depth      = region->depth;
  dstRegion.layerCount = region->layerCount;

  if (!formatResolveCopyAspect(src->format,
                               region->src.aspect,
                               &srcAspect)
      || !formatResolveCopyAspect(dst->format,
                                  region->dst.aspect,
                                  &dstAspect)
      || srcAspect != dstAspect
      || !validTextureCopyRegion(&srcRegion, src)
      || !validTextureCopyRegion(&dstRegion, dst)
      || (src == dst && textureCopySubresourcesOverlap(src, region))) {
    return;
  }

  if (!(api = transferPassApi(pass)) || !api->renderPass.copyTextureToTexture) {
    return;
  }

  api->renderPass.copyTextureToTexture(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyMemoryIndirectEXT(GPUTransferPassEncoder             *pass,
                         const GPUIndirectMemoryCopyInfoEXT *info) {
  GPUDevice *device;
  GPUApi    *api;

  device = pass && pass->_cmdb ? commandBufferDevice(pass->_cmdb) : NULL;

  if (!pass || pass->_ended || !info
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_MEMORY_COPY)
      || !validIndirectCommandRange(&info->commands,
                                    device,
                                    info->commandCount,
                                    sizeof(GPUIndirectMemoryCopyCommandEXT))
      || !validAddressCopyFlags(info->srcFlags)
      || !validAddressCopyFlags(info->dstFlags)) {
    return;
  }

  if (!(api = transferPassApi(pass))
      || !api->renderPass.copyMemoryIndirect) {
    return;
  }

  api->renderPass.copyMemoryIndirect(pass, info);
}

GPU_EXPORT
void
GPUCopyMemoryToTextureIndirectEXT(GPUTransferPassEncoder                      *pass,
                                  const GPUIndirectMemoryToTextureCopyInfoEXT *info) {
  GPUDevice *device;
  GPUApi    *api;
  uint32_t   i;

  device = pass && pass->_cmdb ? commandBufferDevice(pass->_cmdb) : NULL;

  if (!pass || pass->_ended || !info || !info->dst
      || !info->pTextureSubresources || info->dst->device != device
      || (info->dst->usage & GPU_TEXTURE_USAGE_COPY_DST) == 0u
      || info->dst->sampleCount != 1u
      || !GPUIsFeatureEnabled(device,
                              GPU_FEATURE_INDIRECT_MEMORY_TO_TEXTURE_COPY)
      || !validIndirectCommandRange(&info->commands,
                                    device,
                                    info->commandCount,
                                    sizeof(GPUIndirectMemoryToTextureCommandEXT))
      || !validAddressCopyFlags(info->srcFlags)) {
    return;
  }

  for (i = 0u; i < info->commandCount; i++) {
    if (!validIndirectTextureSubresource(&info->pTextureSubresources[i],
                                         info->dst)) {
      return;
    }
  }

  if (!(api = transferPassApi(pass))
      || !api->renderPass.copyMemoryToTextureIndirect) {
    return;
  }

  api->renderPass.copyMemoryToTextureIndirect(pass, info);
}

GPU_EXPORT
void
GPUEndTransferPass(GPUTransferPassEncoder *pass) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }

  pass->_ended = true;

  if (pass->_cmdb) {
    pass->_cmdb->_activeEncoder = false;
  }

  if (!(api = transferPassApi(pass)) || !api->renderPass.endTransferPass) {
    return;
  }

  api->renderPass.endTransferPass(pass);
}
