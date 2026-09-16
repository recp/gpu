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
#ifndef dx12_buffer_range_h
#define dx12_buffer_range_h

#include <stdbool.h>
#include <stdint.h>

typedef struct DX12DynamicBufferRange {
  uint32_t maxOffset;
  uint32_t alignmentMask;
  uint32_t stride;
} DX12DynamicBufferRange;

/* Native view span, not the public logical range. Alignments are powers of two. */
static inline bool
dx12_bufferViewRange(uint64_t address, uint64_t extent,
                     uint64_t offset, uint64_t size,
                     uint32_t addressAlignment, uint32_t sizeAlignment,
                     uint64_t *outSize) {
  uint64_t mask = (uint64_t)sizeAlignment - 1u;

  if (!outSize || !address || !size || !addressAlignment || !sizeAlignment ||
      (addressAlignment & (addressAlignment - 1u)) != 0u ||
      (sizeAlignment & (sizeAlignment - 1u)) != 0u ||
      offset > UINT64_MAX - address ||
      ((address + offset) & (addressAlignment - 1u)) != 0u ||
      size > UINT64_MAX - mask) {
    return false;
  }
  size = (size + mask) & ~mask;
  if (offset > extent || size > extent - offset ||
      size - 1u > UINT64_MAX - (address + offset)) {
    return false;
  }
  *outSize = size;
  return true;
}

/* Build once from an immutable bind-group range, in canonical dynamic order. */
static inline bool
dx12_dynamicBufferRange(uint64_t address, uint64_t extent, uint64_t logicalSize,
                        uint64_t offset, uint64_t size,
                        uint32_t addressAlignment, uint32_t sizeAlignment,
                        uint32_t stride, DX12DynamicBufferRange *outRange) {
  uint64_t span, limit, available;
  uint32_t mask;

  if (!outRange || offset > logicalSize || size > logicalSize - offset ||
      (stride && (offset % stride || size % stride)) ||
      !dx12_bufferViewRange(address, extent, offset, size,
                            addressAlignment, sizeAlignment, &span)) {
    return false;
  }
  limit = logicalSize - offset - size;
  available = extent - offset - span;
  if (limit > available) limit = available;
  available = UINT64_MAX - (address + offset) - (span - 1u);
  if (limit > available) limit = available;
  mask = addressAlignment - 1u;
  if (stride && (stride & (stride - 1u)) == 0u) {
    mask |= stride - 1u;
    stride = 0u;
  }
  outRange->maxOffset     = limit > UINT32_MAX ? UINT32_MAX : (uint32_t)limit;
  outRange->alignmentMask = mask;
  outRange->stride        = stride;
  return true;
}

static inline bool
dx12_dynamicOffsetsValid(const DX12DynamicBufferRange *ranges,
                         uint32_t rangeCount, uint32_t offsetCount,
                         const uint32_t *offsets) {
  if (rangeCount != offsetCount || (rangeCount && (!ranges || !offsets))) {
    return false;
  }
  for (uint32_t i = 0u; i < rangeCount; i++) {
    if (offsets[i] > ranges[i].maxOffset ||
        (offsets[i] & ranges[i].alignmentMask) != 0u ||
        (ranges[i].stride && offsets[i] % ranges[i].stride != 0u)) {
      return false;
    }
  }
  return true;
}

#endif
