#include <gpu/gpu.h>
#include "../../../src/api/device_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  ValueCount                    = 512u,
  RoundtripCount                = 4u,
  WarmTextureRoundtripCount     = 4u,
  TextureBaseWidth              = 16u,
  TextureBaseHeight             = 16u,
  TextureWidth                  = 8u,
  TextureHeight                 = 8u,
  TextureLayers                 = 2u,
  CubeLayers                    = 6u,
  CubeArrayLayers               = 12u,
  TextureMipLevel               = 1u,
  TextureMipCount               = 4u,
  FilterMipWidth                = TextureWidth / 2u,
  FilterMipHeight               = TextureHeight / 2u,
  TextureTexelCount             = TextureWidth * TextureHeight * TextureLayers,
  TextureValueCount             = TextureTexelCount * 4u,
  FilterMipValueCount           = FilterMipWidth * FilterMipHeight *
                                  TextureLayers * 4u,
  CubeFaceValueCount            = TextureWidth * TextureHeight * 4u,
  CubeValueCount                = CubeFaceValueCount * CubeLayers,
  CubeOutputValueCount          = CubeLayers * 4u,
  CubeArrayValueCount           = CubeFaceValueCount * CubeArrayLayers,
  CubeArrayOutputValueCount     = CubeArrayLayers * 4u,
  HalfTextureValueCount         = TextureValueCount,
  HalfFloatOutputValueCount     = HalfTextureValueCount * 2u,
  ByteTextureValueCount         = TextureValueCount,
  ByteFloatOutputValueCount     = ByteTextureValueCount * 2u,
  WordTextureValueCount         = TextureValueCount,
  WordFloatOutputValueCount     = WordTextureValueCount * 2u,
  RgbaFormatCount               = 11u,
  NarrowFormatCount             = 24u,
  FormatCaseCount               = RgbaFormatCount + NarrowFormatCount,
  FormatFloatOutputValueCount   = TextureValueCount * 2u * FormatCaseCount,
  SrgbFloatOutputValueCount     = TextureValueCount,
  PackedFloatOutputValueCount   = TextureValueCount * 2u,
  BgraFloatOutputValueCount     = TextureValueCount * 2u,
  DepthFloatOutputValueCount    = TextureValueCount * 2u,
  RgbaColorFilterCount          = 7u,
  NarrowColorFilterCount        = 12u,
  ColorFilterCaseCount          = RgbaColorFilterCount +
                                  NarrowColorFilterCount,
  ColorFilterOutputValueCount   = TextureValueCount * ColorFilterCaseCount,
  SamplerModeCount              = 5u,
  SamplerOutputValueCount       = TextureValueCount * SamplerModeCount,
  NarrowRBytesPerTexel          = 4u * 1u + 5u * 2u + 3u * 4u,
  NarrowRgBytesPerTexel         = NarrowRBytesPerTexel * 2u,
  NarrowRawByteCount            = TextureTexelCount *
                                  (NarrowRBytesPerTexel +
                                   NarrowRgBytesPerTexel),
  TextureOutputValueCount       = TextureValueCount * 2u +
                                  CubeOutputValueCount +
                                  CubeArrayOutputValueCount +
                                  FormatFloatOutputValueCount +
                                  SrgbFloatOutputValueCount +
                                  PackedFloatOutputValueCount +
                                  BgraFloatOutputValueCount +
                                  DepthFloatOutputValueCount +
                                  ColorFilterOutputValueCount +
                                  SamplerOutputValueCount
};

typedef enum InteropFormatCase {
  InteropFormatHalf,
  InteropFormatUnorm8,
  InteropFormatSnorm8,
  InteropFormatUint8,
  InteropFormatSint8,
  InteropFormatUnorm16,
  InteropFormatSnorm16,
  InteropFormatUint16,
  InteropFormatSint16,
  InteropFormatUint32,
  InteropFormatSint32,
  InteropFormatR8Unorm,
  InteropFormatR8Snorm,
  InteropFormatR8Uint,
  InteropFormatR8Sint,
  InteropFormatR16Unorm,
  InteropFormatR16Snorm,
  InteropFormatR16Uint,
  InteropFormatR16Sint,
  InteropFormatR16Float,
  InteropFormatR32Uint,
  InteropFormatR32Sint,
  InteropFormatR32Float,
  InteropFormatRg8Unorm,
  InteropFormatRg8Snorm,
  InteropFormatRg8Uint,
  InteropFormatRg8Sint,
  InteropFormatRg16Unorm,
  InteropFormatRg16Snorm,
  InteropFormatRg16Uint,
  InteropFormatRg16Sint,
  InteropFormatRg16Float,
  InteropFormatRg32Uint,
  InteropFormatRg32Sint,
  InteropFormatRg32Float,
  InteropFormatCount
} InteropFormatCase;

typedef enum InteropValueKind {
  InteropValueUnorm,
  InteropValueSnorm,
  InteropValueUint,
  InteropValueSint,
  InteropValueFloat
} InteropValueKind;

typedef struct Params {
  float scale;
  float bias;
} Params;

typedef struct AdapterList {
  GPUAdapter **items;
  uint32_t     count;
} AdapterList;

typedef struct InteropFormatTexture {
  GPUTexture     *graphicsTexture;
  GPUTexture     *cudaTexture;
  GPUTextureView *storageView;
  GPUTextureView *sampledView;
  GPUBuffer      *readback;
} InteropFormatTexture;

typedef struct InteropFormatTransfer {
  const void *input;
  void       *output;
  uint64_t    size;
  uint32_t    bytesPerRow;
} InteropFormatTransfer;

typedef struct InteropNarrowFormat {
  const char        *label;
  GPUFormat          format;
  InteropFormatCase  formatCase;
  InteropValueKind   valueKind;
  uint8_t            channelCount;
  uint8_t            componentBytes;
} InteropNarrowFormat;

static const InteropNarrowFormat narrowFormats[NarrowFormatCount] = {
  {"r8-unorm-interop", GPU_FORMAT_R8_UNORM,
   InteropFormatR8Unorm, InteropValueUnorm, 1u, 1u},
  {"r8-snorm-interop", GPU_FORMAT_R8_SNORM,
   InteropFormatR8Snorm, InteropValueSnorm, 1u, 1u},
  {"r8-uint-interop", GPU_FORMAT_R8_UINT,
   InteropFormatR8Uint, InteropValueUint, 1u, 1u},
  {"r8-sint-interop", GPU_FORMAT_R8_SINT,
   InteropFormatR8Sint, InteropValueSint, 1u, 1u},
  {"r16-unorm-interop", GPU_FORMAT_R16_UNORM,
   InteropFormatR16Unorm, InteropValueUnorm, 1u, 2u},
  {"r16-snorm-interop", GPU_FORMAT_R16_SNORM,
   InteropFormatR16Snorm, InteropValueSnorm, 1u, 2u},
  {"r16-uint-interop", GPU_FORMAT_R16_UINT,
   InteropFormatR16Uint, InteropValueUint, 1u, 2u},
  {"r16-sint-interop", GPU_FORMAT_R16_SINT,
   InteropFormatR16Sint, InteropValueSint, 1u, 2u},
  {"r16f-interop", GPU_FORMAT_R16_FLOAT,
   InteropFormatR16Float, InteropValueFloat, 1u, 2u},
  {"r32-uint-interop", GPU_FORMAT_R32_UINT,
   InteropFormatR32Uint, InteropValueUint, 1u, 4u},
  {"r32-sint-interop", GPU_FORMAT_R32_SINT,
   InteropFormatR32Sint, InteropValueSint, 1u, 4u},
  {"r32f-interop", GPU_FORMAT_R32_FLOAT,
   InteropFormatR32Float, InteropValueFloat, 1u, 4u},
  {"rg8-unorm-interop", GPU_FORMAT_RG8_UNORM,
   InteropFormatRg8Unorm, InteropValueUnorm, 2u, 1u},
  {"rg8-snorm-interop", GPU_FORMAT_RG8_SNORM,
   InteropFormatRg8Snorm, InteropValueSnorm, 2u, 1u},
  {"rg8-uint-interop", GPU_FORMAT_RG8_UINT,
   InteropFormatRg8Uint, InteropValueUint, 2u, 1u},
  {"rg8-sint-interop", GPU_FORMAT_RG8_SINT,
   InteropFormatRg8Sint, InteropValueSint, 2u, 1u},
  {"rg16-unorm-interop", GPU_FORMAT_RG16_UNORM,
   InteropFormatRg16Unorm, InteropValueUnorm, 2u, 2u},
  {"rg16-snorm-interop", GPU_FORMAT_RG16_SNORM,
   InteropFormatRg16Snorm, InteropValueSnorm, 2u, 2u},
  {"rg16-uint-interop", GPU_FORMAT_RG16_UINT,
   InteropFormatRg16Uint, InteropValueUint, 2u, 2u},
  {"rg16-sint-interop", GPU_FORMAT_RG16_SINT,
   InteropFormatRg16Sint, InteropValueSint, 2u, 2u},
  {"rg16f-interop", GPU_FORMAT_RG16_FLOAT,
   InteropFormatRg16Float, InteropValueFloat, 2u, 2u},
  {"rg32-uint-interop", GPU_FORMAT_RG32_UINT,
   InteropFormatRg32Uint, InteropValueUint, 2u, 4u},
  {"rg32-sint-interop", GPU_FORMAT_RG32_SINT,
   InteropFormatRg32Sint, InteropValueSint, 2u, 4u},
  {"rg32f-interop", GPU_FORMAT_RG32_FLOAT,
   InteropFormatRg32Float, InteropValueFloat, 2u, 4u}
};

static const InteropFormatCase narrowFilterFormats[NarrowColorFilterCount] = {
  InteropFormatR8Unorm,
  InteropFormatR8Snorm,
  InteropFormatR16Unorm,
  InteropFormatR16Snorm,
  InteropFormatR16Float,
  InteropFormatR32Float,
  InteropFormatRg8Unorm,
  InteropFormatRg8Snorm,
  InteropFormatRg16Unorm,
  InteropFormatRg16Snorm,
  InteropFormatRg16Float,
  InteropFormatRg32Float
};

_Static_assert((uint32_t)InteropFormatCount == FormatCaseCount,
               "interop format count must match output layout");
_Static_assert((uint32_t)InteropFormatR8Unorm == RgbaFormatCount,
               "narrow interop formats must follow RGBA formats");

typedef struct RoundtripState {
  GPUDeviceInteropEXT *interop;
  GPUDevice           *graphicsDevice;
  GPUDevice           *cudaDevice;
  GPUQueue            *graphicsQueue;
  GPUQueue            *cudaQueue;
  GPUBuffer           *graphicsBuffer;
  GPUBuffer           *cudaBuffer;
  GPUBuffer           *paramsBuffer;
  GPUBuffer           *textureReadback;
  GPUBuffer           *textureCudaReadback;
  GPUBuffer           *srgbReadback;
  GPUBuffer           *packedReadback;
  GPUBuffer           *bgraReadback;
  GPUBuffer           *depth32Readback;
  GPUBuffer           *depth16Readback;
  GPUTexture          *graphicsTexture;
  GPUTexture          *cudaTexture;
  GPUTexture          *graphicsCubeTexture;
  GPUTexture          *cudaCubeTexture;
  GPUTexture          *graphicsCubeArrayTexture;
  GPUTexture          *cudaCubeArrayTexture;
  GPUTexture          *graphicsSrgbTexture;
  GPUTexture          *cudaSrgbTexture;
  GPUTexture          *graphicsPackedTexture;
  GPUTexture          *cudaPackedTexture;
  GPUTexture          *graphicsBgraTexture;
  GPUTexture          *cudaBgraTexture;
  GPUTexture          *graphicsDepth32Texture;
  GPUTexture          *cudaDepth32Texture;
  GPUTexture          *graphicsDepth16Texture;
  GPUTexture          *cudaDepth16Texture;
  GPUTextureView      *cudaTextureView;
  GPUTextureView      *cudaSampledTextureView;
  GPUTextureView      *cudaCubeTextureView;
  GPUTextureView      *cudaCubeArrayTextureView;
  GPUTextureView      *cudaSrgbTextureView;
  GPUTextureView      *cudaPackedTextureView;
  GPUTextureView      *cudaBgraTextureView;
  GPUTextureView      *cudaDepth32TextureView;
  GPUTextureView      *cudaDepth16TextureView;
  GPUSemaphore        *graphicsSemaphore;
  GPUSemaphore        *cudaSemaphore;
  GPUFence            *releaseFence;
  GPUFence            *acquireFence;
  GPUComputePipeline  *pipeline;
  GPUComputePipeline  *textureSamplePipeline;
  GPUComputePipeline  *textureStorePipeline;
  GPUComputePipeline  *narrowSamplePipeline;
  GPUComputePipeline  *narrowStorePipeline;
  GPUComputePipeline  *colorFilterPipeline;
  GPUComputePipeline  *narrowFilterPipeline;
  GPUBindGroup        *paramsGroup;
  GPUBindGroup        *dataGroup;
  GPUBindGroup        *textureGroup;
  GPUBindGroup        *narrowGroup;
  uint32_t             textureMipLevel;
  bool                 depth16Shared;
  bool                 mipFilterShared;
  InteropFormatTexture formatTextures[InteropFormatCount];
} RoundtripState;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userdata) {
  (void)device;
  (void)userdata;
  fprintf(stderr,
          "CUDA interop device error: %s\n",
          error && error->message ? error->message : "unknown error");
}

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static GPUResult
enumerate_adapters(GPUInstance *instance, AdapterList *outList) {
  GPUResult result;
  uint32_t  count;

  if (!instance || !outList) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  outList->items = NULL;
  outList->count = 0u;
  count          = 0u;
  result = GPUEnumerateAdapters(instance, &count, NULL);
  if (result != GPU_OK || count == 0u) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }

  outList->items = calloc(count, sizeof(*outList->items));
  if (!outList->items) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  outList->count = count;
  result = GPUEnumerateAdapters(instance, &count, outList->items);
  if (result != GPU_OK || count != outList->count) {
    free(outList->items);
    outList->items = NULL;
    outList->count = 0u;
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static int
find_matching_adapters(const AdapterList *graphicsAdapters,
                       const AdapterList *cudaAdapters,
                       GPUAdapter       **outGraphicsAdapter,
                       GPUAdapter       **outCudaAdapter) {
  for (uint32_t graphicsIndex = 0u;
       graphicsIndex < graphicsAdapters->count;
       graphicsIndex++) {
    for (uint32_t cudaIndex = 0u;
         cudaIndex < cudaAdapters->count;
         cudaIndex++) {
      bool sameDevice;

      sameDevice = false;
      if (GPUAdaptersSharePhysicalDevice(
            graphicsAdapters->items[graphicsIndex],
            cudaAdapters->items[cudaIndex],
            &sameDevice
          ) == GPU_OK && sameDevice) {
        *outGraphicsAdapter = graphicsAdapters->items[graphicsIndex];
        *outCudaAdapter     = cudaAdapters->items[cudaIndex];
        return 1;
      }
    }
  }
  return 0;
}

static int
create_interop_format_texture(RoundtripState            *state,
                              InteropFormatCase          formatCase,
                              const GPUTextureCreateInfo *graphicsTemplate,
                              GPUFormat                  format,
                              const char                *label,
                              uint64_t                   readbackSize,
                              bool                       cudaFirst) {
  InteropFormatTexture     *texture;
  GPUTextureCreateInfo      graphicsInfo, cudaInfo;
  GPUTextureViewCreateInfo  viewInfo = {0};
  GPUBufferCreateInfo       bufferInfo = {0};
  GPUMemoryRequirements     requirements;
  GPUResult                 requirementsResult, createResult;

  if (!state || formatCase >= InteropFormatCount || !graphicsTemplate ||
      !label || readbackSize == 0u) {
    return 0;
  }
  texture                 = &state->formatTextures[formatCase];
  graphicsInfo            = *graphicsTemplate;
  graphicsInfo.label      = label;
  graphicsInfo.format     = format;
  cudaInfo                = graphicsInfo;
  cudaInfo.usage          = GPU_TEXTURE_USAGE_SAMPLED |
                            GPU_TEXTURE_USAGE_STORAGE;
  requirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state->interop,
    cudaFirst ? &cudaInfo : &graphicsInfo,
    cudaFirst ? &graphicsInfo : &cudaInfo,
    &requirements
  );
  createResult = requirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state->interop,
                                cudaFirst ? &cudaInfo : &graphicsInfo,
                                cudaFirst ? &graphicsInfo : &cudaInfo,
                                cudaFirst
                                  ? &texture->cudaTexture
                                  : &texture->graphicsTexture,
                                cudaFirst
                                  ? &texture->graphicsTexture
                                  : &texture->cudaTexture)
    : requirementsResult;
  if (requirementsResult != GPU_OK || requirements.sizeBytes == 0u ||
      createResult != GPU_OK || !texture->graphicsTexture ||
      !texture->cudaTexture) {
    fprintf(stderr,
            "shared graphics/CUDA %s creation failed (%d, %d)\n",
            label,
            requirementsResult,
            createResult);
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = format;
  viewInfo.baseMipLevel     = state->textureMipLevel;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = TextureLayers;
  if (GPUCreateTextureView(texture->cudaTexture,
                           &viewInfo,
                           &texture->storageView) != GPU_OK ||
      !texture->storageView) {
    fprintf(stderr, "shared graphics/CUDA %s storage view failed\n", label);
    return 0;
  }
  viewInfo.mipLevelCount = graphicsInfo.mipLevelCount -
                           state->textureMipLevel;
  if (GPUCreateTextureView(texture->cudaTexture,
                           &viewInfo,
                           &texture->sampledView) != GPU_OK ||
      !texture->sampledView) {
    fprintf(stderr, "shared graphics/CUDA %s sampled view failed\n", label);
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = label;
  bufferInfo.sizeBytes        = readbackSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(state->graphicsDevice,
                      &bufferInfo,
                      &texture->readback) != GPU_OK ||
      !texture->readback) {
    fprintf(stderr, "shared graphics/CUDA %s readback failed\n", label);
    return 0;
  }
  return 1;
}

