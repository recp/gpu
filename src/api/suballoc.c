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

#include "suballoc_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  define SUB_LOCK(a)   AcquireSRWLockExclusive(&(a)->lock)
#  define SUB_UNLOCK(a) ReleaseSRWLockExclusive(&(a)->lock)
#else
#  define SUB_LOCK(a)   pthread_mutex_lock(&(a)->lock)
#  define SUB_UNLOCK(a) pthread_mutex_unlock(&(a)->lock)
#endif

GPU_HIDE
bool
gpuSubInit(GPUSubAllocator *alloc, const GPUSubAllocatorInfo *info) {
  if (!alloc || !info || !info->createBlock || !info->destroyBlock
      || !info->firstBlockSize || info->largestBlockSize < info->firstBlockSize
      || !info->maxResourceSize)
    return false;

  memset(alloc, 0, sizeof(*alloc));
  alloc->info = *info;
  /* a resource over a quarter of the largest block takes an allocation of its own: two or three of them in one
   * block leave, once one goes, a gap too small for the next, and the block is kept for whatever small one stays
   * beside them */
  if (alloc->info.largestBlockSize / 4u && alloc->info.maxResourceSize > alloc->info.largestBlockSize / 4u)
    alloc->info.maxResourceSize = alloc->info.largestBlockSize / 4u;
#if defined(_WIN32) || defined(WIN32)
  InitializeSRWLock(&alloc->lock);
#else
  if (pthread_mutex_init(&alloc->lock, NULL) != 0)
    return false;
#endif

  return true;
}

static void
gpuSub__releaseBlock(GPUSubAllocator *alloc, GPUSubBlock *block) {
  alloc->info.destroyBlock(alloc->info.user, block->key, block->handle, block->mapped);
  free(block->free);
  free(block);
}

GPU_HIDE
void
gpuSubDestroy(GPUSubAllocator *alloc) {
  uint32_t i;

  if (!alloc || !alloc->info.destroyBlock)
    return;

  for (i = 0u; i < alloc->blockCount; i++)
    gpuSub__releaseBlock(alloc, alloc->blocks[i]);

  free(alloc->blocks);
  alloc->blocks        = NULL;
  alloc->blockCount    = 0u;
  alloc->blockCapacity = 0u;
#if !defined(_WIN32) && !defined(WIN32)
  pthread_mutex_destroy(&alloc->lock);
#endif
}

/* room for one more free range */

static bool
gpuSub__reserve(GPUSubBlock *block) {
  GPUSubRange *ranges;
  uint32_t     capacity;

  if (block->freeCount < block->freeCapacity)
    return true;

  capacity = block->freeCapacity ? block->freeCapacity * 2u : 8u;
  if (!(ranges = realloc(block->free, capacity * sizeof(*ranges))))
    return false;

  block->free         = ranges;
  block->freeCapacity = capacity;

  return true;
}

/* first fit; the gap an alignment leaves stays free */

static bool
gpuSub__place(GPUSubBlock *block, uint64_t size, uint64_t alignment, uint64_t *outOffset) {
  GPUSubRange *range;
  uint64_t     start, end, head, tail;
  uint32_t     i;

  for (i = 0u; i < block->freeCount; i++) {
    range = &block->free[i];
    start = (range->offset + alignment - 1u) & ~(alignment - 1u);
    end   = range->offset + range->size;

    if (start < range->offset || start > end || end - start < size)
      continue;

    head = start - range->offset;
    tail = end - (start + size);

    if (head && tail) {
      if (!gpuSub__reserve(block))
        return false;

      range = &block->free[i];
      memmove(&block->free[i + 2u], &block->free[i + 1u],
              (block->freeCount - i - 1u) * sizeof(*range));
      block->free[i + 1u].offset = start + size;
      block->free[i + 1u].size   = tail;
      range->size                = head;
      block->freeCount++;
    } else if (head) {
      range->size = head;
    } else if (tail) {
      range->offset = start + size;
      range->size   = tail;
    } else {
      memmove(range, range + 1, (block->freeCount - i - 1u) * sizeof(*range));
      block->freeCount--;
    }

    block->used += size;
    *outOffset   = start;
    return true;
  }

  return false;
}

/* a new block's size: each of a key twice the last, up to the largest, and
 * twice what it's made for. Under the lock */
static uint64_t
gpuSub__blockSize(const GPUSubAllocator *alloc, uint32_t key, uint64_t size) {
  uint64_t blockSize;
  uint32_t i;

  blockSize = alloc->info.firstBlockSize;
  for (i = 0u; i < alloc->blockCount; i++) {
    if (alloc->blocks[i]->key == key && alloc->blocks[i]->size >= blockSize && blockSize < alloc->info.largestBlockSize)
      blockSize = alloc->blocks[i]->size * 2u;
  }

  if (blockSize > alloc->info.largestBlockSize)
    blockSize = alloc->info.largestBlockSize;

  while (blockSize < 2u * size)
    blockSize *= 2u;

  return blockSize;
}

