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
#include "../impl.h"
#include "../../../api/vrs_internal.h"
#include "../../../api/pass/blit_internal.h"

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
  return op == GPU_STORE_OP_DONT_CARE ? MTLStoreActionDontCare : MTLStoreActionStore;
}

static MTLStoreAction
mt_resolveStoreAction(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionMultisampleResolve;
}

static MTLClearColor
mt_clearColor(const GPUClearColorValue *color, GPUFormat format) {
  switch (gpuFormatNumericType(format)) {
    case GPU_FORMAT_NUMERIC_UINT:
      return MTLClearColorMake((double)color->uint32[0],
                               (double)color->uint32[1],
                               (double)color->uint32[2],
                               (double)color->uint32[3]);
    case GPU_FORMAT_NUMERIC_SINT:
      return MTLClearColorMake((double)color->sint32[0],
                               (double)color->sint32[1],
                               (double)color->sint32[2],
                               (double)color->sint32[3]);
    default:
      return MTLClearColorMake(color->float32[0],
                               color->float32[1],
                               color->float32[2],
                               color->float32[3]);
  }
}

#if MT_HAS_COMMAND_BARRIERS

static uint64_t
mt_stageMask(GPUPipelineStageMask stages) {
  uint64_t result;

  result = 0;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if ((stages & GPU_STAGE_VERTEX) != 0u) {
      result |= MTLStageVertex | MTLStageObject | MTLStageMesh;
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

#endif

static void
mt_prepareRenderPass(MTRenderPass *pass, uint32_t colorAttachmentCount) {
  MTLRenderPassDescriptor *classic;
  MTRenderPassColorState  *state;
  uint32_t                 previousColorAttachmentCount;
  uint32_t                 classicIndex;
#if MT_HAS_METAL4
  uint32_t                 modernIndex;
#endif

  if (!pass || !pass->classic) {
    return;
  }

  classic                      = pass->classic;
  previousColorAttachmentCount = pass->colorAttachmentCount;
  pass->width                  = 0u;
  pass->height                 = 0u;
  pass->colorAttachmentCount   = colorAttachmentCount;

  for (classicIndex = colorAttachmentCount;
       classicIndex < previousColorAttachmentCount;
       classicIndex++) {
    state = &pass->colorAttachments[classicIndex];

    classic.colorAttachments[classicIndex].texture        = nil;
    classic.colorAttachments[classicIndex].resolveTexture = nil;
    classic.colorAttachments[classicIndex].loadAction     = MTLLoadActionDontCare;
    classic.colorAttachments[classicIndex].storeAction    = MTLStoreActionDontCare;
    memset(state, 0, sizeof(*state));
  }

#if MT_HAS_METAL4
  if (pass->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4RenderPassDescriptor *modern = pass->modern;

      for (modernIndex = colorAttachmentCount;
           modernIndex < previousColorAttachmentCount;
           modernIndex++) {
        modern.colorAttachments[modernIndex].texture        = nil;
        modern.colorAttachments[modernIndex].resolveTexture = nil;
        modern.colorAttachments[modernIndex].loadAction     = MTLLoadActionDontCare;
        modern.colorAttachments[modernIndex].storeAction    = MTLStoreActionDontCare;
      }
    }
  }
#endif
}

static MTCopyEncoder*
mt_copyEncoder(GPUTransferPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static id<MTLBlitCommandEncoder>
mt_nativeCopyEncoder(GPUTransferPassEncoder *pass) {
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

  if (!buffer || !texture || !region || !outBytesPerImage
      || !gpuFormatAspectDataLayout(texture->format,
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

static void
mt_copyDepthStencilPlane(GPUTransferPassEncoder              *pass,
                         GPUTexture                          *src,
                         GPUTexture                          *dst,
                         const GPUTextureToTextureCopyRegion *region) {
  GPUFormatLayout layout;
  MTLSize         size;
  id<MTLTexture>  srcTexture;
  id<MTLTexture>  dstTexture;
  id<MTLBuffer>   scratch;
  MTLBlitOption   option;
  uint64_t        scratchOffset;
  uint64_t        rowBytes;
  uint64_t        rowPitch;
  uint64_t        bytesPerImage;
  uint64_t        scratchBytes;
#if MT_HAS_METAL4
  uint32_t        modernReadLayer;
  uint32_t        modernWriteLayer;
#endif
  uint32_t        classicReadLayer;
  uint32_t        classicWriteLayer;

  srcTexture = mt_nativeTexture(src);
  dstTexture = mt_nativeTexture(dst);
  option     = mt_copyOption(src->format, region->src.aspect);

  if (!srcTexture || !dstTexture || option == MTLBlitOptionNone
      || !gpuFormatAspectLayout(src->format, region->src.aspect, &layout)
      || layout.blockWidth != 1u || layout.blockHeight != 1u
      || region->width > UINT64_MAX / layout.bytesPerBlock) {
    return;
  }

  rowBytes = (uint64_t)region->width * layout.bytesPerBlock;

  if (rowBytes > UINT64_MAX - 255u) {
    return;
  }

  rowPitch = (rowBytes + 255u) & ~UINT64_C(255);

  if (region->height > UINT64_MAX / rowPitch) {
    return;
  }

  bytesPerImage = rowPitch * region->height;

  if (region->layerCount > UINT64_MAX / bytesPerImage) {
    return;
  }

  scratchBytes = bytesPerImage * region->layerCount;

  if (!mt_reserveUpload(pass->_cmdb,
                        scratchBytes,
                        256u,
                        &scratch,
                        &scratchOffset)) {
    return;
  }

  size = MTLSizeMake(region->width, region->height, 1u);
#if MT_HAS_METAL4
  if (mt_copyEncoder(pass)->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      id<MTL4ComputeCommandEncoder> modern;

      modern = mt_copyEncoder(pass)->modern;
      mt_useAllocation(pass->_cmdb, srcTexture);
      mt_useAllocation(pass->_cmdb, dstTexture);

      for (modernReadLayer = 0u; modernReadLayer < region->layerCount; modernReadLayer++) {
        [modern copyFromTexture:srcTexture
                    sourceSlice:region->src.baseArrayLayer + modernReadLayer
                    sourceLevel:region->src.mipLevel
                   sourceOrigin:MTLOriginMake(0u, 0u, 0u)
                     sourceSize:size
                       toBuffer:scratch
              destinationOffset:(NSUInteger)(scratchOffset +
                                               (uint64_t)modernReadLayer * bytesPerImage)
         destinationBytesPerRow:(NSUInteger)rowPitch
       destinationBytesPerImage:(NSUInteger)bytesPerImage
                        options:option];
      }

      [modern barrierAfterEncoderStages:MTLStageBlit
                    beforeEncoderStages:MTLStageBlit
                      visibilityOptions:MTL4VisibilityOptionDevice];

      for (modernWriteLayer = 0u; modernWriteLayer < region->layerCount; modernWriteLayer++) {
        [modern copyFromBuffer:scratch
                  sourceOffset:(NSUInteger)(scratchOffset +
                                             (uint64_t)modernWriteLayer * bytesPerImage)
             sourceBytesPerRow:(NSUInteger)rowPitch
           sourceBytesPerImage:(NSUInteger)bytesPerImage
                    sourceSize:size
                     toTexture:dstTexture
              destinationSlice:region->dst.baseArrayLayer + modernWriteLayer
              destinationLevel:region->dst.mipLevel
             destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                       options:option];
      }
    }

    return;
  }
#endif

  for (classicReadLayer = 0u; classicReadLayer < region->layerCount; classicReadLayer++) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:region->src.baseArrayLayer + classicReadLayer
                                    sourceLevel:region->src.mipLevel
                                   sourceOrigin:MTLOriginMake(0u, 0u, 0u)
                                     sourceSize:size
                                       toBuffer:scratch
                              destinationOffset:(NSUInteger)(scratchOffset +
                                                              (uint64_t)classicReadLayer * bytesPerImage)
                         destinationBytesPerRow:(NSUInteger)rowPitch
                       destinationBytesPerImage:(NSUInteger)bytesPerImage
                                        options:option];
  }

  for (classicWriteLayer = 0u; classicWriteLayer < region->layerCount; classicWriteLayer++) {
    [mt_nativeCopyEncoder(pass) copyFromBuffer:scratch
                                  sourceOffset:(NSUInteger)(scratchOffset +
                                                            (uint64_t)classicWriteLayer * bytesPerImage)
                             sourceBytesPerRow:(NSUInteger)rowPitch
                           sourceBytesPerImage:(NSUInteger)bytesPerImage
                                    sourceSize:size
                                     toTexture:dstTexture
                              destinationSlice:region->dst.baseArrayLayer + classicWriteLayer
                              destinationLevel:region->dst.mipLevel
                             destinationOrigin:MTLOriginMake(0u, 0u, 0u)
                                       options:option];
  }
}

static void
mt_generateMipmaps(GPUCommandBuffer *cmdb, GPUTexture *texture) {
  GPUTransferPassEncoder *pass;
  MTCopyEncoder          *native;
  GPUAdapterMT           *adapter;
  id<MTLTexture>          nativeTexture;
#if MT_HAS_METAL4
  MTCommandBuffer        *command;
#endif

#if MT_HAS_METAL4
  command = mt_commandBuffer(cmdb);

  if (command && command->mode == MTCommandMode4) {
    gpuGenerateMipmapsFallback(cmdb, texture, mt_blitTexture);
    return;
  }
#endif

  adapter = texture && texture->device && texture->device->adapter ? texture->device->adapter->_priv : NULL;

  if (texture && texture->depthOrLayers > 1u && adapter
      && !adapter->appleFamily1) {
    gpuGenerateMipmapsFallback(cmdb, texture, mt_blitTexture);
    return;
  }

  pass = GPUBeginTransferPass(cmdb, "gpu-generate-mipmaps");

  if (!pass) {
    return;
  }

  native        = mt_copyEncoder(pass);
  nativeTexture = mt_nativeTexture(texture);

  if (!native || !nativeTexture) {
    GPUEndTransferPass(pass);
    return;
  }

#if MT_HAS_METAL4
  if (native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      mt_useAllocation(cmdb, nativeTexture);
      [(id<MTL4ComputeCommandEncoder>)native->modern generateMipmapsForTexture:nativeTexture];
    }
  } else
#endif
  {
    [native->classic generateMipmapsForTexture:nativeTexture];
  }

  GPUEndTransferPass(pass);
}

GPU_HIDE
GPURenderPassDesc*
mt_beginRenderPass(GPUCommandBuffer              *cmdb,
                   const GPURenderPassCreateInfo *info) {
  const GPUShadingRateAttachmentEXT          *shadingRate;
  const GPURasterizationRateMapRenderPassEXT *rateMap;
  const GPURenderPassColorAttachment         *color;
  const GPURenderPassDepthStencilAttachment  *depthStencil;
  MTCommandBuffer                            *commandState;
  GPURenderPassDesc                          *renderPass;
  MTRenderPass                               *nativePass;
  MTLRenderPassDescriptor                    *rpd;
  MTQuerySet                                 *occlusion;
  id                                          visibilityResultBuffer;
  id                                          rasterizationRateMap;
#if MT_HAS_METAL4
  id                                          rpd4;
#endif
  MTRenderPassColorState                     *state;
  id<MTLTexture>                              colorTexture;
  id<MTLTexture>                              resolveTexture;
  id<MTLTexture>                              depthTexture;
  MTLLoadAction                               loadAction;
  MTLStoreAction                              storeAction;
  uint32_t                                    i;
  GPUFormat                                   format;
  bool                                        depthAttachmentActive;
  bool                                        rasterizationRateMapChanged;
  bool                                        stencilAttachmentActive;
  bool                                        visibilityResultBufferChanged;
  bool                                        clearColorChanged;
  bool                                        loadActionChanged;
  bool                                        resolveTextureChanged;
  bool                                        storeActionChanged;
  bool                                        textureChanged;

  if (!cmdb || !info
      || info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS
      || (info->colorAttachmentCount > 0 && !info->pColorAttachments))
    return NULL;

  if (!gpuRenderPassVRSExtensions(info, &shadingRate, &rateMap)
      || shadingRate) {
    return NULL;
  }

  if (rateMap
      && (!rateMap->map
          || rateMap->map->device != gpuCommandBufferDevice(cmdb)
          || !rateMap->map->_priv)) {
    return NULL;
  }

  commandState = mt_commandBuffer(cmdb);

  if (!commandState) {
    return NULL;
  }

  renderPass = &commandState->renderPass;
  nativePass = &commandState->renderPassState;

  if (!nativePass->classic) {
    nativePass->classic = [MTLRenderPassDescriptor new];
  }
#if MT_HAS_METAL4
  if (commandState->mode == MTCommandMode4 && !nativePass->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      nativePass->modern = [MTL4RenderPassDescriptor new];
    }
  }
#endif
  if (!nativePass->classic) {
    return NULL;
  }
#if MT_HAS_METAL4
  if (commandState->mode == MTCommandMode4 && !nativePass->modern) {
    return NULL;
  }
#endif

  rpd = nativePass->classic;
#if MT_HAS_METAL4
  rpd4 = nativePass->modern;
#endif
  mt_prepareRenderPass(nativePass, info->colorAttachmentCount);

  occlusion = info->occlusionQuerySet ? info->occlusionQuerySet->_priv : NULL;

  if (info->occlusionQuerySet && (!occlusion || !occlusion->visibility)) {
    return NULL;
  }

  visibilityResultBuffer        = occlusion ? occlusion->visibility : nil;
  rasterizationRateMap          = rateMap ? (id<MTLRasterizationRateMap>)rateMap->map->_priv : nil;
  visibilityResultBufferChanged = nativePass->visibilityResultBuffer != visibilityResultBuffer;
  rasterizationRateMapChanged   = nativePass->rasterizationRateMap != rasterizationRateMap;

  if (visibilityResultBufferChanged) {
    rpd.visibilityResultBuffer = visibilityResultBuffer;
  }

  if (rasterizationRateMapChanged) {
    rpd.rasterizationRateMap = rasterizationRateMap;
  }
#if MT_HAS_METAL4
  if (rpd4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4RenderPassDescriptor *visibilityDescriptor = rpd4;

      if (visibilityResultBufferChanged) {
        visibilityDescriptor.visibilityResultBuffer = visibilityResultBuffer;
        visibilityDescriptor.visibilityResultType   = MTLVisibilityResultTypeReset;
      }

      if (rasterizationRateMapChanged) {
        visibilityDescriptor.rasterizationRateMap = rasterizationRateMap;
      }

      mt_useAllocation(cmdb, visibilityResultBuffer);
    }
  }
