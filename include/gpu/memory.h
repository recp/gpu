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

typedef enum GPUHeapUsage {
  GPU_HEAP_USAGE_PLACED = 0,
  GPU_HEAP_USAGE_SPARSE = 1
} GPUHeapUsage;

typedef enum GPUSparseMappingMode {
  GPU_SPARSE_MAPPING_MAP   = 0,
  GPU_SPARSE_MAPPING_UNMAP = 1
} GPUSparseMappingMode;

#define GPU_SPARSE_HEAP_TILE_AUTO UINT64_MAX

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
  uint64_t           pageSizeBytes;
  GPUHeapUsage       usage;
} GPUHeapCreateInfo;

typedef struct GPUSparseTextureRequirements {
  uint64_t compatibilityMask;
  uint64_t pageSizeBytes;
  uint64_t mipTailTileCount;
  uint64_t mipTailLayerStrideTiles;
  uint32_t tileWidth;
  uint32_t tileHeight;
  uint32_t tileDepth;
  uint32_t firstMipInTail;
} GPUSparseTextureRequirements;

typedef struct GPUSparseBufferRequirements {
  uint64_t compatibilityMask;
  uint64_t pageSizeBytes;
  uint64_t tileCount;
} GPUSparseBufferRequirements;

typedef struct GPUSparseBufferMapping {
  GPUBuffer           *buffer;
  GPUHeap             *heap;
  uint64_t             bufferTileOffset;
  uint64_t             heapTileOffset;
  uint64_t             tileCount;
  GPUSparseMappingMode mode;
} GPUSparseBufferMapping;

typedef struct GPUSparseTextureMapping {
  GPUTexture          *texture;
  GPUHeap             *heap;
  uint64_t             heapTileOffset;
  uint32_t             tileX;
  uint32_t             tileY;
  uint32_t             tileZ;
  uint32_t             tileWidth;
  uint32_t             tileHeight;
  uint32_t             tileDepth;
  uint32_t             mipLevel;
  uint32_t             arrayLayer;
  GPUSparseMappingMode mode;
} GPUSparseTextureMapping;

typedef struct GPUQueueSparseSubmitInfo {
  GPUChainedStruct               chain;
  const GPUSparseBufferMapping  *pBufferMappings;
  const GPUSparseTextureMapping *pTextureMappings;
  const GPUQueueSemaphoreWait   *pWaits;
  const GPUQueueSemaphoreSignal *pSignals;
  GPUFence                      *fence;
  uint32_t                       bufferMappingCount;
  uint32_t                       textureMappingCount;
  uint32_t                       waitCount;
  uint32_t                       signalCount;
} GPUQueueSparseSubmitInfo;

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
GPUGetSparseBufferRequirements(GPUDevice                   * __restrict device,
                               const GPUBufferCreateInfo   * __restrict info,
                               GPUSparseBufferRequirements * __restrict outRequirements);

GPU_EXPORT
GPUResult
GPUGetSparseTextureRequirements(GPUDevice                    * __restrict device,
                                const GPUTextureCreateInfo   * __restrict info,
                                GPUSparseTextureRequirements * __restrict outRequirements);

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

GPU_EXPORT
GPUResult
GPUCreateSparseBuffer(GPUDevice                 * __restrict device,
                      const GPUBufferCreateInfo * __restrict info,
                      GPUHeap                   * __restrict heap,
                      GPUBuffer                ** __restrict outBuffer);

GPU_EXPORT
GPUResult
GPUCreateSparseTexture(GPUDevice                  * __restrict device,
                       const GPUTextureCreateInfo * __restrict info,
                       GPUHeap                    * __restrict heap,
                       GPUTexture                ** __restrict outTexture);

GPU_EXPORT
GPUResult
GPUQueueSubmitSparse(GPUQueue                       * __restrict queue,
                     const GPUQueueSparseSubmitInfo * __restrict info);

#ifdef __cplusplus
}
#endif
#endif /* gpu_memory_h */
