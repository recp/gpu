/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPULoadOp
webgpu_loadOp(GPULoadOp op) {
  switch (op) {
    case GPU_LOAD_OP_CLEAR:
      return WGPULoadOp_Clear;
    case GPU_LOAD_OP_DONT_CARE:
      return WGPULoadOp_Clear;
    case GPU_LOAD_OP_LOAD:
    default:
      return WGPULoadOp_Load;
  }
}

static WGPUStoreOp
webgpu_storeOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_DONT_CARE
           ? WGPUStoreOp_Discard
           : WGPUStoreOp_Store;
}

static bool
webgpu_formatHasDepth(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH16_UNORM ||
         format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
         format == GPU_FORMAT_DEPTH32_FLOAT ||
         format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static bool
webgpu_formatHasStencil(GPUFormat format) {
  return format == GPU_FORMAT_STENCIL8 ||
         format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
         format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static WGPUTextureAspect
webgpu_copyAspect(GPUTextureAspect aspect) {
  static const WGPUTextureAspect aspects[] = {
    [GPU_TEXTURE_ASPECT_ALL]          = WGPUTextureAspect_All,
    [GPU_TEXTURE_ASPECT_DEPTH_ONLY]   = WGPUTextureAspect_DepthOnly,
    [GPU_TEXTURE_ASPECT_STENCIL_ONLY] = WGPUTextureAspect_StencilOnly
  };

  return (uint32_t)aspect < GPU_ARRAY_LEN(aspects)
           ? aspects[aspect]
           : WGPUTextureAspect_Undefined;
}

static GPUCommandWebGPU *
webgpu_copyCommand(GPUCopyPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static void
webgpu_fillTextureCopy(WGPUTexelCopyTextureInfo *copy,
                       GPUTexture               *texture,
                       const GPUTextureLocation *location) {
  *copy = (WGPUTexelCopyTextureInfo)WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  copy->texture  = texture->_priv;
  copy->mipLevel = location->mipLevel;
  copy->origin.x = location->x;
  copy->origin.y = location->y;
  copy->origin.z = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                     ? location->z
                     : location->baseArrayLayer;
  copy->aspect   = webgpu_copyAspect(location->aspect);
}

static void
webgpu_copyBufferTexture(GPUCopyPassEncoder               *pass,
                         GPUBuffer                        *buffer,
                         GPUTexture                       *texture,
                         const GPUBufferTextureCopyRegion *region,
                         bool                              bufferToTexture) {
  WGPUTexelCopyBufferInfo  bufferCopy = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
  WGPUTexelCopyTextureInfo textureCopy = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUExtent3D             extent = WGPU_EXTENT_3D_INIT;
  const GPUTextureSubresourceRegion *textureRegion;
  GPUCommandWebGPU                 *command;
  GPUFormatDataLayout               dataLayout;
  GPUFormatLayout                   formatLayout;
  uint32_t                          imageCount;

  command = webgpu_copyCommand(pass);
  if (!command || !command->encoder || !buffer || !buffer->_priv ||
      !texture || !texture->_priv || !region) {
    return;
  }

  textureRegion = &region->texture;
  if (!gpuFormatAspectLayout(texture->format,
                             textureRegion->texture.aspect,
                             &formatLayout) ||
      !gpuFormatAspectDataLayout(texture->format,
                                 textureRegion->texture.aspect,
                                 textureRegion->width,
                                 textureRegion->height,
                                 textureRegion->depth,
                                 textureRegion->layerCount,
                                 region->bytesPerRow,
                                 region->rowsPerImage,
                                 &dataLayout)) {
    return;
  }

  imageCount = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                 ? textureRegion->depth
                 : textureRegion->layerCount;
  bufferCopy.buffer              = buffer->_priv;
  bufferCopy.layout.offset       = region->bufferOffset;
  bufferCopy.layout.bytesPerRow  = region->bytesPerRow;
  bufferCopy.layout.rowsPerImage = region->rowsPerImage
                                     ? region->rowsPerImage
                                     : textureRegion->height;
  webgpu_fillTextureCopy(&textureCopy,
                         texture,
                         &textureRegion->texture);
  extent.width              = textureRegion->width;
  extent.height             = textureRegion->height;
  extent.depthOrArrayLayers = imageCount;

  if ((region->bytesPerRow & 255u) == 0u) {
    if (bufferToTexture) {
      wgpuCommandEncoderCopyBufferToTexture(command->encoder,
                                            &bufferCopy,
                                            &textureCopy,
                                            &extent);
    } else {
      wgpuCommandEncoderCopyTextureToBuffer(command->encoder,
                                            &textureCopy,
                                            &bufferCopy,
                                            &extent);
    }
    return;
  }

  /* WebGPU requires 256-byte row pitches for multi-row encoder copies. */
  for (uint32_t image = 0u; image < imageCount; image++) {
    for (uint32_t row = 0u; row < dataLayout.blockRows; row++) {
      uint32_t copiedHeight;

      bufferCopy.layout.offset = region->bufferOffset +
                                 (uint64_t)image * dataLayout.bytesPerImage +
                                 (uint64_t)row * region->bytesPerRow;
      bufferCopy.layout.bytesPerRow  = WGPU_COPY_STRIDE_UNDEFINED;
      bufferCopy.layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;
      textureCopy.origin.y = textureRegion->texture.y +
                             row * formatLayout.blockHeight;
      textureCopy.origin.z = (texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                ? textureRegion->texture.z
                                : textureRegion->texture.baseArrayLayer) + image;
      copiedHeight = textureRegion->height - row * formatLayout.blockHeight;
      extent.height = copiedHeight < formatLayout.blockHeight
                        ? copiedHeight
                        : formatLayout.blockHeight;
      extent.depthOrArrayLayers = 1u;
      if (bufferToTexture) {
        wgpuCommandEncoderCopyBufferToTexture(command->encoder,
                                              &bufferCopy,
                                              &textureCopy,
                                              &extent);
      } else {
        wgpuCommandEncoderCopyTextureToBuffer(command->encoder,
                                              &textureCopy,
                                              &bufferCopy,
                                              &extent);
      }
    }
  }
}

static GPURenderPassDesc *
webgpu_beginRenderPass(GPUCommandBuffer              *cmdb,
                       const GPURenderPassCreateInfo *info) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS) {
    return NULL;
  }

  command->renderPassDesc =
    (WGPURenderPassDescriptor)WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  memset(command->colorAttachments, 0, sizeof(command->colorAttachments));
  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *source;
    WGPURenderPassColorAttachment      *target;

    source  = &info->pColorAttachments[i];
    target  = &command->colorAttachments[i];
    *target = (WGPURenderPassColorAttachment)
      WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    target->view          = source->view->_priv;
    target->resolveTarget = source->resolveView
                              ? source->resolveView->_priv
                              : NULL;
    target->loadOp        = webgpu_loadOp(source->loadOp);
    target->storeOp       = webgpu_storeOp(source->storeOp);
    target->clearValue.r  = source->clearColor.float32[0];
    target->clearValue.g  = source->clearColor.float32[1];
    target->clearValue.b  = source->clearColor.float32[2];
    target->clearValue.a  = source->clearColor.float32[3];
  }

  command->renderPassDesc.label = gpu_webgpuString(info->label);
  command->renderPassDesc.colorAttachmentCount = info->colorAttachmentCount;
  command->renderPassDesc.colorAttachments = command->colorAttachments;
  if (info->pDepthStencilAttachment) {
    const GPURenderPassDepthStencilAttachment *source;
    WGPURenderPassDepthStencilAttachment      *target;
    GPUFormat                                  format;

    source  = info->pDepthStencilAttachment;
    target  = &command->depthStencilAttachment;
    format  = source->view->format;
    *target = (WGPURenderPassDepthStencilAttachment)
      WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    target->view = source->view->_priv;
    if (webgpu_formatHasDepth(format)) {
      target->depthLoadOp     = webgpu_loadOp(source->depthLoadOp);
      target->depthStoreOp    = webgpu_storeOp(source->depthStoreOp);
      target->depthClearValue = source->clearDepth;
    }
    if (webgpu_formatHasStencil(format)) {
      target->stencilLoadOp     = webgpu_loadOp(source->stencilLoadOp);
      target->stencilStoreOp    = webgpu_storeOp(source->stencilStoreOp);
      target->stencilClearValue = source->clearStencil;
    }
    command->renderPassDesc.depthStencilAttachment = target;
  }
  command->renderPass._priv = command;
  command->renderPass.label = info->label;
  return &command->renderPass;
}

