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

#ifndef gpu_gpudef_memory_h
#define gpu_gpudef_memory_h

#include <gpu/gpu.h>

typedef struct GPUApiMemory {
  GPUResult
  (*getBufferRequirements)(GPUDevice                 *device,
                           const GPUBufferCreateInfo *info,
                           GPUMemoryRequirements     *outRequirements);

  GPUResult
  (*getTextureRequirements)(GPUDevice                  *device,
                            const GPUTextureCreateInfo *info,
                            GPUMemoryRequirements      *outRequirements);

  GPUResult
  (*getSparseBufferRequirements)(
    GPUDevice                   *device,
    const GPUBufferCreateInfo   *info,
    GPUSparseBufferRequirements *outRequirements
  );

  GPUResult
  (*getSparseTextureRequirements)(
    GPUDevice                    *device,
    const GPUTextureCreateInfo   *info,
    GPUSparseTextureRequirements *outRequirements
  );

  GPUResult
  (*createHeap)(GPUDevice               *device,
                const GPUHeapCreateInfo *info,
                GPUHeap                **outHeap);

  void
  (*destroyHeap)(GPUHeap *heap);

  GPUResult
  (*createPlacedBuffer)(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        uint64_t                   heapOffset,
                        GPUBuffer                **outBuffer);

  GPUResult
  (*createPlacedTexture)(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         uint64_t                    heapOffset,
                         GPUTexture                **outTexture);

  GPUResult
  (*createSparseBuffer)(GPUDevice                 *device,
                        const GPUBufferCreateInfo *info,
                        GPUHeap                   *heap,
                        GPUBuffer                **outBuffer);

  GPUResult
  (*createSparseTexture)(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         GPUTexture                **outTexture);

  GPUResult
  (*submitSparse)(GPUQueue                       *queue,
                  const GPUQueueSparseSubmitInfo *info);
} GPUApiMemory;

#endif /* gpu_gpudef_memory_h */