#endif
  nativePass->visibilityResultBuffer = visibilityResultBuffer;
  nativePass->rasterizationRateMap   = rasterizationRateMap;

  for (i = 0; i < info->colorAttachmentCount; i++) {
    color = &info->pColorAttachments[i];

    if (!color->view)
      return NULL;

    state                 = &nativePass->colorAttachments[i];
    colorTexture          = (id<MTLTexture>)color->view->_priv;
    resolveTexture        = color->resolveView ? (id<MTLTexture>)color->resolveView->_priv : nil;
    loadAction            = mt_loadAction(color->loadOp);
    storeAction           = color->resolveView ? mt_resolveStoreAction(color->storeOp) : mt_storeAction(color->storeOp);
    textureChanged        = !state->valid || state->texture != colorTexture;
    resolveTextureChanged = !state->valid
                            || state->resolveTexture != resolveTexture;
    loadActionChanged     = !state->valid
                            || state->loadAction != loadAction;
    storeActionChanged    = !state->valid
                            || state->storeAction != storeAction;
    clearColorChanged     = loadAction == MTLLoadActionClear
                            && (!state->valid
                                || loadActionChanged
                                || state->format != color->view->format
                                || memcmp(&state->clearColor,
                                          &color->clearColor,
                                          sizeof(state->clearColor)) != 0);

    if (textureChanged) {
      rpd.colorAttachments[i].texture = colorTexture;
    }

    if (resolveTextureChanged) {
      rpd.colorAttachments[i].resolveTexture = resolveTexture;
    }

    if (loadActionChanged) {
      rpd.colorAttachments[i].loadAction = loadAction;
    }

    if (storeActionChanged) {
      rpd.colorAttachments[i].storeAction = storeAction;
    }

    if (clearColorChanged) {
      rpd.colorAttachments[i].clearColor = mt_clearColor(&color->clearColor, color->view->format);
    }

    if (nativePass->width == 0u) {
      nativePass->width  = (uint32_t)colorTexture.width;
      nativePass->height = (uint32_t)colorTexture.height;
    }
#if MT_HAS_METAL4
    if (rpd4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        MTL4RenderPassDescriptor *colorDescriptor = rpd4;

        if (textureChanged) {
          colorDescriptor.colorAttachments[i].texture = colorTexture;
        }

        if (resolveTextureChanged) {
          colorDescriptor.colorAttachments[i].resolveTexture = resolveTexture;
        }

        if (loadActionChanged) {
          colorDescriptor.colorAttachments[i].loadAction = loadAction;
        }

        if (storeActionChanged) {
          colorDescriptor.colorAttachments[i].storeAction = storeAction;
        }

        if (clearColorChanged) {
          colorDescriptor.colorAttachments[i].clearColor = rpd.colorAttachments[i].clearColor;
        }

        mt_useAllocation(cmdb, mt_nativeTexture(color->view->_texture));

        if (color->resolveView) {
          mt_useAllocation(cmdb,
                           mt_nativeTexture(color->resolveView->_texture));
        }
      }
    }
