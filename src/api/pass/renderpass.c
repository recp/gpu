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
#include "../texture_internal.h"

#define GPU_RENDER_PASS_MAX_COLOR_ATTACHMENTS 8u

static bool
gpu_validLoadOp(GPULoadOp op) {
  return op == GPU_LOAD_OP_LOAD ||
         op == GPU_LOAD_OP_CLEAR ||
         op == GPU_LOAD_OP_DONT_CARE;
}

static bool
gpu_validStoreOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_STORE ||
         op == GPU_STORE_OP_DONT_CARE;
}

static bool
gpu_validRenderPassCreateInfo(const GPURenderPassCreateInfo *info) {
  const GPURenderPassDepthStencilAttachment *depthStencil;

  if (!info) {
    return false;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO) {
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

  for (uint32_t i = 0; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *color;

    color = &info->pColorAttachments[i];
    if (!color->view ||
        !gpu_validLoadOp(color->loadOp) ||
        !gpu_validStoreOp(color->storeOp)) {
      return false;
    }
  }

  depthStencil = info->pDepthStencilAttachment;
  if (depthStencil) {
    if (!depthStencil->view ||
        !gpu_validLoadOp(depthStencil->depthLoadOp) ||
        !gpu_validStoreOp(depthStencil->depthStoreOp) ||
        !gpu_validLoadOp(depthStencil->stencilLoadOp) ||
        !gpu_validStoreOp(depthStencil->stencilStoreOp)) {
      return false;
    }
  }

  return info->colorAttachmentCount > 0 || depthStencil != NULL;
}

static uint32_t
gpu_copyMipExtent(uint32_t extent, uint32_t mipLevel) {
  uint32_t mipExtent;

  mipExtent = extent >> mipLevel;
  return mipExtent > 0 ? mipExtent : 1u;
}

static bool
gpu_validTextureCopyRegion(const GPUTextureSubresourceRegion *region,
                           const GPUTexture                  *texture) {
  const GPUTextureLocation *location;
  uint32_t mipWidth;
  uint32_t mipHeight;
  uint32_t mipDepth;

  if (!region ||
      !texture ||
      region->width == 0 ||
      region->height == 0 ||
      region->depth == 0 ||
      region->layerCount == 0) {
    return false;
  }

  location = &region->texture;
  if (location->mipLevel >= texture->mipLevelCount) {
    return false;
  }

  mipWidth = gpu_copyMipExtent(texture->width, location->mipLevel);
  mipHeight = gpu_copyMipExtent(texture->height, location->mipLevel);
  if (location->x > mipWidth ||
      region->width > mipWidth - location->x ||
      location->y > mipHeight ||
      region->height > mipHeight - location->y) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_1D &&
      (location->y != 0 || region->height != 1)) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
    mipDepth = gpu_copyMipExtent(texture->depthOrLayers, location->mipLevel);
    return location->baseArrayLayer == 0 &&
           region->layerCount == 1 &&
           location->z <= mipDepth &&
           region->depth <= mipDepth - location->z;
  }

  return location->z == 0 &&
         region->depth == 1 &&
         location->baseArrayLayer < texture->depthOrLayers &&
         region->layerCount <= texture->depthOrLayers - location->baseArrayLayer;
}

static bool
gpu_validBufferTextureCopy(const GPUBufferTextureCopyRegion *region,
                           const GPUTexture                 *texture) {
  if (!region ||
      region->bytesPerRow == 0 ||
      (region->rowsPerImage != 0 &&
       region->rowsPerImage < region->texture.height)) {
    return false;
  }

  return gpu_validTextureCopyRegion(&region->texture, texture);
}

static void
gpu_destroyRenderPass(GPURenderPassDesc *pass) {
  GPUApi *api;

  if (!pass) {
    return;
  }

  api = gpuActiveGPUApi();
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
  GPUApi               *api;

  if (!cmdb || cmdb->_submitted || !gpu_validRenderPassCreateInfo(info))
    return NULL;
  if (!(api = gpuActiveGPUApi()))
    return NULL;
  if (!api->renderPass.beginRenderPass || !api->rce.renderCommandEncoder)
    return NULL;

  desc = api->renderPass.beginRenderPass(info);
  if (!desc)
    return NULL;

  encoder = api->rce.renderCommandEncoder(cmdb, desc);
  gpu_destroyRenderPass(desc);

  return encoder;
}

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassEncoder *pass) {
  GPUApi *api;

  if (!pass || pass->_ended || !(api = gpuActiveGPUApi()) || !api->rce.endEncoding)
    return;

  pass->_ended = true;
  api->rce.endEncoding(pass);
}

GPU_EXPORT
GPUCopyPassEncoder*
GPUBeginCopyPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted) {
    return NULL;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.beginCopyPass) {
    return NULL;
  }

  return api->renderPass.beginCopyPass(cmdb, label);
}

GPU_EXPORT
void
GPUCopyBufferToBuffer(GPUCopyPassEncoder        *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region) {
  GPUApi *api;

  if (!pass || pass->_ended ||
      !src || !dst || !region || region->sizeBytes == 0) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.copyBufferToBuffer) {
    return;
  }

  api->renderPass.copyBufferToBuffer(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyBufferToTexture(GPUCopyPassEncoder               *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUApi *api;

  if (!pass || pass->_ended ||
      !src || !dst || !gpu_validBufferTextureCopy(region, dst)) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.copyBufferToTexture) {
    return;
  }

  api->renderPass.copyBufferToTexture(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyTextureToBuffer(GPUCopyPassEncoder               *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region) {
  GPUApi *api;

  if (!pass || pass->_ended ||
      !src || !dst || !gpu_validBufferTextureCopy(region, src)) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.copyTextureToBuffer) {
    return;
  }

  api->renderPass.copyTextureToBuffer(pass, src, dst, region);
}

GPU_EXPORT
void
GPUCopyTextureToTexture(GPUCopyPassEncoder                  *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region) {
  GPUTextureSubresourceRegion srcRegion;
  GPUTextureSubresourceRegion dstRegion;
  GPUApi *api;

  if (!pass || pass->_ended || !src || !dst || !region ||
      src->dimension != dst->dimension ||
      region->width == 0 ||
      region->height == 0 ||
      region->depth == 0 ||
      region->layerCount == 0) {
    return;
  }

  memset(&srcRegion, 0, sizeof(srcRegion));
  memset(&dstRegion, 0, sizeof(dstRegion));
  srcRegion.texture = region->src;
  srcRegion.width = region->width;
  srcRegion.height = region->height;
  srcRegion.depth = region->depth;
  srcRegion.layerCount = region->layerCount;
  dstRegion.texture = region->dst;
  dstRegion.width = region->width;
  dstRegion.height = region->height;
  dstRegion.depth = region->depth;
  dstRegion.layerCount = region->layerCount;
  if (!gpu_validTextureCopyRegion(&srcRegion, src) ||
      !gpu_validTextureCopyRegion(&dstRegion, dst)) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.copyTextureToTexture) {
    return;
  }

  api->renderPass.copyTextureToTexture(pass, src, dst, region);
}

GPU_EXPORT
void
GPUEndCopyPass(GPUCopyPassEncoder *pass) {
  GPUApi *api;

  if (!pass || pass->_ended) {
    return;
  }
  if (!(api = gpuActiveGPUApi()) || !api->renderPass.endCopyPass) {
    return;
  }

  pass->_ended = true;
  api->renderPass.endCopyPass(pass);
}
