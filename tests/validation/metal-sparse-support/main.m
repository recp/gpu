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

#include "../../../src/backend/mt/common.h"
#include <stdio.h>

#if MT_HAS_METAL4 && TARGET_OS_OSX
#define RAY_FEATURE       (UINT64_C(1) << GPU_FEATURE_RAY_QUERY)
#define SPARSE_PLACEMENT  (UINT64_C(1) << GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)
#define SPARSE_BUFFER     (SPARSE_PLACEMENT | (UINT64_C(1) << GPU_FEATURE_SPARSE_BUFFERS))
#define SPARSE_TEXTURE    (SPARSE_PLACEMENT | (UINT64_C(1) << GPU_FEATURE_SPARSE_TEXTURES))
#define IFT_FEATURES      (RAY_FEATURE | (UINT64_C(1) << GPU_FEATURE_INTERSECTION_FUNCTION_TABLE))
#define TIMESTAMP_FEATURE (UINT64_C(1) << GPU_FEATURE_TIMESTAMPS)

#if defined(__MAC_27_0) && defined(__IPHONE_27_0)
#define LAST_APPLE_FAMILY MTLGPUFamilyApple11
#else
#define LAST_APPLE_FAMILY MTLGPUFamilyApple9
#endif

enum {
  METAL4_QUEUE   = 1u,
  METAL4_ALLOC   = 2u,
  METAL4_TABLE   = 4u,
  METAL4_COMPILE = 8u,
  METAL4_ALL     = 15u
};

typedef struct SparseCase {
  const char  *name;
  const char  *mode;
  MTLGPUFamily family;
  uint8_t      methods;
  uint8_t      modern;
  uint8_t      legacy;
  bool         placement;
  bool         automatic;
} SparseCase;

typedef struct ModeCase {
  const char  *name;
  const char  *mode;
  uint64_t     features;
  MTLGPUFamily family;
  uint8_t      methods;
  int8_t       expected;
} ModeCase;

typedef struct TimestampCase {
  const char  *name;
  const char  *mode;
  uint64_t     features;
  uint64_t     frequency;
  MTLGPUFamily family;
  uint8_t      methods;
  int8_t       expectedMode;
  bool         counterSet;
  bool         blit;
  bool         supported;
} TimestampCase;

@interface SparseDevice : NSObject {
@public
  uint64_t     frequency;
  MTLGPUFamily family;
  uint32_t     counterQueries;
  uint8_t      methods;
  bool         placement;
  bool         counterSet;
  bool         blit;
}
@end

