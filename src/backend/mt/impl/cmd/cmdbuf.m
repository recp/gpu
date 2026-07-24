/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../../common.h"

enum {
  MT_CONSTANT_UPLOAD_CHUNK_SIZE = 64u * 1024u,
  MT_CONSTANT_UPLOAD_ALIGNMENT  = 256u
};

static uint64_t
mt_alignUp(uint64_t value, uint64_t alignment) {
  return (value + alignment - 1u) & ~(alignment - 1u);
}

#if MT_HAS_METAL4
static void
mt_resetArgumentState(MTArgumentState *state) {
  if (!state || !state->table) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    id<MTL4ArgumentTable> table = state->table;
    MTLResourceID empty = {0};
    uint32_t bufferMask;
    uint32_t resourceMask;
    uint16_t samplerMask;

    bufferMask = state->bufferMask;
    while (bufferMask != 0u) {
      uint32_t index = (uint32_t)__builtin_ctz(bufferMask);

      [table setAddress:0 atIndex:index];
      bufferMask &= bufferMask - 1u;
    }

    resourceMask = state->resourceMask;
    while (resourceMask != 0u) {
      uint32_t index = (uint32_t)__builtin_ctz(resourceMask);

      [table setResource:empty atBufferIndex:index];
      resourceMask &= resourceMask - 1u;
    }

    for (uint32_t word = 0; word < 2u; word++) {
      uint64_t textureMask = state->textureMask[word];

      while (textureMask != 0u) {
        uint32_t index = word * 64u + (uint32_t)__builtin_ctzll(textureMask);

        [table setTexture:empty atIndex:index];
        textureMask &= textureMask - 1u;
      }
    }

    samplerMask = state->samplerMask;
    while (samplerMask != 0u) {
      uint32_t index = (uint32_t)__builtin_ctz((uint32_t)samplerMask);

      [table setSamplerState:empty atIndex:index];
      samplerMask &= (uint16_t)(samplerMask - 1u);
    }
  }

  memset(state->textureMask, 0, sizeof(state->textureMask));
  state->bufferMask = 0u;
  state->resourceMask = 0u;
  state->samplerMask = 0u;
}
#endif

GPU_HIDE
bool
mt_prepareArgumentState(GPUCommandBuffer *cmdb,
                        MTArgumentState  *state,
                        const char       *label) {
#if MT_HAS_METAL4
  GPUDeviceMT *deviceMT;
  NSError     *error;

  if (!cmdb || !state || !mt_commandBufferIsModern(cmdb) ||
      !cmdb->_queue || !cmdb->_queue->_device) {
    return false;
  }
  if (state->table) {
    mt_resetArgumentState(state);
    return true;
  }

  deviceMT = cmdb->_queue->_device->_priv;
  if (!deviceMT || !deviceMT->device) {
    return false;
  }

  error = nil;
  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTL4ArgumentTableDescriptor *desc;

    desc = [MTL4ArgumentTableDescriptor new];
    desc.maxBufferBindCount = MT_ARGUMENT_BUFFER_COUNT;
    desc.maxTextureBindCount = MT_ARGUMENT_TEXTURE_COUNT;
    desc.maxSamplerStateBindCount = MT_ARGUMENT_SAMPLER_COUNT;
    desc.initializeBindings = YES;
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (gpuDeviceDebugMarkersEnabled(cmdb->_queue->_device) &&
        label && label[0] != '\0') {
      desc.label = [NSString stringWithUTF8String:label];
    }
#else
    GPU__UNUSED(label);
#endif

    state->table = [deviceMT->device newArgumentTableWithDescriptor:desc error:&error];
    [desc release];
  }
  if (!state->table) {
#if GPU_BUILD_WITH_VALIDATION
    if (cmdb->_queue->_device->runtimeConfig.enableVerboseLogs && error) {
      NSLog(@"GPU Metal 4 argument table failed: %@", error);
    }
#endif
    return false;
  }

  return true;
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(state);
  GPU__UNUSED(label);
  return false;
#endif
}

