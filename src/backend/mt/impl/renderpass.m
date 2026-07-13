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

static MTLLoadAction
mt_loadAction(GPULoadOp op) {
  switch (op) {
    case GPU_LOAD_OP_CLEAR:
      return MTLLoadActionClear;
    case GPU_LOAD_OP_DONT_CARE:
      return MTLLoadActionDontCare;
    case GPU_LOAD_OP_LOAD:
    default:
      return MTLLoadActionLoad;
  }
}

static MTLStoreAction
mt_storeAction(GPUStoreOp op) {
  return op == GPU_STORE_OP_DONT_CARE ?
    MTLStoreActionDontCare : MTLStoreActionStore;
}

static MTLStoreAction
mt_resolveStoreAction(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE
           ? MTLStoreActionStoreAndMultisampleResolve
           : MTLStoreActionMultisampleResolve;
}

static uint64_t
mt_stageMask(GPUPipelineStageMask stages) {
  uint64_t result;

  result = 0;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    if ((stages & GPU_STAGE_VERTEX) != 0u) {
      result |= MTLStageVertex;
    }
    if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
      result |= MTLStageFragment;
    }
    if ((stages & GPU_STAGE_COMPUTE) != 0u) {
      result |= MTLStageDispatch;
    }
    if ((stages & GPU_STAGE_TRANSFER) != 0u) {
      result |= MTLStageBlit;
    }
    if ((stages & (GPU_STAGE_TOP | GPU_STAGE_BOTTOM)) != 0u) {
      result |= MTLStageAll;
    }
  }
  return result;
}

static void
mt_resetRenderPass(MTRenderPass *pass) {
  MTLRenderPassDescriptor *classic;

  if (!pass || !pass->classic) {
    return;
  }

  classic = pass->classic;
  classic.visibilityResultBuffer = nil;
  for (uint32_t i = 0; i < GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS; i++) {
    classic.colorAttachments[i].texture = nil;
    classic.colorAttachments[i].resolveTexture = nil;
    classic.colorAttachments[i].loadAction = MTLLoadActionDontCare;
    classic.colorAttachments[i].storeAction = MTLStoreActionDontCare;
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4RenderPassDescriptor *modern = pass->modern;

      modern.colorAttachments[i].texture = nil;
      modern.colorAttachments[i].resolveTexture = nil;
      modern.colorAttachments[i].loadAction = MTLLoadActionDontCare;
      modern.colorAttachments[i].storeAction = MTLStoreActionDontCare;
    }
  }

  classic.depthAttachment.texture = nil;
  classic.stencilAttachment.texture = nil;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTL4RenderPassDescriptor *modern = pass->modern;

    modern.depthAttachment.texture = nil;
    modern.stencilAttachment.texture = nil;
    modern.visibilityResultBuffer = nil;
  }
}

