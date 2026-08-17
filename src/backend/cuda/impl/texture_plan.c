/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "../texture_plan.h"

static uint32_t
cuda__maxMipLevelCount(const GPUTextureCreateInfo *info) {
  uint32_t extent;
  uint32_t count;

  extent = info->width;
  if (info->dimension != GPU_TEXTURE_DIMENSION_1D &&
      info->height > extent) {
    extent = info->height;
  }
  if (info->dimension == GPU_TEXTURE_DIMENSION_3D &&
      info->depthOrLayers > extent) {
    extent = info->depthOrLayers;
  }

  count = 0u;
  do {
    count++;
    extent >>= 1u;
  } while (extent != 0u);
  return count;
}

static size_t
cuda__mipExtent(uint32_t extent, uint32_t mipLevel) {
  size_t value;

  value = (size_t)extent >> mipLevel;
  return value > 0u ? value : 1u;
}

static bool
cuda__cubeCompatible(const GPUTextureCreateInfo *info) {
  return info->dimension == GPU_TEXTURE_DIMENSION_2D &&
         (info->usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u &&
         info->width == info->height && info->depthOrLayers >= 6u &&
         info->depthOrLayers % 6u == 0u;
}

static bool
cuda__textureViewSupported(GPUTextureViewType viewType) {
  return cuda_textureStorageViewSupported(viewType) ||
         viewType == GPU_TEXTURE_VIEW_CUBE ||
         viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY;
}

bool
cuda_texturePlan(const GPUTextureCreateInfo *info,
                 const GPUCudaFormatInfo    *format,
                 GPUCudaTexturePlan         *outPlan) {
  const GPUTextureUsageFlags allowedUsage =
    GPU_TEXTURE_USAGE_SAMPLED |
    GPU_TEXTURE_USAGE_STORAGE |
    GPU_TEXTURE_USAGE_COPY_SRC |
    GPU_TEXTURE_USAGE_COPY_DST;
  GPUCudaTexturePlan plan;

  if (outPlan) {
    memset(outPlan, 0, sizeof(*outPlan));
  }
  if (!info || !format || !outPlan || info->width == 0u ||
      info->height == 0u || info->depthOrLayers == 0u ||
      (info->sampleCount != 0u && info->sampleCount != 1u) ||
      (info->usage & ~allowedUsage) != 0u ||
      ((info->usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u &&
       (format->flags & GPU_CUDA_FORMAT_SAMPLED_BIT) == 0u) ||
      ((info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u &&
       (format->flags & GPU_CUDA_FORMAT_STORAGE_BIT) == 0u)) {
    return false;
  }

  memset(&plan, 0, sizeof(plan));
  plan.mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  if (plan.mipLevelCount > cuda__maxMipLevelCount(info)) {
    return false;
  }

  plan.desc.Width       = info->width;
  plan.desc.Format      = format->arrayFormat;
  plan.desc.NumChannels = format->channelCount;
  switch (info->dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      if (info->height != 1u) {
        return false;
      }
      if (info->depthOrLayers > 1u) {
        plan.desc.Depth  = info->depthOrLayers;
        plan.desc.Flags |= CUDA_ARRAY3D_LAYERED;
      }
      break;
    case GPU_TEXTURE_DIMENSION_2D:
      plan.desc.Height = info->height;
      if (info->depthOrLayers > 1u) {
        plan.desc.Depth  = info->depthOrLayers;
        if (cuda__cubeCompatible(info)) {
          plan.desc.Flags |= CUDA_ARRAY3D_CUBEMAP;
          if (info->depthOrLayers > 6u) {
            plan.desc.Flags |= CUDA_ARRAY3D_LAYERED;
          }
        } else {
          plan.desc.Flags |= CUDA_ARRAY3D_LAYERED;
        }
      }
      break;
    case GPU_TEXTURE_DIMENSION_3D:
      plan.desc.Height = info->height;
      plan.desc.Depth  = info->depthOrLayers;
      break;
    default:
      return false;
  }
  if ((info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    plan.desc.Flags |= CUDA_ARRAY3D_SURFACE_LDST;
  }
  plan.mipmapped = plan.mipLevelCount > 1u;
  *outPlan       = plan;
  return true;
}

bool
cuda_textureStorageViewSupported(GPUTextureViewType viewType) {
  return viewType == GPU_TEXTURE_VIEW_1D ||
         viewType == GPU_TEXTURE_VIEW_1D_ARRAY ||
         viewType == GPU_TEXTURE_VIEW_2D ||
         viewType == GPU_TEXTURE_VIEW_2D_ARRAY ||
         viewType == GPU_TEXTURE_VIEW_3D;
}

bool
cuda_textureViewPlan(const GPUTexture               *texture,
                     const GPUTextureViewCreateInfo *info,
                     GPUCudaTextureViewPlan         *outPlan) {
  GPUCudaTextureViewPlan plan;
  uint32_t               layerCount;
  bool                   layered;

  if (outPlan) {
    memset(outPlan, 0, sizeof(*outPlan));
  }
  if (!texture || !info || !outPlan || info->format != texture->format ||
      !gpuTextureSubresourceRangeValid(texture,
                                       info->baseMipLevel,
                                       info->mipLevelCount,
                                       info->baseArrayLayer,
                                       info->arrayLayerCount) ||
      !cuda__textureViewSupported(info->viewType)) {
    return false;
  }

  layered    = texture->dimension != GPU_TEXTURE_DIMENSION_3D &&
               texture->depthOrLayers > 1u;
  layerCount = gpuTextureArrayLayerCount(texture);
  switch (texture->dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      if ((texture->depthOrLayers == 1u &&
           info->viewType != GPU_TEXTURE_VIEW_1D) ||
          (texture->depthOrLayers > 1u &&
           info->viewType != GPU_TEXTURE_VIEW_1D_ARRAY)) {
        return false;
      }
      break;
    case GPU_TEXTURE_DIMENSION_2D:
      if ((texture->depthOrLayers == 1u &&
           info->viewType != GPU_TEXTURE_VIEW_2D) ||
          (texture->depthOrLayers > 1u &&
           info->viewType != GPU_TEXTURE_VIEW_2D_ARRAY &&
           info->viewType != GPU_TEXTURE_VIEW_CUBE &&
           info->viewType != GPU_TEXTURE_VIEW_CUBE_ARRAY)) {
        return false;
      }
      if ((info->viewType == GPU_TEXTURE_VIEW_CUBE ||
           info->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY) &&
          (texture->width != texture->height ||
           texture->depthOrLayers < 6u ||
           texture->depthOrLayers % 6u != 0u ||
           (info->viewType == GPU_TEXTURE_VIEW_CUBE &&
            (info->baseArrayLayer != 0u ||
             info->arrayLayerCount != 6u)) ||
           (info->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY &&
            (info->baseArrayLayer % 6u != 0u ||
             info->arrayLayerCount % 6u != 0u)))) {
        return false;
      }
      break;
    case GPU_TEXTURE_DIMENSION_3D:
      if (info->viewType != GPU_TEXTURE_VIEW_3D ||
          info->baseArrayLayer != 0u || info->arrayLayerCount != 1u) {
        return false;
      }
      break;
    default:
      return false;
  }

  memset(&plan, 0, sizeof(plan));
  plan.singleLevel            = info->mipLevelCount == 1u;
  plan.desc.format            = CU_RES_VIEW_FORMAT_NONE;
  plan.desc.width             = plan.singleLevel
                                  ? cuda__mipExtent(texture->width,
                                                    info->baseMipLevel)
                                  : texture->width;
  plan.desc.height            = texture->dimension == GPU_TEXTURE_DIMENSION_1D
                                  ? 0u
                                  : plan.singleLevel
                                      ? cuda__mipExtent(texture->height,
                                                        info->baseMipLevel)
                                      : texture->height;
  plan.desc.depth             = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                                  ? plan.singleLevel
                                      ? cuda__mipExtent(texture->depthOrLayers,
                                                        info->baseMipLevel)
                                      : texture->depthOrLayers
                                  : layered
                                      ? texture->depthOrLayers
                                      : 0u;
  plan.desc.firstMipmapLevel  = plan.singleLevel ? 0u : info->baseMipLevel;
  plan.desc.lastMipmapLevel   = plan.singleLevel
                                  ? 0u
                                  : info->baseMipLevel +
                                    info->mipLevelCount - 1u;
  plan.desc.firstLayer        = layered ? info->baseArrayLayer : 0u;
  plan.desc.lastLayer         = layered
                                  ? info->baseArrayLayer +
                                    info->arrayLayerCount - 1u
                                  : 0u;
  plan.mipLevel               = info->baseMipLevel;
  plan.hasResourceView        = (!plan.singleLevel &&
                                 (info->baseMipLevel != 0u ||
                                  info->mipLevelCount !=
                                    texture->mipLevelCount)) ||
                                info->baseArrayLayer != 0u ||
                                info->arrayLayerCount != layerCount;
  plan.surfaceCompatible =
    cuda_textureStorageViewSupported(info->viewType) &&
    plan.singleLevel &&
    (!layered ||
     (info->baseArrayLayer == 0u &&
      info->arrayLayerCount == layerCount));
  *outPlan = plan;
  return true;
}
