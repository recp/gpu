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

/*
 * small resources placed in shared device memory blocks
 *
 * backends whose memory is allocated explicitly (Vulkan's VkDeviceMemory,
 * D3D12 heaps) give each small buffer or texture a range of a larger block
 * instead of an allocation of its own: allocating is a kernel call and
 * drivers cap how many allocations a device holds. a key names what may
 * share a block (memory type, buffers or images, mapping, allocation
 * flags); blocks grow from firstBlockSize to largestBlockSize as a key
 * needs more. a free range goes back at once, so a resource is freed only
 * once the GPU is done with it, as for a dedicated allocation. empty blocks
 * stay until gpuSubTrim (device wait idle) or gpuSubDestroy.
 */

#ifndef gpu_suballoc_internal_h
#define gpu_suballoc_internal_h

#include "../common.h"

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

typedef struct GPUSubRange {
  uint64_t offset;
  uint64_t size;
} GPUSubRange;

typedef struct GPUSubBlock {
  GPUSubRange *free;   /* sorted by offset, coalesced */
  void        *mapped; /* host address of offset 0, or NULL */
  uint64_t     handle; /* the backend's memory object */
  uint64_t     size;
  uint64_t     used;
  uint32_t     freeCount;
  uint32_t     freeCapacity;
  uint32_t     key;
} GPUSubBlock;

typedef struct GPUSubAllocatorInfo {
  /* a block of size bytes for key; false: none (the caller allocates on its own) */
  bool (*createBlock)(void *user, uint32_t key, uint64_t size, uint64_t *outHandle, void **outMapped);
  void (*destroyBlock)(void *user, uint32_t key, uint64_t handle, void *mapped);
  void     *user;
  uint64_t  firstBlockSize;
  uint64_t  largestBlockSize;
  uint64_t  maxResourceSize;
} GPUSubAllocatorInfo;

typedef struct GPUSubAllocator {
  GPUSubAllocatorInfo info;
  GPUSubBlock       **blocks;
  uint32_t            blockCount;
  uint32_t            blockCapacity;
#if defined(_WIN32) || defined(WIN32)
  SRWLOCK             lock;
#else
  pthread_mutex_t     lock;
#endif
} GPUSubAllocator;

typedef struct GPUSubAllocation {
  GPUSubBlock *block; /* NULL: not placed */
  uint64_t     offset;
  uint64_t     size;
} GPUSubAllocation;

GPU_HIDE
bool
gpuSubInit(GPUSubAllocator *alloc, const GPUSubAllocatorInfo *info);

/* every block goes back, whatever still lives in it */
GPU_HIDE
void
gpuSubDestroy(GPUSubAllocator *alloc);

/* alignment: a power of two; false when size is over maxResourceSize or no
 * block can be had */
GPU_HIDE
bool
gpuSubAllocate(GPUSubAllocator  *alloc,
               uint32_t          key,
               uint64_t          size,
               uint64_t          alignment,
               GPUSubAllocation *out);

GPU_HIDE
void
gpuSubFree(GPUSubAllocator *alloc, const GPUSubAllocation *allocation);

/* empty blocks go back, but the newest of each key */
GPU_HIDE
void
gpuSubTrim(GPUSubAllocator *alloc);

#endif /* gpu_suballoc_internal_h */