#endif
    state->texture        = colorTexture;
    state->resolveTexture = resolveTexture;
    state->loadAction     = loadAction;
    state->storeAction    = storeAction;
    state->clearColor     = color->clearColor;
    state->format         = color->view->format;
    state->valid          = true;
  }

  depthStencil            = info->pDepthStencilAttachment;
  depthAttachmentActive   = depthStencil && depthStencil->view
                            && depthStencil->view->format != GPU_FORMAT_STENCIL8;
  stencilAttachmentActive = depthStencil && depthStencil->view
                            && (depthStencil->view->format == GPU_FORMAT_STENCIL8
                                || depthStencil->view->format ==
                               GPU_FORMAT_DEPTH24_UNORM_STENCIL8
                                || depthStencil->view->format ==
                               GPU_FORMAT_DEPTH32_FLOAT_STENCIL8);

  if (!depthAttachmentActive && nativePass->depthAttachmentActive) {
    rpd.depthAttachment.texture = nil;
  }

  if (!stencilAttachmentActive && nativePass->stencilAttachmentActive) {
    rpd.stencilAttachment.texture = nil;
  }
#if MT_HAS_METAL4
  if (rpd4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4RenderPassDescriptor *clearDescriptor = rpd4;

      if (!depthAttachmentActive && nativePass->depthAttachmentActive) {
        clearDescriptor.depthAttachment.texture = nil;
      }

      if (!stencilAttachmentActive && nativePass->stencilAttachmentActive) {
        clearDescriptor.stencilAttachment.texture = nil;
      }
    }
  }