static const SparseCase cases[] = {
  {"query-no-apple8", "auto", MTLGPUFamilyApple8, METAL4_ALL, 0u, 7u, false, false},
  {"query-no-apple7", "auto", MTLGPUFamilyApple7, METAL4_ALL, 0u, 7u, false, false},
  {"query-yes-apple8", "auto", MTLGPUFamilyApple8, METAL4_ALL, 7u, 7u, true, false},
  {"query-yes-apple7", "auto", MTLGPUFamilyApple7, METAL4_ALL, 7u, 7u, true, false},
#if defined(__MAC_27_0) && defined(__IPHONE_27_0)
  {"query-no-apple10", "auto", MTLGPUFamilyApple10, METAL4_ALL, 0u, 7u, false, false},
  {"query-no-apple11", "auto", MTLGPUFamilyApple11, METAL4_ALL, 0u, 7u, false, false},
  {"query-yes-apple10", "auto", MTLGPUFamilyApple10, METAL4_ALL, 7u, 7u, true, false},
  {"query-yes-apple11", "auto", MTLGPUFamilyApple11, METAL4_ALL, 7u, 7u, true, false},
#endif
  {"query-yes-other", "auto", MTLGPUFamilyMetal3, METAL4_ALL, 7u, 0u, true, false},
  {"query-no-forced", "metal4", MTLGPUFamilyApple8, METAL4_ALL, 0u, 7u, false, false},
  {"query-yes-forced", "metal4", MTLGPUFamilyApple8, METAL4_ALL, 7u, 7u, true, false},
  {"query-no-default", NULL, MTLGPUFamilyApple8, METAL4_ALL, 0u, 7u, false, false},
  {"query-yes-default", NULL, MTLGPUFamilyApple8, METAL4_ALL, 7u, 7u, true, false},
  {"automatic-modern", "auto", MTLGPUFamilyMetal3, METAL4_ALL, 0u, 0u, false, true},
  {"automatic-classic", "classic", MTLGPUFamilyMetal3, METAL4_ALL, 1u, 1u, false, true},
  {"placement-classic", "classic", MTLGPUFamilyApple8, METAL4_ALL, 0u, 0u, true, false},
  {"missing-queue", "auto", MTLGPUFamilyApple8, 14u, 0u, 0u, true, false},
  {"missing-allocator", "auto", MTLGPUFamilyApple8, 13u, 0u, 0u, true, false},
  {"missing-table", "auto", MTLGPUFamilyApple8, 11u, 0u, 0u, true, false},
  {"missing-compiler", "auto", MTLGPUFamilyApple8, 7u, 0u, 0u, true, false},
  {"automatic-fallback", "auto", MTLGPUFamilyMetal3, 0u, 1u, 1u, false, true},
  {"forced-no-api", "metal4", MTLGPUFamilyMetal3, 0u, 0u, 0u, false, true},
  {"old-family", "auto", MTLGPUFamilyApple6, METAL4_ALL, 0u, 0u, false, false}
};

static const GPUFeature features[] = {
  GPU_FEATURE_SPARSE_TEXTURES,
  GPU_FEATURE_SPARSE_BUFFERS,
  GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT
};

static const ModeCase modeCases[] = {
  {"default-apple7", NULL, 0u, MTLGPUFamilyApple7, METAL4_ALL, MTCommandMode4},
  {"ray-default-apple7", NULL, RAY_FEATURE, MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic},
  {"ray-auto-apple7", "auto", RAY_FEATURE, MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic},
  {"ray-auto-apple8", "auto", RAY_FEATURE, MTLGPUFamilyApple8, METAL4_ALL, MTCommandModeClassic},
  {"ray-auto-apple9", "auto", RAY_FEATURE, MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4},
  {"sparse-auto-apple7", "auto", SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, MTCommandMode4},
  {"sparse-auto-apple8", "auto", SPARSE_PLACEMENT, MTLGPUFamilyApple8, METAL4_ALL, MTCommandMode4},
  {"both-auto-apple7", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"both-auto-apple8", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple8, METAL4_ALL, -1},
  {"both-auto-apple9", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4},
  {"both-default-apple7", NULL, RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"ray-buffer-apple7", "auto", RAY_FEATURE | SPARSE_BUFFER, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"ray-buffer-apple8", "auto", RAY_FEATURE | SPARSE_BUFFER, MTLGPUFamilyApple8, METAL4_ALL, -1},
  {"ray-texture-apple7", "auto", RAY_FEATURE | SPARSE_TEXTURE, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"ift-sparse-apple7", "auto", IFT_FEATURES | SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"ift-sparse-apple9", "auto", IFT_FEATURES | SPARSE_PLACEMENT, MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4},
  {"ray-classic", "classic", RAY_FEATURE, MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic},
  {"ray-auto-texture-classic", "classic", RAY_FEATURE | (UINT64_C(1) << GPU_FEATURE_SPARSE_TEXTURES),
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic},
  {"ift-auto-texture-classic", "classic", IFT_FEATURES | (UINT64_C(1) << GPU_FEATURE_SPARSE_TEXTURES),
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic},
  {"sparse-classic", "classic", SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"both-classic", "classic", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple9, METAL4_ALL, -1},
  {"ray-metal4-apple7", "metal4", RAY_FEATURE, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"ray-metal4-apple8", "metal4", RAY_FEATURE, MTLGPUFamilyApple8, METAL4_ALL, -1},
  {"ray-metal4-apple9", "metal4", RAY_FEATURE, MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4},
  {"both-metal4-apple7", "metal4", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple7, METAL4_ALL, -1},
  {"both-metal4-apple9", "metal4", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4},
#if defined(__MAC_27_0) && defined(__IPHONE_27_0)
  {"both-auto-apple10", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple10, METAL4_ALL, MTCommandMode4},
  {"both-auto-apple11", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple11, METAL4_ALL, MTCommandMode4},
  {"both-metal4-apple10", "metal4", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple10, METAL4_ALL, MTCommandMode4},
  {"both-metal4-apple11", "metal4", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple11, METAL4_ALL, MTCommandMode4},
  {"both-classic-apple10", "classic", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple10, METAL4_ALL, -1},
  {"both-classic-apple11", "classic", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple11, METAL4_ALL, -1},
  {"both-missing-compiler10", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple10, 7u, -1},
  {"both-missing-compiler11", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple11, 7u, -1},
#endif
  {"ray-old-api", "auto", RAY_FEATURE, MTLGPUFamilyApple7, 0u, MTCommandModeClassic},
  {"sparse-old-api", "auto", SPARSE_PLACEMENT, MTLGPUFamilyApple7, 0u, -1},
  {"both-old-api", "auto", RAY_FEATURE | SPARSE_PLACEMENT, MTLGPUFamilyApple7, 0u, -1},
  {"forced-old-api", "metal4", 0u, MTLGPUFamilyApple7, 0u, -1},
  {"invalid-mode", "invalid", 0u, MTLGPUFamilyApple7, METAL4_ALL, -1}
};

