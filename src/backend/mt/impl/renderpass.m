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

GPU_HIDE
GPURenderPassDesc*
mt_beginRenderPass(const GPURenderPassCreateInfo *info) {
  const GPURenderPassColorAttachment        *color;
  const GPURenderPassDepthStencilAttachment *depthStencil;
  GPURenderPassDesc                         *renderPass;
  MTLRenderPassDescriptor                   *rpd;
  uint32_t                                   i;

  if (!info || (info->colorAttachmentCount > 0 && !info->pColorAttachments))
    return NULL;

  rpd = [MTLRenderPassDescriptor renderPassDescriptor];

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];
    if (!color->view)
      return NULL;

    rpd.colorAttachments[i].texture        = (id<MTLTexture>)color->view->_priv;
    rpd.colorAttachments[i].resolveTexture = color->resolveView ?
      (id<MTLTexture>)color->resolveView->_priv : nil;

    switch (color->loadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.colorAttachments[i].loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.colorAttachments[i].loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.colorAttachments[i].loadAction = MTLLoadActionLoad;
        break;
    }

    switch (color->storeOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.colorAttachments[i].storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.colorAttachments[i].storeAction = MTLStoreActionStore;
        break;
    }

    rpd.colorAttachments[i].clearColor = MTLClearColorMake(color->clearColor.float32[0],
                                                           color->clearColor.float32[1],
                                                           color->clearColor.float32[2],
                                                           color->clearColor.float32[3]);
  }

  depthStencil = info->pDepthStencilAttachment;
  if (depthStencil && depthStencil->view) {
    rpd.depthAttachment.texture   = (id<MTLTexture>)depthStencil->view->_priv;
    rpd.stencilAttachment.texture = (id<MTLTexture>)depthStencil->view->_priv;

    switch (depthStencil->depthLoadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.depthAttachment.loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.depthAttachment.loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.depthAttachment.loadAction = MTLLoadActionLoad;
        break;
    }

    switch (depthStencil->depthStoreOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.depthAttachment.storeAction = MTLStoreActionStore;
        break;
    }

    switch (depthStencil->stencilLoadOp) {
      case GPU_LOAD_OP_CLEAR:
        rpd.stencilAttachment.loadAction = MTLLoadActionClear;
        break;
      case GPU_LOAD_OP_DONT_CARE:
        rpd.stencilAttachment.loadAction = MTLLoadActionDontCare;
        break;
      case GPU_LOAD_OP_LOAD:
      default:
        rpd.stencilAttachment.loadAction = MTLLoadActionLoad;
        break;
    }

    switch (depthStencil->stencilStoreOp) {
      case GPU_STORE_OP_DONT_CARE:
        rpd.stencilAttachment.storeAction = MTLStoreActionDontCare;
        break;
      case GPU_STORE_OP_STORE:
      default:
        rpd.stencilAttachment.storeAction = MTLStoreActionStore;
        break;
    }

    rpd.depthAttachment.clearDepth       = depthStencil->clearDepth;
    rpd.stencilAttachment.clearStencil   = depthStencil->clearStencil;
  }

  renderPass = calloc(1, sizeof(*renderPass));
  if (!renderPass)
    return NULL;
  renderPass->_priv = rpd;
  renderPass->label = info->label;

  return renderPass;
}

GPU_HIDE
void
mt_destroyRenderPass(GPURenderPassDesc *pass) {
  free(pass);
}

static id<MTLBlitCommandEncoder>
mt_nativeCopyEncoder(GPUCopyPassEncoder *pass) {
  return pass ? (__bridge id<MTLBlitCommandEncoder>)pass->_priv : nil;
}

static id<MTLBuffer>
mt_nativeBuffer(GPUBuffer *buffer) {
  return buffer ? (id<MTLBuffer>)buffer->_priv : nil;
}

static bool
mt_bufferCopyRangeValid(id<MTLBuffer> buffer, uint64_t offset, uint64_t size) {
  uint64_t length;

  if (!buffer || size == 0) {
    return false;
  }

  length = (uint64_t)[buffer length];
  return offset <= length && size <= length - offset;
}

