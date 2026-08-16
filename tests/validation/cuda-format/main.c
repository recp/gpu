#include "backend/cuda/format.h"

#include <stdio.h>
#include <string.h>

#define FLOAT_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_FILTERABLE_BIT | \
   GPU_CUDA_FORMAT_STORAGE_BIT)

#define INTEGER_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_STORAGE_BIT | \
   GPU_CUDA_FORMAT_READ_AS_INTEGER_BIT)

#define SRGB_FLAGS \
  (GPU_CUDA_FORMAT_SAMPLED_BIT | GPU_CUDA_FORMAT_FILTERABLE_BIT | \
   GPU_CUDA_FORMAT_SRGB_BIT)

typedef struct ExpectedFormat {
  GPUFormat          format;
  CUarray_format     arrayFormat;
  GPUCudaFormatFlags flags;
  uint32_t           bytesPerTexel;
  uint32_t           channelCount;
} ExpectedFormat;

#define EXPECT(FORMAT, NATIVE, FLAGS, BYTES, CHANNELS) \
  {FORMAT, NATIVE, FLAGS, BYTES, CHANNELS}

static const ExpectedFormat expected[] = {
  EXPECT(GPU_FORMAT_R8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8,
         FLOAT_FLAGS, 1u, 1u),
  EXPECT(GPU_FORMAT_R8_SNORM, CU_AD_FORMAT_SIGNED_INT8,
         FLOAT_FLAGS, 1u, 1u),
  EXPECT(GPU_FORMAT_R8_UINT, CU_AD_FORMAT_UNSIGNED_INT8,
         INTEGER_FLAGS, 1u, 1u),
  EXPECT(GPU_FORMAT_R8_SINT, CU_AD_FORMAT_SIGNED_INT8,
         INTEGER_FLAGS, 1u, 1u),
  EXPECT(GPU_FORMAT_R16_UNORM, CU_AD_FORMAT_UNSIGNED_INT16,
         FLOAT_FLAGS, 2u, 1u),
  EXPECT(GPU_FORMAT_R16_SNORM, CU_AD_FORMAT_SIGNED_INT16,
         FLOAT_FLAGS, 2u, 1u),
  EXPECT(GPU_FORMAT_R16_UINT, CU_AD_FORMAT_UNSIGNED_INT16,
         INTEGER_FLAGS, 2u, 1u),
  EXPECT(GPU_FORMAT_R16_SINT, CU_AD_FORMAT_SIGNED_INT16,
         INTEGER_FLAGS, 2u, 1u),
  EXPECT(GPU_FORMAT_R16_FLOAT, CU_AD_FORMAT_HALF,
         FLOAT_FLAGS, 2u, 1u),
  EXPECT(GPU_FORMAT_RG8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8,
         FLOAT_FLAGS, 2u, 2u),
  EXPECT(GPU_FORMAT_RG8_SNORM, CU_AD_FORMAT_SIGNED_INT8,
         FLOAT_FLAGS, 2u, 2u),
  EXPECT(GPU_FORMAT_RG8_UINT, CU_AD_FORMAT_UNSIGNED_INT8,
         INTEGER_FLAGS, 2u, 2u),
  EXPECT(GPU_FORMAT_RG8_SINT, CU_AD_FORMAT_SIGNED_INT8,
         INTEGER_FLAGS, 2u, 2u),
  EXPECT(GPU_FORMAT_R32_UINT, CU_AD_FORMAT_UNSIGNED_INT32,
         INTEGER_FLAGS, 4u, 1u),
  EXPECT(GPU_FORMAT_R32_SINT, CU_AD_FORMAT_SIGNED_INT32,
         INTEGER_FLAGS, 4u, 1u),
  EXPECT(GPU_FORMAT_R32_FLOAT, CU_AD_FORMAT_FLOAT,
         FLOAT_FLAGS, 4u, 1u),
  EXPECT(GPU_FORMAT_RG16_UNORM, CU_AD_FORMAT_UNSIGNED_INT16,
         FLOAT_FLAGS, 4u, 2u),
  EXPECT(GPU_FORMAT_RG16_SNORM, CU_AD_FORMAT_SIGNED_INT16,
         FLOAT_FLAGS, 4u, 2u),
  EXPECT(GPU_FORMAT_RG16_UINT, CU_AD_FORMAT_UNSIGNED_INT16,
         INTEGER_FLAGS, 4u, 2u),
  EXPECT(GPU_FORMAT_RG16_SINT, CU_AD_FORMAT_SIGNED_INT16,
         INTEGER_FLAGS, 4u, 2u),
  EXPECT(GPU_FORMAT_RG16_FLOAT, CU_AD_FORMAT_HALF,
         FLOAT_FLAGS, 4u, 2u),
  EXPECT(GPU_FORMAT_RGBA8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8,
         FLOAT_FLAGS, 4u, 4u),
  EXPECT(GPU_FORMAT_RGBA8_UNORM_SRGB, CU_AD_FORMAT_UNSIGNED_INT8,
         SRGB_FLAGS, 4u, 4u),
  EXPECT(GPU_FORMAT_RGBA8_SNORM, CU_AD_FORMAT_SIGNED_INT8,
         FLOAT_FLAGS, 4u, 4u),
  EXPECT(GPU_FORMAT_RGBA8_UINT, CU_AD_FORMAT_UNSIGNED_INT8,
         INTEGER_FLAGS, 4u, 4u),
  EXPECT(GPU_FORMAT_RGBA8_SINT, CU_AD_FORMAT_SIGNED_INT8,
         INTEGER_FLAGS, 4u, 4u),
  EXPECT(GPU_FORMAT_RG32_UINT, CU_AD_FORMAT_UNSIGNED_INT32,
         INTEGER_FLAGS, 8u, 2u),
  EXPECT(GPU_FORMAT_RG32_SINT, CU_AD_FORMAT_SIGNED_INT32,
         INTEGER_FLAGS, 8u, 2u),
  EXPECT(GPU_FORMAT_RG32_FLOAT, CU_AD_FORMAT_FLOAT,
         FLOAT_FLAGS, 8u, 2u),
  EXPECT(GPU_FORMAT_RGBA16_UNORM, CU_AD_FORMAT_UNSIGNED_INT16,
         FLOAT_FLAGS, 8u, 4u),
  EXPECT(GPU_FORMAT_RGBA16_SNORM, CU_AD_FORMAT_SIGNED_INT16,
         FLOAT_FLAGS, 8u, 4u),
  EXPECT(GPU_FORMAT_RGBA16_UINT, CU_AD_FORMAT_UNSIGNED_INT16,
         INTEGER_FLAGS, 8u, 4u),
  EXPECT(GPU_FORMAT_RGBA16_SINT, CU_AD_FORMAT_SIGNED_INT16,
         INTEGER_FLAGS, 8u, 4u),
  EXPECT(GPU_FORMAT_RGBA16_FLOAT, CU_AD_FORMAT_HALF,
         FLOAT_FLAGS, 8u, 4u),
  EXPECT(GPU_FORMAT_RGBA32_UINT, CU_AD_FORMAT_UNSIGNED_INT32,
         INTEGER_FLAGS, 16u, 4u),
  EXPECT(GPU_FORMAT_RGBA32_SINT, CU_AD_FORMAT_SIGNED_INT32,
         INTEGER_FLAGS, 16u, 4u),
  EXPECT(GPU_FORMAT_RGBA32_FLOAT, CU_AD_FORMAT_FLOAT,
         FLOAT_FLAGS, 16u, 4u)
};

