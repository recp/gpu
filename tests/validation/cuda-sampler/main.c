#include "backend/cuda/sampler_plan.h"

#include <stdio.h>
#include <string.h>

#include <us/compiler.h>

#define CHECK(COND) \
  do { \
    if (!(COND)) { \
      fprintf(stderr, "CUDA sampler contract failed at line %d\n", __LINE__); \
      return 0; \
    } \
  } while (0)

static GPUSamplerDesc
dynamic_sampler(void) {
  GPUSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter     = GPU_FILTER_NEAREST;
  desc.magFilter     = GPU_FILTER_NEAREST;
  desc.mipFilter     = GPU_MIP_FILTER_NEAREST;
  desc.addressU      = GPU_ADDRESS_MODE_REPEAT;
  desc.addressV      = GPU_ADDRESS_MODE_MIRRORED_REPEAT;
  desc.addressW      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  desc.maxAnisotropy = 1u;
  return desc;
}

static GPUStaticSamplerDesc
static_sampler(void) {
  GPUStaticSamplerDesc desc;

  memset(&desc, 0, sizeof(desc));
  desc.minFilter     = USL_RUNTIME_FILTER_NEAREST;
  desc.magFilter     = USL_RUNTIME_FILTER_NEAREST;
  desc.mipFilter     = USL_RUNTIME_FILTER_NEAREST;
  desc.addressMode   = USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE;
  desc.coordSpace    = USL_RUNTIME_COORD_NORMALIZED;
  desc.maxAnisotropy = 1u;
  return desc;
}

static int
validate_dynamic_sampler(void) {
  GPUSamplerDesc   source;
  CUDA_TEXTURE_DESC actual;

  source = dynamic_sampler();
  CHECK(cuda_samplerTextureDesc(&source, &actual));
  CHECK(actual.addressMode[0] == CU_TR_ADDRESS_MODE_WRAP &&
        actual.addressMode[1] == CU_TR_ADDRESS_MODE_MIRROR &&
        actual.addressMode[2] == CU_TR_ADDRESS_MODE_CLAMP &&
        actual.filterMode == CU_TR_FILTER_MODE_POINT &&
        actual.mipmapFilterMode == CU_TR_FILTER_MODE_POINT &&
        actual.flags == CU_TRSF_NORMALIZED_COORDINATES &&
        actual.maxAnisotropy == 1u);

  source.minFilter     = GPU_FILTER_LINEAR;
  source.magFilter     = GPU_FILTER_LINEAR;
  source.mipFilter     = GPU_MIP_FILTER_LINEAR;
  source.maxAnisotropy = 16u;
  CHECK(cuda_samplerTextureDesc(&source, &actual));
  CHECK(actual.filterMode == CU_TR_FILTER_MODE_LINEAR &&
        actual.mipmapFilterMode == CU_TR_FILTER_MODE_LINEAR &&
        actual.maxAnisotropy == 16u);

  source.magFilter = GPU_FILTER_NEAREST;
  memset(&actual, 0xa5, sizeof(actual));
  CHECK(!cuda_samplerTextureDesc(&source, &actual) &&
        memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) == 0);
  source = dynamic_sampler();
  source.compareEnable = true;
  CHECK(!cuda_samplerTextureDesc(&source, &actual));
  source = dynamic_sampler();
  source.addressV = (GPUAddressMode)99;
  CHECK(!cuda_samplerTextureDesc(&source, &actual));
  source = dynamic_sampler();
  source.maxAnisotropy = 17u;
  CHECK(!cuda_samplerTextureDesc(&source, &actual));
  source = dynamic_sampler();
  source.maxAnisotropy = 8u;
  CHECK(!cuda_samplerTextureDesc(&source, &actual));
  CHECK(!cuda_samplerTextureDesc(NULL, &actual));
  CHECK(!cuda_samplerTextureDesc(&source, NULL));
  return 1;
}