#endif
  if (depthStencil && depthStencil->view) {
    format = depthStencil->view->format;

    if (nativePass->width == 0u) {
      depthTexture = (id<MTLTexture>)depthStencil->view->_priv;

      nativePass->width  = (uint32_t)depthTexture.width;
      nativePass->height = (uint32_t)depthTexture.height;
    }

    if (format != GPU_FORMAT_STENCIL8) {
      rpd.depthAttachment.texture     = (id<MTLTexture>)depthStencil->view->_priv;
      rpd.depthAttachment.loadAction  = mt_loadAction(depthStencil->depthLoadOp);
      rpd.depthAttachment.storeAction = mt_storeAction(depthStencil->depthStoreOp);
      rpd.depthAttachment.clearDepth  = depthStencil->clearDepth;
    }

    if (format == GPU_FORMAT_STENCIL8
        || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
        || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8) {
      rpd.stencilAttachment.texture      = (id<MTLTexture>)depthStencil->view->_priv;
      rpd.stencilAttachment.loadAction   = mt_loadAction(depthStencil->stencilLoadOp);
      rpd.stencilAttachment.storeAction  = mt_storeAction(depthStencil->stencilStoreOp);
      rpd.stencilAttachment.clearStencil = depthStencil->clearStencil;
    }
#if MT_HAS_METAL4
    if (rpd4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        MTL4RenderPassDescriptor *depthDescriptor = rpd4;

        if (depthAttachmentActive) {
          depthDescriptor.depthAttachment = rpd.depthAttachment;
        }

        if (stencilAttachmentActive) {
          depthDescriptor.stencilAttachment = rpd.stencilAttachment;
        }

        mt_useAllocation(cmdb,
                         mt_nativeTexture(depthStencil->view->_texture));
      }
    }
