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

#include "../../src/backend/dx12/buffer_range.h"

#include <stdio.h>

#if defined(_WIN32)
#  include <d3d12.h>
_Static_assert(D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT == 16u, "raw address alignment");
_Static_assert(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 256u, "CBV alignment");
#endif

#define CHECK_DYNAMIC(x) \
  do { \
    if (!(x)) { \
      fprintf(stderr, "dynamic buffer range line %d failed\n", __LINE__); \
      return 0; \
    } \
  } while (0)

typedef struct RangeCase {
  uint64_t address, extent, offset, size, expected;
  uint32_t addressAlignment, sizeAlignment;
} RangeCase;

static const struct {
  uint64_t address, extent, logicalSize, offset, size;
  uint32_t addressAlignment, sizeAlignment, stride;
} dynamicCases[] = {
  {65536u, 24u, 22u, 0u, 6u, 16u, 4u, 2u},
  {65536u, 22u, 22u, 0u, 6u, 16u, 4u, 2u},
  {65536u, 6u, 6u, 0u, 2u, 1u, 1u, 2u},
  {65536u, 144u, 144u, 24u, 24u, 1u, 1u, 12u},
  {65536u, 144u, 144u, 0u, 24u, 16u, 4u, 12u},
  {65536u, 516u, 516u, 0u, 4u, 256u, 1u, 0u},
  {65536u, 516u, 516u, 0u, 4u, 256u, 256u, 0u},
  {65536u, UINT64_C(1) << 34u, UINT64_C(1) << 34u, 16u, 6u, 16u, 4u, 2u},
  {UINT64_MAX - 63u, 128u, 128u, 0u, 16u, 16u, 4u, 0u}
};

static const uint32_t edges[] = {UINT32_MAX, UINT32_MAX - 15u, UINT32_MAX - 255u};

static const RangeCase rangeCases[] = {
  {65536u, 2u, 0u, 2u, 0u, 16u, 4u},
  {65536u, 4u, 0u, 2u, 4u, 16u, 4u},
  {65536u, 6u, 0u, 6u, 0u, 16u, 4u},
  {65536u, 8u, 0u, 6u, 8u, 16u, 4u},
  {65536u, 126u, 0u, 126u, 0u, 16u, 4u},
  {65536u, 128u, 0u, 126u, 128u, 16u, 4u},
  {65536u, 128u, 0u, 128u, 128u, 16u, 4u},
  {65536u, 24u, 16u, 6u, 8u, 16u, 4u},
  {65536u, 22u, 16u, 6u, 0u, 16u, 4u},
  {65536u, 24u, 2u, 6u, 0u, 16u, 4u},
  {65536u, 24u, 4u, 6u, 0u, 16u, 4u},
  {65536u, 24u, 8u, 6u, 0u, 16u, 4u},
  {65536u, 24u, 12u, 6u, 0u, 16u, 4u},
  {65544u, 24u, 0u, 6u, 0u, 16u, 4u},
  {65544u, 24u, 8u, 6u, 8u, 16u, 4u},
  /* structured native16 keeps the exact span, including a final half. */
  {65536u, 6u, 0u, 6u, 6u, 1u, 1u},
  {65536u, 8u, 2u, 6u, 6u, 1u, 1u},
  {65536u, 6u, 2u, 6u, 0u, 1u, 1u},
  {65536u, 256u, 0u, 4u, 256u, 256u, 256u},
  {65536u, 255u, 0u, 4u, 0u, 256u, 256u},
  {65536u, 512u, 256u, 4u, 256u, 256u, 256u},
  {65536u, 512u, 128u, 4u, 0u, 256u, 256u},
  {65536u, 512u, 0u, 257u, 512u, 256u, 256u},
  {65536u, 512u, 0u, 0u, 0u, 16u, 4u},
  {0u, 8u, 0u, 2u, 0u, 16u, 4u},
  {65536u, 8u, 0u, 2u, 0u, 0u, 4u},
  {65536u, 8u, 0u, 2u, 0u, 16u, 0u},
  {65536u, 8u, 0u, 2u, 0u, 3u, 4u},
  {65536u, 8u, 0u, 2u, 0u, 16u, 3u},
  {65536u, UINT64_MAX, 0u, UINT64_MAX, 0u, 16u, 4u},
  {65536u, UINT64_MAX, UINT64_MAX - 15u, 2u, 0u, 16u, 4u},
  {UINT64_MAX - 15u, 32u, 0u, 16u, 16u, 16u, 4u},
  {UINT64_MAX - 15u, 32u, 0u, 18u, 0u, 16u, 4u},
  {UINT64_MAX - 15u, 32u, 16u, 2u, 0u, 16u, 4u}
};

