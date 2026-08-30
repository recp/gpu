#include "backend/cuda/texture_plan.h"

#include <stdio.h>
#include <string.h>

#define CHECK(CONDITION) \
  do { \
    if (!(CONDITION)) { \
      fprintf(stderr, "CUDA texture contract mismatch at line %d\n", __LINE__); \
      return 0; \
    } \
  } while (0)

static GPUTextureCreateInfo
texture_info(GPUTextureDimension dimension,
             uint32_t            width,
             uint32_t            height,
             uint32_t            depthOrLayers,
             uint32_t            mipLevelCount,
             GPUTextureUsageFlags usage) {
  GPUTextureCreateInfo info = {0};

  info.dimension     = dimension;
  info.format        = GPU_FORMAT_RGBA32_FLOAT;
  info.width         = width;
  info.height        = height;
  info.depthOrLayers = depthOrLayers;
  info.mipLevelCount = mipLevelCount;
  info.sampleCount   = 1u;
  info.usage         = usage;
  return info;
}

static GPUTextureViewCreateInfo
view_info(GPUTextureViewType viewType,
          uint32_t           baseMipLevel,
          uint32_t           mipLevelCount,
          uint32_t           baseArrayLayer,
          uint32_t           arrayLayerCount) {
  GPUTextureViewCreateInfo info = {0};

  info.viewType        = viewType;
  info.format          = GPU_FORMAT_RGBA32_FLOAT;
  info.baseMipLevel    = baseMipLevel;
  info.mipLevelCount   = mipLevelCount;
  info.baseArrayLayer  = baseArrayLayer;
  info.arrayLayerCount = arrayLayerCount;
  return info;
}