GPU_HIDE
GPURenderPassDesc*
mt_beginRenderPass(GPUCommandBuffer              *cmdb,
                   const GPURenderPassCreateInfo *info) {
  const GPURenderPassColorAttachment        *color;
  const GPURenderPassDepthStencilAttachment *depthStencil;
  MTCommandBuffer                           *commandState;
  GPURenderPassDesc                         *renderPass;
  MTRenderPass                              *nativePass;
  MTLRenderPassDescriptor                   *rpd;
  MTQuerySet                                *occlusion;
  id                                         rpd4;
  uint32_t                                   i;

  if (!cmdb || !info ||
      (info->colorAttachmentCount > 0 && !info->pColorAttachments))
    return NULL;

  commandState = mt_commandBuffer(cmdb);
  if (!commandState) {
    return NULL;
  }

  renderPass = &commandState->renderPass;
  nativePass = &commandState->renderPassState;
  if (!nativePass->classic) {
    nativePass->classic = [MTLRenderPassDescriptor new];
  }
  if (commandState->mode == MTCommandMode4 && !nativePass->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      nativePass->modern = [MTL4RenderPassDescriptor new];
    }
  }
  if (!nativePass->classic ||
      (commandState->mode == MTCommandMode4 && !nativePass->modern)) {
    return NULL;
  }

  rpd = nativePass->classic;
  rpd4 = nativePass->modern;
  mt_resetRenderPass(nativePass);
  occlusion = info->occlusionQuerySet ? info->occlusionQuerySet->_priv : NULL;
  if (info->occlusionQuerySet && (!occlusion || !occlusion->visibility)) {
    return NULL;
  }
  rpd.visibilityResultBuffer = occlusion ? occlusion->visibility : nil;
  if (rpd4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4RenderPassDescriptor *modern = rpd4;

      modern.visibilityResultBuffer = rpd.visibilityResultBuffer;
      modern.visibilityResultType   = MTLVisibilityResultTypeReset;
    }
  }

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];
    if (!color->view)
      return NULL;

    rpd.colorAttachments[i].texture        = (id<MTLTexture>)color->view->_priv;
    rpd.colorAttachments[i].resolveTexture = color->resolveView ?
      (id<MTLTexture>)color->resolveView->_priv : nil;
    rpd.colorAttachments[i].loadAction = mt_loadAction(color->loadOp);
    rpd.colorAttachments[i].storeAction = color->resolveView
                                            ? mt_resolveStoreAction(color->storeOp)
                                            : mt_storeAction(color->storeOp);
    rpd.colorAttachments[i].clearColor = MTLClearColorMake(color->clearColor.float32[0],
                                                           color->clearColor.float32[1],
                                                           color->clearColor.float32[2],
                                                           color->clearColor.float32[3]);
    if (rpd4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        MTL4RenderPassDescriptor *modern = rpd4;

        modern.colorAttachments[i].texture = rpd.colorAttachments[i].texture;
        modern.colorAttachments[i].resolveTexture = rpd.colorAttachments[i].resolveTexture;
        modern.colorAttachments[i].loadAction = rpd.colorAttachments[i].loadAction;
        modern.colorAttachments[i].storeAction = rpd.colorAttachments[i].storeAction;
        modern.colorAttachments[i].clearColor = rpd.colorAttachments[i].clearColor;
      }
    }
  }

  depthStencil = info->pDepthStencilAttachment;
  if (depthStencil && depthStencil->view) {
    GPUFormat format;

    format = depthStencil->view->format;
    if (format != GPU_FORMAT_STENCIL8) {
      rpd.depthAttachment.texture =
        (id<MTLTexture>)depthStencil->view->_priv;
      rpd.depthAttachment.loadAction =
        mt_loadAction(depthStencil->depthLoadOp);
      rpd.depthAttachment.storeAction =
        mt_storeAction(depthStencil->depthStoreOp);
      rpd.depthAttachment.clearDepth = depthStencil->clearDepth;
    }
    if (format == GPU_FORMAT_STENCIL8 ||
        format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
        format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
      rpd.stencilAttachment.texture =
        (id<MTLTexture>)depthStencil->view->_priv;
      rpd.stencilAttachment.loadAction =
        mt_loadAction(depthStencil->stencilLoadOp);
      rpd.stencilAttachment.storeAction =
        mt_storeAction(depthStencil->stencilStoreOp);
      rpd.stencilAttachment.clearStencil = depthStencil->clearStencil;
    }
    if (rpd4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        MTL4RenderPassDescriptor *modern = rpd4;

        modern.depthAttachment = rpd.depthAttachment;
        modern.stencilAttachment = rpd.stencilAttachment;
      }
    }
  }

  memset(renderPass, 0, sizeof(*renderPass));
  renderPass->_priv = nativePass;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  renderPass->label = info->label;
#endif

  return renderPass;
}

GPU_HIDE
void
mt_destroyRenderPass(GPURenderPassDesc *pass) {
  (void)pass;
}

static MTCopyEncoder *
mt_copyEncoder(GPUCopyPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static id<MTLBlitCommandEncoder>
mt_nativeCopyEncoder(GPUCopyPassEncoder *pass) {
  MTCopyEncoder *native;

  native = mt_copyEncoder(pass);
  return native ? native->classic : nil;
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
mt_bufferTextureRangeValid(id<MTLBuffer>                     buffer,
                           const GPUTexture                 *texture,
                           const GPUBufferTextureCopyRegion *region,
                           uint64_t                         *outBytesPerImage) {
  GPUFormatDataLayout layout;

  if (!buffer || !texture || !region || !outBytesPerImage ||
      !gpuFormatDataLayout(texture->format,
                           region->texture.width,
                           region->texture.height,
                           region->texture.depth,
                           region->texture.layerCount,
                           region->bytesPerRow,
                           region->rowsPerImage,
                           &layout)) {
    return false;
  }
  if (!mt_bufferCopyRangeValid(buffer,
                               region->bufferOffset,
                               layout.requiredBytes)) {
    return false;
  }

  *outBytesPerImage = layout.bytesPerImage;
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
  MTCommandBuffer    *commandState;
  MTCopyEncoder      *native;
  GPUCopyPassEncoder *pass;

  if (!cmdb) {
    return NULL;
  }

  commandState = mt_commandBuffer(cmdb);
  if (!commandState) {
    return NULL;
  }
  pass = &commandState->copyEncoder;
  native = &commandState->copyState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

  if (commandState && commandState->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      native->modern = [commandState->modern computeCommandEncoder];
      mt_applyPendingBarrier(cmdb, native->modern);
    }
  } else {
    native->classic = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
  }
  if (!native->classic && !native->modern) {
    return NULL;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    NSString *nativeLabel = [NSString stringWithUTF8String:label];

    native->classic.label = nativeLabel;
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4ComputeCommandEncoder>)native->modern setLabel:nativeLabel];
    }
  }