static const uint32_t alignments[][2] = {{1u, 1u}, {16u, 4u}, {256u, 256u}};

static int
check_dynamic_ranges(void) {
  DX12DynamicBufferRange ranges[2];
  DX12DynamicBufferRange previous;
  uint32_t               offsets[2] = {0u, 0u};
  uint64_t               shifted;
  uint64_t               span;
  uint32_t               checked = 0u;
  uint32_t               caseIndex;
  uint32_t               j;
  uint32_t               delta;
  uint32_t               warmIndex;
  bool                   expected;

  CHECK_DYNAMIC(dx12_dynamicOffsetsValid(NULL, 0u, 0u, NULL));

  for (caseIndex = 0u; caseIndex < sizeof(dynamicCases) / sizeof(dynamicCases[0]); caseIndex++) {
    CHECK_DYNAMIC(dx12_dynamicBufferRange(dynamicCases[caseIndex].address,
                                          dynamicCases[caseIndex].extent,
                                          dynamicCases[caseIndex].logicalSize,
                                          dynamicCases[caseIndex].offset,
                                          dynamicCases[caseIndex].size,
                                          dynamicCases[caseIndex].addressAlignment,
                                          dynamicCases[caseIndex].sizeAlignment,
                                          dynamicCases[caseIndex].stride,
                                          &ranges[0]));
    CHECK_DYNAMIC(ranges[0].stride ==
      ((dynamicCases[caseIndex].stride & (dynamicCases[caseIndex].stride - 1u)) ? dynamicCases[caseIndex].stride : 0u));

    for (j = 0u; j < 1027u; j++) {
      delta    = j < 1024u ? j : edges[j - 1024u];
      shifted  = dynamicCases[caseIndex].offset + delta;
      expected = shifted <= dynamicCases[caseIndex].logicalSize
                 && dynamicCases[caseIndex].size <= dynamicCases[caseIndex].logicalSize - shifted
                 && (!dynamicCases[caseIndex].stride || shifted % dynamicCases[caseIndex].stride == 0u)
                 && dx12_bufferViewRange(dynamicCases[caseIndex].address,
                                         dynamicCases[caseIndex].extent,
                                         shifted,
                                         dynamicCases[caseIndex].size,
                                         dynamicCases[caseIndex].addressAlignment,
                                         dynamicCases[caseIndex].sizeAlignment,
                                         &span);
      CHECK_DYNAMIC(dx12_dynamicOffsetsValid(ranges, 1u, 1u, &delta) == expected);
      checked++;
    }
  }

  /* a later invalid offset must fail the entire vector before native setters. */
  CHECK_DYNAMIC(dx12_dynamicBufferRange(65536u,
                                        24u,
                                        22u,
                                        0u,
                                        6u,
                                        16u,
                                        4u,
                                        2u,
                                        &ranges[0]));
  ranges[1]  = ranges[0];
  offsets[0] = 16u;
  offsets[1] = 2u;
  CHECK_DYNAMIC(!dx12_dynamicOffsetsValid(ranges, 2u, 2u, offsets));
  offsets[1] = 16u;
  CHECK_DYNAMIC(dx12_dynamicOffsetsValid(ranges, 2u, 2u, offsets));
  CHECK_DYNAMIC(!dx12_dynamicOffsetsValid(ranges, 2u, 1u, offsets));
  CHECK_DYNAMIC(!dx12_dynamicOffsetsValid(ranges, 2u, 3u, offsets));
  CHECK_DYNAMIC(!dx12_dynamicOffsetsValid(ranges, 2u, 2u, NULL));
  CHECK_DYNAMIC(!dx12_dynamicOffsetsValid(NULL, 2u, 2u, offsets));
  previous = ranges[0];
  CHECK_DYNAMIC(!dx12_dynamicBufferRange(65536u,
                                         6u,
                                         6u,
                                         0u,
                                         6u,
                                         16u,
                                         4u,
                                         2u,
                                         &ranges[0]));
  CHECK_DYNAMIC(ranges[0].maxOffset == previous.maxOffset
                 && ranges[0].alignmentMask == previous.alignmentMask
                 && ranges[0].stride == previous.stride);
  CHECK_DYNAMIC(!dx12_dynamicBufferRange(65536u,
                                         24u,
                                         22u,
                                         1u,
                                         6u,
                                         1u,
                                         1u,
                                         2u,
                                         &ranges[0]));

  for (warmIndex = 0u; warmIndex < 100000u; warmIndex++) {
    offsets[0] = (warmIndex & 1u) ? 0u : 16u;
    CHECK_DYNAMIC(dx12_dynamicOffsetsValid(ranges, 2u, 2u, offsets));
  }

  printf("dynamic native ranges: %u equivalence cases, 100000 warm checks passed\n", checked);

  return 1;
#undef CHECK_DYNAMIC
}

