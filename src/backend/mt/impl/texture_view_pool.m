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

#include "../common.h"
#include "texture_view_pool.h"

enum {
  MT_TEXTURE_VIEW_POOL_SIZE  = 256u,
  MT_TEXTURE_VIEW_POOL_WORDS = MT_TEXTURE_VIEW_POOL_SIZE / 64u
};

struct MTTextureViewPoolPage {
  id                         pool;
  struct MTTextureViewPoolPage *next;
  uint64_t                   used[MT_TEXTURE_VIEW_POOL_WORDS];
};

#if MT_HAS_METAL4
static bool
mt_prepareTextureViewPool(GPUDeviceMT *device,
                          MTTextureViewPoolPage **outPage) {
  MTLResourceViewPoolDescriptor *descriptor;
  MTTextureViewPoolPage         *page;
  NSError                       *error;

  if (!device || !outPage) {
    return false;
  }

  if (!device->textureViewPlaceholder) {
    MTLTextureDescriptor *textureDescriptor;

    textureDescriptor                 = [MTLTextureDescriptor new];
    textureDescriptor.textureType     = MTLTextureType2D;
    textureDescriptor.pixelFormat     = MTLPixelFormatR8Unorm;
    textureDescriptor.width           = 1u;
    textureDescriptor.height          = 1u;
    textureDescriptor.depth           = 1u;
    textureDescriptor.mipmapLevelCount = 1u;
    textureDescriptor.sampleCount     = 1u;
    textureDescriptor.storageMode     = MTLStorageModePrivate;
    textureDescriptor.usage           = MTLTextureUsageShaderRead;
    device->textureViewPlaceholder =
      [device->device newTextureWithDescriptor:textureDescriptor];
    [textureDescriptor release];
    if (!device->textureViewPlaceholder) {
      return false;
    }
  }

  page = calloc(1, sizeof(*page));
  if (!page) {
    return false;
  }

  descriptor                   = [MTLResourceViewPoolDescriptor new];
  descriptor.resourceViewCount = MT_TEXTURE_VIEW_POOL_SIZE;
  descriptor.label             = @"GPU texture views";
  error                        = nil;
  page->pool = [device->device newTextureViewPoolWithDescriptor:descriptor
                                                          error:&error];
  [descriptor release];
  if (!page->pool) {
    if (error) {
      NSLog(@"Metal texture view pool creation failed: %@", error);
    }
    free(page);
    return false;
  }

  page->next               = device->textureViewPools;
  device->textureViewPools = page;
  *outPage                 = page;
  return true;
}

static bool
mt_findTextureViewSlot(GPUDeviceMT          *device,
                       MTTextureViewPoolPage **outPage,
                       uint32_t              *outIndex) {
  MTTextureViewPoolPage *page;

  for (page = device->textureViewPools; page; page = page->next) {
    for (uint32_t word = 0u; word < MT_TEXTURE_VIEW_POOL_WORDS; word++) {
      uint64_t available;

      available = ~page->used[word];
      if (available != 0u) {
        *outPage  = page;
        *outIndex = word * 64u + (uint32_t)__builtin_ctzll(available);
        return true;
      }
    }
  }

  if (!mt_prepareTextureViewPool(device, &page)) {
    return false;
  }
  *outPage  = page;
  *outIndex = 0u;
  return true;
}
#endif

GPU_HIDE
GPUResult
mt_acquireTextureView(GPUDeviceMT      *device,
                      id<MTLTexture>    texture,
                      id                descriptor,
                      MTTextureViewSlot *slot,
                      uint64_t          *outResourceID) {
#if MT_HAS_METAL4
  MTTextureViewPoolPage *page;
  MTLResourceID          resourceID;
  uint32_t               index;

  if (!device || !texture || !descriptor || !slot || !outResourceID ||
      device->commandMode != MTCommandMode4) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    os_unfair_lock_lock(&device->textureViewPoolLock);
    if (!mt_findTextureViewSlot(device, &page, &index)) {
      os_unfair_lock_unlock(&device->textureViewPoolLock);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    page->used[index / 64u] |= 1ull << (index % 64u);
    resourceID = [(id<MTLTextureViewPool>)page->pool
      setTextureView:texture
          descriptor:(MTLTextureViewDescriptor *)descriptor
             atIndex:index];
    if (resourceID._impl == 0u) {
      page->used[index / 64u] &= ~(1ull << (index % 64u));
      os_unfair_lock_unlock(&device->textureViewPoolLock);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    slot->page    = page;
    slot->index   = index;
    *outResourceID = resourceID._impl;
    os_unfair_lock_unlock(&device->textureViewPoolLock);
    return GPU_OK;
  }
#else
  GPU__UNUSED(device);
  GPU__UNUSED(texture);
  GPU__UNUSED(descriptor);
  GPU__UNUSED(slot);
  GPU__UNUSED(outResourceID);
#endif
  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
mt_releaseTextureView(GPUDeviceMT *device, MTTextureViewSlot *slot) {
#if MT_HAS_METAL4
  MTTextureViewPoolPage *page;
  uint32_t               index;

  if (!device || !slot || !(page = slot->page) ||
      (index = slot->index) >= MT_TEXTURE_VIEW_POOL_SIZE) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    os_unfair_lock_lock(&device->textureViewPoolLock);
    if ((page->used[index / 64u] & (1ull << (index % 64u))) != 0u) {
      [(id<MTLTextureViewPool>)page->pool
        setTextureView:(id<MTLTexture>)device->textureViewPlaceholder
              atIndex:index];
      page->used[index / 64u] &= ~(1ull << (index % 64u));
    }
    slot->page  = NULL;
    slot->index = 0u;
    os_unfair_lock_unlock(&device->textureViewPoolLock);
  }
#else
  GPU__UNUSED(device);
  GPU__UNUSED(slot);
#endif
}

GPU_HIDE
void
mt_destroyTextureViewPools(GPUDeviceMT *device) {
  MTTextureViewPoolPage *page;

  if (!device) {
    return;
  }

  page = device->textureViewPools;
  while (page) {
    MTTextureViewPoolPage *next;

    next = page->next;
    [page->pool release];
    free(page);
    page = next;
  }
  [device->textureViewPlaceholder release];
  device->textureViewPlaceholder = nil;
  device->textureViewPools       = NULL;
}