GPU_HIDE
void
mt_useAllocation(GPUCommandBuffer *cmdb, id allocation) {
#if MT_HAS_METAL4
  MTCommandBuffer *native;
  uint32_t         count;

  native = mt_commandBuffer(cmdb);
  if (!native || native->mode != MTCommandMode4 ||
      !native->residency || !allocation) {
    return;
  }

  count = native->residencyAllocationCount;
  for (uint32_t i = count; i > 0u; i--) {
    if (native->residencyAllocations[i - 1u] == allocation) {
      return;
    }
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    if (count < MT_RESIDENCY_CACHE_SIZE) {
      native->residencyAllocations[count] = allocation;
      native->residencyAllocationCount    = count + 1u;
      [native->residency addAllocation:allocation];
    } else if (![native->residency containsAllocation:allocation]) {
      [native->residency addAllocation:allocation];
    }
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(allocation);
#endif
}

GPU_HIDE
void
mt_setArgumentBuffer(GPUCommandBuffer *cmdb,
                     MTArgumentState  *state,
                     GPUBuffer        *buffer,
                     uint64_t           offset,
                     uint32_t           index) {
#if MT_HAS_METAL4
  if (!state || !state->table || !buffer ||
      index >= MT_ARGUMENT_BUFFER_COUNT || offset > buffer->sizeBytes) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    [(id<MTL4ArgumentTable>)state->table
      setAddress:buffer->_gpuAddress + offset
         atIndex:index];
    state->bufferMask |= 1u << index;
    mt_useAllocation(cmdb, (id<MTLBuffer>)buffer->_priv);
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(state);
  GPU__UNUSED(buffer);
  GPU__UNUSED(offset);
  GPU__UNUSED(index);
#endif
}

GPU_HIDE
void
mt_setArgumentTexture(GPUCommandBuffer *cmdb,
                      MTArgumentState  *state,
                      GPUTextureView   *view,
                      uint32_t           index) {
#if MT_HAS_METAL4
  if (!state || !state->table || !view || !view->_priv ||
      index >= MT_ARGUMENT_TEXTURE_COUNT) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTLResourceID resourceID;

    resourceID._impl = view->_gpuResourceID;
    [(id<MTL4ArgumentTable>)state->table
      setTexture:resourceID
         atIndex:index];
    state->textureMask[index / 64u] |= 1ull << (index % 64u);
    mt_useAllocation(cmdb, (id<MTLTexture>)view->_priv);
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(state);
  GPU__UNUSED(view);
  GPU__UNUSED(index);
#endif
}

GPU_HIDE
void
mt_setArgumentSampler(MTArgumentState *state,
                      GPUSampler      *sampler,
                      uint32_t         index) {
#if MT_HAS_METAL4
  if (!state || !state->table || !sampler || !sampler->_priv ||
      index >= MT_ARGUMENT_SAMPLER_COUNT) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
    MTLResourceID resourceID;

    resourceID._impl = sampler->_gpuResourceID;
    [(id<MTL4ArgumentTable>)state->table
      setSamplerState:resourceID
              atIndex:index];
    state->samplerMask |= (uint16_t)(1u << index);
  }
#else
  GPU__UNUSED(state);
  GPU__UNUSED(sampler);
  GPU__UNUSED(index);
#endif
}

GPU_HIDE
void
mt_setArgumentAccelerationStructure(
  GPUCommandBuffer            *cmdb,
  MTArgumentState             *state,
  GPUAccelerationStructureEXT *structure,
  uint32_t                     index) {
#if MT_HAS_METAL4
  GPUAccelerationStructureMT *native;

  if (!state || !state->table || !structure ||
      index >= MT_ARGUMENT_BUFFER_COUNT) {
    return;
  }
  native = structure->_priv;
  if (!native || !native->structure) {
    return;
  }
  if (@available(macOS 26.0, iOS 26.0, *)) {
    [(id<MTL4ArgumentTable>)state->table
      setResource:native->structure.gpuResourceID
    atBufferIndex:index];
    state->resourceMask |= 1u << index;
    mt_useAllocation(cmdb, native->structure);
  }
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(state);
  GPU__UNUSED(structure);
  GPU__UNUSED(index);
#endif
}