#else
  GPU__UNUSED(label);
#endif

  pass->_priv = native;
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

  if (mt_copyEncoder(pass)->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(pass->_cmdb, srcBuffer);
      mt_useAllocation(pass->_cmdb, dstBuffer);
      [mt_copyEncoder(pass)->modern copyFromBuffer:srcBuffer
                                      sourceOffset:(NSUInteger)region->srcOffset
                                          toBuffer:dstBuffer
                                 destinationOffset:(NSUInteger)region->dstOffset
                                              size:(NSUInteger)region->sizeBytes];
    }
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
      !mt_bufferTextureRangeValid(srcBuffer,
                                  dst,
                                  region,
                                  &bytesPerImage)) {
    return;
  }

  texRegion = &region->texture;
  dstTexture = (id<MTLTexture>)dst->_priv;
  texture3D = dst->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (mt_copyEncoder(pass)->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(pass->_cmdb, srcBuffer);
      mt_useAllocation(pass->_cmdb, dstTexture);
      if (texture3D) {
        [mt_copyEncoder(pass)->modern copyFromBuffer:srcBuffer
                                        sourceOffset:(NSUInteger)region->bufferOffset
                                   sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                                 sourceBytesPerImage:(NSUInteger)bytesPerImage
                                          sourceSize:mt_textureCopySize(texRegion, true)
                                           toTexture:dstTexture
                                    destinationSlice:0
                                    destinationLevel:texRegion->texture.mipLevel
                                   destinationOrigin:mt_textureOrigin(&texRegion->texture, true)];
      } else {
        for (uint32_t i = 0; i < texRegion->layerCount; i++) {
          [mt_copyEncoder(pass)->modern copyFromBuffer:srcBuffer
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
    }
    return;
  }
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
      !mt_bufferTextureRangeValid(dstBuffer,
                                  src,
                                  region,
                                  &bytesPerImage)) {
    return;
  }

  texRegion = &region->texture;
  srcTexture = (id<MTLTexture>)src->_priv;
  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (mt_copyEncoder(pass)->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(pass->_cmdb, srcTexture);
      mt_useAllocation(pass->_cmdb, dstBuffer);
      if (texture3D) {
        [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
                                          sourceSlice:0
                                          sourceLevel:texRegion->texture.mipLevel
                                         sourceOrigin:mt_textureOrigin(&texRegion->texture, true)
                                           sourceSize:mt_textureCopySize(texRegion, true)
                                             toBuffer:dstBuffer
                                    destinationOffset:(NSUInteger)region->bufferOffset
                               destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                             destinationBytesPerImage:(NSUInteger)bytesPerImage];
      } else {
        for (uint32_t i = 0; i < texRegion->layerCount; i++) {
          [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
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
    }
    return;
  }
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
  if (mt_copyEncoder(pass)->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(pass->_cmdb, srcTexture);
      mt_useAllocation(pass->_cmdb, dstTexture);
      if (texture3D) {
        [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
                                          sourceSlice:0
                                          sourceLevel:region->src.mipLevel
                                         sourceOrigin:mt_textureOrigin(&region->src, true)
                                           sourceSize:size
                                            toTexture:dstTexture
                                     destinationSlice:0
                                     destinationLevel:region->dst.mipLevel
                                    destinationOrigin:mt_textureOrigin(&region->dst, true)];
      } else {
        for (uint32_t i = 0; i < region->layerCount; i++) {
          [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
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
    }
    return;
  }
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
  MTCopyEncoder *native;

  if (!pass) {
    return;
  }

  native = mt_copyEncoder(pass);
  if (native && native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern endEncoding];
    }
  } else {
    [native->classic endEncoding];
  }
  native->classic = nil;
  native->modern = nil;
}

GPU_HIDE
void
mt_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  MTCommandBuffer *native;

  if (!cmdb || !barriers || !mt_commandBufferIsModern(cmdb)) {
    return;
  }

  native = mt_commandBuffer(cmdb);
  native->pendingAfterStages |= mt_stageMask(barriers->srcStages);
  native->pendingBeforeStages |= mt_stageMask(barriers->dstStages);
  if (@available(macOS 26.0, iOS 26.0, *)) {
    native->pendingVisibility |= MTL4VisibilityOptionDevice;
  }

  for (uint32_t i = 0; i < barriers->bufferBarrierCount; i++) {
    mt_useAllocation(cmdb, (id<MTLBuffer>)barriers->pBufferBarriers[i].buffer->_priv);
  }
  for (uint32_t i = 0; i < barriers->textureBarrierCount; i++) {
    mt_useAllocation(cmdb, (id<MTLTexture>)barriers->pTextureBarriers[i].texture->_priv);
  }
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