int
main(void) {
  const RangeCase *c;
  uint64_t         caseSize;
  uint64_t         base;
  uint64_t         extent;
  uint64_t         offset;
  uint64_t         bytes;
  uint64_t         rounded;
  uint64_t         sweepSize;
  uint32_t         checked = 0u;
  uint32_t         caseIndex;
  uint32_t         a;
  bool             caseValid;
  bool             expected;
  bool             sweepValid;

  if (!check_dynamic_ranges())
    return 1;

  for (caseIndex = 0u; caseIndex < sizeof(rangeCases) / sizeof(rangeCases[0]); caseIndex++) {
    c        = &rangeCases[caseIndex];
    caseSize = UINT64_MAX;

    caseValid = dx12_bufferViewRange(c->address,
                                     c->extent,
                                     c->offset,
                                     c->size,
                                     c->addressAlignment,
                                     c->sizeAlignment,
                                     &caseSize);

    if (caseValid != (c->expected != 0u)
        || caseSize != (caseValid ? c->expected : UINT64_MAX)) {
      fprintf(stderr, "native buffer range case %u failed\n", caseIndex);
      return 1;
    }

    checked++;
  }

  if (dx12_bufferViewRange(65536u, 8u, 0u, 2u, 16u, 4u, NULL))
    return 1;

  for (a = 0u; a < sizeof(alignments) / sizeof(alignments[0]); a++) {
    for (base = 65536u; base <= 65544u; base += 8u) {
      for (extent = 0u; extent <= 40u; extent++) {
        for (offset = 0u; offset <= 48u; offset++) {
          for (bytes = 0u; bytes <= 40u; bytes++) {
            rounded    = ((bytes + alignments[a][1] - 1u) /
                          alignments[a][1]) * alignments[a][1];
            sweepSize  = UINT64_MAX;
            expected   = bytes != 0u
                         && (base + offset) % alignments[a][0] == 0u
                         && offset + rounded <= extent;
            sweepValid = dx12_bufferViewRange(base,
                                              extent,
                                              offset,
                                              bytes,
                                              alignments[a][0],
                                              alignments[a][1],
                                              &sweepSize);

            if (sweepValid != expected || sweepSize != (sweepValid ? rounded : UINT64_MAX)) {
              fprintf(stderr, "native buffer range sweep failed\n");
              return 1;
            }

            checked++;
          }
        }
      }
    }
  }

  printf("native buffer range arithmetic: %u cases passed (no GPU execution)\n", checked);

  return 0;
}
