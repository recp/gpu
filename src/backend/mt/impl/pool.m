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

/* small buffers and textures placed in pooled heaps: one driver allocation
 * serves many resources, so creating and destroying them stays out of the
 * kernel. Metal 4 devices only: their command buffers order work through
 * GPU-C's barriers, while classic ones would track a pooled heap as one
 * resource and serialize unrelated work. residency sets retain what they
 * hold, so a destroyed resource's space is reused only once the GPU is done
 * with it. GPU_RESOURCE_POOL=0 turns the pool off. */

#include "../common.h"

#include <mach/vm_page_size.h>
#include <stdlib.h>
#include <string.h>

/* buffers of a page or more keep page-aligned host memory (it can back
 * another buffer, GPU_BUFFER_HOST_MEMORY_EXT): page multiples in heaps of
 * their own, where every offset stays a page multiple */
enum {
  MT_POOL_BUFFERS          = 0,
  MT_POOL_PAGE_BUFFERS     = 1,
  MT_POOL_SHARED_TEXTURES  = 2,
  MT_POOL_PRIVATE_TEXTURES = 3,
  MT_POOL_KINDS            = 4,
  MT_POOL_MAX_HEAPS        = 16
};

#define MT_POOL_FIRST_HEAP   ((NSUInteger)8u << 20)
#define MT_POOL_LARGEST_HEAP ((NSUInteger)64u << 20)
#define MT_POOL_MAX_RESOURCE ((NSUInteger)4u << 20)

struct MTResourcePool {
  os_unfair_lock lock;
  id<MTLHeap>    heaps[MT_POOL_KINDS][MT_POOL_MAX_HEAPS];
  NSUInteger     nextSize[MT_POOL_KINDS];
  uint32_t       count[MT_POOL_KINDS];
};

GPU_HIDE
void
mt_initResourcePool(GPUDeviceMT *deviceMT) {
  MTResourcePool *pool;
  const char     *env;
  uint32_t        kind;

  if (!deviceMT || deviceMT->commandMode != MTCommandMode4
      || ((env = getenv("GPU_RESOURCE_POOL")) && strcmp(env, "0") == 0)
      || ![deviceMT->device respondsToSelector:@selector(hasUnifiedMemory)]
      || !deviceMT->device.hasUnifiedMemory)
    return;

  if (!(pool = calloc(1, sizeof(*pool))))
    return;

  pool->lock = OS_UNFAIR_LOCK_INIT;
  for (kind = 0u; kind < MT_POOL_KINDS; kind++)
    pool->nextSize[kind] = MT_POOL_FIRST_HEAP;

  deviceMT->resourcePool = pool;
}

GPU_HIDE
void
mt_destroyResourcePool(GPUDeviceMT *deviceMT) {
  MTResourcePool *pool;
  uint32_t        kind, i;

  if (!deviceMT || !(pool = deviceMT->resourcePool))
    return;

  /* resources still alive keep their heap */
  for (kind = 0u; kind < MT_POOL_KINDS; kind++) {
    for (i = 0u; i < pool->count[kind]; i++)
      [pool->heaps[kind][i] release];
  }

  free(pool);
  deviceMT->resourcePool = NULL;
}

/* heaps nothing lives in, but the newest of each kind */

GPU_HIDE
void
mt_trimResourcePool(GPUDeviceMT *deviceMT) {
  MTResourcePool *pool;
  uint32_t        kind, i, kept;

  if (!deviceMT || !(pool = deviceMT->resourcePool))
    return;

  os_unfair_lock_lock(&pool->lock);
  for (kind = 0u; kind < MT_POOL_KINDS; kind++) {
    kept = 0u;
    for (i = 0u; i < pool->count[kind]; i++) {
      if (i + 1u < pool->count[kind] && pool->heaps[kind][i].usedSize == 0u) {
        [pool->heaps[kind][i] release];
        continue;
      }
      pool->heaps[kind][kept++] = pool->heaps[kind][i];
    }
    pool->count[kind] = kept;
  }
  os_unfair_lock_unlock(&pool->lock);
}

static id
mt_placeIn(id<MTLHeap> heap, MTLTextureDescriptor *desc, NSUInteger length) {
  return desc ? (id)[heap newTextureWithDescriptor:desc]
              : (id)[heap newBufferWithLength:length options:heap.resourceOptions];
}

/* newest heaps first: they are the largest */

