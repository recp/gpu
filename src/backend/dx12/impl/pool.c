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

/* small buffers and textures placed in pooled ID3D12Heaps (api/suballoc)
 * instead of a committed resource (an implicit heap) each. heaps hold one
 * heap type and either buffers or non render-target/depth textures, as
 * resource heap tier 1 asks; render targets and depth stay committed, as
 * placed ones must be cleared, discarded or copied before first use. small
 * textures take 4 KB placement where the device allows it (a committed
 * resource is at least 64 KB). GPU_RESOURCE_POOL=0 turns the pool off. */

#include "../common.h"
#include "../impl.h"
#include "../../../api/suballoc_internal.h"

#include <stdlib.h>
#include <string.h>

enum {
  DX12_POOL_TYPE_MASK = 0xfu,
  DX12_POOL_TEXTURES  = 1u << 4
};

#define DX12_POOL_FIRST_BLOCK   ((uint64_t)8u << 20)
#define DX12_POOL_LARGEST_BLOCK ((uint64_t)64u << 20)
#define DX12_POOL_MAX_RESOURCE  ((uint64_t)4u << 20)

static bool
dx12__poolCreateBlock(void *user, uint32_t key, uint64_t size, uint64_t *outHandle, void **outMapped) {
  D3D12_HEAP_DESC desc = {0};
  GPUDeviceDX12  *deviceDX12;
  ID3D12Heap     *heap;

  deviceDX12                       = user;
  desc.SizeInBytes                 = size;
  desc.Properties.Type             = (D3D12_HEAP_TYPE)(key & DX12_POOL_TYPE_MASK);
  desc.Properties.CreationNodeMask = 1u;
  desc.Properties.VisibleNodeMask  = 1u;
  desc.Alignment                   = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Flags                       = (key & DX12_POOL_TEXTURES) ? D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
                                                                : D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  heap                             = NULL;

  if (FAILED(deviceDX12->d3dDevice->lpVtbl->CreateHeap(deviceDX12->d3dDevice, &desc, &IID_ID3D12Heap,
                                                       (void **)&heap))
      || !heap)
    return false;

  *outHandle = (uint64_t)(uintptr_t)heap;
  *outMapped = NULL;

  return true;
}

static void
dx12__poolDestroyBlock(void *user, uint32_t key, uint64_t handle, void *mapped) {
  ID3D12Heap *heap;

  (void)user;
  (void)key;
  (void)mapped;

  if ((heap = (ID3D12Heap *)(uintptr_t)handle))
    heap->lpVtbl->Release(heap);
}

GPU_HIDE
void
dx12_initResourcePool(GPUDeviceDX12 *deviceDX12) {
  GPUSubAllocatorInfo info = {0};
  GPUSubAllocator    *pool;
  const char         *env;

  if (!deviceDX12 || !deviceDX12->d3dDevice
      || ((env = getenv("GPU_RESOURCE_POOL")) && strcmp(env, "0") == 0))
    return;

  if (!(pool = malloc(sizeof(*pool))))
    return;

  info.createBlock      = dx12__poolCreateBlock;
  info.destroyBlock     = dx12__poolDestroyBlock;
  info.user             = deviceDX12;
  info.firstBlockSize   = DX12_POOL_FIRST_BLOCK;
  info.largestBlockSize = DX12_POOL_LARGEST_BLOCK;
  info.maxResourceSize  = DX12_POOL_MAX_RESOURCE;

  if (!gpuSubInit(pool, &info)) {
    free(pool);
    return;
  }

  deviceDX12->resourcePool = pool;
}

/* after every resource placed in it is gone, before the device */

GPU_HIDE
void
dx12_destroyResourcePool(GPUDeviceDX12 *deviceDX12) {
  if (!deviceDX12 || !deviceDX12->resourcePool)
    return;

  gpuSubDestroy(deviceDX12->resourcePool);
  free(deviceDX12->resourcePool);
  deviceDX12->resourcePool = NULL;
}

GPU_HIDE
void
dx12_trimResourcePool(GPUDeviceDX12 *deviceDX12) {
  if (deviceDX12 && deviceDX12->resourcePool)
    gpuSubTrim(deviceDX12->resourcePool);
}