static void
narrow_reference(const InteropNarrowFormat *format,
                 uint32_t                   pattern,
                 uint32_t                  *outInputBits,
                 uint32_t                  *outOutputBits,
                 float                     *outInputValue,
                 float                     *outOutputValue) {
  static const uint8_t  unorm8[8] = {
    0u, 16u, 32u, 64u, 128u, 192u, 240u, 255u
  };
  static const uint16_t unorm16[8] = {
    0u, 4096u, 8192u, 16384u, 32768u, 49152u, 61440u, 65535u
  };
  static const int8_t   snorm8[8] = {
    -120, -64, -32, -1, 0, 1, 64, 120
  };
  static const int16_t  snorm16[8] = {
    -30000, -16384, -8192, -1, 0, 1, 16384, 30000
  };
  static const uint8_t  uint8Values[8] = {
    0u, 1u, 2u, 3u, 120u, 200u, 250u, 253u
  };
  static const uint16_t uint16Values[8] = {
    0u, 1u, 2u, 3u, 1000u, 60000u, 65000u, 65530u
  };
  static const uint32_t uint32Values[8] = {
    0u, 1u, 2u, 3u, 1000u, 65535u, 1048576u, 16777214u
  };
  static const int32_t  sintValues[8] = {
    -1000000, -1000, -10, -1, 0, 1, 1000, 1000000
  };
  static const uint16_t float16Bits[8] = {
    0x0000u, 0x3c00u, 0xc000u, 0x3800u,
    0x4200u, 0xbc00u, 0x4400u, 0x3400u
  };
  static const uint16_t float16OutputBits[8] = {
    0x3c00u, 0x4200u, 0xc200u, 0x4000u,
    0x4700u, 0xbc00u, 0x4880u, 0x3e00u
  };
  static const float    floatValues[8] = {
    0.0f, 1.0f, -2.0f, 0.5f, 3.0f, -1.0f, 4.0f, 0.25f
  };
  uint32_t inputBits, outputBits, maxValue;
  int32_t  signedInput, signedOutput;
  float    inputValue, outputValue;

  pattern %= 8u;
  inputBits   = 0u;
  outputBits  = 0u;
  inputValue  = 0.0f;
  outputValue = 0.0f;
  switch (format->valueKind) {
    case InteropValueUnorm:
      maxValue = format->componentBytes == 1u ? 255u : 65535u;
      inputBits = format->componentBytes == 1u
        ? (uint32_t)unorm8[pattern]
        : (uint32_t)unorm16[pattern];
      outputBits  = maxValue - inputBits;
      inputValue  = (float)inputBits / (float)maxValue;
      outputValue = (float)outputBits / (float)maxValue;
      break;
    case InteropValueSnorm:
      signedInput = format->componentBytes == 1u
        ? (int32_t)snorm8[pattern]
        : (int32_t)snorm16[pattern];
      signedOutput = -signedInput;
      inputBits    = (uint32_t)signedInput;
      outputBits   = (uint32_t)signedOutput;
      maxValue     = format->componentBytes == 1u ? 127u : 32767u;
      inputValue   = (float)signedInput / (float)maxValue;
      outputValue  = (float)signedOutput / (float)maxValue;
      break;
    case InteropValueUint:
      inputBits = format->componentBytes == 1u
        ? (uint32_t)uint8Values[pattern]
        : format->componentBytes == 2u
          ? (uint32_t)uint16Values[pattern]
          : uint32Values[pattern];
      outputBits  = inputBits + 1u;
      inputValue  = (float)inputBits;
      outputValue = (float)outputBits;
      break;
    case InteropValueSint:
      signedInput = sintValues[pattern];
      if (format->componentBytes == 1u) {
        signedInput = snorm8[pattern];
      } else if (format->componentBytes == 2u) {
        signedInput = snorm16[pattern];
      }
      signedOutput = signedInput + 7;
      inputBits    = (uint32_t)signedInput;
      outputBits   = (uint32_t)signedOutput;
      inputValue   = (float)signedInput;
      outputValue  = (float)signedOutput;
      break;
    case InteropValueFloat:
      inputValue  = floatValues[pattern];
      outputValue = inputValue * 2.0f + 1.0f;
      if (format->componentBytes == 2u) {
        inputBits  = float16Bits[pattern];
        outputBits = float16OutputBits[pattern];
      } else {
        memcpy(&inputBits, &inputValue, sizeof(inputBits));
        memcpy(&outputBits, &outputValue, sizeof(outputBits));
      }
      break;
  }
  *outInputBits   = inputBits;
  *outOutputBits  = outputBits;
  *outInputValue  = inputValue;
  *outOutputValue = outputValue;
}

static float
srgb_to_linear(uint8_t value) {
  float encoded;

  encoded = (float)value / 255.0f;
  return encoded <= 0.04045f
    ? encoded / 12.92f
    : powf((encoded + 0.055f) / 1.055f, 2.4f);
}

static uint32_t
format_value_pattern(uint32_t valueIndex) {
  return (valueIndex / 4u + valueIndex % 4u) % 8u;
}

static int
values_match(const float values[ValueCount]) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * 2.0f + 1.0f;

    if (fabsf(values[i] - expected) > 0.0001f) {
      return 0;
    }
  }
  return 1;
}

