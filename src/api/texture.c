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
#include "texture_internal.h"

static bool
gpuIsTextureDimensionValid(GPUTextureDimension dimension) {
  return dimension == GPU_TEXTURE_DIMENSION_1D ||
         dimension == GPU_TEXTURE_DIMENSION_2D ||
         dimension == GPU_TEXTURE_DIMENSION_3D;
}

static bool
gpuIsTextureViewTypeValid(GPUTextureViewType viewType) {
  return viewType == GPU_TEXTURE_VIEW_1D ||
         viewType == GPU_TEXTURE_VIEW_2D ||
         viewType == GPU_TEXTURE_VIEW_2D_ARRAY ||
         viewType == GPU_TEXTURE_VIEW_CUBE ||
         viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY ||
         viewType == GPU_TEXTURE_VIEW_3D;
}

static uint32_t
gpuMipExtent(uint32_t extent, uint32_t mipLevel) {
  uint32_t mipExtent;

  mipExtent = extent >> mipLevel;
  return mipExtent > 0 ? mipExtent : 1u;
}

static bool
gpuTextureRegionInRange(const GPUTexture *texture,
                        uint32_t          mipLevel,
                        uint32_t          x,
                        uint32_t          y,
                        uint32_t          z,
                        uint32_t          width,
                        uint32_t          height,
                        uint32_t          depth,
                        uint32_t          baseArrayLayer,
                        uint32_t          layerCount) {
  uint32_t mipWidth;
  uint32_t mipHeight;
  uint32_t mipDepth;

  if (!texture ||
      mipLevel >= texture->mipLevelCount ||
      width == 0 ||
      height == 0 ||
      depth == 0 ||
      layerCount == 0) {
    return false;
  }

  mipWidth = gpuMipExtent(texture->width, mipLevel);
  mipHeight = gpuMipExtent(texture->height, mipLevel);
  if (x > mipWidth || width > mipWidth - x ||
      y > mipHeight || height > mipHeight - y) {
    return false;
  }

  if (texture->dimension == GPU_TEXTURE_DIMENSION_3D) {
    mipDepth = gpuMipExtent(texture->depthOrLayers, mipLevel);
    return baseArrayLayer == 0 &&
           layerCount == 1 &&
           z <= mipDepth &&
           depth <= mipDepth - z;
  }

  return z == 0 &&
         depth == 1 &&
         baseArrayLayer < texture->depthOrLayers &&
         layerCount <= texture->depthOrLayers - baseArrayLayer;
}

GPU_EXPORT
GPUResult
GPUCreateTexture(GPUDevice                  * __restrict device,
                 const GPUTextureCreateInfo * __restrict info,
                 GPUTexture                ** __restrict outTexture) {
  GPUApi *api;

  if (!outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;

  if (!device || !info ||
      info->format == GPU_FORMAT_UNDEFINED ||
      info->width == 0 ||
      info->height == 0 ||
      info->depthOrLayers == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuIsTextureDimensionValid(info->dimension) || info->usage == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.create) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.create(device, info, outTexture);
}

GPU_EXPORT
void
GPUDestroyTexture(GPUTexture * __restrict texture) {
  GPUApi *api;

  if (!texture) {
    return;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.destroy) {
    return;
  }

  api->texture.destroy(texture);
}

GPU_EXPORT
GPUResult
GPUGetTextureInfo(GPUTexture * __restrict texture,
                  GPUTextureInfo * __restrict outInfo) {
  if (!outInfo) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outInfo, 0, sizeof(*outInfo));
  if (!texture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  outInfo->dimension = texture->dimension;
  outInfo->format = texture->format;
  outInfo->width = texture->width;
  outInfo->height = texture->height;
  outInfo->depthOrLayers = texture->depthOrLayers;
  outInfo->mipLevelCount = texture->mipLevelCount;
  outInfo->sampleCount = texture->sampleCount;
  outInfo->usage = texture->usage;
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUCreateTextureView(GPUTexture                     * __restrict texture,
                     const GPUTextureViewCreateInfo * __restrict info,
                     GPUTextureView                ** __restrict outView) {
  GPUApi *api;

  if (!outView) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outView = NULL;

  if (!texture || !info ||
      info->format == GPU_FORMAT_UNDEFINED ||
      info->mipLevelCount == 0 ||
      info->arrayLayerCount == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE &&
      info->chain.sType != GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuIsTextureViewTypeValid(info->viewType) ||
      info->baseMipLevel >= texture->mipLevelCount ||
      info->mipLevelCount > texture->mipLevelCount - info->baseMipLevel ||
      info->baseArrayLayer >= texture->depthOrLayers ||
      info->arrayLayerCount > texture->depthOrLayers - info->baseArrayLayer) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.createView) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.createView(texture, info, outView);
}

GPU_EXPORT
void
GPUDestroyTextureView(GPUTextureView * __restrict view) {
  GPUApi *api;

  if (!view) {
    return;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.destroyView) {
    return;
  }

  api->texture.destroyView(view);
}

GPU_EXPORT
GPUResult
GPUQueueWriteTexture(GPUCommandQueue             * __restrict queue,
                     GPUTexture                  * __restrict texture,
                     const GPUTextureWriteRegion * __restrict region,
                     const void                  * __restrict data,
                     uint64_t                                 sizeBytes) {
  GPUApi *api;

  if (!queue || !texture || !region || !data ||
      region->width == 0 ||
      region->height == 0 ||
      region->depth == 0 ||
      region->layerCount == 0 ||
      region->bytesPerRow == 0 ||
      sizeBytes == 0) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (!gpuTextureRegionInRange(texture,
                               region->mipLevel,
                               0u,
                               0u,
                               0u,
                               region->width,
                               region->height,
                               region->depth,
                               region->baseArrayLayer,
                               region->layerCount)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (region->rowsPerImage != 0 && region->rowsPerImage < region->height) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(api = gpuActiveGPUApi()) || !api->texture.write) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  return api->texture.write(queue, texture, region, data, sizeBytes);
}