static bool
dx12__poolPlace(GPUDeviceDX12             *deviceDX12,
                uint32_t                   key,
                uint64_t                   size,
                uint64_t                   alignment,
                const D3D12_RESOURCE_DESC *desc,
                D3D12_RESOURCE_STATES      initialState,
                ID3D12Resource           **outResource,
                DX12PoolRange             *outRange) {
  GPUSubAllocation allocation;
  ID3D12Heap      *heap;
  ID3D12Resource  *resource;

  if (!gpuSubAllocate(deviceDX12->resourcePool, key, size, alignment, &allocation))
    return false;

  heap     = (ID3D12Heap *)(uintptr_t)allocation.block->handle;
  resource = NULL;

  if (FAILED(deviceDX12->d3dDevice->lpVtbl->CreatePlacedResource(deviceDX12->d3dDevice, heap, allocation.offset,
                                                                 desc, initialState, NULL,
                                                                 &IID_ID3D12Resource, (void **)&resource))
      || !resource) {
    gpuSubFree(deviceDX12->resourcePool, &allocation);
    return false;
  }

  outRange->pool   = deviceDX12->resourcePool;
  outRange->block  = allocation.block;
  outRange->offset = allocation.offset;
  outRange->size   = allocation.size;
  *outResource     = resource;

  return true;
}

GPU_HIDE
bool
dx12_poolPlaceBuffer(GPUDeviceDX12             *deviceDX12,
                     D3D12_HEAP_TYPE            heapType,
                     const D3D12_RESOURCE_DESC *desc,
                     D3D12_RESOURCE_STATES      initialState,
                     ID3D12Resource           **outResource,
                     DX12PoolRange             *outRange) {
  uint64_t size;

  if (!deviceDX12 || !deviceDX12->resourcePool || !desc
      || desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER
      || (uint32_t)heapType > DX12_POOL_TYPE_MASK
      || desc->Width > DX12_POOL_MAX_RESOURCE)
    return false;

  size = (desc->Width + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1u)
         & ~(uint64_t)(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1u);

  return dx12__poolPlace(deviceDX12, (uint32_t)heapType, size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                         desc, initialState, outResource, outRange);
}

/* default-heap textures that aren't render targets, depth or multisampled */

GPU_HIDE
bool
dx12_poolPlaceTexture(GPUDeviceDX12             *deviceDX12,
                      const D3D12_RESOURCE_DESC *desc,
                      D3D12_RESOURCE_STATES      initialState,
                      ID3D12Resource           **outResource,
                      DX12PoolRange             *outRange) {
  D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
  D3D12_RESOURCE_DESC            placed;

  if (!deviceDX12 || !deviceDX12->resourcePool || !desc
      || desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
      || desc->SampleDesc.Count > 1u
      || desc->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN
      || (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
    return false;

  /* 4 KB placement when the texture is small enough for it */
  placed           = *desc;
  placed.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
  deviceDX12->d3dDevice->lpVtbl->GetResourceAllocationInfo(deviceDX12->d3dDevice, &allocationInfo, 0u, 1u, &placed);

  if (allocationInfo.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
    placed.Alignment = 0u;
    deviceDX12->d3dDevice->lpVtbl->GetResourceAllocationInfo(deviceDX12->d3dDevice, &allocationInfo, 0u, 1u, &placed);
  }

  if (allocationInfo.SizeInBytes == UINT64_MAX || !allocationInfo.Alignment
      || allocationInfo.SizeInBytes > DX12_POOL_MAX_RESOURCE)
    return false;

  return dx12__poolPlace(deviceDX12, (uint32_t)D3D12_HEAP_TYPE_DEFAULT | DX12_POOL_TEXTURES,
                         allocationInfo.SizeInBytes, allocationInfo.Alignment,
                         &placed, initialState, outResource, outRange);
}

GPU_HIDE
void
dx12_poolRelease(DX12PoolRange *range) {
  GPUSubAllocation allocation;

  if (!range || !range->pool || !range->block)
    return;

  allocation.block  = range->block;
  allocation.offset = range->offset;
  allocation.size   = range->size;
  gpuSubFree(range->pool, &allocation);
  memset(range, 0, sizeof(*range));
}