GPU_HIDE
bool
mt_reserveUpload(GPUCommandBuffer *cmdb,
                 uint64_t          sizeBytes,
                 uint64_t          alignment,
                 id<MTLBuffer>     *outBuffer,
                 uint64_t         *outOffset) {
  MTCommandBuffer *native;
  MTUploadChunk   *chunk;
  MTUploadChunk   *candidate;
  GPUDeviceMT     *deviceMT;
  uint64_t         alignedOffset;
  uint64_t         capacity;

  if (!cmdb || sizeBytes == 0u || sizeBytes > NSUIntegerMax ||
      alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
      !outBuffer || !outOffset ||
      !cmdb->_queue || !cmdb->_queue->_device) {
    return false;
  }
  *outBuffer = nil;
  *outOffset = 0u;

  native   = mt_commandBuffer(cmdb);
  deviceMT = cmdb->_queue->_device->_priv;
  if (!native || !deviceMT) {
    return false;
  }

  chunk         = NULL;
  alignedOffset = 0u;
  for (candidate = native->uploads; candidate; candidate = candidate->next) {
    uint64_t offset;

    if (candidate->offset > UINT64_MAX - (alignment - 1u)) {
      continue;
    }
    offset = mt_alignUp(candidate->offset, alignment);

    if (offset <= candidate->capacity &&
        sizeBytes <= candidate->capacity - offset) {
      chunk = candidate;
      alignedOffset = offset;
      break;
    }
  }
  if (!chunk) {
    if (sizeBytes > UINT64_MAX - (alignment - 1u)) {
      return false;
    }
    capacity = sizeBytes > MT_CONSTANT_UPLOAD_CHUNK_SIZE
                 ? mt_alignUp(sizeBytes, alignment)
                 : MT_CONSTANT_UPLOAD_CHUNK_SIZE;
    if (capacity > NSUIntegerMax) {
      return false;
    }
    chunk = calloc(1, sizeof(*chunk));
    if (!chunk) {
      return false;
    }

    chunk->buffer = [deviceMT->device newBufferWithLength:(NSUInteger)capacity
                                                   options:MTLResourceStorageModeShared];
    if (!chunk->buffer) {
      free(chunk);
      return false;
    }
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (gpuDeviceDebugMarkersEnabled(cmdb->_queue->_device)) {
      chunk->buffer.label = @"gpu-command-upload";
    }
#endif
    chunk->capacity = capacity;
    chunk->next = native->uploads;
    native->uploads = chunk;
    gpuDeviceRecordHotPathAlloc(cmdb->_queue->_device,
                                sizeof(*chunk) + capacity);
    alignedOffset = 0u;
  }

  chunk->offset = alignedOffset + sizeBytes;
  *outBuffer    = chunk->buffer;
  *outOffset    = alignedOffset;
  mt_useAllocation(cmdb, chunk->buffer);
  return true;
}

GPU_HIDE
bool
mt_uploadConstants(GPUCommandBuffer *cmdb,
                   const void       *data,
                   uint32_t          sizeBytes,
                   uint64_t         *outAddress) {
#if MT_HAS_METAL4
  MTCommandBuffer *native;
  id<MTLBuffer>    buffer;
  uint64_t         offset;

  native = mt_commandBuffer(cmdb);
  if (!native || native->mode != MTCommandMode4 || !data || !outAddress ||
      !mt_reserveUpload(cmdb,
                        sizeBytes,
                        MT_CONSTANT_UPLOAD_ALIGNMENT,
                        &buffer,
                        &offset)) {
    return false;
  }

  memcpy((uint8_t *)buffer.contents + offset, data, sizeBytes);
  if (@available(macOS 26.0, iOS 26.0, *)) {
    *outAddress = buffer.gpuAddress + offset;
  } else {
    return false;
  }
  return true;
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(data);
  GPU__UNUSED(sizeBytes);
  GPU__UNUSED(outAddress);
  return false;
#endif
}