#endif
  }

  nativePass->depthAttachmentActive   = depthAttachmentActive;
  nativePass->stencilAttachmentActive = stencilAttachmentActive;

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

GPU_HIDE
GPUTransferPassEncoder*
mt_beginTransferPass(GPUCommandBuffer *cmdb, const char *label) {
  MTCommandBuffer        *commandState;
  MTCopyEncoder          *native;
  GPUTransferPassEncoder *pass;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  NSString               *nativeLabel;
#endif

  if (!cmdb) {
    return NULL;
  }

  commandState = mt_commandBuffer(cmdb);

  if (!commandState) {
    return NULL;
  }

  pass   = &commandState->copyEncoder;
  native = &commandState->copyState;
  memset(pass, 0, sizeof(*pass));
  memset(native, 0, sizeof(*native));

#if MT_HAS_METAL4
  if (commandState->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      native->modern = [commandState->modern computeCommandEncoder];
      mt_applyPendingBarrier(cmdb, native->modern);
    }
  } else
#endif
  {
    @autoreleasepool {
      native->classic = [[mt_classicCommandBuffer(cmdb) blitCommandEncoder] retain];
    }

    mt_applyPendingBarrier(cmdb, native->classic);
  }

  if (!native->classic && !native->modern) {
    return NULL;
  }
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    nativeLabel = [NSString stringWithUTF8String:label];

    native->classic.label = nativeLabel;
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [(id<MTL4ComputeCommandEncoder>)native->modern setLabel:nativeLabel];
    }
#endif
  }
#else
  GPU__UNUSED(label);
#endif

  pass->_priv = native;

  return pass;
}