static int
run_roundtrip(RoundtripState *state, uint32_t iteration) {
  GPUCommandBuffer          *releaseCmdb, *cudaCmdb, *acquireCmdb;
  GPUComputePassEncoder     *pass;
  GPUSharedBufferBarrierEXT  toCuda = {0}, toGraphics = {0};
  GPUSharedBarrierBatchEXT   acquireCuda = {0}, acquireGraphics = {0};
  GPUQueueSemaphoreWait      wait = {0};
  GPUQueueSemaphoreSignal    signal = {0};
  GPUQueueSubmitExInfo       submit = {0};
  float                      values[ValueCount];
  uint64_t                   cudaValue, graphicsValue;
  uint32_t                   dynamicOffset;
  int                        releaseSubmitted, cudaSubmitted, ok;

  releaseCmdb      = NULL;
  cudaCmdb         = NULL;
  acquireCmdb      = NULL;
  pass             = NULL;
  releaseSubmitted = 0;
  cudaSubmitted    = 0;
  ok               = 0;
  cudaValue        = (uint64_t)iteration * 2u + 1u;
  graphicsValue    = cudaValue + 1u;
  for (uint32_t i = 0u; i < ValueCount; i++) {
    values[i] = (float)i;
  }

  if (GPUQueueWriteBuffer(state->graphicsQueue,
                          state->graphicsBuffer,
                          0u,
                          values,
                          sizeof(values)) != GPU_OK ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-release",
                              &releaseCmdb) != GPU_OK ||
      !releaseCmdb) {
    goto cleanup;
  }

  toCuda.sourceBuffer      = state->graphicsBuffer;
  toCuda.destinationBuffer = state->cudaBuffer;
  toCuda.sizeBytes         = sizeof(values);
  toCuda.srcAccess         = GPU_ACCESS_TRANSFER_WRITE;
  toCuda.dstAccess         = GPU_ACCESS_SHADER_WRITE;
  acquireCuda.pBufferBarriers    = &toCuda;
  acquireCuda.srcStages          = GPU_STAGE_TRANSFER;
  acquireCuda.dstStages          = GPU_STAGE_COMPUTE;
  acquireCuda.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                releaseCmdb,
                                &acquireCuda) != GPU_OK) {
    goto cleanup;
  }

  signal.semaphore          = state->graphicsSemaphore;
  signal.value              = cudaValue;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &releaseCmdb;
  submit.pSignals           = &signal;
  submit.fence              = state->releaseFence;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  releaseCmdb      = NULL;
  releaseSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->cudaQueue,
                              "cuda-graphics-roundtrip",
                              &cudaCmdb) != GPU_OK ||
      !cudaCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                cudaCmdb,
                                &acquireCuda) != GPU_OK ||
      !(pass = GPUBeginComputePass(cudaCmdb, "cuda-saxpy"))) {
    goto cleanup;
  }
  GPUBindComputePipeline(pass, state->pipeline);
  dynamicOffset = 0u;
  GPUBindComputeGroup(pass, 0u, state->paramsGroup, 1u, &dynamicOffset);
  GPUBindComputeGroup(pass, 1u, state->dataGroup, 0u, NULL);
  GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  toGraphics.sourceBuffer      = state->cudaBuffer;
  toGraphics.destinationBuffer = state->graphicsBuffer;
  toGraphics.sizeBytes         = sizeof(values);
  toGraphics.srcAccess         = GPU_ACCESS_SHADER_WRITE;
  toGraphics.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  acquireGraphics.pBufferBarriers    = &toGraphics;
  acquireGraphics.srcStages          = GPU_STAGE_COMPUTE;
  acquireGraphics.dstStages          = GPU_STAGE_TRANSFER;
  acquireGraphics.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                cudaCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->cudaSemaphore;
  wait.value                = cudaValue;
  wait.waitStages           = GPU_STAGE_COMPUTE;
  signal.semaphore          = state->cudaSemaphore;
  signal.value              = graphicsValue;
  submit.ppCommandBuffers   = &cudaCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = &signal;
  submit.fence              = NULL;
  submit.waitCount          = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->cudaQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  cudaCmdb      = NULL;
  cudaSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-acquire",
                              &acquireCmdb) != GPU_OK ||
      !acquireCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                acquireCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->graphicsSemaphore;
  wait.value                = graphicsValue;
  wait.waitStages           = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers   = &acquireCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = NULL;
  submit.fence              = state->acquireFence;
  submit.signalCount        = 0u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  acquireCmdb = NULL;
  if (GPUWaitFence(state->acquireFence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(state->graphicsQueue,
                         state->graphicsBuffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK ||
      !values_match(values)) {
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (acquireCmdb) {
    (void)GPUDiscardCommandBuffer(acquireCmdb);
  }
  if (cudaCmdb) {
    (void)GPUDiscardCommandBuffer(cudaCmdb);
  }
  if (releaseCmdb) {
    (void)GPUDiscardCommandBuffer(releaseCmdb);
  }
  if (!ok && cudaSubmitted) {
    (void)GPUQueueReadBuffer(state->cudaQueue,
                             state->cudaBuffer,
                             0u,
                             values,
                             sizeof(values));
  }
  if (!ok && releaseSubmitted) {
    (void)GPUWaitFence(state->releaseFence, UINT64_MAX);
  }
  GPUResetFence(state->acquireFence);
  GPUResetFence(state->releaseFence);
  return ok;
}

static int
run_texture_roundtrip(RoundtripState *state, uint32_t sequence) {
  GPUCommandBuffer           *releaseCmdb, *cudaCmdb, *acquireCmdb;
  GPUComputePassEncoder      *computePass;
  GPUTransferPassEncoder     *transferPass;
  GPUSharedTextureBarrierEXT  toCuda[8 + InteropFormatCount] = {0};
  GPUSharedTextureBarrierEXT  toGraphics[8 + InteropFormatCount] = {0};
  GPUSharedBarrierBatchEXT    acquireCuda = {0}, acquireGraphics = {0};
  GPUBufferTextureCopyRegion  copyRegion = {0};
  GPUTextureWriteRegion       writeRegion = {0};
  GPUQueueSemaphoreWait       wait = {0};
  GPUQueueSemaphoreSignal     signal = {0};
  GPUQueueSubmitExInfo        submit = {0};
  GPUFence                   *cudaFence;
  float                       input[TextureValueCount];
  float                       filterMipInput[FilterMipValueCount];
  float                       cubeInput[CubeValueCount];
  float                       cubeArrayInput[CubeArrayValueCount];
  uint16_t                    halfInput[HalfTextureValueCount];
  uint16_t                    halfOutput[HalfTextureValueCount];
  uint8_t                     unorm8Input[ByteTextureValueCount];
  uint8_t                     unorm8Output[ByteTextureValueCount];
  int8_t                      snorm8Input[ByteTextureValueCount];
  int8_t                      snorm8Output[ByteTextureValueCount];
  uint8_t                     uint8Input[ByteTextureValueCount];
  uint8_t                     uint8Output[ByteTextureValueCount];
  int8_t                      sint8Input[ByteTextureValueCount];
  int8_t                      sint8Output[ByteTextureValueCount];
  uint16_t                    unorm16Input[WordTextureValueCount];
  uint16_t                    unorm16Output[WordTextureValueCount];
  int16_t                     snorm16Input[WordTextureValueCount];
  int16_t                     snorm16Output[WordTextureValueCount];
  uint16_t                    uint16Input[WordTextureValueCount];
  uint16_t                    uint16Output[WordTextureValueCount];
  int16_t                     sint16Input[WordTextureValueCount];
  int16_t                     sint16Output[WordTextureValueCount];
  uint32_t                    uint32Input[TextureValueCount];
  uint32_t                    uint32Output[TextureValueCount];
  int32_t                     sint32Input[TextureValueCount];
  int32_t                     sint32Output[TextureValueCount];
  uint8_t                     srgbInput[ByteTextureValueCount];
  uint8_t                     srgbOutput[ByteTextureValueCount];
  uint32_t                    packedRawInput[TextureTexelCount];
  uint32_t                    packedRawOutput[TextureTexelCount];
  float                       packedWriteInput[TextureValueCount];
  uint8_t                     bgraInput[ByteTextureValueCount];
  uint8_t                     bgraOutput[ByteTextureValueCount];
  float                       depth32Input[TextureTexelCount];
  float                       depth32Output[TextureTexelCount];
  uint16_t                    depth16Input[TextureTexelCount];
  uint16_t                    depth16Output[TextureTexelCount];
  uint8_t                     narrowInput[NarrowRawByteCount];
  uint8_t                     narrowOutput[NarrowRawByteCount];
  InteropFormatTransfer       formatTransfers[InteropFormatCount];
  float                       output[TextureValueCount];
  float                       cudaOutput[TextureOutputValueCount];
  const char                 *failure;
  uint32_t                    halfBase, unorm8Base, snorm8Base;
  uint32_t                    uint8Base, sint8Base, unorm16Base;
  uint32_t                    snorm16Base, uint16Base, sint16Base;
  uint32_t                    uint32Base, sint32Base, srgbBase;
  uint32_t                    packedReadBase, packedInputBase;
  uint32_t                    bgraBase;
  uint32_t                    depth32Base, depth16Base;
  uint32_t                    filterBase, samplerBase;
  uint32_t                    narrowOffset;
  uint32_t                    fixedTextureCount;
  int                         releaseSubmitted, cudaSubmitted;
  int                         acquireSubmitted, ok;
  static const uint16_t       halfInputBits[8] = {
    0x0000u, 0x3800u, 0x3c00u, 0xb800u,
    0x4000u, 0xbc00u, 0x3400u, 0xb400u
  };
  static const uint16_t       halfOutputBits[8] = {
    0x3c00u, 0x4000u, 0x4200u, 0x0000u,
    0x4500u, 0xbc00u, 0x3e00u, 0x3800u
  };
  static const float          halfInputValues[8] = {
    0.0f, 0.5f, 1.0f, -0.5f,
    2.0f, -1.0f, 0.25f, -0.25f
  };
  static const float          halfOutputValues[8] = {
    1.0f, 2.0f, 3.0f, 0.0f,
    5.0f, -1.0f, 1.5f, 0.5f
  };
  static const uint8_t        unorm8InputValues[8] = {
    0u, 64u, 128u, 255u, 16u, 32u, 192u, 240u
  };
  static const int8_t         snorm8InputValues[8] = {
    -120, -64, -1, 0, 1, 32, 64, 120
  };
  static const uint8_t        uint8InputValues[8] = {
    0u, 1u, 2u, 3u, 250u, 251u, 252u, 253u
  };
  static const int8_t         sint8InputValues[8] = {
    -10, -1, 0, 1, 10, 20, 30, 100
  };
  static const uint16_t       unorm16InputValues[8] = {
    0u, 16384u, 32768u, 65535u, 4096u, 8192u, 49152u, 61440u
  };
  static const int16_t        snorm16InputValues[8] = {
    -30000, -16384, -1, 0, 1, 8192, 16384, 30000
  };
  static const uint16_t       uint16InputValues[8] = {
    0u, 1u, 2u, 3u, 65000u, 65001u, 65002u, 65003u
  };
  static const int16_t        sint16InputValues[8] = {
    -30000, -1, 0, 1, 100, 1000, 20000, 30000
  };
  static const uint32_t       uint32InputValues[8] = {
    0u, 1u, 2u, 3u, 1000u, 65535u, 1048576u, 16777214u
  };
  static const int32_t        sint32InputValues[8] = {
    -1000000, -1000, -10, -1, 0, 1, 1000, 1000000
  };
  static const uint8_t        srgbInputValues[8] = {
    0u, 1u, 10u, 11u, 64u, 128u, 192u, 255u
  };
  static const uint32_t       packedRawValues[4] = {
    UINT32_C(0xffc00000), UINT32_C(0xf83e07c0),
    UINT32_C(0xf7fdffbf), UINT32_C(0x00400801)
  };
  static const uint32_t       packedWriteBits[16] = {
    UINT32_C(0xbf800000), UINT32_C(0xff800000),
    UINT32_C(0x7fc00000), UINT32_C(0x3f800000),
    UINT32_C(0x7f800000), UINT32_C(0x7f800000),
    UINT32_C(0x7f800000), UINT32_C(0x3f800000),
    UINT32_C(0x477e0000), UINT32_C(0x477e0000),
    UINT32_C(0x477c0000), UINT32_C(0x3f800000),
    UINT32_C(0x35800000), UINT32_C(0x35800000),
    UINT32_C(0x36000000), UINT32_C(0x3f800000)
  };
  static const uint8_t        bgraInputValues[8] = {
    0u, 16u, 64u, 128u, 192u, 224u, 240u, 255u
  };
  static const float          depth32InputValues[8] = {
    0.0f, 0.0625f, 0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f
  };
  static const uint16_t       depth16InputValues[8] = {
    0u, 1u, 1024u, 16384u, 32768u, 49152u, 65534u, 65535u
  };

  releaseCmdb       = NULL;
  cudaCmdb          = NULL;
  acquireCmdb       = NULL;
  computePass       = NULL;
  transferPass      = NULL;
  cudaFence         = NULL;
  releaseSubmitted  = 0;
  cudaSubmitted     = 0;
  acquireSubmitted  = 0;
  ok                = 0;
  failure           = "setup";
  for (uint32_t i = 0u; i < TextureOutputValueCount; i++) {
    cudaOutput[i] = 0.0f;
  }
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    input[i]  = (float)i * 0.125f;
    output[i] = 0.0f;
  }
  for (uint32_t i = 0u; i < FilterMipValueCount; i++) {
    uint32_t layer, channel;

    layer             = i / (FilterMipWidth * FilterMipHeight * 4u);
    channel           = i % 4u;
    filterMipInput[i] = 1000.0f + (float)(layer * 16u + channel);
  }
  for (uint32_t i = 0u; i < CubeValueCount; i++) {
    uint32_t face, channel;

    face         = i / CubeFaceValueCount;
    channel      = i % 4u;
    cubeInput[i] = (float)(face * 8u + channel) + 0.25f;
  }
  for (uint32_t i = 0u; i < CubeArrayValueCount; i++) {
    uint32_t layer, channel;

    layer             = i / CubeFaceValueCount;
    channel           = i % 4u;
    cubeArrayInput[i] = (float)(layer * 8u + channel) + 0.5f;
  }
  for (uint32_t i = 0u; i < HalfTextureValueCount; i++) {
    uint32_t pattern;

    pattern       = format_value_pattern(i);
    halfInput[i]  = halfInputBits[pattern];
    halfOutput[i] = 0u;
  }
  for (uint32_t i = 0u; i < ByteTextureValueCount; i++) {
    uint32_t pattern;

    pattern         = format_value_pattern(i);
    unorm8Input[i]  = unorm8InputValues[pattern];
    unorm8Output[i] = 0u;
    snorm8Input[i]  = snorm8InputValues[pattern];
    snorm8Output[i] = 0;
    uint8Input[i]   = uint8InputValues[pattern];
    uint8Output[i]  = 0u;
    sint8Input[i]   = sint8InputValues[pattern];
    sint8Output[i]  = 0;
  }
  for (uint32_t i = 0u; i < WordTextureValueCount; i++) {
    uint32_t pattern;

    pattern          = format_value_pattern(i);
    unorm16Input[i]  = unorm16InputValues[pattern];
    unorm16Output[i] = 0u;
    snorm16Input[i]  = snorm16InputValues[pattern];
    snorm16Output[i] = 0;
    uint16Input[i]   = uint16InputValues[pattern];
    uint16Output[i]  = 0u;
    sint16Input[i]   = sint16InputValues[pattern];
    sint16Output[i]  = 0;
  }
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    uint32_t pattern;

    pattern         = format_value_pattern(i);
    uint32Input[i]  = uint32InputValues[pattern];
    uint32Output[i] = 0u;
    sint32Input[i]  = sint32InputValues[pattern];
    sint32Output[i] = 0;
    srgbInput[i]    = srgbInputValues[pattern];
    srgbOutput[i]   = 0u;
    bgraInput[i]    = bgraInputValues[pattern];
    bgraOutput[i]   = 0u;
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    uint32_t pattern;

    pattern                 = texel % 4u;
    packedRawInput[texel]   = packedRawValues[pattern];
    packedRawOutput[texel]  = 0u;
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t bits;

      bits = packedWriteBits[pattern * 4u + channel];
      memcpy(&packedWriteInput[texel * 4u + channel],
             &bits,
             sizeof(bits));
    }
    pattern              = texel % 8u;
    depth32Input[texel]  = depth32InputValues[pattern];
    depth32Output[texel] = 0.0f;
    depth16Input[texel]  = depth16InputValues[pattern];
    depth16Output[texel] = 0u;
  }
  formatTransfers[InteropFormatHalf] = (InteropFormatTransfer){
    halfInput,
    halfOutput,
    sizeof(halfInput),
    TextureWidth * 4u * sizeof(uint16_t)
  };
  formatTransfers[InteropFormatUnorm8] = (InteropFormatTransfer){
    unorm8Input,
    unorm8Output,
    sizeof(unorm8Input),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatSnorm8] = (InteropFormatTransfer){
    snorm8Input,
    snorm8Output,
    sizeof(snorm8Input),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatUint8] = (InteropFormatTransfer){
    uint8Input,
    uint8Output,
    sizeof(uint8Input),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatSint8] = (InteropFormatTransfer){
    sint8Input,
    sint8Output,
    sizeof(sint8Input),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatUnorm16] = (InteropFormatTransfer){
    unorm16Input,
    unorm16Output,
    sizeof(unorm16Input),
    TextureWidth * 4u * sizeof(uint16_t)
  };
  formatTransfers[InteropFormatSnorm16] = (InteropFormatTransfer){
    snorm16Input,
    snorm16Output,
    sizeof(snorm16Input),
    TextureWidth * 4u * sizeof(int16_t)
  };
  formatTransfers[InteropFormatUint16] = (InteropFormatTransfer){
    uint16Input,
    uint16Output,
    sizeof(uint16Input),
    TextureWidth * 4u * sizeof(uint16_t)
  };
  formatTransfers[InteropFormatSint16] = (InteropFormatTransfer){
    sint16Input,
    sint16Output,
    sizeof(sint16Input),
    TextureWidth * 4u * sizeof(int16_t)
  };
  formatTransfers[InteropFormatUint32] = (InteropFormatTransfer){
    uint32Input,
    uint32Output,
    sizeof(uint32Input),
    TextureWidth * 4u * sizeof(uint32_t)
  };
  formatTransfers[InteropFormatSint32] = (InteropFormatTransfer){
    sint32Input,
    sint32Output,
    sizeof(sint32Input),
    TextureWidth * 4u * sizeof(int32_t)
  };
  narrowOffset = 0u;
  for (uint32_t i = 0u; i < NarrowFormatCount; i++) {
    const InteropNarrowFormat *format;
    InteropFormatTransfer     *transfer;
    uint32_t                   componentCount, size;

    format         = &narrowFormats[i];
    transfer       = &formatTransfers[format->formatCase];
    componentCount = TextureTexelCount * format->channelCount;
    size           = componentCount * format->componentBytes;
    transfer->input       = narrowInput + narrowOffset;
    transfer->output      = narrowOutput + narrowOffset;
    transfer->size        = size;
    transfer->bytesPerRow = TextureWidth * format->channelCount *
                            format->componentBytes;
    for (uint32_t component = 0u; component < componentCount; component++) {
      uint32_t inputBits, outputBits;
      float    inputValue, outputValue;

      narrow_reference(format,
                       component,
                       &inputBits,
                       &outputBits,
                       &inputValue,
                       &outputValue);
      memcpy(narrowInput + narrowOffset +
               component * format->componentBytes,
             &inputBits,
             format->componentBytes);
      memset(narrowOutput + narrowOffset +
               component * format->componentBytes,
             0,
             format->componentBytes);
    }
    narrowOffset += size;
  }
  if (narrowOffset != NarrowRawByteCount) {
    goto cleanup;
  }
  packedReadBase = TextureValueCount * 2u + CubeOutputValueCount +
                   CubeArrayOutputValueCount + FormatFloatOutputValueCount +
                   SrgbFloatOutputValueCount;
  packedInputBase = packedReadBase + TextureValueCount;
  bgraBase        = packedInputBase + TextureValueCount;
  depth32Base     = bgraBase + BgraFloatOutputValueCount;
  depth16Base     = depth32Base + TextureValueCount;
  filterBase      = depth16Base + TextureValueCount;
  samplerBase     = filterBase + ColorFilterOutputValueCount;
  if (GPUQueueWriteBuffer(state->cudaQueue,
                          state->textureCudaReadback,
                          (uint64_t)packedInputBase * sizeof(float),
                          packedWriteInput,
                          sizeof(packedWriteInput)) != GPU_OK) {
    goto cleanup;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TextureWidth;
  writeRegion.height       = TextureHeight;
  writeRegion.depth        = 1u;
  writeRegion.mipLevel     = state->textureMipLevel;
  writeRegion.layerCount   = TextureLayers;
  writeRegion.bytesPerRow  = TextureWidth * 4u * sizeof(float);
  writeRegion.rowsPerImage = TextureHeight;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsTexture,
                           &writeRegion,
                           input,
                           sizeof(input)) != GPU_OK) {
    goto cleanup;
  }
  if (state->mipFilterShared) {
    writeRegion.width        = FilterMipWidth;
    writeRegion.height       = FilterMipHeight;
    writeRegion.mipLevel     = state->textureMipLevel + 1u;
    writeRegion.bytesPerRow  = FilterMipWidth * 4u * sizeof(float);
    writeRegion.rowsPerImage = FilterMipHeight;
    if (GPUQueueWriteTexture(state->graphicsQueue,
                             state->graphicsTexture,
                             &writeRegion,
                             filterMipInput,
                             sizeof(filterMipInput)) != GPU_OK) {
      goto cleanup;
    }
    writeRegion.width        = TextureWidth;
    writeRegion.height       = TextureHeight;
    writeRegion.mipLevel     = state->textureMipLevel;
    writeRegion.bytesPerRow  = TextureWidth * 4u * sizeof(float);
    writeRegion.rowsPerImage = TextureHeight;
  }
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    writeRegion.bytesPerRow = formatTransfers[i].bytesPerRow;
    if (GPUQueueWriteTexture(state->graphicsQueue,
                             state->formatTextures[i].graphicsTexture,
                             &writeRegion,
                             formatTransfers[i].input,
                             formatTransfers[i].size) != GPU_OK) {
      goto cleanup;
    }
  }
  writeRegion.bytesPerRow = TextureWidth * 4u;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsSrgbTexture,
                           &writeRegion,
                           srgbInput,
                           sizeof(srgbInput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsPackedTexture,
                           &writeRegion,
                           packedRawInput,
                           sizeof(packedRawInput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsBgraTexture,
                           &writeRegion,
                           bgraInput,
                           sizeof(bgraInput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsDepth32Texture,
                           &writeRegion,
                           depth32Input,
                           sizeof(depth32Input)) != GPU_OK) {
    goto cleanup;
  }
  writeRegion.bytesPerRow = TextureWidth * sizeof(uint16_t);
  if (state->depth16Shared &&
      GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsDepth16Texture,
                           &writeRegion,
                           depth16Input,
                           sizeof(depth16Input)) != GPU_OK) {
    goto cleanup;
  }
  fixedTextureCount = state->depth16Shared ? 8u : 7u;
  writeRegion.bytesPerRow = TextureWidth * 4u * sizeof(float);
  writeRegion.layerCount = CubeLayers;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsCubeTexture,
                           &writeRegion,
                           cubeInput,
                           sizeof(cubeInput)) != GPU_OK) {
    goto cleanup;
  }
  writeRegion.layerCount = CubeArrayLayers;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsCubeArrayTexture,
                           &writeRegion,
                           cubeArrayInput,
                           sizeof(cubeArrayInput)) != GPU_OK ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-texture-release",
                              &releaseCmdb) != GPU_OK ||
      !releaseCmdb ||
      GPUAcquireCommandBuffer(state->cudaQueue,
                              "cuda-texture-roundtrip",
                              &cudaCmdb) != GPU_OK ||
      !cudaCmdb ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-texture-acquire",
                              &acquireCmdb) != GPU_OK ||
      !acquireCmdb ||
      GPUCreateFence(state->cudaDevice, NULL, &cudaFence) != GPU_OK ||
      !cudaFence) {
    goto cleanup;
  }

  toCuda[0].sourceTexture      = state->graphicsTexture;
  toCuda[0].destinationTexture = state->cudaTexture;
  toCuda[0].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[0].dstAccess          = GPU_ACCESS_SHADER_READ |
                                 GPU_ACCESS_SHADER_WRITE;
  toCuda[0].baseMip            = state->textureMipLevel;
  toCuda[0].mipCount           = state->mipFilterShared ? 2u : 1u;
  toCuda[0].layerCount         = TextureLayers;
  toCuda[1].sourceTexture      = state->graphicsCubeTexture;
  toCuda[1].destinationTexture = state->cudaCubeTexture;
  toCuda[1].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[1].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[1].baseMip            = state->textureMipLevel;
  toCuda[1].mipCount           = 1u;
  toCuda[1].layerCount         = CubeLayers;
  toCuda[2].sourceTexture      = state->graphicsCubeArrayTexture;
  toCuda[2].destinationTexture = state->cudaCubeArrayTexture;
  toCuda[2].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[2].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[2].baseMip            = state->textureMipLevel;
  toCuda[2].mipCount           = 1u;
  toCuda[2].layerCount         = CubeArrayLayers;
  toCuda[3].sourceTexture      = state->graphicsSrgbTexture;
  toCuda[3].destinationTexture = state->cudaSrgbTexture;
  toCuda[3].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[3].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[3].baseMip            = state->textureMipLevel;
  toCuda[3].mipCount           = 1u;
  toCuda[3].layerCount         = TextureLayers;
  toCuda[4].sourceTexture      = state->graphicsPackedTexture;
  toCuda[4].destinationTexture = state->cudaPackedTexture;
  toCuda[4].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[4].dstAccess          = GPU_ACCESS_SHADER_READ |
                                 GPU_ACCESS_SHADER_WRITE;
  toCuda[4].baseMip            = state->textureMipLevel;
  toCuda[4].mipCount           = 1u;
  toCuda[4].layerCount         = TextureLayers;
  toCuda[5].sourceTexture      = state->graphicsBgraTexture;
  toCuda[5].destinationTexture = state->cudaBgraTexture;
  toCuda[5].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[5].dstAccess          = GPU_ACCESS_SHADER_READ |
                                 GPU_ACCESS_SHADER_WRITE;
  toCuda[5].baseMip            = state->textureMipLevel;
  toCuda[5].mipCount           = 1u;
  toCuda[5].layerCount         = TextureLayers;
  toCuda[6].sourceTexture      = state->graphicsDepth32Texture;
  toCuda[6].destinationTexture = state->cudaDepth32Texture;
  toCuda[6].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[6].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[6].baseMip            = state->textureMipLevel;
  toCuda[6].mipCount           = 1u;
  toCuda[6].layerCount         = TextureLayers;
  toCuda[7].sourceTexture      = state->graphicsDepth16Texture;
  toCuda[7].destinationTexture = state->cudaDepth16Texture;
  toCuda[7].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[7].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[7].baseMip            = state->textureMipLevel;
  toCuda[7].mipCount           = 1u;
  toCuda[7].layerCount         = TextureLayers;
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUSharedTextureBarrierEXT *barrier;

    barrier                     = &toCuda[fixedTextureCount + i];
    barrier->sourceTexture      = state->formatTextures[i].graphicsTexture;
    barrier->destinationTexture = state->formatTextures[i].cudaTexture;
    barrier->srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
    barrier->dstAccess          = GPU_ACCESS_SHADER_READ |
                                  GPU_ACCESS_SHADER_WRITE;
    barrier->baseMip            = state->textureMipLevel;
    barrier->mipCount           = 1u;
    barrier->layerCount         = TextureLayers;
  }
  acquireCuda.pTextureBarriers    = toCuda;
  acquireCuda.srcStages           = GPU_STAGE_TRANSFER;
  acquireCuda.dstStages           = GPU_STAGE_COMPUTE;
  acquireCuda.textureBarrierCount = fixedTextureCount + InteropFormatCount;
  failure = "release/acquire encoding";
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                releaseCmdb,
                                &acquireCuda) != GPU_OK ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                cudaCmdb,
                                &acquireCuda) != GPU_OK ||
      !(computePass = GPUBeginComputePass(cudaCmdb,
                                          "cuda-texture-update"))) {
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, state->textureSamplePipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              CubeArrayLayers);
  GPUBindComputePipeline(computePass, state->narrowSamplePipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUBindComputeGroup(computePass, 1u, state->narrowGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              TextureLayers);
  GPUBindComputePipeline(computePass, state->colorFilterPipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              TextureLayers);
  GPUBindComputePipeline(computePass, state->narrowFilterPipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUBindComputeGroup(computePass, 1u, state->narrowGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              TextureLayers);
  GPUBindComputePipeline(computePass, state->textureStorePipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              TextureLayers);
  GPUBindComputePipeline(computePass, state->narrowStorePipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUBindComputeGroup(computePass, 1u, state->narrowGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              TextureLayers);
  GPUEndComputePass(computePass);
  computePass = NULL;

  toGraphics[0].sourceTexture      = state->cudaTexture;
  toGraphics[0].destinationTexture = state->graphicsTexture;
  toGraphics[0].srcAccess          = GPU_ACCESS_SHADER_WRITE;
  toGraphics[0].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[0].baseMip            = state->textureMipLevel;
  toGraphics[0].mipCount           = state->mipFilterShared ? 2u : 1u;
  toGraphics[0].layerCount         = TextureLayers;
  toGraphics[1].sourceTexture      = state->cudaCubeTexture;
  toGraphics[1].destinationTexture = state->graphicsCubeTexture;
  toGraphics[1].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[1].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[1].baseMip            = state->textureMipLevel;
  toGraphics[1].mipCount           = 1u;
  toGraphics[1].layerCount         = CubeLayers;
  toGraphics[2].sourceTexture      = state->cudaCubeArrayTexture;
  toGraphics[2].destinationTexture = state->graphicsCubeArrayTexture;
  toGraphics[2].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[2].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[2].baseMip            = state->textureMipLevel;
  toGraphics[2].mipCount           = 1u;
  toGraphics[2].layerCount         = CubeArrayLayers;
  toGraphics[3].sourceTexture      = state->cudaSrgbTexture;
  toGraphics[3].destinationTexture = state->graphicsSrgbTexture;
  toGraphics[3].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[3].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[3].baseMip            = state->textureMipLevel;
  toGraphics[3].mipCount           = 1u;
  toGraphics[3].layerCount         = TextureLayers;
  toGraphics[4].sourceTexture      = state->cudaPackedTexture;
  toGraphics[4].destinationTexture = state->graphicsPackedTexture;
  toGraphics[4].srcAccess          = GPU_ACCESS_SHADER_WRITE;
  toGraphics[4].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[4].baseMip            = state->textureMipLevel;
  toGraphics[4].mipCount           = 1u;
  toGraphics[4].layerCount         = TextureLayers;
  toGraphics[5].sourceTexture      = state->cudaBgraTexture;
  toGraphics[5].destinationTexture = state->graphicsBgraTexture;
  toGraphics[5].srcAccess          = GPU_ACCESS_SHADER_WRITE;
  toGraphics[5].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[5].baseMip            = state->textureMipLevel;
  toGraphics[5].mipCount           = 1u;
  toGraphics[5].layerCount         = TextureLayers;
  toGraphics[6].sourceTexture      = state->cudaDepth32Texture;
  toGraphics[6].destinationTexture = state->graphicsDepth32Texture;
  toGraphics[6].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[6].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[6].baseMip            = state->textureMipLevel;
  toGraphics[6].mipCount           = 1u;
  toGraphics[6].layerCount         = TextureLayers;
  toGraphics[7].sourceTexture      = state->cudaDepth16Texture;
  toGraphics[7].destinationTexture = state->graphicsDepth16Texture;
  toGraphics[7].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[7].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[7].baseMip            = state->textureMipLevel;
  toGraphics[7].mipCount           = 1u;
  toGraphics[7].layerCount         = TextureLayers;
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUSharedTextureBarrierEXT *barrier;

    barrier                     = &toGraphics[fixedTextureCount + i];
    barrier->sourceTexture      = state->formatTextures[i].cudaTexture;
    barrier->destinationTexture = state->formatTextures[i].graphicsTexture;
    barrier->srcAccess          = GPU_ACCESS_SHADER_WRITE;
    barrier->dstAccess          = GPU_ACCESS_TRANSFER_READ;
    barrier->baseMip            = state->textureMipLevel;
    barrier->mipCount           = 1u;
    barrier->layerCount         = TextureLayers;
  }
  acquireGraphics.pTextureBarriers    = toGraphics;
  acquireGraphics.srcStages           = GPU_STAGE_COMPUTE;
  acquireGraphics.dstStages           = GPU_STAGE_TRANSFER;
  acquireGraphics.textureBarrierCount = fixedTextureCount +
                                         InteropFormatCount;
  failure = "return/copy encoding";
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                cudaCmdb,
                                &acquireGraphics) != GPU_OK ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                acquireCmdb,
                                &acquireGraphics) != GPU_OK ||
      !(transferPass = GPUBeginTransferPass(
          acquireCmdb,
          "graphics-cuda-texture-readback"
        ))) {
    goto cleanup;
  }
  copyRegion.bytesPerRow        = writeRegion.bytesPerRow;
  copyRegion.rowsPerImage       = TextureHeight;
  copyRegion.texture.texture.mipLevel = state->textureMipLevel;
  copyRegion.texture.width      = TextureWidth;
  copyRegion.texture.height     = TextureHeight;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = TextureLayers;
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsTexture,
                         state->textureReadback,
                         &copyRegion);
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    copyRegion.bytesPerRow = formatTransfers[i].bytesPerRow;
    GPUCopyTextureToBuffer(transferPass,
                           state->formatTextures[i].graphicsTexture,
                           state->formatTextures[i].readback,
                           &copyRegion);
  }
  copyRegion.bytesPerRow = TextureWidth * 4u;
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsSrgbTexture,
                         state->srgbReadback,
                         &copyRegion);
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsPackedTexture,
                         state->packedReadback,
                         &copyRegion);
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsBgraTexture,
                         state->bgraReadback,
                         &copyRegion);
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsDepth32Texture,
                         state->depth32Readback,
                         &copyRegion);
  copyRegion.bytesPerRow = TextureWidth * sizeof(uint16_t);
  if (state->depth16Shared) {
    GPUCopyTextureToBuffer(transferPass,
                           state->graphicsDepth16Texture,
                           state->depth16Readback,
                           &copyRegion);
  }
  GPUEndTransferPass(transferPass);
  transferPass = NULL;

  signal.semaphore          = state->graphicsSemaphore;
  signal.value              = RoundtripCount * 2u +
                              (uint64_t)sequence * 2u + 1u;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &releaseCmdb;
  submit.pSignals           = &signal;
  submit.fence              = state->releaseFence;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  failure = "graphics release submit";
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  releaseCmdb      = NULL;
  releaseSubmitted = 1;

  wait.semaphore          = state->cudaSemaphore;
  wait.value              = signal.value;
  wait.waitStages         = GPU_STAGE_COMPUTE;
  signal.semaphore        = state->cudaSemaphore;
  signal.value++;
  submit.ppCommandBuffers = &cudaCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = &signal;
  submit.fence            = cudaFence;
  submit.waitCount        = 1u;
  failure = "CUDA submit";
  if (GPUQueueSubmitEx(state->cudaQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  cudaCmdb      = NULL;
  cudaSubmitted = 1;

  wait.semaphore          = state->graphicsSemaphore;
  wait.value              = signal.value;
  wait.waitStages         = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers = &acquireCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = NULL;
  submit.fence            = state->acquireFence;
  submit.signalCount      = 0u;
  failure = "graphics acquire submit";
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  acquireCmdb      = NULL;
  acquireSubmitted = 1;
  failure = "graphics readback";
  if (GPUWaitFence(state->acquireFence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(state->graphicsQueue,
                         state->textureReadback,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      GPUQueueReadBuffer(state->cudaQueue,
                         state->textureCudaReadback,
                         0u,
                         cudaOutput,
                         sizeof(cudaOutput)) != GPU_OK) {
    goto cleanup;
  }
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    if (GPUQueueReadBuffer(state->graphicsQueue,
                           state->formatTextures[i].readback,
                           0u,
                           formatTransfers[i].output,
                           formatTransfers[i].size) != GPU_OK) {
      goto cleanup;
    }
  }
  if (GPUQueueReadBuffer(state->graphicsQueue,
                         state->srgbReadback,
                         0u,
                         srgbOutput,
                         sizeof(srgbOutput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueReadBuffer(state->graphicsQueue,
                         state->packedReadback,
                         0u,
                         packedRawOutput,
                         sizeof(packedRawOutput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueReadBuffer(state->graphicsQueue,
                         state->bgraReadback,
                         0u,
                         bgraOutput,
                         sizeof(bgraOutput)) != GPU_OK) {
    goto cleanup;
  }
  if (GPUQueueReadBuffer(state->graphicsQueue,
                         state->depth32Readback,
                         0u,
                         depth32Output,
                         sizeof(depth32Output)) != GPU_OK ||
      (state->depth16Shared &&
       GPUQueueReadBuffer(state->graphicsQueue,
                          state->depth16Readback,
                          0u,
                          depth16Output,
                          sizeof(depth16Output)) != GPU_OK)) {
    goto cleanup;
  }
  failure = "result validation";
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    float expected;

    expected = input[i] * 2.0f + 1.0f;
    if (fabsf(cudaOutput[i] - input[i]) > 0.0001f) {
      fprintf(stderr,
              "CUDA sampled texture mismatch at %u: %.9g != %.9g\n",
              i,
              cudaOutput[i],
              input[i]);
      goto cleanup;
    }
    if (fabsf(cudaOutput[TextureValueCount + i] - expected) > 0.0001f) {
      fprintf(stderr,
              "CUDA storage texture mismatch at %u: %.9g != %.9g\n",
              i,
              cudaOutput[TextureValueCount + i],
              expected);
      goto cleanup;
    }
    if (fabsf(output[i] - expected) > 0.0001f) {
      fprintf(stderr,
              "graphics/CUDA texture mismatch at %u: %.9g != %.9g\n",
              i,
              output[i],
              expected);
      goto cleanup;
    }
  }
  for (uint32_t face = 0u; face < CubeLayers; face++) {
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t index, inputIndex;
      float    expected;

      index      = TextureValueCount * 2u + face * 4u + channel;
      inputIndex = face * CubeFaceValueCount + channel;
      expected   = cubeInput[inputIndex];
      if (fabsf(cudaOutput[index] - expected) > 0.0001f) {
        fprintf(stderr,
                "CUDA cube texture mismatch at %u/%u: %.9g != %.9g\n",
                face,
                channel,
                cudaOutput[index],
                expected);
        goto cleanup;
      }
    }
  }
  for (uint32_t layer = 0u; layer < CubeArrayLayers; layer++) {
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t index, inputIndex;
      float    expected;

      index = TextureValueCount * 2u + CubeOutputValueCount +
              layer * 4u + channel;
      inputIndex = layer * CubeFaceValueCount + channel;
      expected   = cubeArrayInput[inputIndex];
      if (fabsf(cudaOutput[index] - expected) > 0.0001f) {
        fprintf(stderr,
                "CUDA cube-array mismatch at %u/%u: %.9g != %.9g\n",
                layer,
                channel,
                cudaOutput[index],
                expected);
        goto cleanup;
      }
    }
  }
  halfBase = TextureValueCount * 2u + CubeOutputValueCount +
             CubeArrayOutputValueCount;
  unorm8Base  = halfBase + HalfFloatOutputValueCount;
  snorm8Base  = unorm8Base + ByteFloatOutputValueCount;
  uint8Base   = snorm8Base + ByteFloatOutputValueCount;
  sint8Base   = uint8Base + ByteFloatOutputValueCount;
  unorm16Base = sint8Base + ByteFloatOutputValueCount;
  snorm16Base = unorm16Base + WordFloatOutputValueCount;
  uint16Base  = snorm16Base + WordFloatOutputValueCount;
  sint16Base  = uint16Base + WordFloatOutputValueCount;
  uint32Base  = sint16Base + WordFloatOutputValueCount;
  sint32Base  = uint32Base + ByteFloatOutputValueCount;
  srgbBase    = halfBase + FormatFloatOutputValueCount;
  for (uint32_t i = 0u; i < HalfTextureValueCount; i++) {
    uint32_t pattern;

    pattern = format_value_pattern(i);
    if (fabsf(cudaOutput[halfBase + i] - halfInputValues[pattern]) >
          0.0001f ||
        fabsf(cudaOutput[halfBase + HalfTextureValueCount + i] -
               halfOutputValues[pattern]) > 0.0001f ||
        halfOutput[i] != halfOutputBits[pattern]) {
      fprintf(stderr,
              "CUDA half texture mismatch at %u: %.9g/%.9g/%04x\n",
              i,
              cudaOutput[halfBase + i],
              cudaOutput[halfBase + HalfTextureValueCount + i],
              (unsigned)halfOutput[i]);
      goto cleanup;
    }
  }
  for (uint32_t i = 0u; i < ByteTextureValueCount; i++) {
    uint32_t pattern;
    float    original, expected;

    pattern  = format_value_pattern(i);
    original = (float)unorm8InputValues[pattern] / 255.0f;
    expected = (float)(255u - unorm8InputValues[pattern]) / 255.0f;
    if (fabsf(cudaOutput[unorm8Base + i] - original) >
          0.5f / 255.0f + 0.000001f ||
        fabsf(cudaOutput[unorm8Base + ByteTextureValueCount + i] -
               expected) > 0.5f / 255.0f + 0.000001f ||
        unorm8Output[i] != 255u - unorm8InputValues[pattern]) {
      fprintf(stderr,
              "CUDA rgba8-unorm mismatch at %u: %.9g/%.9g/%u\n",
              i,
              cudaOutput[unorm8Base + i],
              cudaOutput[unorm8Base + ByteTextureValueCount + i],
              (unsigned)unorm8Output[i]);
      goto cleanup;
    }
    original = (float)snorm8InputValues[pattern] / 127.0f;
    expected = -original;
    if (fabsf(cudaOutput[snorm8Base + i] - original) >
          0.5f / 127.0f + 0.000001f ||
        fabsf(cudaOutput[snorm8Base + ByteTextureValueCount + i] -
               expected) > 0.5f / 127.0f + 0.000001f ||
        snorm8Output[i] != (int8_t)-snorm8InputValues[pattern]) {
      fprintf(stderr, "CUDA rgba8-snorm mismatch at %u\n", i);
      goto cleanup;
    }
    if (cudaOutput[uint8Base + i] != (float)uint8InputValues[pattern] ||
        cudaOutput[uint8Base + ByteTextureValueCount + i] !=
          (float)(uint8InputValues[pattern] + 1u) ||
        uint8Output[i] != (uint8_t)(uint8InputValues[pattern] + 1u)) {
      fprintf(stderr,
              "CUDA rgba8-uint mismatch at %u: %.9g/%.9g/%u != %u/%u\n",
              i,
              cudaOutput[uint8Base + i],
              cudaOutput[uint8Base + ByteTextureValueCount + i],
              (unsigned)uint8Output[i],
              (unsigned)uint8InputValues[pattern],
              (unsigned)uint8InputValues[pattern] + 1u);
      goto cleanup;
    }
    if (cudaOutput[sint8Base + i] != (float)sint8InputValues[pattern] ||
        cudaOutput[sint8Base + ByteTextureValueCount + i] !=
          (float)(sint8InputValues[pattern] + 7) ||
        sint8Output[i] != (int8_t)(sint8InputValues[pattern] + 7)) {
      fprintf(stderr, "CUDA rgba8-sint mismatch at %u\n", i);
      goto cleanup;
    }
  }
  for (uint32_t i = 0u; i < WordTextureValueCount; i++) {
    uint32_t pattern;
    float    original, expected;

    pattern  = format_value_pattern(i);
    original = (float)unorm16InputValues[pattern] / 65535.0f;
    expected = (float)(65535u - unorm16InputValues[pattern]) / 65535.0f;
    if (fabsf(cudaOutput[unorm16Base + i] - original) >
          0.5f / 65535.0f + 0.000001f ||
        fabsf(cudaOutput[unorm16Base + WordTextureValueCount + i] -
               expected) > 0.5f / 65535.0f + 0.000001f ||
        unorm16Output[i] != 65535u - unorm16InputValues[pattern]) {
      fprintf(stderr, "CUDA rgba16-unorm mismatch at %u\n", i);
      goto cleanup;
    }
    original = (float)snorm16InputValues[pattern] / 32767.0f;
    expected = -original;
    if (fabsf(cudaOutput[snorm16Base + i] - original) >
          0.5f / 32767.0f + 0.000001f ||
        fabsf(cudaOutput[snorm16Base + WordTextureValueCount + i] -
               expected) > 0.5f / 32767.0f + 0.000001f ||
        snorm16Output[i] != (int16_t)-snorm16InputValues[pattern]) {
      fprintf(stderr, "CUDA rgba16-snorm mismatch at %u\n", i);
      goto cleanup;
    }
    if (cudaOutput[uint16Base + i] != (float)uint16InputValues[pattern] ||
        cudaOutput[uint16Base + WordTextureValueCount + i] !=
          (float)(uint16InputValues[pattern] + 1u) ||
        uint16Output[i] != (uint16_t)(uint16InputValues[pattern] + 1u)) {
      fprintf(stderr, "CUDA rgba16-uint mismatch at %u\n", i);
      goto cleanup;
    }
    if (cudaOutput[sint16Base + i] != (float)sint16InputValues[pattern] ||
        cudaOutput[sint16Base + WordTextureValueCount + i] !=
          (float)(sint16InputValues[pattern] + 7) ||
        sint16Output[i] != (int16_t)(sint16InputValues[pattern] + 7)) {
      fprintf(stderr, "CUDA rgba16-sint mismatch at %u\n", i);
      goto cleanup;
    }
  }
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    uint32_t pattern;

    pattern = format_value_pattern(i);
    if (cudaOutput[uint32Base + i] !=
          (float)uint32InputValues[pattern] ||
        cudaOutput[uint32Base + TextureValueCount + i] !=
          (float)(uint32InputValues[pattern] + 1u) ||
        uint32Output[i] != uint32InputValues[pattern] + 1u) {
      fprintf(stderr, "CUDA rgba32-uint mismatch at %u\n", i);
      goto cleanup;
    }
    if (cudaOutput[sint32Base + i] !=
          (float)sint32InputValues[pattern] ||
        cudaOutput[sint32Base + TextureValueCount + i] !=
          (float)(sint32InputValues[pattern] + 7) ||
        sint32Output[i] != sint32InputValues[pattern] + 7) {
      fprintf(stderr, "CUDA rgba32-sint mismatch at %u\n", i);
      goto cleanup;
    }
  }
  narrowOffset = 0u;
  for (uint32_t i = 0u; i < NarrowFormatCount; i++) {
    const InteropNarrowFormat *format;
    uint32_t                   base, componentCount, size, rawMask;
    float                      tolerance;

    format         = &narrowFormats[i];
    base           = halfBase +
                     (uint32_t)format->formatCase *
                       ByteFloatOutputValueCount;
    componentCount = TextureTexelCount * format->channelCount;
    size           = componentCount * format->componentBytes;
    rawMask        = format->componentBytes == 1u
      ? UINT8_MAX
      : format->componentBytes == 2u ? UINT16_MAX : UINT32_MAX;
    tolerance = format->valueKind == InteropValueUnorm
      ? 0.5f / (format->componentBytes == 1u ? 255.0f : 65535.0f) +
        0.000001f
      : format->valueKind == InteropValueSnorm
        ? 0.5f / (format->componentBytes == 1u ? 127.0f : 32767.0f) +
          0.000001f
        : 0.0001f;
    for (uint32_t component = 0u; component < componentCount; component++) {
      uint32_t inputBits, outputBits, actualBits;
      uint32_t texel, channel, cudaIndex;
      float    inputValue, outputValue;

      narrow_reference(format,
                       component,
                       &inputBits,
                       &outputBits,
                       &inputValue,
                       &outputValue);
      texel      = component / format->channelCount;
      channel    = component % format->channelCount;
      cudaIndex  = base + texel * 4u + channel;
      actualBits = 0u;
      memcpy(&actualBits,
             narrowOutput + narrowOffset +
               component * format->componentBytes,
             format->componentBytes);
      if (fabsf(cudaOutput[cudaIndex] - inputValue) > tolerance ||
          fabsf(cudaOutput[cudaIndex + TextureValueCount] - outputValue) >
            tolerance ||
          actualBits != (outputBits & rawMask)) {
        fprintf(stderr,
                "CUDA %s mismatch at %u: %.9g/%.9g/%08x\n",
                format->label,
                component,
                cudaOutput[cudaIndex],
                cudaOutput[cudaIndex + TextureValueCount],
                actualBits);
        goto cleanup;
      }
    }
    narrowOffset += size;
  }
  for (uint32_t i = 0u; i < ByteTextureValueCount; i++) {
    uint32_t pattern, channel;
    float    expected;

    pattern = format_value_pattern(i);
    channel = i % 4u;
    expected = channel == 3u
      ? (float)srgbInputValues[pattern] / 255.0f
      : srgb_to_linear(srgbInputValues[pattern]);
    if (fabsf(cudaOutput[srgbBase + i] - expected) > 0.0005f ||
        srgbOutput[i] != srgbInputValues[pattern]) {
      fprintf(stderr,
              "CUDA rgba8-srgb mismatch at %u: %.9g/%u != %.9g/%u\n",
              i,
              cudaOutput[srgbBase + i],
              (unsigned)srgbOutput[i],
              expected,
              (unsigned)srgbInputValues[pattern]);
      goto cleanup;
    }
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    uint32_t outputBits[4], inputBits[4];
    uint32_t pattern;
    bool     decoded;

    pattern = texel % 4u;
    memcpy(outputBits,
           &cudaOutput[packedReadBase + texel * 4u],
           sizeof(outputBits));
    memcpy(inputBits,
           &cudaOutput[packedInputBase + texel * 4u],
           sizeof(inputBits));
    decoded = false;
    if (pattern == 0u) {
      decoded = outputBits[0] == 0u && outputBits[1] == 0u &&
                (outputBits[2] & UINT32_C(0x7f800000)) ==
                  UINT32_C(0x7f800000) &&
                (outputBits[2] & UINT32_C(0x007fffff)) != 0u;
    } else if (pattern == 1u) {
      decoded = outputBits[0] == UINT32_C(0x7f800000) &&
                outputBits[1] == UINT32_C(0x7f800000) &&
                outputBits[2] == UINT32_C(0x7f800000);
    } else if (pattern == 2u) {
      decoded = outputBits[0] == UINT32_C(0x477e0000) &&
                outputBits[1] == UINT32_C(0x477e0000) &&
                outputBits[2] == UINT32_C(0x477c0000);
    } else {
      decoded = outputBits[0] == UINT32_C(0x35800000) &&
                outputBits[1] == UINT32_C(0x35800000) &&
                outputBits[2] == UINT32_C(0x36000000);
    }
    if (!decoded || outputBits[3] != UINT32_C(0x3f800000) ||
        memcmp(inputBits,
               &packedWriteBits[pattern * 4u],
               sizeof(inputBits)) != 0 ||
        packedRawOutput[texel] != packedRawValues[pattern]) {
      fprintf(stderr,
              "CUDA rg11b10 mismatch at %u: %08x/%08x/%08x/%08x\n",
              texel,
              outputBits[0],
              outputBits[1],
              outputBits[2],
              packedRawOutput[texel]);
      goto cleanup;
    }
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    static const uint8_t memoryChannel[4] = {2u, 1u, 0u, 3u};

    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t logicalIndex, memoryIndex;
      float    original, expected;

      logicalIndex = texel * 4u + channel;
      memoryIndex  = texel * 4u + memoryChannel[channel];
      original     = (float)bgraInput[memoryIndex] / 255.0f;
      expected     = 1.0f - original;
      if (fabsf(cudaOutput[bgraBase + logicalIndex] - original) >
            0.5f / 255.0f + 0.000001f ||
          fabsf(cudaOutput[bgraBase + TextureValueCount + logicalIndex] -
                 expected) > 0.5f / 255.0f + 0.000001f ||
          bgraOutput[memoryIndex] != 255u - bgraInput[memoryIndex]) {
        fprintf(stderr,
                "CUDA bgra8-unorm mismatch at %u/%u: %.9g/%.9g/%u\n",
                texel,
                channel,
                cudaOutput[bgraBase + logicalIndex],
                cudaOutput[bgraBase + TextureValueCount + logicalIndex],
                (unsigned)bgraOutput[memoryIndex]);
        goto cleanup;
      }
    }
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    uint32_t pattern, nextPattern, depth32Index, depth16Index;
    float    depth32Linear, depth16Expected, depth16Linear;

    pattern         = texel % 8u;
    nextPattern     = pattern < 7u ? pattern + 1u : pattern;
    depth32Index    = depth32Base + texel * 4u;
    depth16Index    = depth16Base + texel * 4u;
    depth32Linear   = (depth32InputValues[pattern] +
                       depth32InputValues[nextPattern]) * 0.5f;
    depth16Expected = (float)depth16InputValues[pattern] / 65535.0f;
    depth16Linear   = ((float)depth16InputValues[pattern] +
                       (float)depth16InputValues[nextPattern]) /
                      (2.0f * 65535.0f);
    if (fabsf(cudaOutput[depth32Index] - depth32InputValues[pattern]) >
          0.00001f ||
        fabsf(cudaOutput[depth32Index + 1u] - depth32InputValues[pattern]) >
          0.00001f ||
        fabsf(cudaOutput[depth32Index + 2u] - depth32Linear) > 0.00001f ||
        depth32Output[texel] != depth32InputValues[pattern]) {
      fprintf(stderr,
              "CUDA depth32 mismatch at %u: %.9g/%.9g/%.9g/%.9g\n",
              texel,
              cudaOutput[depth32Index],
              cudaOutput[depth32Index + 1u],
              cudaOutput[depth32Index + 2u],
              depth32Output[texel]);
      goto cleanup;
    }
    if ((state->depth16Shared &&
         (fabsf(cudaOutput[depth16Index] - depth16Expected) >
            0.5f / 65535.0f + 0.000001f ||
          fabsf(cudaOutput[depth16Index + 1u] - depth16Expected) >
            0.5f / 65535.0f + 0.000001f ||
          fabsf(cudaOutput[depth16Index + 2u] - depth16Linear) >
            0.00002f ||
          depth16Output[texel] != depth16InputValues[pattern])) ||
        (!state->depth16Shared &&
         (fabsf(cudaOutput[depth16Index] - depth32InputValues[pattern]) >
            0.00001f ||
          fabsf(cudaOutput[depth16Index + 1u] -
                 depth32InputValues[pattern]) > 0.00001f ||
          fabsf(cudaOutput[depth16Index + 2u] - depth32Linear) >
            0.00001f))) {
      fprintf(stderr,
              "CUDA depth16 mismatch at %u: %.9g/%.9g/%.9g/%u\n",
              texel,
              cudaOutput[depth16Index],
              cudaOutput[depth16Index + 1u],
              cudaOutput[depth16Index + 2u],
              (unsigned)depth16Output[texel]);
      goto cleanup;
    }
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    static const float tolerance[RgbaColorFilterCount] = {
      0.00001f, 0.002f, 0.005f, 0.005f, 0.00005f, 0.00005f, 0.0025f
    };
    uint32_t x, nextTexel;

    x         = texel % TextureWidth;
    nextTexel = x + 1u < TextureWidth ? texel + 1u : texel;
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      float    current[RgbaColorFilterCount];
      float    next[RgbaColorFilterCount];
      uint32_t valueIndex, nextValueIndex, pattern, nextPattern;

      valueIndex     = texel * 4u + channel;
      nextValueIndex = nextTexel * 4u + channel;
      pattern        = format_value_pattern(valueIndex);
      nextPattern    = format_value_pattern(nextValueIndex);
      current[0]     = input[valueIndex];
      next[0]        = input[nextValueIndex];
      current[1]     = halfInputValues[pattern];
      next[1]        = halfInputValues[nextPattern];
      current[2]     = (float)unorm8InputValues[pattern] / 255.0f;
      next[2]        = (float)unorm8InputValues[nextPattern] / 255.0f;
      current[3]     = (float)snorm8InputValues[pattern] / 127.0f;
      next[3]        = (float)snorm8InputValues[nextPattern] / 127.0f;
      current[4]     = (float)unorm16InputValues[pattern] / 65535.0f;
      next[4]        = (float)unorm16InputValues[nextPattern] / 65535.0f;
      current[5]     = (float)snorm16InputValues[pattern] / 32767.0f;
      next[5]        = (float)snorm16InputValues[nextPattern] / 32767.0f;
      current[6]     = channel == 3u
        ? (float)srgbInputValues[pattern] / 255.0f
        : srgb_to_linear(srgbInputValues[pattern]);
      next[6]        = channel == 3u
        ? (float)srgbInputValues[nextPattern] / 255.0f
        : srgb_to_linear(srgbInputValues[nextPattern]);
      for (uint32_t filter = 0u; filter < RgbaColorFilterCount; filter++) {
        uint32_t outputIndex;
        float    expected;

        outputIndex = filterBase + filter * TextureValueCount + valueIndex;
        expected    = (current[filter] + next[filter]) * 0.5f;
        if (fabsf(cudaOutput[outputIndex] - expected) > tolerance[filter]) {
          fprintf(stderr,
                  "CUDA color filter mismatch at %u/%u/%u: %.9g != %.9g\n",
                  filter,
                  texel,
                  channel,
                  cudaOutput[outputIndex],
                  expected);
          goto cleanup;
        }
      }
    }
  }
  for (uint32_t filter = 0u; filter < NarrowColorFilterCount; filter++) {
    InteropFormatCase          formatCase;
    const InteropNarrowFormat *format;
    float                      tolerance;

    formatCase = narrowFilterFormats[filter];
    format     = &narrowFormats[(uint32_t)formatCase - RgbaFormatCount];
    tolerance = format->componentBytes == 1u
      ? 0.005f
      : format->valueKind == InteropValueFloat &&
          format->componentBytes == 2u
        ? 0.002f
        : format->componentBytes == 2u ? 0.00005f : 0.00001f;
    for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
      uint32_t x, nextTexel;

      x         = texel % TextureWidth;
      nextTexel = x + 1u < TextureWidth ? texel + 1u : texel;
      for (uint32_t channel = 0u; channel < format->channelCount; channel++) {
        uint32_t component, nextComponent;
        uint32_t inputBits, outputBits;
        float    current, next, ignored;
        uint32_t outputIndex;

        component     = texel * format->channelCount + channel;
        nextComponent = nextTexel * format->channelCount + channel;
        narrow_reference(format,
                         component,
                         &inputBits,
                         &outputBits,
                         &current,
                         &ignored);
        narrow_reference(format,
                         nextComponent,
                         &inputBits,
                         &outputBits,
                         &next,
                         &ignored);
        outputIndex = filterBase +
                      (RgbaColorFilterCount + filter) * TextureValueCount +
                      texel * 4u + channel;
        if (fabsf(cudaOutput[outputIndex] - (current + next) * 0.5f) >
            tolerance) {
          fprintf(stderr,
                  "CUDA narrow filter mismatch at %u/%u/%u\n",
                  filter,
                  texel,
                  channel);
          goto cleanup;
        }
      }
    }
  }
  for (uint32_t texel = 0u; texel < TextureTexelCount; texel++) {
    uint32_t layer, rowStart;

    layer    = texel / (TextureWidth * TextureHeight);
    rowStart = (texel / TextureWidth) * TextureWidth;
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t valueIndex, outputIndex;
      float    expected[SamplerModeCount];

      valueIndex  = texel * 4u + channel;
      expected[0] = input[(rowStart + 1u) * 4u + channel];
      expected[1] = input[(rowStart + 6u) * 4u + channel];
      expected[2] = input[(rowStart + 7u) * 4u + channel];
      expected[3] = state->mipFilterShared
        ? (input[valueIndex] +
           1000.0f + (float)(layer * 16u + channel)) * 0.5f
        : input[valueIndex];
      expected[4] = input[valueIndex];
      for (uint32_t mode = 0u; mode < SamplerModeCount; mode++) {
        outputIndex = samplerBase + mode * TextureValueCount + valueIndex;
        if (fabsf(cudaOutput[outputIndex] - expected[mode]) > 0.0001f) {
          fprintf(stderr,
                  "CUDA sampler mode mismatch at %u/%u/%u: %.9g != %.9g\n",
                  mode,
                  texel,
                  channel,
                  cudaOutput[outputIndex],
                  expected[mode]);
          goto cleanup;
        }
      }
    }
  }
  ok = 1;

