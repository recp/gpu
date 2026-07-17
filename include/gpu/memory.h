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

#ifndef gpu_memory_h
#define gpu_memory_h
#ifdef __cplusplus
extern "C" {
#endif

#include "buffer.h"
#include "texture.h"

typedef struct GPUHeap GPUHeap;

typedef struct GPUMemoryRequirements {
  uint64_t sizeBytes;
  uint64_t alignmentBytes;
  uint64_t compatibilityMask;
} GPUMemoryRequirements;

typedef struct GPUHeapCreateInfo {
  GPUChainedStruct   chain;
  const char        *label;
  uint64_t           sizeBytes;
  uint64_t           compatibilityMask;
} GPUHeapCreateInfo;

GPU_EXPORT
GPUResult
GPUGetBufferMemoryRequirements(GPUDevice                 * __restrict device,
                               const GPUBufferCreateInfo * __restrict info,
                               GPUMemoryRequirements     * __restrict outRequirements);

GPU_EXPORT
GPUResult
GPUGetTextureMemoryRequirements(GPUDevice                  * __restrict device,
                                const GPUTextureCreateInfo * __restrict info,
                                GPUMemoryRequirements      * __restrict outRequirements);

GPU_EXPORT
GPUResult
GPUCreateHeap(GPUDevice               * __restrict device,
              const GPUHeapCreateInfo * __restrict info,
              GPUHeap                ** __restrict outHeap);

GPU_EXPORT
void
GPUDestroyHeap(GPUHeap * __restrict heap);

GPU_EXPORT
GPUResult
GPUCreatePlacedBuffer(GPUDevice                 * __restrict device,
                      const GPUBufferCreateInfo * __restrict info,
                      GPUHeap                   * __restrict heap,
                      uint64_t                               heapOffset,
                      GPUBuffer                ** __restrict outBuffer);

GPU_EXPORT
GPUResult
GPUCreatePlacedTexture(GPUDevice                  * __restrict device,
                       const GPUTextureCreateInfo * __restrict info,
                       GPUHeap                    * __restrict heap,
                       uint64_t                                heapOffset,
                       GPUTexture                ** __restrict outTexture);

#ifdef __cplusplus
}
#endif
#endif /* gpu_memory_h */