GPU_HIDE
void
mt_copyBufferToBuffer(GPUTransferPassEncoder    *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region) {
  id<MTLBuffer> srcBuffer;
  id<MTLBuffer> dstBuffer;

  srcBuffer = mt_nativeBuffer(src);
  dstBuffer = mt_nativeBuffer(dst);

  if (!pass
      || !region
      || !mt_bufferCopyRangeValid(srcBuffer, region->srcOffset, region->sizeBytes)
      || !mt_bufferCopyRangeValid(dstBuffer, region->dstOffset, region->sizeBytes)) {
    return;
  }

#if MT_HAS_METAL4
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
#endif
  [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                sourceOffset:(NSUInteger)region->srcOffset
                                    toBuffer:dstBuffer
                           destinationOffset:(NSUInteger)region->dstOffset
                                        size:(NSUInteger)region->sizeBytes];
}

GPU_HIDE
void
mt_copyBufferToTexture(GPUTransferPassEncoder           *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  const GPUTextureSubresourceRegion *texRegion;
  id<MTLBuffer>                      srcBuffer;
  id<MTLTexture>                     dstTexture;
  MTLBlitOption                      option;
  uint64_t                           bytesPerImage;
#if MT_HAS_METAL4
  uint32_t                           modernLayer;
#endif
  uint32_t                           classicLayer;
  bool                               texture3D;

  srcBuffer = mt_nativeBuffer(src);

  if (!pass
      || !dst
      || !dst->_priv
      || !mt_bufferTextureRangeValid(srcBuffer,
                                     dst,
                                     region,
                                     &bytesPerImage)) {
    return;
  }

  texRegion  = &region->texture;
  option     = mt_copyOption(dst->format, texRegion->texture.aspect);
  dstTexture = option == MTLBlitOptionNone ? mt_copyTexture(dst, texRegion->texture.aspect) : mt_nativeTexture(dst);

  if (!dstTexture) {
    return;
  }

  texture3D = dst->dimension == GPU_TEXTURE_DIMENSION_3D;
#if MT_HAS_METAL4
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
                                   destinationOrigin:mt_textureOrigin(&texRegion->texture, true)
                                             options:option];
      } else {
        for (modernLayer = 0; modernLayer < texRegion->layerCount; modernLayer++) {
          [mt_copyEncoder(pass)->modern copyFromBuffer:srcBuffer
                                          sourceOffset:(NSUInteger)(region->bufferOffset +
                                                                    ((uint64_t)modernLayer * bytesPerImage))
                                     sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                                   sourceBytesPerImage:(NSUInteger)bytesPerImage
                                            sourceSize:mt_textureCopySize(texRegion, false)
                                             toTexture:dstTexture
                                      destinationSlice:texRegion->texture.baseArrayLayer + modernLayer
                                      destinationLevel:texRegion->texture.mipLevel
                                     destinationOrigin:mt_textureOrigin(&texRegion->texture, false)
                                               options:option];
        }
      }
    }

    return;
  }
#endif
  if (texture3D) {
    [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                  sourceOffset:(NSUInteger)region->bufferOffset
                             sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                           sourceBytesPerImage:(NSUInteger)bytesPerImage
                                    sourceSize:mt_textureCopySize(texRegion, true)
                                     toTexture:dstTexture
                              destinationSlice:0
                              destinationLevel:texRegion->texture.mipLevel
                             destinationOrigin:mt_textureOrigin(&texRegion->texture, true)
                                       options:option];
    return;
  }

  for (classicLayer = 0; classicLayer < texRegion->layerCount; classicLayer++) {
    [mt_nativeCopyEncoder(pass) copyFromBuffer:srcBuffer
                                  sourceOffset:(NSUInteger)(region->bufferOffset +
                                                            ((uint64_t)classicLayer * bytesPerImage))
                             sourceBytesPerRow:(NSUInteger)region->bytesPerRow
                           sourceBytesPerImage:(NSUInteger)bytesPerImage
                                    sourceSize:mt_textureCopySize(texRegion, false)
                                     toTexture:dstTexture
                              destinationSlice:texRegion->texture.baseArrayLayer + classicLayer
                              destinationLevel:texRegion->texture.mipLevel
                             destinationOrigin:mt_textureOrigin(&texRegion->texture, false)
                                       options:option];
  }
}