static void
webgpu_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

static GPUCopyPassEncoder *
webgpu_beginCopyPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder) {
    return NULL;
  }

  memset(&command->copy, 0, sizeof(command->copy));
  command->copy._priv      = command;
  command->copyDebugGroup = false;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (label && label[0] != '\0') {
    wgpuCommandEncoderPushDebugGroup(command->encoder,
                                     gpu_webgpuString(label));
    command->copyDebugGroup = true;
  }
#else
  GPU__UNUSED(label);
#endif
  return &command->copy;
}

static void
webgpu_copyBufferToBuffer(GPUCopyPassEncoder        *pass,
                          GPUBuffer                 *src,
                          GPUBuffer                 *dst,
                          const GPUBufferCopyRegion *region) {
  GPUCommandWebGPU *command;

  command = webgpu_copyCommand(pass);
  if (!command || !command->encoder || !src || !src->_priv ||
      !dst || !dst->_priv || !region) {
    return;
  }
  wgpuCommandEncoderCopyBufferToBuffer(command->encoder,
                                       src->_priv,
                                       region->srcOffset,
                                       dst->_priv,
                                       region->dstOffset,
                                       region->sizeBytes);
}

static void
webgpu_copyBufferToTexture(GPUCopyPassEncoder               *pass,
                           GPUBuffer                        *src,
                           GPUTexture                       *dst,
                           const GPUBufferTextureCopyRegion *region) {
  webgpu_copyBufferTexture(pass, src, dst, region, true);
}