static int
validate_static_sampler(void) {
  GPUStaticSamplerDesc source;
  CUDA_TEXTURE_DESC    actual;

  source = static_sampler();
  CHECK(cuda_staticSamplerTextureDesc(&source, &actual));
  CHECK(actual.addressMode[0] == CU_TR_ADDRESS_MODE_CLAMP &&
        actual.addressMode[1] == CU_TR_ADDRESS_MODE_CLAMP &&
        actual.addressMode[2] == CU_TR_ADDRESS_MODE_CLAMP &&
        actual.filterMode == CU_TR_FILTER_MODE_POINT &&
        actual.mipmapFilterMode == CU_TR_FILTER_MODE_POINT &&
        actual.flags == CU_TRSF_NORMALIZED_COORDINATES &&
        actual.maxAnisotropy == 1u);

  source.minFilter     = USL_RUNTIME_FILTER_LINEAR;
  source.magFilter     = USL_RUNTIME_FILTER_LINEAR;
  source.mipFilter     = USL_RUNTIME_FILTER_LINEAR;
  source.addressMode   = USL_RUNTIME_ADDRESS_REPEAT;
  source.maxAnisotropy = 8u;
  CHECK(cuda_staticSamplerTextureDesc(&source, &actual));
  CHECK(actual.addressMode[0] == CU_TR_ADDRESS_MODE_WRAP &&
        actual.filterMode == CU_TR_FILTER_MODE_LINEAR &&
        actual.mipmapFilterMode == CU_TR_FILTER_MODE_LINEAR &&
        actual.flags == CU_TRSF_NORMALIZED_COORDINATES &&
        actual.maxAnisotropy == 8u);

  source = static_sampler();
  source.coordSpace = USL_RUNTIME_COORD_PIXEL;
  CHECK(cuda_staticSamplerTextureDesc(&source, &actual));
  CHECK(actual.flags == 0u);
  source.addressMode = USL_RUNTIME_ADDRESS_REPEAT;
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual));

  source = static_sampler();
  source.addressMode = USL_RUNTIME_ADDRESS_CLAMP_TO_ZERO;
  CHECK(cuda_staticSamplerTextureDesc(&source, &actual));
  CHECK(actual.addressMode[0] == CU_TR_ADDRESS_MODE_BORDER);
  source.addressMode = USL_RUNTIME_ADDRESS_CLAMP_TO_BORDER;
  CHECK(cuda_staticSamplerTextureDesc(&source, &actual));
  CHECK(actual.addressMode[0] == CU_TR_ADDRESS_MODE_BORDER);

  source = static_sampler();
  source.hasCompare = 1u;
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual));
  source = static_sampler();
  source.magFilter = USL_RUNTIME_FILTER_LINEAR;
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual));
  source = static_sampler();
  source.maxAnisotropy = 17u;
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual));
  source = static_sampler();
  source.maxAnisotropy = 8u;
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual));
  source = static_sampler();
  source.addressMode = 99u;
  memset(&actual, 0xa5, sizeof(actual));
  CHECK(!cuda_staticSamplerTextureDesc(&source, &actual) &&
        memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) == 0);
  CHECK(!cuda_staticSamplerTextureDesc(NULL, &actual));
  CHECK(!cuda_staticSamplerTextureDesc(&source, NULL));
  return 1;
}

static int
validate_exact_sampler(void) {
  const CUDA_TEXTURE_DESC *desc;

  desc = cuda_exactTextureDesc();
  CHECK(desc && desc == cuda_exactTextureDesc() &&
        desc->addressMode[0] == CU_TR_ADDRESS_MODE_CLAMP &&
        desc->addressMode[1] == CU_TR_ADDRESS_MODE_CLAMP &&
        desc->addressMode[2] == CU_TR_ADDRESS_MODE_CLAMP &&
        desc->filterMode == CU_TR_FILTER_MODE_POINT &&
        desc->mipmapFilterMode == CU_TR_FILTER_MODE_POINT &&
        desc->flags == 0u && desc->maxAnisotropy == 1u);
  return 1;
}

int
main(void) {
  if (!validate_dynamic_sampler() || !validate_static_sampler() ||
      !validate_exact_sampler()) {
    return 1;
  }

  puts("CUDA sampler contract validation passed");
  return 0;
}

#undef CHECK