static bool
mt_bufferTextureRangeValid(id<MTLBuffer>                         buffer,
                           const GPUBufferTextureCopyRegion     *region,
                           uint64_t                             *outBytesPerImage) {
  uint64_t rowsPerImage;
  uint64_t bytesPerImage;
  uint64_t imageCount;
  uint64_t totalBytes;

  if (!buffer || !region || region->bytesPerRow == 0) {
    return false;
  }

  rowsPerImage = region->rowsPerImage ?
    region->rowsPerImage : region->texture.height;
  if (rowsPerImage == 0 ||
      region->bytesPerRow > UINT64_MAX / rowsPerImage) {
    return false;
  }

  bytesPerImage = (uint64_t)region->bytesPerRow * rowsPerImage;
  if (region->texture.depth == 0 ||
      region->texture.layerCount == 0 ||
      region->texture.depth > UINT64_MAX / region->texture.layerCount) {
    return false;
  }

  imageCount = (uint64_t)region->texture.depth * region->texture.layerCount;
  if (bytesPerImage > UINT64_MAX / imageCount) {
    return false;
  }

  totalBytes = bytesPerImage * imageCount;
  if (!mt_bufferCopyRangeValid(buffer, region->bufferOffset, totalBytes)) {
    return false;
  }

  *outBytesPerImage = bytesPerImage;
  return true;
}

static MTLOrigin
mt_textureOrigin(const GPUTextureLocation *location, bool texture3D) {
  return MTLOriginMake(location->x, location->y, texture3D ? location->z : 0u);
}

static MTLSize
mt_textureCopySize(const GPUTextureSubresourceRegion *region, bool texture3D) {
  return MTLSizeMake(region->width, region->height, texture3D ? region->depth : 1u);
}

GPU_HIDE
GPUCopyPassEncoder*
mt_beginCopyPass(GPUCommandBuffer *cmdb, const char *label) {
  id<MTLBlitCommandEncoder> native;
  GPUCopyPassEncoder *pass;

  if (!cmdb) {
    return NULL;
  }

  native = [(id<MTLCommandBuffer>)cmdb->_priv blitCommandEncoder];
  if (!native) {
    return NULL;
  }
  if (label) {
    native.label = [NSString stringWithUTF8String:label];
  }

  pass = calloc(1, sizeof(*pass));
  if (!pass) {
    [native endEncoding];
    return NULL;
  }

  pass->_priv = (__bridge void *)native;
  return pass;
}

GPU_HIDE
void
mt_copyBufferToBuffer(GPUCopyPassEncoder        *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region) {
  id<MTLBuffer> srcBuffer;
  id<MTLBuffer> dstBuffer;

  srcBuffer = mt_nativeBuffer(src);
  dstBuffer = mt_nativeBuffer(dst);
  if (!pass ||
      !region ||
      !mt_bufferCopyRangeValid(srcBuffer, region->srcOffset, region->sizeBytes) ||
      !mt_bufferCopyRangeValid(dstBuffer, region->dstOffset, region->sizeBytes)) {
    return;
  }

  [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                sourceOffset:(NSUInteger)region->srcOffset
                                    toBuffer:dstBuffer
                           destinationOffset:(NSUInteger)region->dstOffset
                                        size:(NSUInteger)region->sizeBytes];
}

GPU_HIDE
void
mt_copyBufferToTexture(GPUCopyPassEncoder               *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  const GPUTextureSubresourceRegion *texRegion;
  id<MTLBuffer> srcBuffer;
  id<MTLTexture> dstTexture;
  uint64_t bytesPerImage;
  bool texture3D;

  srcBuffer = mt_nativeBuffer(src);
  if (!pass ||
      !dst ||
      !dst->_priv ||
      !mt_bufferTextureRangeValid(srcBuffer, region, &bytesPerImage)) {
    return;
  }

  texRegion = &region->texture;
  dstTexture = (id<MTLTexture>)dst->_priv;
  texture3D = dst->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (texture3D) {
    [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                  sourceOffset:(NSUInteger)region->bufferOffset
                             sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                           sourceBytesPerImage:(NSUInteger)bytesPerImage
                                    sourceSize:mt_textureCopySize(texRegion, true)
                                     toTexture:dstTexture
                              destinationSlice:0
                              destinationLevel:texRegion->texture.mipLevel
                             destinationOrigin:mt_textureOrigin(&texRegion->texture, true)];
    return;
  }

  for (uint32_t i = 0; i < texRegion->layerCount; i++) {
    [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                  sourceOffset:(NSUInteger)(region->bufferOffset +
                                                            ((uint64_t)i * bytesPerImage))
                             sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                           sourceBytesPerImage:(NSUInteger)bytesPerImage
                                    sourceSize:mt_textureCopySize(texRegion, false)
                                     toTexture:dstTexture
                              destinationSlice:texRegion->texture.baseArrayLayer + i
                              destinationLevel:texRegion->texture.mipLevel
                             destinationOrigin:mt_textureOrigin(&texRegion->texture, false)];
  }
}