static id
mt_placeResource(GPUDeviceMT          *deviceMT,
                 uint32_t              kind,
                 MTLStorageMode        storageMode,
                 MTLSizeAndAlign       sizeAndAlign,
                 MTLTextureDescriptor *desc,
                 NSUInteger            length) {
  MTLHeapDescriptor *heapDesc;
  MTResourcePool    *pool;
  id<MTLHeap>        heap;
  NSUInteger         size;
  id                 resource;
  uint32_t           i;

  pool = deviceMT->resourcePool;
  if (!pool || !sizeAndAlign.size || sizeAndAlign.size > MT_POOL_MAX_RESOURCE)
    return nil;

  resource = nil;
  os_unfair_lock_lock(&pool->lock);

  for (i = pool->count[kind]; i > 0u && !resource; i--) {
    heap = pool->heaps[kind][i - 1u];
    if ([heap maxAvailableSizeWithAlignment:sizeAndAlign.align] >= sizeAndAlign.size)
      resource = mt_placeIn(heap, desc, length);
  }

  if (!resource && pool->count[kind] == MT_POOL_MAX_HEAPS) {
    os_unfair_lock_unlock(&pool->lock);
    mt_trimResourcePool(deviceMT);
    os_unfair_lock_lock(&pool->lock);
  }

  if (!resource && pool->count[kind] < MT_POOL_MAX_HEAPS) {
    size = pool->nextSize[kind];
    while (size < 2u * sizeAndAlign.size)
      size *= 2u;

    heapDesc                    = [MTLHeapDescriptor new];
    heapDesc.type               = MTLHeapTypeAutomatic;
    heapDesc.storageMode        = storageMode;
    heapDesc.cpuCacheMode       = MTLCPUCacheModeDefaultCache;
    heapDesc.hazardTrackingMode = MTLHazardTrackingModeTracked;
    heapDesc.size               = size;
    heap                        = [deviceMT->device newHeapWithDescriptor:heapDesc];
    [heapDesc release];

    if (heap) {
#if GPU_BUILD_WITH_DEBUG_MARKERS
      heap.label = @"gpu-resource-pool";
#endif
      pool->heaps[kind][pool->count[kind]++] = heap;
      if (pool->nextSize[kind] < MT_POOL_LARGEST_HEAP)
        pool->nextSize[kind] *= 2u;
      resource = mt_placeIn(heap, desc, length);
    }
  }

  os_unfair_lock_unlock(&pool->lock);

  return resource;
}

GPU_HIDE
id<MTLBuffer>
mt_poolBuffer(GPUDeviceMT *deviceMT, NSUInteger length) {
  MTLSizeAndAlign sizeAndAlign;
  id<MTLBuffer>   buffer;
  NSUInteger      page;
  uint32_t        kind;

  if (!deviceMT || !deviceMT->resourcePool || !length || length > MT_POOL_MAX_RESOURCE)
    return nil;

  page = (NSUInteger)vm_page_size;
  kind = MT_POOL_BUFFERS;
  if (length >= page) {
    kind   = MT_POOL_PAGE_BUFFERS;
    length = (length + page - 1u) & ~(page - 1u);
  }

  sizeAndAlign = [deviceMT->device heapBufferSizeAndAlignWithLength:length
                                                            options:MTLResourceStorageModeShared];
  if (kind == MT_POOL_PAGE_BUFFERS && (sizeAndAlign.size & (page - 1u)))
    return nil;

  buffer = mt_placeResource(deviceMT, kind, MTLStorageModeShared, sizeAndAlign, nil, length);

  if (buffer && kind == MT_POOL_PAGE_BUFFERS && ((uintptr_t)buffer.contents & (page - 1u))) {
    [buffer release];
    buffer = nil;
  }

  return buffer;
}

/* desc's storage mode: private or shared; managed (copy destinations on
 * macOS) is shared here, which is the same memory on unified-memory GPUs */

GPU_HIDE
id<MTLTexture>
mt_poolTexture(GPUDeviceMT *deviceMT, MTLTextureDescriptor *desc) {
  MTLSizeAndAlign sizeAndAlign;
  MTLStorageMode  storageMode, original;
  id<MTLTexture>  texture;

  if (!deviceMT || !deviceMT->resourcePool || !desc)
    return nil;

  storageMode = original = desc.storageMode;
  if (storageMode == MTLStorageModeManaged)
    storageMode = MTLStorageModeShared;

  if (storageMode != MTLStorageModeShared && storageMode != MTLStorageModePrivate)
    return nil;

  desc.storageMode = storageMode;
  sizeAndAlign     = [deviceMT->device heapTextureSizeAndAlignWithDescriptor:desc];
  texture          = mt_placeResource(deviceMT,
                                      storageMode == MTLStorageModeShared ? MT_POOL_SHARED_TEXTURES
                                                                          : MT_POOL_PRIVATE_TEXTURES,
                                      storageMode, sizeAndAlign, desc, 0u);

  if (!texture)
    desc.storageMode = original;

  return texture;
}