static int
matches(const GPUCudaFormatInfo *actual, const ExpectedFormat *item) {
  return actual->arrayFormat == item->arrayFormat &&
         actual->flags == item->flags &&
         actual->bytesPerTexel == item->bytesPerTexel &&
         actual->channelCount == item->channelCount;
}

static int
validate_texture_desc(void) {
  GPUCudaFormatInfo info;
  GPUCudaFormatInfo unsupported = {0};
  CUDA_TEXTURE_DESC source = {0};
  CUDA_TEXTURE_DESC actual;

  source.addressMode[0]   = CU_TR_ADDRESS_MODE_WRAP;
  source.filterMode       = CU_TR_FILTER_MODE_POINT;
  source.mipmapFilterMode = CU_TR_FILTER_MODE_POINT;
  source.flags            = CU_TRSF_NORMALIZED_COORDINATES |
                            CU_TRSF_SRGB;
  source.maxAnisotropy    = 1u;

  if (!cuda_formatInfo(GPU_FORMAT_RGBA8_UINT, &info) ||
      !cuda_formatTextureDesc(&info, &source, &actual) ||
      actual.flags != (CU_TRSF_NORMALIZED_COORDINATES |
                       CU_TRSF_READ_AS_INTEGER)) {
    return 0;
  }

  source.filterMode = CU_TR_FILTER_MODE_LINEAR;
  memset(&actual, 0xa5, sizeof(actual));
  if (cuda_formatTextureDesc(&info, &source, &actual) ||
      memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) != 0) {
    return 0;
  }

  source.filterMode = CU_TR_FILTER_MODE_LINEAR;
  source.flags      = CU_TRSF_NORMALIZED_COORDINATES |
                      CU_TRSF_READ_AS_INTEGER;
  if (!cuda_formatInfo(GPU_FORMAT_RGBA8_UNORM_SRGB, &info) ||
      !cuda_formatTextureDesc(&info, &source, &actual) ||
      actual.flags != (CU_TRSF_NORMALIZED_COORDINATES | CU_TRSF_SRGB)) {
    return 0;
  }

  source.flags = CU_TRSF_NORMALIZED_COORDINATES |
                 CU_TRSF_READ_AS_INTEGER |
                 CU_TRSF_SRGB;
  if (!cuda_formatInfo(GPU_FORMAT_RGBA32_FLOAT, &info) ||
      !cuda_formatTextureDesc(&info, &source, &actual) ||
      actual.flags != CU_TRSF_NORMALIZED_COORDINATES) {
    return 0;
  }
  memset(&actual, 0xa5, sizeof(actual));
  if (cuda_formatTextureDesc(NULL, &source, &actual) ||
      memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) != 0) {
    return 0;
  }
  memset(&actual, 0xa5, sizeof(actual));
  if (cuda_formatTextureDesc(&info, NULL, &actual) ||
      memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) != 0) {
    return 0;
  }
  memset(&actual, 0xa5, sizeof(actual));
  if (cuda_formatTextureDesc(&unsupported, &source, &actual) ||
      memcmp(&actual, &(CUDA_TEXTURE_DESC){0}, sizeof(actual)) != 0 ||
      cuda_formatTextureDesc(&info, &source, NULL)) {
    return 0;
  }
  return 1;
}