static int
validate_texture_plans(void) {
  const GPUCudaFormatInfo format = {
    CU_AD_FORMAT_FLOAT,
    GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_STORAGE_BIT,
    16u,
    4u
  };
  GPUCudaFormatInfo     limitedFormat;
  GPUTextureCreateInfo info;
  GPUCudaTexturePlan   plan;

  info = texture_info(GPU_TEXTURE_DIMENSION_1D,
                      64u,
                      1u,
                      1u,
                      1u,
                      GPU_TEXTURE_USAGE_SAMPLED);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Width == 64u && plan.desc.Height == 0u &&
        plan.desc.Depth == 0u && plan.desc.Flags == 0u &&
        plan.mipLevelCount == 1u && !plan.mipmapped);

  info = texture_info(GPU_TEXTURE_DIMENSION_1D,
                      64u,
                      1u,
                      8u,
                      4u,
                      GPU_TEXTURE_USAGE_SAMPLED |
                      GPU_TEXTURE_USAGE_STORAGE);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Width == 64u && plan.desc.Height == 0u &&
        plan.desc.Depth == 8u &&
        plan.desc.Flags == (CUDA_ARRAY3D_LAYERED |
                            CUDA_ARRAY3D_SURFACE_LDST) &&
        plan.mipLevelCount == 4u && plan.mipmapped);

  info = texture_info(GPU_TEXTURE_DIMENSION_2D,
                      64u,
                      32u,
                      1u,
                      0u,
                      GPU_TEXTURE_USAGE_SAMPLED);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Width == 64u && plan.desc.Height == 32u &&
        plan.desc.Depth == 0u && plan.mipLevelCount == 1u &&
        !plan.mipmapped);

  info = texture_info(GPU_TEXTURE_DIMENSION_2D,
                      64u,
                      32u,
                      8u,
                      4u,
                      GPU_TEXTURE_USAGE_SAMPLED |
                      GPU_TEXTURE_USAGE_STORAGE);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Height == 32u && plan.desc.Depth == 8u &&
        plan.desc.Flags == (CUDA_ARRAY3D_LAYERED |
                            CUDA_ARRAY3D_SURFACE_LDST) &&
        plan.mipmapped);

  info = texture_info(GPU_TEXTURE_DIMENSION_2D,
                      64u,
                      64u,
                      6u,
                      1u,
                      GPU_TEXTURE_USAGE_SAMPLED);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Depth == 6u &&
        plan.desc.Flags == CUDA_ARRAY3D_CUBEMAP && !plan.mipmapped);

  info.depthOrLayers = 12u;
  info.mipLevelCount = 4u;
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Depth == 12u &&
        plan.desc.Flags == (CUDA_ARRAY3D_CUBEMAP |
                            CUDA_ARRAY3D_LAYERED) &&
        plan.mipmapped);

  info.usage = GPU_TEXTURE_USAGE_STORAGE;
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Flags == (CUDA_ARRAY3D_LAYERED |
                            CUDA_ARRAY3D_SURFACE_LDST));

  info = texture_info(GPU_TEXTURE_DIMENSION_3D,
                      64u,
                      32u,
                      16u,
                      5u,
                      GPU_TEXTURE_USAGE_STORAGE);
  CHECK(cuda_texturePlan(&info, &format, &plan));
  CHECK(plan.desc.Width == 64u && plan.desc.Height == 32u &&
        plan.desc.Depth == 16u &&
        plan.desc.Flags == CUDA_ARRAY3D_SURFACE_LDST && plan.mipmapped);

  info = texture_info(GPU_TEXTURE_DIMENSION_1D,
                      64u,
                      2u,
                      1u,
                      1u,
                      GPU_TEXTURE_USAGE_SAMPLED);
  memset(&plan, 0xa5, sizeof(plan));
  CHECK(!cuda_texturePlan(&info, &format, &plan));
  CHECK(memcmp(&plan, &(GPUCudaTexturePlan){0}, sizeof(plan)) == 0);

  info = texture_info(GPU_TEXTURE_DIMENSION_2D,
                      64u,
                      32u,
                      1u,
                      8u,
                      GPU_TEXTURE_USAGE_SAMPLED);
  CHECK(!cuda_texturePlan(&info, &format, &plan));
  info.mipLevelCount = 1u;
  info.sampleCount   = 4u;
  CHECK(!cuda_texturePlan(&info, &format, &plan));
  info.sampleCount = 1u;
  info.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET;
  CHECK(!cuda_texturePlan(&info, &format, &plan));

  limitedFormat       = format;
  limitedFormat.flags = GPU_CUDA_FORMAT_SAMPLED_BIT;
  info.usage          = GPU_TEXTURE_USAGE_STORAGE;
  CHECK(!cuda_texturePlan(&info, &limitedFormat, &plan));

  limitedFormat = (GPUCudaFormatInfo){
    CU_AD_FORMAT_UNSIGNED_INT32,
    GPU_CUDA_FORMAT_STORAGE_BIT,
    4u,
    1u
  };
  info.format = GPU_FORMAT_RG11B10_UFLOAT;
  CHECK(cuda_texturePlan(&info, &limitedFormat, &plan));
  CHECK(plan.desc.Format == CU_AD_FORMAT_UNSIGNED_INT32 &&
        plan.desc.NumChannels == 1u &&
        plan.desc.Flags == CUDA_ARRAY3D_SURFACE_LDST);
  info.usage = GPU_TEXTURE_USAGE_SAMPLED;
  CHECK(!cuda_texturePlan(&info, &limitedFormat, &plan));

  limitedFormat = (GPUCudaFormatInfo){
    CU_AD_FORMAT_UNSIGNED_INT16,
    GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_FILTERABLE_BIT,
    2u,
    1u
  };
  info.format = GPU_FORMAT_DEPTH16_UNORM;
  CHECK(cuda_texturePlan(&info, &limitedFormat, &plan));
  CHECK(plan.desc.Format == CU_AD_FORMAT_UNSIGNED_INT16 &&
        plan.desc.NumChannels == 1u && plan.desc.Flags == 0u);
  info.usage = GPU_TEXTURE_USAGE_STORAGE;
  CHECK(!cuda_texturePlan(&info, &limitedFormat, &plan));
  return 1;
}

