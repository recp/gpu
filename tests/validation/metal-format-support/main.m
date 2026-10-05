/*
 * Copyright (C) 2026 Recep Aslantas
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

#include <gpu/gpu.h>
#include "../../../src/backend/mt/common.h"

typedef enum FilterSupport {
  FILTER_NONE,
  FILTER_ALWAYS,
  FILTER_FLOAT32,
  FILTER_DEPTH32_STENCIL
} FilterSupport;

typedef struct FormatCase {
  GPUFormat      format;
  FilterSupport  filter;
  MTLPixelFormat nativeFormat;
  bool           integer32;
  bool           depth;
} FormatCase;

/* Apple Metal feature tables: allocation, resolve and filtering are distinct. */
static const FormatCase cases[] = {
  {GPU_FORMAT_R32_UINT, FILTER_NONE, MTLPixelFormatR32Uint, true, false},
  {GPU_FORMAT_R32_SINT, FILTER_NONE, MTLPixelFormatR32Sint, true, false},
  {GPU_FORMAT_RG32_UINT, FILTER_NONE, MTLPixelFormatRG32Uint, true, false},
  {GPU_FORMAT_RG32_SINT, FILTER_NONE, MTLPixelFormatRG32Sint, true, false},
  {GPU_FORMAT_RGBA32_UINT, FILTER_NONE, MTLPixelFormatRGBA32Uint, true, false},
  {GPU_FORMAT_RGBA32_SINT, FILTER_NONE, MTLPixelFormatRGBA32Sint, true, false},
  {GPU_FORMAT_R32_FLOAT, FILTER_FLOAT32, MTLPixelFormatR32Float, false, false},
  {GPU_FORMAT_RG32_FLOAT, FILTER_FLOAT32, MTLPixelFormatRG32Float, false, false},
  {GPU_FORMAT_RGBA32_FLOAT, FILTER_FLOAT32, MTLPixelFormatRGBA32Float, false, false},
  {GPU_FORMAT_DEPTH16_UNORM, FILTER_ALWAYS, MTLPixelFormatDepth16Unorm, false, true},
  {GPU_FORMAT_DEPTH32_FLOAT, FILTER_FLOAT32, MTLPixelFormatDepth32Float, false, true},
  {GPU_FORMAT_DEPTH32_FLOAT_STENCIL8, FILTER_DEPTH32_STENCIL, MTLPixelFormatDepth32Float_Stencil8, false, true},
  {GPU_FORMAT_DEPTH24_UNORM_STENCIL8, FILTER_ALWAYS, MTLPixelFormatInvalid, false, true},
  {GPU_FORMAT_STENCIL8, FILTER_NONE, MTLPixelFormatStencil8, false, true},
  {GPU_FORMAT_R16_UINT, FILTER_NONE, MTLPixelFormatR16Uint, false, false},
  {GPU_FORMAT_RGBA8_UNORM, FILTER_ALWAYS, MTLPixelFormatRGBA8Unorm, false, false}
};

static const GPUSampleCountFlags sampleMasks[] = {
  GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT,
  GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_2_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT
};

