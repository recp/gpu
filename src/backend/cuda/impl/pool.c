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

/* small buffers placed in ranges of shared cuMemAlloc blocks
 * (api/suballoc): one driver allocation a block, not a buffer. a range
 * goes back once the work given before its destroy finished (the device's
 * retire list, api/retire_internal.h), as cuMemFree waits for the device;
 * the destroy doesn't wait. GPU_RESOURCE_POOL=0 turns the pool off. */

#include "../common.h"
#include "../../../api/suballoc_internal.h"

#include <stdlib.h>
#include <string.h>

#define CUDA_POOL_FIRST_BLOCK   ((uint64_t)8u << 20)
#define CUDA_POOL_LARGEST_BLOCK ((uint64_t)64u << 20)
#define CUDA_POOL_MAX_RESOURCE  ((uint64_t)4u << 20)
#define CUDA_POOL_ALIGNMENT     256u

static bool
cuda__poolCreateBlock(void *user, uint32_t key, uint64_t size, uint64_t *outHandle, void **outMapped) {
  GPUDeviceCuda *native;
  CUdeviceptr    address;
  CUresult       result;

  native = user;
  (void)key;

  if (size > SIZE_MAX || cuda_push(native->driver, native->context) != GPU_OK)
    return false;

  address = 0u;
  result  = native->driver->memAlloc(&address, (size_t)size);
  cuda_pop(native->driver);

  if (result != CUDA_SUCCESS)
    return false;

  *outHandle = (uint64_t)address;
  *outMapped = NULL;

  return true;
}

static void
cuda__poolDestroyBlock(void *user, uint32_t key, uint64_t handle, void *mapped) {
  GPUDeviceCuda *native;

  native = user;
  (void)key;
  (void)mapped;

  if (cuda_push(native->driver, native->context) == GPU_OK) {
    (void)native->driver->memFree((CUdeviceptr)handle);
    cuda_pop(native->driver);
  }
}

void
cuda_initResourcePool(GPUDeviceCuda *native) {
  GPUSubAllocatorInfo info = {0};
  GPUSubAllocator    *pool;
  const char         *env;

  if (!native || ((env = getenv("GPU_RESOURCE_POOL")) && strcmp(env, "0") == 0))
    return;

  if (!(pool = malloc(sizeof(*pool))))
    return;

  info.createBlock      = cuda__poolCreateBlock;
  info.destroyBlock     = cuda__poolDestroyBlock;
  info.user             = native;
  info.firstBlockSize   = CUDA_POOL_FIRST_BLOCK;
  info.largestBlockSize = CUDA_POOL_LARGEST_BLOCK;
  info.maxResourceSize  = CUDA_POOL_MAX_RESOURCE;

  if (!gpuSubInit(pool, &info)) {
    free(pool);
    return;
  }

  native->resourcePool = pool;
}

void
cuda_destroyResourcePool(GPUDeviceCuda *native) {
  if (!native || !native->resourcePool)
    return;

  gpuSubDestroy(native->resourcePool);
  free(native->resourcePool);
  native->resourcePool = NULL;
}

void
cuda_trimResourcePool(GPUDeviceCuda *native) {
  if (native && native->resourcePool)
    gpuSubTrim(native->resourcePool);
}

bool
cuda_poolAllocate(GPUDeviceCuda *native, uint64_t size, GPUBufferCuda *buffer) {
  GPUSubAllocation allocation;

  if (!native || !native->resourcePool || !buffer
      || !gpuSubAllocate(native->resourcePool, 0u, size, CUDA_POOL_ALIGNMENT, &allocation))
    return false;

  buffer->address    = (CUdeviceptr)(allocation.block->handle + allocation.offset);
  buffer->poolBlock  = allocation.block;
  buffer->poolOffset = allocation.offset;
  buffer->poolSize   = allocation.size;

  return true;
}

static void
cuda__poolReturn(void *context, void *owner) {
  GPUDeviceCuda   *native;
  GPUBufferCuda   *buffer;
  GPUSubAllocation allocation;

  native = context;
  buffer = owner;

  allocation.block  = buffer->poolBlock;
  allocation.offset = buffer->poolOffset;
  allocation.size   = buffer->poolSize;
  gpuSubFree(native->resourcePool, &allocation);
  free(buffer);
}

/* the range goes back once the work that may read it finished, and buffer
 * with it */

void
cuda_poolRelease(GPUDeviceCuda *native, GPUBufferCuda *buffer) {
  if (!buffer)
    return;

  if (!native || !native->resourcePool || !buffer->poolBlock) {
    free(buffer);
    return;
  }

  gpuRetire(&native->retire,
            native->queueList,
            native->queueCount,
            &buffer->retired,
            buffer,
            cuda__poolReturn);
}
