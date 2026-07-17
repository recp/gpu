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

#ifndef gpu_memory_internal_h
#define gpu_memory_internal_h

#include "../common.h"

struct GPUHeap {
  void               *_priv;
  GPUDevice          *device;
  uint64_t            sizeBytes;
  uint64_t            compatibilityMask;
};

static inline bool
gpuHeapRangeValid(const GPUHeap              *heap,
                  const GPUMemoryRequirements *requirements,
                  uint64_t                    offset) {
  return heap && requirements &&
         requirements->sizeBytes > 0u &&
         requirements->alignmentBytes > 0u &&
         offset % requirements->alignmentBytes == 0u &&
         offset <= heap->sizeBytes &&
         requirements->sizeBytes <= heap->sizeBytes - offset &&
         (heap->compatibilityMask & requirements->compatibilityMask) != 0u;
}

#endif /* gpu_memory_internal_h */