static int
validate_view_plans(void) {
  GPUTexture               texture = {0};
  GPUTextureViewCreateInfo info;
  GPUCudaTextureViewPlan   plan;

  texture.format        = GPU_FORMAT_RGBA32_FLOAT;
  texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
  texture.width         = 64u;
  texture.height        = 32u;
  texture.depthOrLayers = 8u;
  texture.mipLevelCount = 4u;

  info = view_info(GPU_TEXTURE_VIEW_2D_ARRAY, 0u, 4u, 0u, 8u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(!plan.singleLevel && !plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.width == 64u &&
        plan.desc.height == 32u && plan.desc.depth == 8u &&
        plan.desc.firstMipmapLevel == 0u &&
        plan.desc.lastMipmapLevel == 3u &&
        plan.desc.firstLayer == 0u && plan.desc.lastLayer == 7u);

  info = view_info(GPU_TEXTURE_VIEW_2D_ARRAY, 1u, 2u, 2u, 3u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(!plan.singleLevel && plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.width == 64u &&
        plan.desc.height == 32u && plan.desc.depth == 8u &&
        plan.desc.firstMipmapLevel == 1u &&
        plan.desc.lastMipmapLevel == 2u &&
        plan.desc.firstLayer == 2u && plan.desc.lastLayer == 4u);

  info = view_info(GPU_TEXTURE_VIEW_2D_ARRAY, 2u, 1u, 0u, 8u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(plan.singleLevel && !plan.hasResourceView &&
        plan.surfaceCompatible && plan.mipLevel == 2u &&
        plan.desc.width == 16u && plan.desc.height == 8u &&
        plan.desc.depth == 8u && plan.desc.firstMipmapLevel == 0u &&
        plan.desc.lastMipmapLevel == 0u);

  info = view_info(GPU_TEXTURE_VIEW_2D_ARRAY, 2u, 1u, 2u, 3u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(plan.singleLevel && plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.firstLayer == 2u &&
        plan.desc.lastLayer == 4u);

  texture.dimension     = GPU_TEXTURE_DIMENSION_3D;
  texture.depthOrLayers = 16u;
  texture.mipLevelCount = 5u;
  info = view_info(GPU_TEXTURE_VIEW_3D, 2u, 1u, 0u, 1u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(plan.singleLevel && plan.surfaceCompatible &&
        !plan.hasResourceView && plan.desc.width == 16u &&
        plan.desc.height == 8u && plan.desc.depth == 4u);

  texture.dimension     = GPU_TEXTURE_DIMENSION_1D;
  texture.height        = 1u;
  texture.depthOrLayers = 1u;
  texture.mipLevelCount = 4u;
  info = view_info(GPU_TEXTURE_VIEW_1D, 1u, 1u, 0u, 1u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(plan.singleLevel && plan.surfaceCompatible &&
        plan.desc.width == 32u && plan.desc.height == 0u &&
        plan.desc.depth == 0u);

  texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
  texture.height        = 64u;
  texture.depthOrLayers = 12u;
  info = view_info(GPU_TEXTURE_VIEW_CUBE, 0u, 1u, 0u, 6u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(plan.singleLevel && plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.firstLayer == 0u &&
        plan.desc.lastLayer == 5u);
  info = view_info(GPU_TEXTURE_VIEW_CUBE_ARRAY, 0u, 4u, 0u, 12u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(!plan.singleLevel && !plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.depth == 12u);
  info = view_info(GPU_TEXTURE_VIEW_CUBE_ARRAY, 1u, 2u, 6u, 6u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(!plan.singleLevel && plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.firstLayer == 6u &&
        plan.desc.lastLayer == 11u);
  info = view_info(GPU_TEXTURE_VIEW_CUBE_ARRAY, 0u, 1u, 1u, 6u);
  CHECK(!cuda_textureViewPlan(&texture, &info, &plan));

  texture.depthOrLayers = 6u;
  info = view_info(GPU_TEXTURE_VIEW_CUBE, 1u, 2u, 0u, 6u);
  CHECK(cuda_textureViewPlan(&texture, &info, &plan));
  CHECK(!plan.singleLevel && plan.hasResourceView &&
        !plan.surfaceCompatible && plan.desc.depth == 6u &&
        plan.desc.firstMipmapLevel == 1u &&
        plan.desc.lastMipmapLevel == 2u &&
        plan.desc.firstLayer == 0u && plan.desc.lastLayer == 0u);
  texture.height = 32u;
  info = view_info(GPU_TEXTURE_VIEW_CUBE, 0u, 1u, 0u, 6u);
  CHECK(!cuda_textureViewPlan(&texture, &info, &plan));
  info = view_info(GPU_TEXTURE_VIEW_2D, 0u, 1u, 0u, 1u);
  CHECK(!cuda_textureViewPlan(&texture, &info, &plan));
  info = view_info(GPU_TEXTURE_VIEW_2D_ARRAY, 4u, 1u, 0u, 12u);
  CHECK(!cuda_textureViewPlan(&texture, &info, &plan));

  CHECK(cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_1D));
  CHECK(cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_1D_ARRAY));
  CHECK(cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_2D));
  CHECK(cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_2D_ARRAY));
  CHECK(cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_3D));
  CHECK(!cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_CUBE));
  CHECK(!cuda_textureStorageViewSupported(GPU_TEXTURE_VIEW_CUBE_ARRAY));
  return 1;
}

int
main(void) {
  if (!validate_texture_plans() || !validate_view_plans()) {
    return 1;
  }

  puts("CUDA texture contract validation passed");
  return 0;
}

#undef CHECK