GPU_HIDE
void
mt_applyPendingBarrier(GPUCommandBuffer *cmdb, id encoder) {
#if MT_HAS_COMMAND_BARRIERS
  MTCommandBuffer *native;
#if MT_HAS_METAL4
  MTCommandQueue  *queue;
  bool             sparseBarrier;
#endif

  native = mt_commandBuffer(cmdb);
  if (!native || !encoder) {
    return;
  }

#if MT_HAS_METAL4
  queue = native->owner;
  if (native->mode == MTCommandMode4 && queue) {
    os_unfair_lock_lock(&queue->poolLock);
    sparseBarrier = queue->pendingSparseBarrier;
    queue->pendingSparseBarrier = false;
    os_unfair_lock_unlock(&queue->poolLock);
    if (sparseBarrier) {
      native->pendingAfterStages  |= MTLStageResourceState;
      native->pendingBeforeStages |= MTLStageAll;
      native->pendingVisibility   |= MTL4VisibilityOptionDevice;
    }
  }
#endif
  if (native->pendingAfterStages == 0 || native->pendingBeforeStages == 0) {
    return;
  }

  if (@available(macOS 26.0, iOS 26.0, *)) {
#if MT_HAS_METAL4
    if (native->mode == MTCommandMode4) {
      [(id<MTL4CommandEncoder>)encoder
        barrierAfterQueueStages:native->pendingAfterStages
                   beforeStages:native->pendingBeforeStages
              visibilityOptions:native->pendingVisibility];
    } else
#endif
    {
      [(id<MTLCommandEncoder>)encoder
        barrierAfterQueueStages:native->pendingAfterStages
                   beforeStages:native->pendingBeforeStages];
    }
  }
  native->pendingAfterStages  = 0;
  native->pendingBeforeStages = 0;
  native->pendingVisibility   = 0u;
#else
  GPU__UNUSED(cmdb);
  GPU__UNUSED(encoder);
#endif
}

GPU_HIDE
void
mt_destroyCommandBufferState(MTCommandBuffer *native) {
  MTUploadChunk *chunk;

  if (!native) {
    return;
  }

  [native->vertexArguments.table release];
  [native->fragmentArguments.table release];
  [native->taskArguments.table release];
  [native->meshArguments.table release];
  [native->computeArguments.table release];
  [native->renderState.classic release];
  [native->computeState.classic release];
  [native->copyState.classic release];
  [native->rayQueryState.classic release];
  [native->renderPassState.classic release];
  [native->renderPassState.modern release];
  [native->classic release];
  [native->drawable release];
  [native->residency release];
  [native->allocator release];
  [native->modern release];

  chunk = native->uploads;
  while (chunk) {
    MTUploadChunk *next = chunk->next;

    [chunk->buffer release];
    free(chunk);
    chunk = next;
  }
  free(native);
}

GPU_HIDE
void
mt_recycleCommandBuffer(GPUCommandBuffer *cmdb) {
  MTCommandBuffer *native;
  MTCommandQueue  *queue;
  MTUploadChunk   *upload;

  native = mt_commandBuffer(cmdb);
  if (!native || !native->owner) {
    return;
  }

  queue = native->owner;
  [native->renderState.classic release];
  native->renderState.classic = nil;
  [native->computeState.classic release];
  native->computeState.classic = nil;
  [native->copyState.classic release];
  native->copyState.classic = nil;
  [native->rayQueryState.classic release];
  native->rayQueryState.classic = nil;
  [native->classic release];
  native->classic = nil;
  [native->drawable release];
  native->drawable = nil;
  native->pendingAfterStages = 0u;
  native->pendingBeforeStages = 0u;
  native->pendingVisibility = 0u;
  for (upload = native->uploads; upload; upload = upload->next) {
    upload->offset = 0u;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [native->allocator reset];
      [native->residency removeAllAllocations];
      native->residencyAllocationCount = 0u;
    }
  }