static const TimestampCase timestampCases[] = {
  {"ray-apple7", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, false, false, true},
  {"ray-apple8", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple8, METAL4_ALL, -1, false, false, true},
  {"ray-default", NULL, RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, false, false, true},
  {"ift-apple7", "auto", IFT_FEATURES | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, false, false, true},
  {"timestamp-only", "auto", TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandMode4, false, false, true},
  {"ray-apple9", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4, false, false, true},
#if defined(__MAC_27_0) && defined(__IPHONE_27_0)
  {"ray-apple10", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple10, METAL4_ALL, MTCommandMode4, false, false, true},
  {"ray-apple11", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple11, METAL4_ALL, MTCommandMode4, false, false, true},
  {"classic-missing-apple10", "classic", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple10, METAL4_ALL, -1, false, false, false},
  {"classic-missing-apple11", "classic", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple11, METAL4_ALL, -1, false, false, false},
#endif
  {"ray-classic-counter7", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic, true, true, true},
  {"ray-classic-counter8", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple8, METAL4_ALL, MTCommandModeClassic, true, true, true},
  {"ray-no-blit", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, true, false, true},
  {"ray-no-counter", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, false, true, true},
  {"forced-classic-missing", "classic", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, false, false, false},
  {"forced-classic-valid", "classic", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic, true, true, true},
  {"classic-timestamp-only", "classic", TIMESTAMP_FEATURE, 0u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic, true, true, true},
  {"classic-no-blit", "classic", TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, true, false, false},
  {"forced-metal4-modern", "metal4", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4, false, false, true},
  {"forced-metal4-old-ray", "metal4", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, -1, true, true, true},
  {"old-api-missing", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, 0u, -1, false, false, false},
  {"old-api-valid", "auto", RAY_FEATURE | TIMESTAMP_FEATURE, 24000000u,
   MTLGPUFamilyApple7, 0u, MTCommandModeClassic, true, true, true},
  {"ray-without-timestamps", "auto", RAY_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic, false, false, true},
  {"zero-frequency", "auto", RAY_FEATURE, 0u,
   MTLGPUFamilyApple9, METAL4_ALL, MTCommandMode4, true, true, false},
  {"classic-without-timestamps", "classic", RAY_FEATURE, 24000000u,
   MTLGPUFamilyApple7, METAL4_ALL, MTCommandModeClassic, false, false, false}
};