static void
webgpu_copyTextureToBuffer(GPUCopyPassEncoder               *pass,
                           GPUTexture                       *src,
                           GPUBuffer                        *dst,
                           const GPUBufferTextureCopyRegion *region) {
  webgpu_copyBufferTexture(pass, dst, src, region, false);
}

static void
webgpu_copyTextureToTexture(
  GPUCopyPassEncoder                  *pass,
  GPUTexture                          *src,
  GPUTexture                          *dst,
  const GPUTextureToTextureCopyRegion *region) {
  WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  WGPUExtent3D             extent = WGPU_EXTENT_3D_INIT;
  GPUCommandWebGPU        *command;

  command = webgpu_copyCommand(pass);
  if (!command || !command->encoder || !src || !src->_priv ||
      !dst || !dst->_priv || !region) {
    return;
  }

  webgpu_fillTextureCopy(&source, src, &region->src);
  webgpu_fillTextureCopy(&destination, dst, &region->dst);
  extent.width  = region->width;
  extent.height = region->height;
  extent.depthOrArrayLayers = src->dimension == GPU_TEXTURE_DIMENSION_3D
                                ? region->depth
                                : region->layerCount;
  wgpuCommandEncoderCopyTextureToTexture(command->encoder,
                                         &source,
                                         &destination,
                                         &extent);
}

static void
webgpu_endCopyPass(GPUCopyPassEncoder *pass) {
  GPUCommandWebGPU *command;

  command = webgpu_copyCommand(pass);
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (command && command->encoder && command->copyDebugGroup) {
    wgpuCommandEncoderPopDebugGroup(command->encoder);
    command->copyDebugGroup = false;
  }
#else
  GPU__UNUSED(command);
#endif
}

void
webgpu_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass      = webgpu_beginRenderPass;
  api->destroyRenderPass    = webgpu_destroyRenderPass;
  api->beginCopyPass        = webgpu_beginCopyPass;
  api->copyBufferToBuffer   = webgpu_copyBufferToBuffer;
  api->copyBufferToTexture  = webgpu_copyBufferToTexture;
  api->copyTextureToBuffer  = webgpu_copyTextureToBuffer;
  api->copyTextureToTexture = webgpu_copyTextureToTexture;
  api->endCopyPass          = webgpu_endCopyPass;
}