#endif

  os_unfair_lock_lock(&queue->poolLock);
  native->poolNext = queue->freeCommands;
  queue->freeCommands = native;
  os_unfair_lock_unlock(&queue->poolLock);
}

GPU_HIDE
bool
mt_cmdBufDrawable(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  MTCommandBuffer *native;

  native = mt_commandBuffer(cmdb);
  if (!native || !frame || !frame->drawable) {
    return false;
  }
#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    [native->drawable release];
    native->drawable = [(id<CAMetalDrawable>)frame->drawable retain];
    return true;
  }
#endif

  [native->classic presentDrawable:(id<CAMetalDrawable>)frame->drawable];
  return true;
}

static bool
mt_counterApiAvailable(void) {
  if (@available(macOS 10.15, iOS 14.0, *)) {
    return true;
  }

  return false;
}

static id<MTLCounterSet>
mt_timestampCounterSet(id<MTLDevice> device) {
  if (!device) {
    return nil;
  }

  if (!mt_counterApiAvailable()) {
    return nil;
  }

  for (id<MTLCounterSet> counterSet in device.counterSets) {
    if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
      return counterSet;
    }
  }

  return nil;
}

static bool
mt_supportsBlitCounterSampling(id<MTLDevice> device) {
  if (!device) {
    return false;
  }

  if (@available(macOS 11.0, iOS 14.0, *)) {
    return [device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];
  }

  return false;
}