GPU_HIDE
void
mt_copyTextureToBuffer(GPUTransferPassEncoder           *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  const GPUTextureSubresourceRegion *texRegion;
  id<MTLTexture>                     srcTexture;
  id<MTLBuffer>                      dstBuffer;
  MTLBlitOption                      option;
  uint64_t                           bytesPerImage;
#if MT_HAS_METAL4
  uint32_t                           modernLayer;
#endif
  uint32_t                           classicLayer;
  bool                               texture3D;

  dstBuffer = mt_nativeBuffer(dst);

  if (!pass
      || !src
      || !src->_priv
      || !mt_bufferTextureRangeValid(dstBuffer,
                                     src,
                                     region,
                                     &bytesPerImage)) {
    return;
  }

  texRegion  = &region->texture;
  option     = mt_copyOption(src->format, texRegion->texture.aspect);
  srcTexture = option == MTLBlitOptionNone ? mt_copyTexture(src, texRegion->texture.aspect) : mt_nativeTexture(src);

  if (!srcTexture) {
    return;
  }

  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
#if MT_HAS_METAL4
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
                             destinationBytesPerImage:(NSUInteger)bytesPerImage
                                              options:option];
      } else {
        for (modernLayer = 0; modernLayer < texRegion->layerCount; modernLayer++) {
          [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
                                            sourceSlice:texRegion->texture.baseArrayLayer + modernLayer
                                            sourceLevel:texRegion->texture.mipLevel
                                           sourceOrigin:mt_textureOrigin(&texRegion->texture, false)
                                             sourceSize:mt_textureCopySize(texRegion, false)
                                               toBuffer:dstBuffer
                                      destinationOffset:(NSUInteger)(region->bufferOffset +
                                                                      ((uint64_t)modernLayer * bytesPerImage))
                                 destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                               destinationBytesPerImage:(NSUInteger)bytesPerImage
                                                options:option];
        }
      }
    }

    return;
  }
#endif
  if (texture3D) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:0
                                    sourceLevel:texRegion->texture.mipLevel
                                   sourceOrigin:mt_textureOrigin(&texRegion->texture, true)
                                     sourceSize:mt_textureCopySize(texRegion, true)
                                       toBuffer:dstBuffer
                              destinationOffset:(NSUInteger)region->bufferOffset
                         destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                       destinationBytesPerImage:(NSUInteger)bytesPerImage
                                        options:option];
    return;
  }

  for (classicLayer = 0; classicLayer < texRegion->layerCount; classicLayer++) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:texRegion->texture.baseArrayLayer + classicLayer
                                    sourceLevel:texRegion->texture.mipLevel
                                   sourceOrigin:mt_textureOrigin(&texRegion->texture, false)
                                     sourceSize:mt_textureCopySize(texRegion, false)
                                       toBuffer:dstBuffer
                              destinationOffset:(NSUInteger)(region->bufferOffset +
                                                            ((uint64_t)classicLayer * bytesPerImage))
                         destinationBytesPerRow:(NSUInteger)region->bytesPerRow
                       destinationBytesPerImage:(NSUInteger)bytesPerImage
                                        options:option];
  }
}

GPU_HIDE
void
mt_copyTextureToTexture(GPUTransferPassEncoder              *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  MTLSize        size;
  id<MTLTexture> srcTexture;
  id<MTLTexture> dstTexture;
  MTLBlitOption  option;
#if MT_HAS_METAL4
  uint32_t       modernLayer;
#endif
  uint32_t       classicLayer;
  bool           texture3D;

  if (!pass || !src || !dst || !src->_priv || !dst->_priv || !region) {
    return;
  }

  option = mt_copyOption(src->format, region->src.aspect);

  if (option != MTLBlitOptionNone) {
    mt_copyDepthStencilPlane(pass, src, dst, region);
    return;
  }

  srcTexture = mt_copyTexture(src, region->src.aspect);
  dstTexture = mt_copyTexture(dst, region->dst.aspect);

  if (!srcTexture || !dstTexture) {
    return;
  }

  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
  size      = MTLSizeMake(region->width, region->height, texture3D ? region->depth : 1u);
#if MT_HAS_METAL4
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
        for (modernLayer = 0; modernLayer < region->layerCount; modernLayer++) {
          [mt_copyEncoder(pass)->modern copyFromTexture:srcTexture
                                            sourceSlice:region->src.baseArrayLayer + modernLayer
                                            sourceLevel:region->src.mipLevel
                                           sourceOrigin:mt_textureOrigin(&region->src, false)
                                             sourceSize:size
                                              toTexture:dstTexture
                                       destinationSlice:region->dst.baseArrayLayer + modernLayer
                                       destinationLevel:region->dst.mipLevel
                                      destinationOrigin:mt_textureOrigin(&region->dst, false)];
        }
      }
    }

    return;
  }
#endif
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

  for (classicLayer = 0; classicLayer < region->layerCount; classicLayer++) {
    [mt_nativeCopyEncoder(pass) copyFromTexture:srcTexture
                                    sourceSlice:region->src.baseArrayLayer + classicLayer
                                    sourceLevel:region->src.mipLevel
                                   sourceOrigin:mt_textureOrigin(&region->src, false)
                                     sourceSize:size
                                      toTexture:dstTexture
                               destinationSlice:region->dst.baseArrayLayer + classicLayer
                               destinationLevel:region->dst.mipLevel
                              destinationOrigin:mt_textureOrigin(&region->dst, false)];
  }
}