cleanup:
  if (transferPass) {
    GPUEndTransferPass(transferPass);
  }
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  if (acquireCmdb) {
    (void)GPUDiscardCommandBuffer(acquireCmdb);
  }
  if (cudaCmdb) {
    (void)GPUDiscardCommandBuffer(cudaCmdb);
  }
  if (releaseCmdb) {
    (void)GPUDiscardCommandBuffer(releaseCmdb);
  }
  if (!acquireSubmitted && cudaSubmitted) {
    (void)GPUWaitFence(cudaFence, UINT64_MAX);
  }
  if (!cudaSubmitted && releaseSubmitted) {
    (void)GPUWaitFence(state->releaseFence, UINT64_MAX);
  }
  GPUDestroyFence(cudaFence);
  GPUResetFence(state->acquireFence);
  GPUResetFence(state->releaseFence);
  if (!ok) {
    fprintf(stderr, "graphics/CUDA texture stage failed: %s\n", failure);
  }
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstance                 *graphicsInstance, *cudaInstance;
  GPUAdapter                  *graphicsAdapter, *cudaAdapter;
  GPUShaderLibrary            *library;
  GPUShaderLibrary            *textureLibrary;
  GPUShaderLayout             *shaderLayout;
  GPUShaderLayout             *textureLayout;
  const char                  *graphicsBackend;
  void                        *artifact;
  void                        *textureArtifact;
  GPUInstanceCreateInfo        graphicsInstanceInfo = {0};
  GPUInstanceCreateInfo        cudaInstanceInfo = {0};
  GPUBufferCreateInfo          graphicsBufferInfo = {0};
  GPUBufferCreateInfo          cudaBufferInfo = {0};
  GPUBufferCreateInfo          paramsBufferInfo = {0};
  GPUBufferCreateInfo          textureReadbackInfo = {0};
  GPUBufferCreateInfo          textureCudaReadbackInfo = {0};
  GPUBufferCreateInfo          srgbReadbackInfo = {0};
  GPUBufferCreateInfo          packedReadbackInfo = {0};
  GPUBufferCreateInfo          bgraReadbackInfo = {0};
  GPUBufferCreateInfo          depth32ReadbackInfo = {0};
  GPUBufferCreateInfo          depth16ReadbackInfo = {0};
  GPUTextureCreateInfo         graphicsTextureInfo = {0};
  GPUTextureCreateInfo         cudaTextureInfo = {0};
  GPUTextureCreateInfo         graphicsCubeTextureInfo = {0};
  GPUTextureCreateInfo         cudaCubeTextureInfo = {0};
  GPUTextureCreateInfo         graphicsCubeArrayTextureInfo = {0};
  GPUTextureCreateInfo         cudaCubeArrayTextureInfo = {0};
  GPUTextureCreateInfo         graphicsSrgbTextureInfo = {0};
  GPUTextureCreateInfo         cudaSrgbTextureInfo = {0};
  GPUTextureCreateInfo         storageSrgbCudaInfo = {0};
  GPUTextureCreateInfo         graphicsPackedTextureInfo = {0};
  GPUTextureCreateInfo         cudaPackedTextureInfo = {0};
  GPUTextureCreateInfo         sampledPackedCudaInfo = {0};
  GPUTextureCreateInfo         graphicsBgraTextureInfo = {0};
  GPUTextureCreateInfo         cudaBgraTextureInfo = {0};
  GPUTextureCreateInfo         sampledBgraCudaInfo = {0};
  GPUTextureCreateInfo         graphicsDepth32TextureInfo = {0};
  GPUTextureCreateInfo         cudaDepth32TextureInfo = {0};
  GPUTextureCreateInfo         graphicsDepth16TextureInfo = {0};
  GPUTextureCreateInfo         cudaDepth16TextureInfo = {0};
  GPUTextureCreateInfo         storageDepthCudaInfo = {0};
  GPUTextureCreateInfo         layeredMipGraphicsInfo = {0};
  GPUTextureCreateInfo         layeredMipCudaInfo = {0};
  GPUTextureViewCreateInfo     textureViewInfo = {0};
  GPUSemaphoreCreateInfo       semaphoreInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBindGroupEntry            paramsEntry = {0};
  GPUBindGroupEntry            dataEntries[2] = {0};
  GPUBindGroupEntry            textureEntries[10 + RgbaFormatCount * 2] = {0};
  GPUBindGroupEntry            narrowEntries[NarrowFormatCount * 2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUMemoryRequirements        memoryRequirements;
  GPUResult                    textureRequirementsResult;
  GPUResult                    textureCreateResult;
  GPUResult                    cubeRequirementsResult;
  GPUResult                    cubeCreateResult;
  GPUResult                    cubeArrayRequirementsResult;
  GPUResult                    cubeArrayCreateResult;
  GPUResult                    srgbRequirementsResult;
  GPUResult                    srgbCreateResult;
  GPUResult                    packedRequirementsResult;
  GPUResult                    packedCreateResult;
  GPUResult                    bgraRequirementsResult;
  GPUResult                    bgraCreateResult;
  GPUResult                    depth32RequirementsResult;
  GPUResult                    depth32CreateResult;
  GPUResult                    depth16RequirementsResult;
  GPUResult                    depth16CreateResult;
  AdapterList                  graphicsAdapters = {0}, cudaAdapters = {0};
  RoundtripState               state = {0};
  uint64_t                     artifactSize;
  uint64_t                     textureArtifactSize;
  Params                       params = {2.0f, 1.0f};
  bool                         cudaFirst;
  bool                         vulkanGraphics;
  int                          status;

  if (argc != 3) {
    fprintf(stderr,
            "usage: gpu-graphics-cuda-interop-usl buffer.us texture.us\n");
    return 1;
  }

  graphicsInstance = NULL;
  cudaInstance     = NULL;
  graphicsAdapter  = NULL;
  cudaAdapter      = NULL;
  library          = NULL;
  textureLibrary   = NULL;
  shaderLayout     = NULL;
  textureLayout    = NULL;
  graphicsBackend  = getenv("GPU_GRAPHICS_BACKEND");
  cudaFirst        = getenv("GPU_CUDA_FIRST") != NULL;
  artifactSize        = 0u;
  textureArtifactSize = 0u;
  artifact            = read_file(argv[1], &artifactSize);
  textureArtifact     = read_file(argv[2], &textureArtifactSize);
  status              = 1;
  if (!artifact || !textureArtifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  graphicsInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  graphicsInstanceInfo.chain.structSize = sizeof(graphicsInstanceInfo);
#if defined(_WIN32) || defined(WIN32)
  if (!graphicsBackend || strcmp(graphicsBackend, "dx12") == 0) {
    graphicsInstanceInfo.preferredBackend = GPU_BACKEND_DX12;
    vulkanGraphics = false;
  } else if (strcmp(graphicsBackend, "vulkan") == 0) {
    graphicsInstanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
    vulkanGraphics = true;
  } else {
    fprintf(stderr, "unsupported graphics backend: %s\n", graphicsBackend);
    goto cleanup;
  }
#else
  (void)graphicsBackend;
  graphicsInstanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  vulkanGraphics = true;
#endif
  graphicsInstanceInfo.enableValidation = true;
  cudaInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  cudaInstanceInfo.chain.structSize = sizeof(cudaInstanceInfo);
  cudaInstanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  cudaInstanceInfo.enableValidation = true;
  if (GPUCreateInstance(&graphicsInstanceInfo, &graphicsInstance) != GPU_OK ||
      !graphicsInstance ||
      GPUCreateInstance(&cudaInstanceInfo, &cudaInstance) != GPU_OK ||
      !cudaInstance ||
      enumerate_adapters(graphicsInstance, &graphicsAdapters) != GPU_OK ||
      enumerate_adapters(cudaInstance, &cudaAdapters) != GPU_OK ||
      !find_matching_adapters(&graphicsAdapters,
                              &cudaAdapters,
                              &graphicsAdapter,
                              &cudaAdapter)) {
    puts("matching graphics/CUDA adapters unavailable");
    status = 77;
    goto cleanup;
  }

  state.graphicsDevice = GPUCreateDeviceWithDefaultQueues(graphicsAdapter);
  state.cudaDevice     = GPUCreateDeviceWithDefaultQueues(cudaAdapter);
  state.graphicsQueue  = GPUGetQueue(state.graphicsDevice,
                                     GPU_QUEUE_GRAPHICS,
                                     0u);
  state.cudaQueue      = GPUGetQueue(state.cudaDevice, GPU_QUEUE_COMPUTE, 0u);
  if (!state.graphicsDevice || !state.cudaDevice ||
      !state.graphicsQueue || !state.cudaQueue ||
      GPUCreateDeviceInteropEXT(cudaFirst
                                  ? state.cudaDevice
                                  : state.graphicsDevice,
                                cudaFirst
                                  ? state.graphicsDevice
                                  : state.cudaDevice,
                                &state.interop) != GPU_OK ||
      !state.interop) {
    fprintf(stderr, "graphics/CUDA device interop creation failed\n");
    goto cleanup;
  }
  if (GPUSetDeviceErrorCallback(state.cudaDevice,
                                device_error,
                                NULL) != GPU_OK) {
    fprintf(stderr, "CUDA interop error callback setup failed\n");
    goto cleanup;
  }

  graphicsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  graphicsBufferInfo.chain.structSize = sizeof(graphicsBufferInfo);
  graphicsBufferInfo.label       = "graphics-cuda-buffer";
  graphicsBufferInfo.sizeBytes   = sizeof(float) * ValueCount;
  graphicsBufferInfo.usage       = GPU_BUFFER_USAGE_COPY_SRC |
                                   GPU_BUFFER_USAGE_COPY_DST;
  cudaBufferInfo                 = graphicsBufferInfo;
  cudaBufferInfo.label           = "cuda-graphics-buffer";
  cudaBufferInfo.usage           = GPU_BUFFER_USAGE_STORAGE |
                                   GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUGetSharedBufferMemoryRequirementsEXT(state.interop,
                                                cudaFirst
                                                  ? &cudaBufferInfo
                                                  : &graphicsBufferInfo,
                                                cudaFirst
                                                  ? &graphicsBufferInfo
                                                  : &cudaBufferInfo,
                                                &memoryRequirements) != GPU_OK ||
      memoryRequirements.sizeBytes < graphicsBufferInfo.sizeBytes ||
      GPUCreateSharedBufferEXT(state.interop,
                               cudaFirst
                                 ? &cudaBufferInfo
                                 : &graphicsBufferInfo,
                               cudaFirst
                                 ? &graphicsBufferInfo
                                 : &cudaBufferInfo,
                               cudaFirst
                                 ? &state.cudaBuffer
                                 : &state.graphicsBuffer,
                               cudaFirst
                                 ? &state.graphicsBuffer
                                 : &state.cudaBuffer) != GPU_OK ||
      !state.graphicsBuffer || !state.cudaBuffer) {
    fprintf(stderr, "shared graphics/CUDA buffer creation failed\n");
    goto cleanup;
  }

  semaphoreInfo.chain.sType      = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label            = "graphics-cuda-timeline";
  if (GPUCreateSharedSemaphoreEXT(state.interop,
                                  &semaphoreInfo,
                                  cudaFirst
                                    ? &state.cudaSemaphore
                                    : &state.graphicsSemaphore,
                                  cudaFirst
                                    ? &state.graphicsSemaphore
                                    : &state.cudaSemaphore) != GPU_OK ||
      !state.graphicsSemaphore || !state.cudaSemaphore ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.releaseFence) !=
        GPU_OK ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.acquireFence) !=
        GPU_OK ||
      !state.releaseFence || !state.acquireFence) {
    fprintf(stderr, "shared graphics/CUDA synchronization creation failed\n");
    goto cleanup;
  }

  graphicsTextureInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  graphicsTextureInfo.chain.structSize = sizeof(graphicsTextureInfo);
  graphicsTextureInfo.label            = "graphics-cuda-texture";
  graphicsTextureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  graphicsTextureInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  graphicsTextureInfo.width            = vulkanGraphics
                                           ? TextureWidth
                                           : TextureBaseWidth;
  graphicsTextureInfo.height           = vulkanGraphics
                                           ? TextureHeight
                                           : TextureBaseHeight;
  graphicsTextureInfo.depthOrLayers    = TextureLayers;
  graphicsTextureInfo.mipLevelCount    = vulkanGraphics
                                           ? 1u
                                           : TextureMipCount;
  graphicsTextureInfo.sampleCount      = 1u;
  graphicsTextureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                         GPU_TEXTURE_USAGE_COPY_SRC |
                                         GPU_TEXTURE_USAGE_COPY_DST;
  cudaTextureInfo                      = graphicsTextureInfo;
  cudaTextureInfo.label                = "cuda-graphics-texture";
  cudaTextureInfo.usage                = GPU_TEXTURE_USAGE_SAMPLED |
                                         GPU_TEXTURE_USAGE_STORAGE;