static bool
check_policy(GPUAdapter *adapter) {
  GPUFormatCapabilities caps;
  GPUAdapterMT         *native;
  const FormatCase     *test;
  GPUSampleCountFlags   expectedSamples;
  uint32_t              flags, mask, i;
  uint32_t              failures = 0u;
  bool                  expectedFilter;
  bool                  supported;

  native = adapter->_priv;

  /* vary cached cold-path facts without sending synthetic capabilities to Metal. */
  for (flags = 0u; flags < 16u; flags++) {
    native->float32Filterable        = (flags & 1u) != 0u;
    native->msaa32Supported          = (flags & 2u) != 0u;
    native->depth24Supported         = (flags & 4u) != 0u;
    native->depth32StencilFilterable = (flags & 8u) != 0u;

    for (mask = 0u; mask < GPU_ARRAY_LEN(sampleMasks); mask++) {
      native->sampleCounts = sampleMasks[mask];

      for (i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
        test            = &cases[i];
        supported       = test->format != GPU_FORMAT_DEPTH24_UNORM_STENCIL8
                          || (flags & 4u) != 0u;
        expectedSamples = sampleMasks[mask];

        if (!supported) {
          expectedSamples = 0u;
        } else if (test->integer32 && !(flags & 2u)) {
          expectedSamples = GPU_SAMPLE_COUNT_1_BIT;
        }

        expectedFilter = supported
                         && (test->filter == FILTER_ALWAYS
                             || (test->filter == FILTER_FLOAT32 && (flags & 1u))
                             || (test->filter == FILTER_DEPTH32_STENCIL && (flags & 8u)));

        if (GPUGetFormatCapabilities(adapter, test->format, &caps) != GPU_OK
            || caps.supportedSampleCounts != expectedSamples
            || caps.filterable != expectedFilter
            || caps.sampled != supported
            || caps.depthStencil != (supported && test->depth)
            || caps.colorAttachment != (supported && !test->depth)) {
          fprintf(stderr, "format=%u flags=%u mask=%u: samples=%u filter=%u\n",
                  test->format, flags, mask, caps.supportedSampleCounts, caps.filterable);
          failures++;
        }
      }
    }
  }

  printf("format-policy: 512 cases, %u failures\n", failures);
  return failures == 0u;
}

static bool
check_native(GPUAdapter *adapter) {
  GPUFormatCapabilities caps;
  GPUAdapterMT         *native;
  const FormatCase     *test;
  MTLTextureDescriptor *desc;
  id<MTLTexture>        texture;
  id<MTLDevice>         device;
  uint32_t              i, samples;
  uint32_t              textures = 0u;
  bool                  stencilFilter;

  native        = adapter->_priv;
  device        = native->device;
  stencilFilter = [device supportsFamily:MTLGPUFamilyMac2];

  if (@available(macOS 14.0, *)) {
    stencilFilter |= [device supportsFamily:MTLGPUFamilyApple9];
  }

  if (@available(macOS 11.0, *)) {
    if (native->float32Filterable != device.supports32BitFloatFiltering
        || native->msaa32Supported != device.supports32BitMSAA
        || native->depth32StencilFilterable != stencilFilter) {
      fprintf(stderr, "native format support cache mismatch\n");
      return false;
    }
  }

  for (i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
    test = &cases[i];

    if (test->nativeFormat == MTLPixelFormatInvalid) {
      continue;
    }

    if (GPUGetFormatCapabilities(adapter, test->format, &caps) != GPU_OK) {
      return false;
    }

    for (samples = 1u; samples <= 8u; samples *= 2u) {
      if (!(caps.supportedSampleCounts & samples)) {
        continue;
      }

      desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:test->nativeFormat
                                                              width:2u
                                                             height:2u
                                                          mipmapped:NO];
      desc.textureType = samples == 1u ? MTLTextureType2D : MTLTextureType2DMultisample;
      desc.sampleCount = samples;
      desc.storageMode = MTLStorageModePrivate;
      desc.usage       = MTLTextureUsageRenderTarget;

      if (!(texture = [device newTextureWithDescriptor:desc])) {
        fprintf(stderr, "native format=%u samples=%u allocation failed\n", test->format, samples);
        return false;
      }

      [texture release];
      textures++;
    }
  }

  printf("native-format: %u advertised attachment allocations passed\n", textures);
  return true;
}

static int
run(void) {
  GPUAdapterMT          saved;
  GPUInstanceCreateInfo info = {0};
  GPUInstance          *instance = NULL;
  GPUAdapter           *adapter  = NULL;
  uint32_t              count    = 1u;
  bool                  ok;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_METAL;

  if (GPUCreateInstance(&info, &instance) != GPU_OK) {
    return 1;
  }

  if (GPUEnumerateAdapters(instance, &count, &adapter) != GPU_OK || count == 0u) {
    GPUDestroyInstance(instance);
    return 77;
  }

  saved = *(GPUAdapterMT *)adapter->_priv;
  ok    = check_native(adapter);
  ok    = check_policy(adapter) && ok;

  *(GPUAdapterMT *)adapter->_priv = saved;
  GPUDestroyInstance(instance);
  return ok ? 0 : 1;
}

int
main(void) {
  @autoreleasepool {
    return run();
  }
}