GPU_HIDE
void
mt_endTransferPass(GPUTransferPassEncoder *pass) {
  MTCopyEncoder *native;

  if (!pass) {
    return;
  }

  native = mt_copyEncoder(pass);
#if MT_HAS_METAL4
  if (native && native->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->modern endEncoding];
    }
  } else
#endif
  {
    [native->classic endEncoding];
    [native->classic release];
  }

  native->classic = nil;
  native->modern  = nil;
}

GPU_HIDE
void
mt_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
#if MT_HAS_COMMAND_BARRIERS
  MTCommandBuffer          *native;
#endif
#if MT_HAS_COMMAND_BARRIERS
#if MT_HAS_METAL4
  const GPUTextureBarrier  *textureBarrier;
  const GPUAliasingBarrier *aliasBarrier;
  uint64_t                  afterStages;
  uint64_t                  beforeStages;
  uint64_t                  visibility;
  uint32_t                  bufferIndex;
  uint32_t                  textureIndex;
  uint32_t                  aliasIndex;
#endif
#endif

#if MT_HAS_COMMAND_BARRIERS
  if (!cmdb || !barriers) {
    return;
  }

  native = mt_commandBuffer(cmdb);

  if (!native) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
#if MT_HAS_METAL4
    if (native->mode == MTCommandMode4) {
      id<MTL4ComputeCommandEncoder> encoder;

      afterStages  = mt_stageMask(barriers->srcStages);
      beforeStages = mt_stageMask(barriers->dstStages);
      visibility   = MTL4VisibilityOptionDevice;

      if (barriers->aliasingBarrierCount > 0u) {
        visibility |= MTL4VisibilityOptionResourceAlias;
      }

      for (bufferIndex = 0; bufferIndex < barriers->bufferBarrierCount; bufferIndex++) {
        mt_useAllocation(cmdb,
                         (id<MTLBuffer>)barriers->pBufferBarriers[bufferIndex].buffer->_priv);
      }

      for (textureIndex = 0; textureIndex < barriers->textureBarrierCount; textureIndex++) {
        textureBarrier = &barriers->pTextureBarriers[textureIndex];

        if ((textureBarrier->srcAccess &
             (GPU_ACCESS_COLOR_WRITE | GPU_ACCESS_DEPTH_WRITE)) != 0u) {
          afterStages |= MTLStageAll;
        }

        mt_useAllocation(cmdb,
                         mt_nativeTexture(textureBarrier->texture));
      }

      for (aliasIndex = 0; aliasIndex < barriers->aliasingBarrierCount; aliasIndex++) {
        aliasBarrier = &barriers->pAliasingBarriers[aliasIndex];

        if (aliasBarrier->beforeBuffer) {
          mt_useAllocation(cmdb,
                           (id<MTLBuffer>)aliasBarrier->beforeBuffer->_priv);
        } else {
          mt_useAllocation(cmdb, mt_nativeTexture(aliasBarrier->beforeTexture));
        }

        if (aliasBarrier->afterBuffer) {
          mt_useAllocation(cmdb,
                           (id<MTLBuffer>)aliasBarrier->afterBuffer->_priv);
        } else {
          mt_useAllocation(cmdb, mt_nativeTexture(aliasBarrier->afterTexture));
        }
      }

      if (!(encoder = [(id<MTL4CommandBuffer>)native->modern computeCommandEncoder])) {
        return;
      }

      mt_applyPendingBarrier(cmdb, encoder);
      [encoder barrierAfterStages:afterStages
                beforeQueueStages:beforeStages
                visibilityOptions:visibility];
      [encoder endEncoding];
      return;
    }
#endif
    native->pendingAfterStages  |= mt_stageMask(barriers->srcStages);
    native->pendingBeforeStages |= mt_stageMask(barriers->dstStages);
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(barriers);
#endif
}

GPU_HIDE
void
mt_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass      = mt_beginRenderPass;
  api->destroyRenderPass    = mt_destroyRenderPass;
  api->beginTransferPass    = mt_beginTransferPass;
  api->copyBufferToBuffer   = mt_copyBufferToBuffer;
  api->copyBufferToTexture  = mt_copyBufferToTexture;
  api->copyTextureToBuffer  = mt_copyTextureToBuffer;
  api->copyTextureToTexture = mt_copyTextureToTexture;
  api->endTransferPass      = mt_endTransferPass;
  api->blitTexture          = mt_blitTexture;
  api->generateMipmaps      = mt_generateMipmaps;
  api->encodeBarriers       = mt_encodeBarriers;
}