/* the driver's memory for a block, outside the lock: allocating it can take
 * milliseconds on phones, and no other thread's allocation should wait for
 * that */
static GPUSubBlock*
gpuSub__makeBlock(GPUSubAllocator *alloc, uint32_t key, uint64_t blockSize) {
  GPUSubBlock *block;

  if (!(block = calloc(1u, sizeof(*block))) || !gpuSub__reserve(block)) {
    free(block);
    return NULL;
  }

  if (!alloc->info.createBlock(alloc->info.user, key, blockSize, &block->handle, &block->mapped)) {
    free(block->free);
    free(block);
    return NULL;
  }

  block->key            = key;
  block->size           = blockSize;
  block->free[0].offset = 0u;
  block->free[0].size   = blockSize;
  block->freeCount      = 1u;

  return block;
}

/* a made block joins the allocator, newest last. Under the lock; false:
 * no room, the caller lets it go */
static bool
gpuSub__addBlock(GPUSubAllocator *alloc, GPUSubBlock *block) {
  GPUSubBlock **blocks;
  uint32_t      capacity;

  if (alloc->blockCount == alloc->blockCapacity) {
    capacity = alloc->blockCapacity ? alloc->blockCapacity * 2u : 8u;
    if (!(blocks = realloc(alloc->blocks, capacity * sizeof(*blocks))))
      return false;
    alloc->blocks        = blocks;
    alloc->blockCapacity = capacity;
  }

  alloc->blocks[alloc->blockCount++] = block;
  return true;
}

/* where a key sits in a warm-up's queue: under the lock; -1 not there */
static int
gpuSub__warmFind(const GPUSubAllocator *alloc, uint32_t key) {
  uint32_t i;

  for (i = 0u; i < alloc->warmCount; i++) {
    if (alloc->warmKeys[i] == key)
      return (int)i;
  }

  return -1;
}

static bool
gpuSub__hasKey(const GPUSubAllocator *alloc, uint32_t key) {
  uint32_t i;

  for (i = 0u; i < alloc->blockCount; i++) {
    if (alloc->blocks[i]->key == key)
      return true;
  }

  return false;
}

GPU_HIDE
bool
gpuSubAllocate(GPUSubAllocator  *alloc,
               uint32_t          key,
               uint64_t          size,
               uint64_t          alignment,
               GPUSubAllocation *out) {
  GPUSubBlock *block;
  uint64_t     offset, blockSize;
  uint32_t     i;
  bool         placed;

  if (out)
    memset(out, 0, sizeof(*out));

  if (!alloc || !out || !size || size > alloc->info.maxResourceSize
      || !alignment || (alignment & (alignment - 1u)))
    return false;

  placed = false;
  block  = NULL;
  SUB_LOCK(alloc);

  /* newest blocks first: they are the largest */
  for (i = alloc->blockCount; i > 0u && !placed; i--) {
    block = alloc->blocks[i - 1u];
    if (block->key == key && block->size - block->used >= size)
      placed = gpuSub__place(block, size, alignment, &offset);
  }

  /* a key a warm-up is making the first block of: the caller allocates
   * this one itself, as without a pool, rather than wait for that block
   * or make another (the allocation is small; a block is milliseconds) */
  if (!placed && gpuSub__warmFind(alloc, key) >= 0) {
    SUB_UNLOCK(alloc);
    return false;
  }

  if (!placed) {
    blockSize = gpuSub__blockSize(alloc, key, size);
    SUB_UNLOCK(alloc);

    if (!(block = gpuSub__makeBlock(alloc, key, blockSize)))
      return false;

    SUB_LOCK(alloc);
    if (!gpuSub__addBlock(alloc, block)) {
      SUB_UNLOCK(alloc);
      gpuSub__releaseBlock(alloc, block);
      return false;
    }

    placed = gpuSub__place(block, size, alignment, &offset);
  }

  SUB_UNLOCK(alloc);

  if (!placed)
    return false;

  out->block  = block;
  out->offset = offset;
  out->size   = size;

  return true;
}

/* the first blocks of keys, queued by a warm-up and made by its threads
 * (gpuSubMakeQueued) before anything asks for them; until a key's block is
 * there, its allocations go to the caller (gpuSubAllocate). Keys with a
 * block already, or queued, are passed over */