#if defined(_WIN32) || defined(WIN32)
  if (vulkanGraphics) {
    layeredMipGraphicsInfo               = graphicsTextureInfo;
    layeredMipGraphicsInfo.width         = TextureBaseWidth;
    layeredMipGraphicsInfo.height        = TextureBaseHeight;
    layeredMipGraphicsInfo.mipLevelCount = TextureMipCount;
    layeredMipCudaInfo                   = cudaTextureInfo;
    layeredMipCudaInfo.width             = TextureBaseWidth;
    layeredMipCudaInfo.height            = TextureBaseHeight;
    layeredMipCudaInfo.mipLevelCount     = TextureMipCount;
    if (GPUGetSharedTextureMemoryRequirementsEXT(
          state.interop,
          cudaFirst ? &layeredMipCudaInfo : &layeredMipGraphicsInfo,
          cudaFirst ? &layeredMipGraphicsInfo : &layeredMipCudaInfo,
          &memoryRequirements
        ) != GPU_ERROR_UNSUPPORTED) {
      fprintf(stderr, "layered Vulkan/CUDA mip guard failed\n");
      goto cleanup;
    }
  }
#else
  (void)layeredMipGraphicsInfo;
  (void)layeredMipCudaInfo;
#endif
  state.textureMipLevel = vulkanGraphics ? 0u : TextureMipLevel;
  state.depth16Shared   = !vulkanGraphics;
  state.mipFilterShared = !vulkanGraphics;
  textureRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaTextureInfo : &graphicsTextureInfo,
    cudaFirst ? &graphicsTextureInfo : &cudaTextureInfo,
    &memoryRequirements
  );
  textureCreateResult = textureRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state.interop,
                                cudaFirst
                                  ? &cudaTextureInfo
                                  : &graphicsTextureInfo,
                                cudaFirst
                                  ? &graphicsTextureInfo
                                  : &cudaTextureInfo,
                                cudaFirst
                                  ? &state.cudaTexture
                                  : &state.graphicsTexture,
                                cudaFirst
                                  ? &state.graphicsTexture
                                  : &state.cudaTexture)
    : textureRequirementsResult;
  if (textureRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      textureCreateResult != GPU_OK ||
      !state.graphicsTexture || !state.cudaTexture) {
    fprintf(stderr,
            "shared graphics/CUDA texture creation failed (%d, %d)\n",
            textureRequirementsResult,
            textureCreateResult);
    goto cleanup;
  }

  graphicsCubeTextureInfo               = graphicsTextureInfo;
  graphicsCubeTextureInfo.label         = "graphics-cuda-cube-texture";
  graphicsCubeTextureInfo.depthOrLayers = CubeLayers;
  cudaCubeTextureInfo                   = graphicsCubeTextureInfo;
  cudaCubeTextureInfo.label             = "cuda-graphics-cube-texture";
  cudaCubeTextureInfo.usage             = GPU_TEXTURE_USAGE_SAMPLED;
  cubeRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaCubeTextureInfo : &graphicsCubeTextureInfo,
    cudaFirst ? &graphicsCubeTextureInfo : &cudaCubeTextureInfo,
    &memoryRequirements
  );
  cubeCreateResult = cubeRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state.interop,
                                cudaFirst
                                  ? &cudaCubeTextureInfo
                                  : &graphicsCubeTextureInfo,
                                cudaFirst
                                  ? &graphicsCubeTextureInfo
                                  : &cudaCubeTextureInfo,
                                cudaFirst
                                  ? &state.cudaCubeTexture
                                  : &state.graphicsCubeTexture,
                                cudaFirst
                                  ? &state.graphicsCubeTexture
                                  : &state.cudaCubeTexture)
    : cubeRequirementsResult;
  if (cubeRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      cubeCreateResult != GPU_OK ||
      !state.graphicsCubeTexture || !state.cudaCubeTexture) {
    fprintf(stderr,
            "shared graphics/CUDA cube texture creation failed (%d, %d)\n",
            cubeRequirementsResult,
            cubeCreateResult);
    goto cleanup;
  }

  graphicsCubeArrayTextureInfo               = graphicsTextureInfo;
  graphicsCubeArrayTextureInfo.label         =
    "graphics-cuda-cube-array-texture";
  graphicsCubeArrayTextureInfo.depthOrLayers = CubeArrayLayers;
  cudaCubeArrayTextureInfo                   = graphicsCubeArrayTextureInfo;
  cudaCubeArrayTextureInfo.label             =
    "cuda-graphics-cube-array-texture";
  cudaCubeArrayTextureInfo.usage             = GPU_TEXTURE_USAGE_SAMPLED;
  cubeArrayRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaCubeArrayTextureInfo : &graphicsCubeArrayTextureInfo,
    cudaFirst ? &graphicsCubeArrayTextureInfo : &cudaCubeArrayTextureInfo,
    &memoryRequirements
  );
  cubeArrayCreateResult = cubeArrayRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaCubeArrayTextureInfo : &graphicsCubeArrayTextureInfo,
        cudaFirst ? &graphicsCubeArrayTextureInfo : &cudaCubeArrayTextureInfo,
        cudaFirst
          ? &state.cudaCubeArrayTexture
          : &state.graphicsCubeArrayTexture,
        cudaFirst
          ? &state.graphicsCubeArrayTexture
          : &state.cudaCubeArrayTexture
      )
    : cubeArrayRequirementsResult;
  if (cubeArrayRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      cubeArrayCreateResult != GPU_OK ||
      !state.graphicsCubeArrayTexture || !state.cudaCubeArrayTexture) {
    fprintf(stderr,
            "shared graphics/CUDA cube-array creation failed (%d, %d)\n",
            cubeArrayRequirementsResult,
            cubeArrayCreateResult);
    goto cleanup;
  }

  graphicsSrgbTextureInfo        = graphicsTextureInfo;
  graphicsSrgbTextureInfo.label  = "graphics-cuda-srgb-texture";
  graphicsSrgbTextureInfo.format = GPU_FORMAT_RGBA8_UNORM_SRGB;
  cudaSrgbTextureInfo            = graphicsSrgbTextureInfo;
  cudaSrgbTextureInfo.label      = "cuda-graphics-srgb-texture";
  cudaSrgbTextureInfo.usage      = GPU_TEXTURE_USAGE_SAMPLED;
  storageSrgbCudaInfo            = cudaSrgbTextureInfo;
  storageSrgbCudaInfo.usage     |= GPU_TEXTURE_USAGE_STORAGE;
  if (GPUGetSharedTextureMemoryRequirementsEXT(
        state.interop,
        cudaFirst ? &storageSrgbCudaInfo : &graphicsSrgbTextureInfo,
        cudaFirst ? &graphicsSrgbTextureInfo : &storageSrgbCudaInfo,
        &memoryRequirements
      ) != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "shared graphics/CUDA sRGB storage guard failed\n");
    goto cleanup;
  }
  srgbRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaSrgbTextureInfo : &graphicsSrgbTextureInfo,
    cudaFirst ? &graphicsSrgbTextureInfo : &cudaSrgbTextureInfo,
    &memoryRequirements
  );
  srgbCreateResult = srgbRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaSrgbTextureInfo : &graphicsSrgbTextureInfo,
        cudaFirst ? &graphicsSrgbTextureInfo : &cudaSrgbTextureInfo,
        cudaFirst ? &state.cudaSrgbTexture : &state.graphicsSrgbTexture,
        cudaFirst ? &state.graphicsSrgbTexture : &state.cudaSrgbTexture
      )
    : srgbRequirementsResult;
  if (srgbRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      srgbCreateResult != GPU_OK ||
      !state.graphicsSrgbTexture || !state.cudaSrgbTexture) {
    fprintf(stderr,
            "shared graphics/CUDA sRGB creation failed (%d, %d)\n",
            srgbRequirementsResult,
            srgbCreateResult);
    goto cleanup;
  }

  graphicsPackedTextureInfo        = graphicsTextureInfo;
  graphicsPackedTextureInfo.label  = "graphics-cuda-rg11b10-texture";
  graphicsPackedTextureInfo.format = GPU_FORMAT_RG11B10_UFLOAT;
  cudaPackedTextureInfo            = graphicsPackedTextureInfo;
  cudaPackedTextureInfo.label      = "cuda-graphics-rg11b10-texture";
  cudaPackedTextureInfo.usage      = GPU_TEXTURE_USAGE_STORAGE;
  sampledPackedCudaInfo            = cudaPackedTextureInfo;
  sampledPackedCudaInfo.usage      = GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUGetSharedTextureMemoryRequirementsEXT(
        state.interop,
        cudaFirst ? &sampledPackedCudaInfo : &graphicsPackedTextureInfo,
        cudaFirst ? &graphicsPackedTextureInfo : &sampledPackedCudaInfo,
        &memoryRequirements
      ) != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "shared graphics/CUDA rg11b10 sampled guard failed\n");
    goto cleanup;
  }
  packedRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaPackedTextureInfo : &graphicsPackedTextureInfo,
    cudaFirst ? &graphicsPackedTextureInfo : &cudaPackedTextureInfo,
    &memoryRequirements
  );
  packedCreateResult = packedRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaPackedTextureInfo : &graphicsPackedTextureInfo,
        cudaFirst ? &graphicsPackedTextureInfo : &cudaPackedTextureInfo,
        cudaFirst ? &state.cudaPackedTexture : &state.graphicsPackedTexture,
        cudaFirst ? &state.graphicsPackedTexture : &state.cudaPackedTexture
      )
    : packedRequirementsResult;
  if (packedRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      packedCreateResult != GPU_OK ||
      !state.graphicsPackedTexture || !state.cudaPackedTexture) {
    fprintf(stderr,
            "shared graphics/CUDA rg11b10 creation failed (%d, %d)\n",
            packedRequirementsResult,
            packedCreateResult);
    goto cleanup;
  }

  graphicsBgraTextureInfo        = graphicsTextureInfo;
  graphicsBgraTextureInfo.label  = "graphics-cuda-bgra8-texture";
  graphicsBgraTextureInfo.format = GPU_FORMAT_BGRA8_UNORM;
  cudaBgraTextureInfo            = graphicsBgraTextureInfo;
  cudaBgraTextureInfo.label      = "cuda-graphics-bgra8-texture";
  cudaBgraTextureInfo.usage      = GPU_TEXTURE_USAGE_STORAGE;
  sampledBgraCudaInfo            = cudaBgraTextureInfo;
  sampledBgraCudaInfo.usage      = GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUGetSharedTextureMemoryRequirementsEXT(
        state.interop,
        cudaFirst ? &sampledBgraCudaInfo : &graphicsBgraTextureInfo,
        cudaFirst ? &graphicsBgraTextureInfo : &sampledBgraCudaInfo,
        &memoryRequirements
      ) != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "shared graphics/CUDA BGRA8 sampled guard failed\n");
    goto cleanup;
  }
  bgraRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaBgraTextureInfo : &graphicsBgraTextureInfo,
    cudaFirst ? &graphicsBgraTextureInfo : &cudaBgraTextureInfo,
    &memoryRequirements
  );
  bgraCreateResult = bgraRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaBgraTextureInfo : &graphicsBgraTextureInfo,
        cudaFirst ? &graphicsBgraTextureInfo : &cudaBgraTextureInfo,
        cudaFirst ? &state.cudaBgraTexture : &state.graphicsBgraTexture,
        cudaFirst ? &state.graphicsBgraTexture : &state.cudaBgraTexture
      )
    : bgraRequirementsResult;
  if (bgraRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      bgraCreateResult != GPU_OK ||
      !state.graphicsBgraTexture || !state.cudaBgraTexture) {
    fprintf(stderr,
            "shared graphics/CUDA BGRA8 creation failed (%d, %d)\n",
            bgraRequirementsResult,
            bgraCreateResult);
    goto cleanup;
  }

  graphicsDepth32TextureInfo        = graphicsTextureInfo;
  graphicsDepth32TextureInfo.label  = "graphics-cuda-depth32-texture";
  graphicsDepth32TextureInfo.format = GPU_FORMAT_DEPTH32_FLOAT;
  graphicsDepth32TextureInfo.usage  = GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                                      GPU_TEXTURE_USAGE_COPY_SRC |
                                      GPU_TEXTURE_USAGE_COPY_DST;
  cudaDepth32TextureInfo            = graphicsDepth32TextureInfo;
  cudaDepth32TextureInfo.label      = "cuda-graphics-depth32-texture";
  cudaDepth32TextureInfo.usage      = GPU_TEXTURE_USAGE_SAMPLED;
  storageDepthCudaInfo              = cudaDepth32TextureInfo;
  storageDepthCudaInfo.usage        = GPU_TEXTURE_USAGE_STORAGE;
  if (GPUGetSharedTextureMemoryRequirementsEXT(
        state.interop,
        cudaFirst ? &storageDepthCudaInfo : &graphicsDepth32TextureInfo,
        cudaFirst ? &graphicsDepth32TextureInfo : &storageDepthCudaInfo,
        &memoryRequirements
      ) != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "shared graphics/CUDA depth32 storage guard failed\n");
    goto cleanup;
  }
  depth32RequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaDepth32TextureInfo : &graphicsDepth32TextureInfo,
    cudaFirst ? &graphicsDepth32TextureInfo : &cudaDepth32TextureInfo,
    &memoryRequirements
  );
  depth32CreateResult = depth32RequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaDepth32TextureInfo : &graphicsDepth32TextureInfo,
        cudaFirst ? &graphicsDepth32TextureInfo : &cudaDepth32TextureInfo,
        cudaFirst
          ? &state.cudaDepth32Texture
          : &state.graphicsDepth32Texture,
        cudaFirst
          ? &state.graphicsDepth32Texture
          : &state.cudaDepth32Texture
      )
    : depth32RequirementsResult;
  if (depth32RequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      depth32CreateResult != GPU_OK ||
      !state.graphicsDepth32Texture || !state.cudaDepth32Texture) {
    fprintf(stderr,
            "shared graphics/CUDA depth32 creation failed (%d, %d)\n",
            depth32RequirementsResult,
            depth32CreateResult);
    goto cleanup;
  }

  graphicsDepth16TextureInfo        = graphicsDepth32TextureInfo;
  graphicsDepth16TextureInfo.label  = "graphics-cuda-depth16-texture";
  graphicsDepth16TextureInfo.format = GPU_FORMAT_DEPTH16_UNORM;
  cudaDepth16TextureInfo            = graphicsDepth16TextureInfo;
  cudaDepth16TextureInfo.label      = "cuda-graphics-depth16-texture";
  cudaDepth16TextureInfo.usage      = GPU_TEXTURE_USAGE_SAMPLED;
  storageDepthCudaInfo              = cudaDepth16TextureInfo;
  storageDepthCudaInfo.usage        = GPU_TEXTURE_USAGE_STORAGE;
  if (GPUGetSharedTextureMemoryRequirementsEXT(
        state.interop,
        cudaFirst ? &storageDepthCudaInfo : &graphicsDepth16TextureInfo,
        cudaFirst ? &graphicsDepth16TextureInfo : &storageDepthCudaInfo,
        &memoryRequirements
      ) != GPU_ERROR_UNSUPPORTED) {
    fprintf(stderr, "shared graphics/CUDA depth16 storage guard failed\n");
    goto cleanup;
  }
  depth16RequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaDepth16TextureInfo : &graphicsDepth16TextureInfo,
    cudaFirst ? &graphicsDepth16TextureInfo : &cudaDepth16TextureInfo,
    &memoryRequirements
  );
  depth16CreateResult = state.depth16Shared &&
                        depth16RequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaDepth16TextureInfo : &graphicsDepth16TextureInfo,
        cudaFirst ? &graphicsDepth16TextureInfo : &cudaDepth16TextureInfo,
        cudaFirst
          ? &state.cudaDepth16Texture
          : &state.graphicsDepth16Texture,
        cudaFirst
          ? &state.graphicsDepth16Texture
          : &state.cudaDepth16Texture
      )
    : depth16RequirementsResult;
  if ((!state.depth16Shared &&
       depth16RequirementsResult != GPU_ERROR_UNSUPPORTED) ||
      (state.depth16Shared &&
       (depth16RequirementsResult != GPU_OK ||
        memoryRequirements.sizeBytes == 0u ||
        depth16CreateResult != GPU_OK ||
        !state.graphicsDepth16Texture || !state.cudaDepth16Texture))) {
    fprintf(stderr,
            "shared graphics/CUDA depth16 creation failed (%d, %d)\n",
            depth16RequirementsResult,
            depth16CreateResult);
    goto cleanup;
  }

  if (!create_interop_format_texture(&state,
                                     InteropFormatHalf,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA16_FLOAT,
                                     "rgba16f-interop",
                                     sizeof(uint16_t) * HalfTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatUnorm8,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_UNORM,
                                     "rgba8-unorm-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatSnorm8,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_SNORM,
                                     "rgba8-snorm-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatUint8,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_UINT,
                                     "rgba8-uint-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatSint8,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_SINT,
                                     "rgba8-sint-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(
        &state,
        InteropFormatUnorm16,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA16_UNORM,
        "rgba16-unorm-interop",
        sizeof(uint16_t) * WordTextureValueCount,
        cudaFirst
      ) ||
      !create_interop_format_texture(
        &state,
        InteropFormatSnorm16,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA16_SNORM,
        "rgba16-snorm-interop",
        sizeof(int16_t) * WordTextureValueCount,
        cudaFirst
      ) ||
      !create_interop_format_texture(
        &state,
        InteropFormatUint16,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA16_UINT,
        "rgba16-uint-interop",
        sizeof(uint16_t) * WordTextureValueCount,
        cudaFirst
      ) ||
      !create_interop_format_texture(
        &state,
        InteropFormatSint16,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA16_SINT,
        "rgba16-sint-interop",
        sizeof(int16_t) * WordTextureValueCount,
        cudaFirst
      ) ||
      !create_interop_format_texture(
        &state,
        InteropFormatUint32,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA32_UINT,
        "rgba32-uint-interop",
        sizeof(uint32_t) * TextureValueCount,
        cudaFirst
      ) ||
      !create_interop_format_texture(
        &state,
        InteropFormatSint32,
        &graphicsTextureInfo,
        GPU_FORMAT_RGBA32_SINT,
        "rgba32-sint-interop",
        sizeof(int32_t) * TextureValueCount,
        cudaFirst
      )) {
    goto cleanup;
  }
  for (uint32_t i = 0u; i < NarrowFormatCount; i++) {
    const InteropNarrowFormat *format;
    uint64_t                   readbackSize;

    format       = &narrowFormats[i];
    readbackSize = (uint64_t)TextureTexelCount * format->channelCount *
                   format->componentBytes;
    if (!create_interop_format_texture(&state,
                                       format->formatCase,
                                       &graphicsTextureInfo,
                                       format->format,
                                       format->label,
                                       readbackSize,
                                       cudaFirst)) {
      goto cleanup;
    }
  }

  textureViewInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  textureViewInfo.chain.structSize = sizeof(textureViewInfo);
  textureViewInfo.label            = "cuda-graphics-texture-view";
  textureViewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  textureViewInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  textureViewInfo.baseMipLevel     = state.textureMipLevel;
  textureViewInfo.mipLevelCount    = 1u;
  textureViewInfo.arrayLayerCount  = TextureLayers;
  textureReadbackInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  textureReadbackInfo.chain.structSize = sizeof(textureReadbackInfo);
  textureReadbackInfo.label            = "graphics-cuda-texture-readback";
  textureReadbackInfo.sizeBytes        =
    TextureValueCount * sizeof(float);
  textureReadbackInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                         GPU_BUFFER_USAGE_COPY_SRC;
  textureCudaReadbackInfo              = textureReadbackInfo;
  textureCudaReadbackInfo.label        = "cuda-texture-readback";
  textureCudaReadbackInfo.sizeBytes    =
    TextureOutputValueCount * sizeof(float);
  textureCudaReadbackInfo.usage        = GPU_BUFFER_USAGE_STORAGE |
                                         GPU_BUFFER_USAGE_COPY_SRC |
                                         GPU_BUFFER_USAGE_COPY_DST;
  srgbReadbackInfo                     = textureReadbackInfo;
  srgbReadbackInfo.label               = "graphics-cuda-srgb-readback";
  srgbReadbackInfo.sizeBytes           = ByteTextureValueCount;
  packedReadbackInfo                   = textureReadbackInfo;
  packedReadbackInfo.label             = "graphics-cuda-rg11b10-readback";
  packedReadbackInfo.sizeBytes         =
    TextureTexelCount * sizeof(uint32_t);
  bgraReadbackInfo                     = textureReadbackInfo;
  bgraReadbackInfo.label               = "graphics-cuda-bgra8-readback";
  bgraReadbackInfo.sizeBytes           = ByteTextureValueCount;
  depth32ReadbackInfo                  = textureReadbackInfo;
  depth32ReadbackInfo.label            = "graphics-cuda-depth32-readback";
  depth32ReadbackInfo.sizeBytes        =
    TextureTexelCount * sizeof(float);
  depth16ReadbackInfo                  = textureReadbackInfo;
  depth16ReadbackInfo.label            = "graphics-cuda-depth16-readback";
  depth16ReadbackInfo.sizeBytes        =
    TextureTexelCount * sizeof(uint16_t);
  if (GPUCreateTextureView(state.cudaTexture,
                           &textureViewInfo,
                           &state.cudaTextureView) != GPU_OK ||
      !state.cudaTextureView) {
    fprintf(stderr, "shared graphics/CUDA storage view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label = "cuda-graphics-sampled-texture-view";
  textureViewInfo.mipLevelCount =
    graphicsTextureInfo.mipLevelCount - state.textureMipLevel;
  if (GPUCreateTextureView(state.cudaTexture,
                           &textureViewInfo,
                           &state.cudaSampledTextureView) != GPU_OK ||
      !state.cudaSampledTextureView) {
    fprintf(stderr, "shared graphics/CUDA sampled view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label           = "cuda-graphics-cube-texture-view";
  textureViewInfo.viewType        = GPU_TEXTURE_VIEW_CUBE;
  textureViewInfo.mipLevelCount   =
    graphicsCubeTextureInfo.mipLevelCount - state.textureMipLevel;
  textureViewInfo.arrayLayerCount = CubeLayers;
  if (GPUCreateTextureView(state.cudaCubeTexture,
                           &textureViewInfo,
                           &state.cudaCubeTextureView) != GPU_OK ||
      !state.cudaCubeTextureView) {
    fprintf(stderr, "shared graphics/CUDA cube view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label           = "cuda-graphics-cube-array-view";
  textureViewInfo.viewType        = GPU_TEXTURE_VIEW_CUBE_ARRAY;
  textureViewInfo.mipLevelCount   =
    graphicsCubeArrayTextureInfo.mipLevelCount - state.textureMipLevel;
  textureViewInfo.arrayLayerCount = CubeArrayLayers;
  if (GPUCreateTextureView(state.cudaCubeArrayTexture,
                           &textureViewInfo,
                           &state.cudaCubeArrayTextureView) != GPU_OK ||
      !state.cudaCubeArrayTextureView) {
    fprintf(stderr, "shared graphics/CUDA cube-array view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label           = "cuda-graphics-srgb-view";
  textureViewInfo.viewType        = GPU_TEXTURE_VIEW_2D_ARRAY;
  textureViewInfo.format          = GPU_FORMAT_RGBA8_UNORM_SRGB;
  textureViewInfo.mipLevelCount   =
    graphicsSrgbTextureInfo.mipLevelCount - state.textureMipLevel;
  textureViewInfo.arrayLayerCount = TextureLayers;
  if (GPUCreateTextureView(state.cudaSrgbTexture,
                           &textureViewInfo,
                           &state.cudaSrgbTextureView) != GPU_OK ||
      !state.cudaSrgbTextureView) {
    fprintf(stderr, "shared graphics/CUDA sRGB view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label         = "cuda-graphics-rg11b10-view";
  textureViewInfo.format        = GPU_FORMAT_RG11B10_UFLOAT;
  textureViewInfo.mipLevelCount = 1u;
  if (GPUCreateTextureView(state.cudaPackedTexture,
                           &textureViewInfo,
                           &state.cudaPackedTextureView) != GPU_OK ||
      !state.cudaPackedTextureView) {
    fprintf(stderr, "shared graphics/CUDA rg11b10 view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label  = "cuda-graphics-bgra8-view";
  textureViewInfo.format = GPU_FORMAT_BGRA8_UNORM;
  if (GPUCreateTextureView(state.cudaBgraTexture,
                           &textureViewInfo,
                           &state.cudaBgraTextureView) != GPU_OK ||
      !state.cudaBgraTextureView) {
    fprintf(stderr, "shared graphics/CUDA BGRA8 view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label         = "cuda-graphics-depth32-view";
  textureViewInfo.format        = GPU_FORMAT_DEPTH32_FLOAT;
  textureViewInfo.mipLevelCount =
    graphicsDepth32TextureInfo.mipLevelCount - state.textureMipLevel;
  if (GPUCreateTextureView(state.cudaDepth32Texture,
                           &textureViewInfo,
                           &state.cudaDepth32TextureView) != GPU_OK ||
      !state.cudaDepth32TextureView) {
    fprintf(stderr, "shared graphics/CUDA depth32 view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label         = "cuda-graphics-depth16-view";
  textureViewInfo.format        = GPU_FORMAT_DEPTH16_UNORM;
  textureViewInfo.mipLevelCount =
    graphicsDepth16TextureInfo.mipLevelCount - state.textureMipLevel;
  if (state.depth16Shared &&
      (GPUCreateTextureView(state.cudaDepth16Texture,
                            &textureViewInfo,
                            &state.cudaDepth16TextureView) != GPU_OK ||
       !state.cudaDepth16TextureView)) {
    fprintf(stderr, "shared graphics/CUDA depth16 view setup failed\n");
    goto cleanup;
  }
  if (GPUCreateBuffer(state.graphicsDevice,
                      &textureReadbackInfo,
                      &state.textureReadback) != GPU_OK ||
      !state.textureReadback ||
      GPUCreateBuffer(state.cudaDevice,
                      &textureCudaReadbackInfo,
                      &state.textureCudaReadback) != GPU_OK ||
      !state.textureCudaReadback ||
      GPUCreateBuffer(state.graphicsDevice,
                      &srgbReadbackInfo,
                      &state.srgbReadback) != GPU_OK ||
      !state.srgbReadback ||
      GPUCreateBuffer(state.graphicsDevice,
                      &packedReadbackInfo,
                      &state.packedReadback) != GPU_OK ||
      !state.packedReadback ||
      GPUCreateBuffer(state.graphicsDevice,
                      &bgraReadbackInfo,
                      &state.bgraReadback) != GPU_OK ||
      !state.bgraReadback ||
      GPUCreateBuffer(state.graphicsDevice,
                      &depth32ReadbackInfo,
                      &state.depth32Readback) != GPU_OK ||
      !state.depth32Readback ||
      (state.depth16Shared &&
       (GPUCreateBuffer(state.graphicsDevice,
                        &depth16ReadbackInfo,
                        &state.depth16Readback) != GPU_OK ||
        !state.depth16Readback))) {
    fprintf(stderr, "shared graphics/CUDA output setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(state.cudaDevice,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(state.cudaDevice,
                            library,
                            &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA USL shader layout creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(state.cudaDevice,
                                    textureArtifact,
                                    textureArtifactSize,
                                    &textureLibrary) != GPU_OK ||
      !textureLibrary ||
      GPUCreateShaderLayout(state.cudaDevice,
                            textureLibrary,
                            &textureLayout) != GPU_OK ||
      !textureLayout || textureLayout->bindGroupLayoutCount != 2u ||
      !textureLayout->bindGroupLayouts ||
      !textureLayout->bindGroupLayouts[0] ||
      !textureLayout->bindGroupLayouts[1] ||
      !textureLayout->pipelineLayout) {
    fprintf(stderr, "CUDA interop texture shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "graphics-cuda-saxpy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "saxpy";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.pipeline) != GPU_OK ||
      !state.pipeline) {
    fprintf(stderr, "CUDA interop compute pipeline creation failed\n");
    goto cleanup;
  }

  pipelineInfo.label      = "graphics-cuda-texture-sample";
  pipelineInfo.layout     = textureLayout->pipelineLayout;
  pipelineInfo.library    = textureLibrary;
  pipelineInfo.entryPoint = "interop_texture_sample_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.textureSamplePipeline) != GPU_OK ||
      !state.textureSamplePipeline) {
    fprintf(stderr, "CUDA interop texture sample pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "graphics-cuda-texture-store";
  pipelineInfo.entryPoint = "interop_texture_store_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.textureStorePipeline) != GPU_OK ||
      !state.textureStorePipeline) {
    fprintf(stderr, "CUDA interop texture store pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "graphics-cuda-narrow-sample";
  pipelineInfo.entryPoint = "interop_narrow_sample_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.narrowSamplePipeline) != GPU_OK ||
      !state.narrowSamplePipeline) {
    fprintf(stderr, "CUDA interop narrow sample pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "graphics-cuda-narrow-store";
  pipelineInfo.entryPoint = "interop_narrow_store_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.narrowStorePipeline) != GPU_OK ||
      !state.narrowStorePipeline) {
    fprintf(stderr, "CUDA interop narrow store pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "graphics-cuda-color-filter";
  pipelineInfo.entryPoint = "interop_color_filter_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.colorFilterPipeline) != GPU_OK ||
      !state.colorFilterPipeline) {
    fprintf(stderr, "CUDA interop color filter pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "graphics-cuda-narrow-filter";
  pipelineInfo.entryPoint = "interop_narrow_filter_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.narrowFilterPipeline) != GPU_OK ||
      !state.narrowFilterPipeline) {
    fprintf(stderr, "CUDA interop narrow filter pipeline failed\n");
    goto cleanup;
  }

  paramsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  paramsBufferInfo.chain.structSize = sizeof(paramsBufferInfo);
  paramsBufferInfo.label            = "graphics-cuda-params";
  paramsBufferInfo.sizeBytes        = sizeof(params);
  paramsBufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                      GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state.cudaDevice,
                      &paramsBufferInfo,
                      &state.paramsBuffer) != GPU_OK ||
      !state.paramsBuffer ||
      GPUQueueWriteBuffer(state.cudaQueue,
                          state.paramsBuffer,
                          0u,
                          &params,
                          sizeof(params)) != GPU_OK) {
    fprintf(stderr, "CUDA interop params buffer creation failed\n");
    goto cleanup;
  }

  paramsEntry.binding       = 0u;
  paramsEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  paramsEntry.buffer.buffer = state.paramsBuffer;
  paramsEntry.buffer.size   = sizeof(params);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "graphics-cuda-params-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &paramsEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.paramsGroup) != GPU_OK ||
      !state.paramsGroup) {
    fprintf(stderr, "CUDA interop params bind group creation failed\n");
    goto cleanup;
  }

  textureEntries[0].binding     = 0u;
  textureEntries[0].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  textureEntries[0].textureView = state.cudaTextureView;
  textureEntries[1].binding       = 1u;
  textureEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  textureEntries[1].buffer.buffer = state.textureCudaReadback;
  textureEntries[1].buffer.size   = textureCudaReadbackInfo.sizeBytes;
  textureEntries[2].binding       = 2u;
  textureEntries[2].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[2].textureView   = state.cudaSampledTextureView;
  textureEntries[3].binding       = 3u;
  textureEntries[3].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[3].textureView   = state.cudaCubeTextureView;
  textureEntries[4].binding       = 4u;
  textureEntries[4].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[4].textureView   = state.cudaCubeArrayTextureView;
  for (uint32_t i = 0u; i < RgbaFormatCount; i++) {
    uint32_t storageBinding, sampledBinding;

    storageBinding = 5u + i * 2u;
    sampledBinding = storageBinding + 1u;
    textureEntries[storageBinding].binding     = storageBinding;
    textureEntries[storageBinding].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    textureEntries[storageBinding].textureView =
      state.formatTextures[i].storageView;
    textureEntries[sampledBinding].binding     = sampledBinding;
    textureEntries[sampledBinding].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    textureEntries[sampledBinding].textureView =
      state.formatTextures[i].sampledView;
  }
  textureEntries[5u + RgbaFormatCount * 2u].binding =
    5u + RgbaFormatCount * 2u;
  textureEntries[5u + RgbaFormatCount * 2u].bindingType =
    GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[5u + RgbaFormatCount * 2u].textureView =
    state.cudaSrgbTextureView;
  textureEntries[6u + RgbaFormatCount * 2u].binding =
    6u + RgbaFormatCount * 2u;
  textureEntries[6u + RgbaFormatCount * 2u].bindingType =
    GPU_BINDING_STORAGE_TEXTURE;
  textureEntries[6u + RgbaFormatCount * 2u].textureView =
    state.cudaPackedTextureView;
  textureEntries[7u + RgbaFormatCount * 2u].binding =
    7u + RgbaFormatCount * 2u;
  textureEntries[7u + RgbaFormatCount * 2u].bindingType =
    GPU_BINDING_STORAGE_TEXTURE;
  textureEntries[7u + RgbaFormatCount * 2u].textureView =
    state.cudaBgraTextureView;
  textureEntries[8u + RgbaFormatCount * 2u].binding =
    8u + RgbaFormatCount * 2u;
  textureEntries[8u + RgbaFormatCount * 2u].bindingType =
    GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[8u + RgbaFormatCount * 2u].textureView =
    state.cudaDepth32TextureView;
  textureEntries[9u + RgbaFormatCount * 2u].binding =
    9u + RgbaFormatCount * 2u;
  textureEntries[9u + RgbaFormatCount * 2u].bindingType =
    GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[9u + RgbaFormatCount * 2u].textureView =
    state.depth16Shared
      ? state.cudaDepth16TextureView
      : state.cudaDepth32TextureView;
  groupInfo.label      = "graphics-cuda-texture-group";
  groupInfo.layout     = textureLayout->bindGroupLayouts[0];
  groupInfo.pEntries   = textureEntries;
  groupInfo.entryCount = 10u + RgbaFormatCount * 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.textureGroup) != GPU_OK ||
      !state.textureGroup) {
    fprintf(stderr, "CUDA interop texture bind group creation failed\n");
    goto cleanup;
  }
  for (uint32_t i = 0u; i < NarrowFormatCount; i++) {
    uint32_t formatCase, storageBinding, sampledBinding;

    formatCase     = RgbaFormatCount + i;
    storageBinding = i * 2u;
    sampledBinding = storageBinding + 1u;
    narrowEntries[storageBinding].binding     = storageBinding;
    narrowEntries[storageBinding].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    narrowEntries[storageBinding].textureView =
      state.formatTextures[formatCase].storageView;
    narrowEntries[sampledBinding].binding     = sampledBinding;
    narrowEntries[sampledBinding].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    narrowEntries[sampledBinding].textureView =
      state.formatTextures[formatCase].sampledView;
  }
  groupInfo.label      = "graphics-cuda-narrow-group";
  groupInfo.layout     = textureLayout->bindGroupLayouts[1];
  groupInfo.pEntries   = narrowEntries;
  groupInfo.entryCount = NarrowFormatCount * 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.narrowGroup) != GPU_OK ||
      !state.narrowGroup) {
    fprintf(stderr, "CUDA interop narrow bind group creation failed\n");
    goto cleanup;
  }

  dataEntries[0].binding       = 0u;
  dataEntries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  dataEntries[0].buffer.buffer = state.cudaBuffer;
  dataEntries[0].buffer.size   = cudaBufferInfo.sizeBytes;
  dataEntries[1].binding       = 1u;
  dataEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  dataEntries[1].buffer.buffer = state.cudaBuffer;
  dataEntries[1].buffer.size   = cudaBufferInfo.sizeBytes;
  groupInfo.label              = "graphics-cuda-data-group";
  groupInfo.layout             = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries           = dataEntries;
  groupInfo.entryCount         = 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.dataGroup) != GPU_OK ||
      !state.dataGroup) {
    fprintf(stderr, "CUDA interop data bind group creation failed\n");
    goto cleanup;
  }

  for (uint32_t iteration = 0u; iteration < RoundtripCount; iteration++) {
    if (!run_roundtrip(&state, iteration)) {
      fprintf(stderr,
              "graphics/CUDA roundtrip %u failed\n",
              iteration + 1u);
      goto cleanup;
    }
  }
  if (!run_texture_roundtrip(&state, 0u)) {
    fprintf(stderr, "graphics/CUDA texture roundtrip failed\n");
    goto cleanup;
  }
  GPUResetStats(state.cudaDevice);
  for (uint32_t i = 0u; i < WarmTextureRoundtripCount; i++) {
    if (!run_texture_roundtrip(&state, i + 1u)) {
      fprintf(stderr, "graphics/CUDA warm texture roundtrip failed\n");
      goto cleanup;
    }
  }
  if (state.cudaDevice->currentFrameStats.hotPathAllocCount != 0u ||
      state.cudaDevice->currentFrameStats.hotPathAllocBytes != 0u ||
      state.cudaDevice->currentFrameStats.hotPathFreeCount != 0u ||
      state.cudaDevice->currentFrameStats.hotPathFreeBytes != 0u) {
    fprintf(stderr,
            "CUDA warm texture path allocated: %llu allocs, %llu frees\n",
            (unsigned long long)
              state.cudaDevice->currentFrameStats.hotPathAllocCount,
            (unsigned long long)
              state.cudaDevice->currentFrameStats.hotPathFreeCount);
    goto cleanup;
  }
  status = 0;

cleanup:
  GPUDestroyBindGroup(state.narrowGroup);
  GPUDestroyBindGroup(state.textureGroup);
  GPUDestroyBindGroup(state.dataGroup);
  GPUDestroyBindGroup(state.paramsGroup);
  GPUDestroyComputePipeline(state.textureStorePipeline);
  GPUDestroyComputePipeline(state.textureSamplePipeline);
  GPUDestroyComputePipeline(state.narrowStorePipeline);
  GPUDestroyComputePipeline(state.narrowSamplePipeline);
  GPUDestroyComputePipeline(state.colorFilterPipeline);
  GPUDestroyComputePipeline(state.narrowFilterPipeline);
  GPUDestroyComputePipeline(state.pipeline);
  GPUDestroyShaderLayout(textureLayout);
  GPUDestroyShaderLibrary(textureLibrary);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyFence(state.acquireFence);
  GPUDestroyFence(state.releaseFence);
  GPUDestroyBuffer(state.paramsBuffer);
  GPUDestroyBuffer(state.textureReadback);
  GPUDestroyBuffer(state.textureCudaReadback);
  GPUDestroyBuffer(state.srgbReadback);
  GPUDestroyBuffer(state.packedReadback);
  GPUDestroyBuffer(state.bgraReadback);
  GPUDestroyBuffer(state.depth32Readback);
  GPUDestroyBuffer(state.depth16Readback);
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUDestroyBuffer(state.formatTextures[i].readback);
    GPUDestroyTextureView(state.formatTextures[i].sampledView);
    GPUDestroyTextureView(state.formatTextures[i].storageView);
  }
  GPUDestroyTextureView(state.cudaSrgbTextureView);
  GPUDestroyTextureView(state.cudaPackedTextureView);
  GPUDestroyTextureView(state.cudaBgraTextureView);
  GPUDestroyTextureView(state.cudaDepth32TextureView);
  GPUDestroyTextureView(state.cudaDepth16TextureView);
  GPUDestroyTextureView(state.cudaCubeArrayTextureView);
  GPUDestroyTextureView(state.cudaCubeTextureView);
  GPUDestroyTextureView(state.cudaSampledTextureView);
  GPUDestroyTextureView(state.cudaTextureView);
  if (cudaFirst) {
    GPUDestroySemaphore(state.cudaSemaphore);
    GPUDestroySemaphore(state.graphicsSemaphore);
    GPUDestroyBuffer(state.cudaBuffer);
    GPUDestroyBuffer(state.graphicsBuffer);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].cudaTexture);
    }
    GPUDestroyTexture(state.cudaSrgbTexture);
    GPUDestroyTexture(state.cudaPackedTexture);
    GPUDestroyTexture(state.cudaBgraTexture);
    GPUDestroyTexture(state.cudaDepth32Texture);
    GPUDestroyTexture(state.cudaDepth16Texture);
    GPUDestroyTexture(state.cudaCubeArrayTexture);
    GPUDestroyTexture(state.cudaCubeTexture);
    GPUDestroyTexture(state.cudaTexture);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].graphicsTexture);
    }
    GPUDestroyTexture(state.graphicsSrgbTexture);
    GPUDestroyTexture(state.graphicsPackedTexture);
    GPUDestroyTexture(state.graphicsBgraTexture);
    GPUDestroyTexture(state.graphicsDepth32Texture);
    GPUDestroyTexture(state.graphicsDepth16Texture);
    GPUDestroyTexture(state.graphicsCubeArrayTexture);
    GPUDestroyTexture(state.graphicsCubeTexture);
    GPUDestroyTexture(state.graphicsTexture);
  } else {
    GPUDestroySemaphore(state.graphicsSemaphore);
    GPUDestroySemaphore(state.cudaSemaphore);
    GPUDestroyBuffer(state.graphicsBuffer);
    GPUDestroyBuffer(state.cudaBuffer);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].graphicsTexture);
    }
    GPUDestroyTexture(state.graphicsSrgbTexture);
    GPUDestroyTexture(state.graphicsPackedTexture);
    GPUDestroyTexture(state.graphicsBgraTexture);
    GPUDestroyTexture(state.graphicsDepth32Texture);
    GPUDestroyTexture(state.graphicsDepth16Texture);
    GPUDestroyTexture(state.graphicsCubeArrayTexture);
    GPUDestroyTexture(state.graphicsCubeTexture);
    GPUDestroyTexture(state.graphicsTexture);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].cudaTexture);
    }
    GPUDestroyTexture(state.cudaSrgbTexture);
    GPUDestroyTexture(state.cudaPackedTexture);
    GPUDestroyTexture(state.cudaBgraTexture);
    GPUDestroyTexture(state.cudaDepth32Texture);
    GPUDestroyTexture(state.cudaDepth16Texture);
    GPUDestroyTexture(state.cudaCubeArrayTexture);
    GPUDestroyTexture(state.cudaCubeTexture);
    GPUDestroyTexture(state.cudaTexture);
  }
  GPUDestroyDeviceInteropEXT(state.interop);
  if (cudaFirst) {
    GPUDestroyDevice(state.cudaDevice);
    GPUDestroyDevice(state.graphicsDevice);
  } else {
    GPUDestroyDevice(state.graphicsDevice);
    GPUDestroyDevice(state.cudaDevice);
  }
  free(cudaAdapters.items);
  free(graphicsAdapters.items);
  if (cudaFirst) {
    GPUDestroyInstance(cudaInstance);
    GPUDestroyInstance(graphicsInstance);
  } else {
    GPUDestroyInstance(graphicsInstance);
    GPUDestroyInstance(cudaInstance);
  }
  free(artifact);
  free(textureArtifact);

  if (status == 0) {
    puts("graphics/CUDA USL interop validation passed");
  }
  return status;
}