static MTCommandMode observedMode;
static uint32_t      modeInitializations;
static uint32_t      unexpectedWork;

GPU_HIDE
bool
mt_supportsFeature(const GPUAdapter *adapter, GPUFeature feature);

GPU_HIDE
GPUDevice*
mt_createDevice(GPUAdapter               *adapter,
                const GPUQueueCreateInfo queues[],
                uint32_t                 queueCount,
                uint64_t                 enabledFeatures);

@implementation SparseDevice
- (BOOL)respondsToSelector:(SEL)selector {
  if (selector == @selector(newMTL4CommandQueue)) {
    return (methods & METAL4_QUEUE) != 0u;
  }

  if (selector == @selector(newCommandAllocator)) {
    return (methods & METAL4_ALLOC) != 0u;
  }

  if (selector == @selector(newArgumentTableWithDescriptor:error:)) {
    return (methods & METAL4_TABLE) != 0u;
  }

  if (selector == @selector(newCompilerWithDescriptor:error:)) {
    return (methods & METAL4_COMPILE) != 0u;
  }

  return [super respondsToSelector:selector];
}

- (BOOL)supportsPlacementSparse {
  return placement;
}

- (BOOL)supportsFamily:(MTLGPUFamily)value {
  return family >= MTLGPUFamilyApple1 && family <= LAST_APPLE_FAMILY
         && value >= MTLGPUFamilyApple1 && value <= family;
}

- (NSUInteger)queryTimestampFrequency {
  counterQueries++;
  return (NSUInteger)frequency;
}

- (NSArray<id<MTLCounterSet>> *)counterSets {
  counterQueries++;
  return counterSet ? @[(id<MTLCounterSet>)self] : @[];
}

- (NSString *)name {
  return MTLCommonCounterSetTimestamp;
}

- (BOOL)supportsCounterSampling:(MTLCounterSamplingPoint)point {
  counterQueries++;
  return blit && point == MTLCounterSamplingPointAtBlitBoundary;
}
@end