GPU_HIDE
void
mt_copyTextureToBuffer(GPUCopyPassEncoder               *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  const GPUTextureSubresourceRegion *texRegion;
  id<MTLTexture> srcTexture;
  id<MTLBuffer> dstBuffer;
  uint64_t bytesPerImage;
  bool texture3D;

  dstBuffer = mt_nativeBuffer(dst);
  if (!pass ||
      !src ||
      !src->_priv ||
      !mt_bufferTextureRangeValid(dstBuffer, region, &bytesPerImage)) {
    return;
  }

  texRegion = &region->texture;
  srcTexture = (id<MTLTexture>)src->_priv;
  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (texture3D) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:0
                                    sourceLevel:texRegion->texture.mipLevel
                                   sourceOrigin:mt_textureOrigin(&texRegion->texture, true)
                                     sourceSize:mt_textureCopySize(texRegion, true)
                                       toBuffer:dstBuffer
                              destinationOffset:(NSUInteger)region->bufferOffset
                         destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                       destinationBytesPerImage:(NSUInteger)bytesPerImage];
    return;
  }

  for (uint32_t i = 0; i < texRegion->layerCount; i++) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:texRegion->texture.baseArrayLayer + i
                                    sourceLevel:texRegion->texture.mipLevel
                                   sourceOrigin:mt_textureOrigin(&texRegion->texture, false)
                                     sourceSize:mt_textureCopySize(texRegion, false)
                                       toBuffer:dstBuffer
                              destinationOffset:(NSUInteger)(region->bufferOffset +
                                                            ((uint64_t)i * bytesPerImage))
                         destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                       destinationBytesPerImage:(NSUInteger)bytesPerImage];
  }
}

GPU_HIDE
void
mt_copyTextureToTexture(GPUCopyPassEncoder                  *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  id<MTLTexture> srcTexture;
  id<MTLTexture> dstTexture;
  MTLSize size;
  bool texture3D;

  if (!pass || !src || !dst || !src->_priv || !dst->_priv || !region) {
    return;
  }

  srcTexture = (id<MTLTexture>)src->_priv;
  dstTexture = (id<MTLTexture>)dst->_priv;
  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
  size = MTLSizeMake(region->width, region->height, texture3D ? region->depth : 1u);
  if (texture3D) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:0
                                    sourceLevel:region->src.mipLevel
                                   sourceOrigin:mt_textureOrigin(&region->src, true)
                                     sourceSize:size
                                      toTexture:dstTexture
                               destinationSlice:0
                               destinationLevel:region->dst.mipLevel
                              destinationOrigin:mt_textureOrigin(&region->dst, true)];
    return;
  }

  for (uint32_t i = 0; i < region->layerCount; i++) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:region->src.baseArrayLayer + i
                                    sourceLevel:region->src.mipLevel
                                   sourceOrigin:mt_textureOrigin(&region->src, false)
                                     sourceSize:size
                                      toTexture:dstTexture
                               destinationSlice:region->dst.baseArrayLayer + i
                               destinationLevel:region->dst.mipLevel
                              destinationOrigin:mt_textureOrigin(&region->dst, false)];
  }
}

GPU_HIDE
void
mt_endCopyPass(GPUCopyPassEncoder *pass) {
  if (!pass) {
    return;
  }

  [mt_nativeCopyEncoder(pass) endEncoding];
  free(pass);
}

GPU_HIDE
void
mt_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPU__UNUSED(cmdb);
  GPU__UNUSED(barriers);
}

GPU_HIDE
void
mt_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = mt_beginRenderPass;
  api->destroyRenderPass = mt_destroyRenderPass;
  api->beginCopyPass = mt_beginCopyPass;
  api->copyBufferToBuffer = mt_copyBufferToBuffer;
  api->copyBufferToTexture = mt_copyBufferToTexture;
  api->copyTextureToBuffer = mt_copyTextureToBuffer;
  api->copyTextureToTexture = mt_copyTextureToTexture;
  api->endCopyPass = mt_endCopyPass;
  api->encodeBarriers = mt_encodeBarriers;
}
