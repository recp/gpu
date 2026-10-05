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
enum {
  SPARSE_TEXTURE = 1u,
  SPARSE_BUFFER  = 2u,
  SPARSE_PLACED  = 4u,
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

@interface SparseDevice : NSObject {
@public
  MTLGPUFamily family;
  uint8_t      methods;
  bool         placement;
}
@end

static const SparseCase cases[] = {
  {"query-no-apple8", "auto", MTLGPUFamilyApple8, METAL4_ALL, 0u, 7u, false, false},
  {"query-no-apple7", "auto", MTLGPUFamilyApple7, METAL4_ALL, 0u, 7u, false, false},
  {"query-yes-apple8", "auto", MTLGPUFamilyApple8, METAL4_ALL, 7u, 7u, true, false},
  {"query-yes-apple7", "auto", MTLGPUFamilyApple7, METAL4_ALL, 7u, 7u, true, false},
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

GPU_HIDE
bool
mt_supportsFeature(const GPUAdapter *adapter, GPUFeature feature);

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
  return family >= MTLGPUFamilyApple1 && family <= MTLGPUFamilyApple8
         && value >= MTLGPUFamilyApple1 && value <= family;
}
@end

int
main(void) {
  GPUAdapterMT       native  = {0};
  GPUAdapter         adapter = {0};
  SparseDevice      *device;
  const SparseCase  *test;
  uint32_t           i, j;
  uint8_t            expected;
  bool               modern;
  bool               actual;
  bool               wanted;

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

  [device release];
  printf("metal sparse support: %zu cases passed\n", GPU_ARRAY_LEN(cases) * GPU_ARRAY_LEN(features) + 6u);
  return 0;
}
#else
int
main(void) {
  return 77;
}
#endif