/* stop after mode selection, before native compiler or queue work. */
GPU_HIDE
GPUResult
mt_initPipelineCompiler(GPUDeviceMT *device) {
  observedMode = device->commandMode;
  modeInitializations++;
  return GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
void
mt_destroyPipelineCompiler(GPUDeviceMT *device) {
  GPU__UNUSED(device);
  unexpectedWork++;
}

GPU_HIDE
GPUQueue*
mt_newCommandQueue(GPUDevice *device) {
  GPU__UNUSED(device);
  unexpectedWork++;
  return NULL;
}

GPU_HIDE
void
mt_destroyCommandQueue(GPUQueue *queue) {
  GPU__UNUSED(queue);
  unexpectedWork++;
}

static int
run_tests(void) {
  GPUQueueCreateInfo   queues[1] = {{0}};
  GPUAdapterMT         native    = {0};
  GPUAdapter           adapter   = {0};
  SparseDevice        *device;
  const SparseCase    *test;
  const ModeCase      *modeTest;
  const TimestampCase *timestampTest;
  uint32_t             i, j;
  uint32_t             modeFailures = 0u;
  uint8_t              expected;
  bool                 modern;
  bool                 actual;
  bool                 wanted;

  if (@available(macOS 26.0, *)) {
    modern = false;
  } else {
    return 77;
  }

#if defined(__MAC_26_4) && defined(__IPHONE_26_4)
  if (@available(macOS 26.4, *)) {
    modern = true;
  }
#endif
  device        = [SparseDevice new];
  native.device = (id<MTLDevice>)device;
  adapter._priv = &native;

  for (i = 0u; i < GPU_ARRAY_LEN(cases); i++) {
    test = &cases[i];

    if (test->mode) {
      setenv("GPU_METAL_MODE", test->mode, 1);
    } else {
      unsetenv("GPU_METAL_MODE");
    }

    device->family        = test->family;
    device->methods       = test->methods;
    device->placement     = test->placement;
    native.sparseTextures = test->automatic;
    expected              = modern ? test->modern : test->legacy;

    for (j = 0u; j < GPU_ARRAY_LEN(features); j++) {
      actual = mt_supportsFeature(&adapter, features[j]);
      wanted = (expected & (1u << j)) != 0u;

      if (actual != wanted) {
        fprintf(stderr, "%s feature %u: got %u, expected %u\n", test->name, j, actual, wanted);
        [device release];
        return 1;
      }
    }
  }

  native.device         = nil;
  native.sparseTextures = false;
  unsetenv("GPU_METAL_MODE");

  for (j = 0u; j < GPU_ARRAY_LEN(features); j++) {
    if (mt_supportsFeature(NULL, features[j]) || mt_supportsFeature(&adapter, features[j])) {
      [device release];
      return 1;
    }
  }

  native.device = (id<MTLDevice>)device;

  for (i = 0u; i < GPU_ARRAY_LEN(modeCases); i++) {
    modeTest = &modeCases[i];

    if (modeTest->mode) {
      setenv("GPU_METAL_MODE", modeTest->mode, 1);
    } else {
      unsetenv("GPU_METAL_MODE");
    }

    device->family      = modeTest->family;
    device->methods     = modeTest->methods;
    modeInitializations = 0u;
    unexpectedWork      = 0u;

    if (mt_createDevice(&adapter, queues, 1u, modeTest->features)
        || unexpectedWork != 0u
        || modeInitializations != (modeTest->expected >= 0 ? 1u : 0u)
        || (modeInitializations && observedMode != (MTCommandMode)modeTest->expected)) {
      fprintf(stderr,
              "%s: initialized %u, mode %u, expected %d\n",
              modeTest->name,
              modeInitializations,
              observedMode,
              modeTest->expected);
      modeFailures++;
    }
  }

  for (i = 0u; i < GPU_ARRAY_LEN(timestampCases); i++) {
    timestampTest = &timestampCases[i];

    if (timestampTest->mode) {
      setenv("GPU_METAL_MODE", timestampTest->mode, 1);
    } else {
      unsetenv("GPU_METAL_MODE");
    }

    device->family     = timestampTest->family;
    device->methods    = timestampTest->methods;
    device->frequency  = timestampTest->frequency;
    device->counterSet = timestampTest->counterSet;
    device->blit       = timestampTest->blit;

    actual = mt_supportsFeature(&adapter, GPU_FEATURE_TIMESTAMPS);

    device->counterQueries = 0u;
    modeInitializations    = 0u;
    unexpectedWork         = 0u;

    if (actual != timestampTest->supported
        || mt_createDevice(&adapter, queues, 1u, timestampTest->features)
        || unexpectedWork != 0u
        || modeInitializations != (timestampTest->expectedMode >= 0 ? 1u : 0u)
        || (modeInitializations && observedMode != (MTCommandMode)timestampTest->expectedMode)
        || (!(timestampTest->features & TIMESTAMP_FEATURE) && device->counterQueries != 0u)) {
      fprintf(stderr,
              "%s: supported %u, initialized %u, mode %u, expected %d\n",
              timestampTest->name,
              actual,
              modeInitializations,
              observedMode,
              timestampTest->expectedMode);
      modeFailures++;
    }
  }

  [device release];

  if (modeFailures) {
    return 1;
  }

  printf("metal sparse support: %zu cases passed\n", GPU_ARRAY_LEN(cases) * GPU_ARRAY_LEN(features) + 6u);
  printf("metal command modes: %zu cases passed\n", GPU_ARRAY_LEN(modeCases));
  printf("metal timestamp modes: %zu cases passed\n", GPU_ARRAY_LEN(timestampCases));
  return 0;
}

int
main(void) {
  @autoreleasepool {
    return run_tests();
  }
}
#else
int
main(void) {
  return 77;
}
#endif