GPU_HIDE
void
gpuSubQueue(GPUSubAllocator *alloc, const uint32_t *keys, uint32_t count) {
  uint32_t i;

  if (!alloc || !keys)
    return;

  SUB_LOCK(alloc);
  for (i = 0u; i < count; i++) {
    if (!gpuSub__hasKey(alloc, keys[i]) && gpuSub__warmFind(alloc, keys[i]) < 0
        && alloc->warmCount < GPU_SUB_WARM_MAX) {
      alloc->warmKeys[alloc->warmCount]  = keys[i];
      alloc->warmState[alloc->warmCount] = GPU_SUB_WARM_QUEUED;
      alloc->warmCount++;
    }
  }
  SUB_UNLOCK(alloc);
}

/* makes one queued key's first block on this thread: 1 made, 0 nothing
 * queued, -1 its memory couldn't be had */
GPU_HIDE
int
gpuSubMakeQueued(GPUSubAllocator *alloc) {
  GPUSubBlock *block;
  uint64_t     blockSize;
  uint32_t     key, i;
  int          w;
  bool         made;

  if (!alloc)
    return 0;

  SUB_LOCK(alloc);
  for (i = 0u; i < alloc->warmCount && alloc->warmState[i] != GPU_SUB_WARM_QUEUED; i++) {}
  if (i == alloc->warmCount) {
    SUB_UNLOCK(alloc);
    return 0;
  }
  alloc->warmState[i] = GPU_SUB_WARM_MAKING;
  key                 = alloc->warmKeys[i];
  blockSize           = alloc->info.firstBlockSize;
  SUB_UNLOCK(alloc);

  block = gpuSub__makeBlock(alloc, key, blockSize);
  made  = block != NULL;

  SUB_LOCK(alloc);
  if ((w = gpuSub__warmFind(alloc, key)) >= 0) {
    alloc->warmCount--;
    alloc->warmKeys[w]  = alloc->warmKeys[alloc->warmCount];
    alloc->warmState[w] = alloc->warmState[alloc->warmCount];
  }
  if (block && !gpuSub__hasKey(alloc, key) && gpuSub__addBlock(alloc, block))
    block = NULL;
  SUB_UNLOCK(alloc);

  if (block)
    gpuSub__releaseBlock(alloc, block);

  return made ? 1 : -1;
}

GPU_HIDE
void
gpuSubFree(GPUSubAllocator *alloc, const GPUSubAllocation *allocation) {
  GPUSubBlock *block;
  GPUSubRange *ranges;
  uint64_t     offset, size;
  uint32_t     lo, hi, mid;
  bool         joinsPrev, joinsNext;

  if (!alloc || !allocation || !(block = allocation->block) || !allocation->size)
    return;

  offset = allocation->offset;
  size   = allocation->size;

  SUB_LOCK(alloc);

  /* the first free range after it */
  lo = 0u;
  hi = block->freeCount;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2u;
    if (block->free[mid].offset < offset)
      lo = mid + 1u;
    else
      hi = mid;
  }

  ranges    = block->free;
  joinsPrev = lo > 0u && ranges[lo - 1u].offset + ranges[lo - 1u].size == offset;
  joinsNext = lo < block->freeCount && offset + size == ranges[lo].offset;

  if (joinsPrev && joinsNext) {
    ranges[lo - 1u].size += size + ranges[lo].size;
    memmove(&ranges[lo], &ranges[lo + 1u], (block->freeCount - lo - 1u) * sizeof(*ranges));
    block->freeCount--;
  } else if (joinsPrev) {
    ranges[lo - 1u].size += size;
  } else if (joinsNext) {
    ranges[lo].offset  = offset;
    ranges[lo].size   += size;
  } else if (gpuSub__reserve(block)) {
    ranges = block->free;
    memmove(&ranges[lo + 1u], &ranges[lo], (block->freeCount - lo) * sizeof(*ranges));
    ranges[lo].offset = offset;
    ranges[lo].size   = size;
    block->freeCount++;
  } else {
    /* no room to record it: the range stays used until the block goes */
    SUB_UNLOCK(alloc);
    return;
  }

  block->used -= size;
  SUB_UNLOCK(alloc);
}

GPU_HIDE
void
gpuSubTrim(GPUSubAllocator *alloc, bool keepNewest) {
  GPUSubBlock *block;
  uint32_t     i, j, kept;
  bool         newest;

  if (!alloc)
    return;

  SUB_LOCK(alloc);

  kept = 0u;
  for (i = 0u; i < alloc->blockCount; i++) {
    block  = alloc->blocks[i];
    newest = keepNewest;
    for (j = i + 1u; j < alloc->blockCount && newest; j++)
      newest = alloc->blocks[j]->key != block->key;

    if (block->used == 0u && !newest) {
      gpuSub__releaseBlock(alloc, block);
      continue;
    }

    alloc->blocks[kept++] = block;
  }
  alloc->blockCount = kept;

  SUB_UNLOCK(alloc);
}