int
main(void) {
  GPUCudaFormatInfo info;
  uint32_t          supported;

  supported = 0u;
  for (GPUFormat format = GPU_FORMAT_UNDEFINED;
       format < GPU_FORMAT_COUNT;
       format++) {
    const ExpectedFormat *item;
    bool                  found;
    bool                  mapped;

    item  = NULL;
    found = false;
    for (uint32_t i = 0u; i < GPU_ARRAY_LEN(expected); i++) {
      if (expected[i].format == format) {
        item  = &expected[i];
        found = true;
        break;
      }
    }

    memset(&info, 0xa5, sizeof(info));
    mapped = cuda_formatInfo(format, &info);
    if (mapped != found || (mapped && !matches(&info, item)) ||
        (!mapped && memcmp(&info,
                           &(GPUCudaFormatInfo){0},
                           sizeof(info)) != 0)) {
      fprintf(stderr,
              "CUDA format contract mismatch at %u\n",
              (uint32_t)format);
      return 1;
    }
    supported += mapped ? 1u : 0u;
  }

  if (supported != GPU_ARRAY_LEN(expected) ||
      cuda_formatInfo(GPU_FORMAT_COUNT, &info) ||
      cuda_formatInfo(GPU_FORMAT_RGBA32_FLOAT, NULL) ||
      !validate_texture_desc()) {
    fprintf(stderr, "CUDA format contract boundary mismatch\n");
    return 1;
  }

  puts("CUDA format contract validation passed");
  return 0;
}

#undef EXPECT
#undef SRGB_FLAGS
#undef INTEGER_FLAGS
#undef FLOAT_FLAGS
