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

/* the block suballocator: random sizes, alignments and keys placed and
 * freed in random order; no two live ranges meet, each is aligned and inside
 * its block, a block's free ranges are sorted and coalesced and with its
 * live ranges cover it exactly, and trimming keeps the newest block of a key */

#include "../../../src/api/suballoc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVE_MAX 4096u

typedef struct Live {
  GPUSubAllocation a;
  uint32_t         key;
  uint64_t         alignment;
} Live;

static uint32_t blocksMade, blocksGone;
static uint64_t rng = 0x9e3779b97f4a7c15ull;

static uint64_t
next(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

static bool
createBlock(void *user, uint32_t key, uint64_t size, uint64_t *outHandle, void **outMapped) {
  (void)user;
  if (key == 7u)
    return false; /* a key whose memory can't be had */
  *outHandle = (uint64_t)++blocksMade;
  *outMapped = key == 1u ? malloc((size_t)size) : NULL;
  return true;
}

static void
destroyBlock(void *user, uint32_t key, uint64_t handle, void *mapped) {
  (void)user;
  (void)key;
  (void)handle;
  free(mapped);
  blocksGone++;
}

static int
check(GPUSubAllocator *alloc, Live *live, uint32_t count) {
  uint32_t b, i, j;
  uint64_t covered, inUse;

  for (b = 0u; b < alloc->blockCount; b++) {
    GPUSubBlock *block = alloc->blocks[b];
    covered            = 0u;

    for (i = 0u; i < block->freeCount; i++) {
      if (!block->free[i].size || block->free[i].offset + block->free[i].size > block->size)
        return fprintf(stderr, "suballoc: a free range out of its block\n"), 0;
      if (i && block->free[i - 1u].offset + block->free[i - 1u].size >= block->free[i].offset)
        return fprintf(stderr, "suballoc: free ranges unsorted or not coalesced\n"), 0;
      covered += block->free[i].size;
    }

    inUse = 0u;
    for (i = 0u; i < count; i++) {
      if (live[i].a.block == block)
        inUse += live[i].a.size;
    }

    /* alignment gaps stay free, so free and live cover the block */
    if (covered + inUse != block->size || block->used != inUse)
      return fprintf(stderr, "suballoc: block %u covered %llu of %llu\n", b,
                     (unsigned long long)(covered + inUse), (unsigned long long)block->size), 0;
  }

  for (i = 0u; i < count; i++) {
    if (live[i].a.offset % live[i].alignment || live[i].a.offset + live[i].a.size > live[i].a.block->size
        || live[i].a.block->key != live[i].key)
      return fprintf(stderr, "suballoc: a range misaligned or out of its block\n"), 0;

    for (j = i + 1u; j < count; j++) {
      if (live[i].a.block == live[j].a.block
          && live[i].a.offset < live[j].a.offset + live[j].a.size
          && live[j].a.offset < live[i].a.offset + live[i].a.size)
        return fprintf(stderr, "suballoc: two ranges meet\n"), 0;
    }
  }

  return 1;
}

int
main(void) {
  GPUSubAllocatorInfo info = {0};
  GPUSubAllocator     alloc;
  GPUSubAllocation    none;
  Live               *live;
  uint32_t            count, step, i, keys;
  uint64_t            size;

  info.createBlock      = createBlock;
  info.destroyBlock     = destroyBlock;
  info.firstBlockSize   = 1u << 20;
  info.largestBlockSize = 8u << 20;
  info.maxResourceSize  = 1u << 20;

  if (!(live = calloc(LIVE_MAX, sizeof(*live))) || !gpuSubInit(&alloc, &info))
    return 1;

  if (gpuSubAllocate(&alloc, 0u, info.maxResourceSize + 1u, 256u, &none) || none.block
      || gpuSubAllocate(&alloc, 7u, 4096u, 256u, &none) || none.block
      || gpuSubAllocate(&alloc, 0u, 4096u, 3u, &none)) {
    fprintf(stderr, "suballoc: an allocation that can't be placed was\n");
    return 1;
  }

  count = 0u;
  for (step = 0u; step < 200000u; step++) {
    if (count && (count == LIVE_MAX || next() % 100u < 48u)) {
      i = (uint32_t)(next() % count);
      gpuSubFree(&alloc, &live[i].a);
      live[i] = live[--count];
    } else {
      size  = next() % 4u == 0u ? 1u + next() % (1u << 20) : 1u + next() % 65536u;
      keys  = (uint32_t)(next() % 3u);
      live[count].key       = keys;
      live[count].alignment = 1ull << (next() % 17u);
      if (!gpuSubAllocate(&alloc, keys, size, live[count].alignment, &live[count].a)) {
        fprintf(stderr, "suballoc: %llu bytes not placed\n", (unsigned long long)size);
        return 1;
      }
      count++;
    }

    if (step % 997u == 0u && !check(&alloc, live, count))
      return 1;
    if (step % 50021u == 0u)
      gpuSubTrim(&alloc);
  }

  while (count)
    gpuSubFree(&alloc, &live[--count].a);

  if (!check(&alloc, live, 0u))
    return 1;

  for (i = 0u; i < alloc.blockCount; i++) {
    if (alloc.blocks[i]->used || alloc.blocks[i]->freeCount != 1u)
      return fprintf(stderr, "suballoc: an empty block not whole\n"), 1;
  }

  gpuSubTrim(&alloc);
  if (alloc.blockCount != 3u)
    return fprintf(stderr, "suballoc: trimming kept %u blocks, not one a key\n", alloc.blockCount), 1;

  gpuSubDestroy(&alloc);
  if (blocksGone != blocksMade)
    return fprintf(stderr, "suballoc: %u blocks made, %u gone\n", blocksMade, blocksGone), 1;

  printf("suballoc: 200000 steps, %u blocks made, all ranges apart and coalesced\n", blocksMade);
  free(live);
  return 0;
}