GPU_HIDE
GPUResult
mt_createQuerySet(GPUDevice                  *device,
                  const GPUQuerySetCreateInfo *info,
                  GPUQuerySet                *set) {
  GPUDeviceMT                      *deviceMT;
  MTQuerySet                       *native;
  MTLCounterSampleBufferDescriptor *desc;
  id<MTLCounterSampleBuffer>       sampleBuffer;
  id<MTLCounterSet>                counterSet;
  NSError                         *error;

  if (!device || !info || !set ||
      (info->type != GPU_QUERY_TIMESTAMP &&
       info->type != GPU_QUERY_OCCLUSION)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!device->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  deviceMT = device->_priv;
  native   = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->mode = (MTCommandMode)deviceMT->commandMode;
  if (info->type == GPU_QUERY_OCCLUSION) {
    uint64_t resultBytes;

    resultBytes = (uint64_t)info->count * sizeof(uint64_t);
    if (resultBytes > NSUIntegerMax) {
      free(native);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
    native->visibility = [deviceMT->device
      newBufferWithLength:(NSUInteger)resultBytes
                  options:MTLResourceStorageModePrivate];
    if (!native->visibility) {
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (gpuDeviceDebugMarkersEnabled(device) &&
        info->label && info->label[0] != '\0') {
      native->visibility.label = [NSString stringWithUTF8String:info->label];
    }
#endif
    set->_priv = native;
    return GPU_OK;
  }

  if (!mt_counterApiAvailable()) {
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4CounterHeapDescriptor *heapDesc;

      heapDesc = [MTL4CounterHeapDescriptor new];
      heapDesc.type = MTL4CounterHeapTypeTimestamp;
      heapDesc.count = info->count;
      error = nil;
      native->modern = [deviceMT->device newCounterHeapWithDescriptor:heapDesc
                                                                error:&error];
      [heapDesc release];
#if GPU_BUILD_WITH_DEBUG_MARKERS
      if (native->modern && gpuDeviceDebugMarkersEnabled(device) &&
          info->label && info->label[0] != '\0') {
        [(id<MTL4CounterHeap>)native->modern
          setLabel:[NSString stringWithUTF8String:info->label]];
      }
#endif
    }
    if (!native->modern) {
      free(native);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    set->_priv = native;
    return GPU_OK;
  }
#endif

  if (!mt_supportsBlitCounterSampling(deviceMT->device)) {
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  counterSet = mt_timestampCounterSet(deviceMT->device);
  if (!counterSet) {
    free(native);
    return GPU_ERROR_UNSUPPORTED;
  }

  desc = [[MTLCounterSampleBufferDescriptor alloc] init];
  desc.counterSet = counterSet;
  desc.sampleCount = (NSUInteger)info->count;
  desc.storageMode = MTLStorageModePrivate;
#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuDeviceDebugMarkersEnabled(device) &&
      info->label && info->label[0] != '\0') {
    desc.label = [NSString stringWithUTF8String:info->label];
  }
#endif

  error = nil;
  sampleBuffer = [deviceMT->device newCounterSampleBufferWithDescriptor:desc
                                                                  error:&error];
  [desc release];
  if (!sampleBuffer) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->classic = sampleBuffer;
  set->_priv = native;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyQuerySet(GPUQuerySet *set) {
  MTQuerySet *native;

  if (!set || !set->_priv) {
    return;
  }

  native = set->_priv;
  [native->classic release];
  [native->visibility release];
  [native->modern release];
  free(native);
  set->_priv = NULL;
}

GPU_HIDE
void
mt_writeTimestamp(GPUCommandBuffer *cmdb,
                  GPUQuerySet      *set,
                  uint32_t          queryIndex,
                  bool              beginningOfPass) {
  MTQuerySet *native;
  id<MTLBlitCommandEncoder> blit;

  GPU__UNUSED(beginningOfPass);
  if (!cmdb || !cmdb->_priv || !set || !set->_priv || queryIndex >= set->count) {
    return;
  }

  if (!mt_counterApiAvailable()) {
    return;
  }

  native = set->_priv;
#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [mt_modernCommandBuffer(cmdb) writeTimestampIntoHeap:native->modern
                                                   atIndex:queryIndex];
    }
    return;
  }
#endif
  if (!mt_supportsBlitCounterSampling(native->classic.device)) {
    return;
  }

  @autoreleasepool {
    blit = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
    if (!blit) {
      return;
    }
    mt_applyPendingBarrier(cmdb, blit);

    [blit sampleCountersInBuffer:native->classic
                    atSampleIndex:(NSUInteger)queryIndex
                      withBarrier:YES];
    [blit endEncoding];
  }
}

GPU_HIDE
void
mt_beginOcclusionQuery(GPURenderPassEncoder *pass,
                       GPUQuerySet          *set,
                       uint32_t              queryIndex) {
  MTRenderEncoder *encoder;
  MTQuerySet      *native;
  NSUInteger       offset;

  encoder = pass ? pass->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!encoder || !native || !native->visibility) {
    return;
  }

  offset = (NSUInteger)queryIndex * sizeof(uint64_t);
#if MT_HAS_METAL4
  if (encoder->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [encoder->modern setVisibilityResultMode:MTLVisibilityResultModeBoolean
                                        offset:offset];
    }
    return;
  }
#endif
  [encoder->classic setVisibilityResultMode:MTLVisibilityResultModeBoolean
                                     offset:offset];
}

GPU_HIDE
void
mt_endOcclusionQuery(GPURenderPassEncoder *pass,
                     GPUQuerySet          *set,
                     uint32_t              queryIndex) {
  MTRenderEncoder *encoder;
  MTQuerySet      *native;
  NSUInteger       offset;

  encoder = pass ? pass->_priv : NULL;
  native  = set ? set->_priv : NULL;
  if (!encoder || !native || !native->visibility) {
    return;
  }

  offset = (NSUInteger)queryIndex * sizeof(uint64_t);
#if MT_HAS_METAL4
  if (encoder->modern) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      [encoder->modern setVisibilityResultMode:MTLVisibilityResultModeDisabled
                                        offset:offset];
    }
    return;
  }
#endif
  [encoder->classic setVisibilityResultMode:MTLVisibilityResultModeDisabled
                                     offset:offset];
}

GPU_HIDE
void
mt_resolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet      *set,
                   uint32_t          firstQuery,
                   uint32_t          queryCount,
                   GPUBuffer        *dstBuffer,
                   uint64_t          dstOffset) {
  MTQuerySet               *native;
  id<MTLBlitCommandEncoder> blit;
  id<MTLBuffer>             dst;
  uint64_t                  resolveBytes;

  if (!cmdb || !cmdb->_priv || !set || !set->_priv || !dstBuffer ||
      queryCount == 0u || firstQuery > set->count ||
      queryCount > set->count - firstQuery) {
    return;
  }
  dst          = (id<MTLBuffer>)dstBuffer->_priv;
  resolveBytes = (uint64_t)queryCount * sizeof(uint64_t);
  if (dstOffset > [dst length] || resolveBytes > [dst length] - dstOffset) {
    return;
  }

  native = set->_priv;
  if (set->type == GPU_QUERY_OCCLUSION) {
    uint64_t sourceOffset;

    sourceOffset = (uint64_t)firstQuery * sizeof(uint64_t);
    if (!native->visibility ||
        sourceOffset > [native->visibility length] ||
        resolveBytes > [native->visibility length] - sourceOffset) {
      return;
    }
#if MT_HAS_METAL4
    if (native->mode == MTCommandMode4) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        id<MTL4ComputeCommandEncoder> copy;

        copy = [(id<MTL4CommandBuffer>)mt_modernCommandBuffer(cmdb)
          computeCommandEncoder];
        if (!copy) {
          return;
        }
        mt_applyPendingBarrier(cmdb, copy);
        [copy barrierAfterQueueStages:MTLStageVertex | MTLStageFragment
                         beforeStages:MTLStageBlit
                    visibilityOptions:MTL4VisibilityOptionDevice];
        mt_useAllocation(cmdb, native->visibility);
        mt_useAllocation(cmdb, dst);
        [copy copyFromBuffer:native->visibility
                sourceOffset:(NSUInteger)sourceOffset
                    toBuffer:dst
           destinationOffset:(NSUInteger)dstOffset
                        size:(NSUInteger)resolveBytes];
        [copy endEncoding];
      }
      return;
    }
#endif

    @autoreleasepool {
      blit = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
      if (!blit) {
        return;
      }
      mt_applyPendingBarrier(cmdb, blit);
      [blit copyFromBuffer:native->visibility
              sourceOffset:(NSUInteger)sourceOffset
                  toBuffer:dst
         destinationOffset:(NSUInteger)dstOffset
                      size:(NSUInteger)resolveBytes];
      [blit endEncoding];
    }
    return;
  }
  if (!mt_counterApiAvailable()) {
    return;
  }

#if MT_HAS_METAL4
  if (native->mode == MTCommandMode4) {
    if (@available(macOS 26.0, iOS 26.0, *)) {
      MTL4BufferRange range;

      range = MTL4BufferRangeMake(dst.gpuAddress + dstOffset, resolveBytes);
      mt_useAllocation(cmdb, dst);
      [mt_modernCommandBuffer(cmdb) resolveCounterHeap:native->modern
                                             withRange:NSMakeRange(firstQuery, queryCount)
                                            intoBuffer:range
                                             waitFence:nil
                                           updateFence:nil];
    }
    return;
  }
#endif

  @autoreleasepool {
    blit = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
    if (!blit) {
      return;
    }
    mt_applyPendingBarrier(cmdb, blit);

    [blit resolveCounters:native->classic
                  inRange:NSMakeRange((NSUInteger)firstQuery,
                                      (NSUInteger)queryCount)
        destinationBuffer:dst
        destinationOffset:(NSUInteger)dstOffset];
    [blit endEncoding];
  }
}

GPU_HIDE
void
mt_initCmdBuff(GPUApiCommandBuffer *api) {
  api->presentDrawable     = mt_cmdBufDrawable;
  api->createQuerySet      = mt_createQuerySet;
  api->destroyQuerySet     = mt_destroyQuerySet;
  api->writeTimestamp      = mt_writeTimestamp;
  api->beginOcclusionQuery = mt_beginOcclusionQuery;
  api->endOcclusionQuery   = mt_endOcclusionQuery;
  api->resolveQuerySet     = mt_resolveQuerySet;
}
