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

GPU_HIDE
void
mt_cmdBufDrawable(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  [mt_classicCommandBuffer(cmdb) presentDrawable:(id<CAMetalDrawable>)frame->drawable];
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
mt_createQuerySet(GPUDevice *device,
                  const GPUQuerySetCreateInfo *info,
                  GPUQuerySet *set) {
  GPUDeviceMT *deviceMT;
  MTLCounterSampleBufferDescriptor *desc;
  id<MTLCounterSampleBuffer> sampleBuffer;
  id<MTLCounterSet> counterSet;
  NSError *error;

  if (!device || !info || !set || info->type != GPU_QUERY_TIMESTAMP) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (!device->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!mt_counterApiAvailable()) {
    return GPU_ERROR_UNSUPPORTED;
  }

  deviceMT = device->_priv;
  if (!mt_supportsBlitCounterSampling(deviceMT->device)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  counterSet = mt_timestampCounterSet(deviceMT->device);
  if (!counterSet) {
    return GPU_ERROR_UNSUPPORTED;
  }

  desc = [[MTLCounterSampleBufferDescriptor alloc] init];
  desc.counterSet = counterSet;
  desc.sampleCount = (NSUInteger)info->count;
  desc.storageMode = MTLStorageModePrivate;
  if (info->label) {
    desc.label = [NSString stringWithUTF8String:info->label];
  }

  error = nil;
  sampleBuffer = [deviceMT->device newCounterSampleBufferWithDescriptor:desc
                                                                  error:&error];
  [desc release];
  if (!sampleBuffer) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  set->_priv = sampleBuffer;
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyQuerySet(GPUQuerySet *set) {
  if (!set || !set->_priv) {
    return;
  }

  [(id<MTLCounterSampleBuffer>)set->_priv release];
  set->_priv = NULL;
}

GPU_HIDE
void
mt_writeTimestamp(GPUCommandBuffer *cmdb, GPUQuerySet *set, uint32_t queryIndex) {
  id<MTLBlitCommandEncoder> blit;

  if (!cmdb || !cmdb->_priv || !set || !set->_priv || queryIndex >= set->count) {
    return;
  }

  if (!mt_counterApiAvailable()) {
    return;
  }
  if (!mt_supportsBlitCounterSampling(((id<MTLCounterSampleBuffer>)set->_priv).device)) {
    return;
  }

  blit = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
  if (!blit) {
    return;
  }

  [blit sampleCountersInBuffer:(id<MTLCounterSampleBuffer>)set->_priv
                  atSampleIndex:(NSUInteger)queryIndex
                    withBarrier:YES];
  [blit endEncoding];
}

GPU_HIDE
void
mt_resolveQuerySet(GPUCommandBuffer *cmdb,
                   GPUQuerySet *set,
                   uint32_t firstQuery,
                   uint32_t queryCount,
                   GPUBuffer *dstBuffer,
                   uint64_t dstOffset) {
  id<MTLBlitCommandEncoder> blit;
  id<MTLBuffer> dst;
  uint64_t resolveBytes;

  if (!cmdb || !cmdb->_priv || !set || !set->_priv || !dstBuffer ||
      queryCount == 0u || firstQuery > set->count ||
      queryCount > set->count - firstQuery) {
    return;
  }
  if (!mt_counterApiAvailable()) {
    return;
  }

  dst = (id<MTLBuffer>)dstBuffer->_priv;
  resolveBytes = (uint64_t)queryCount * sizeof(MTLCounterResultTimestamp);
  if (dstOffset > [dst length] || resolveBytes > [dst length] - dstOffset) {
    return;
  }

  blit = [mt_classicCommandBuffer(cmdb) blitCommandEncoder];
  if (!blit) {
    return;
  }

  [blit resolveCounters:(id<MTLCounterSampleBuffer>)set->_priv
                inRange:NSMakeRange((NSUInteger)firstQuery, (NSUInteger)queryCount)
      destinationBuffer:dst
      destinationOffset:(NSUInteger)dstOffset];
  [blit endEncoding];
}

GPU_HIDE
void
mt_initCmdBuff(GPUApiCommandBuffer *api) {
  api->presentDrawable = mt_cmdBufDrawable;
  api->createQuerySet = mt_createQuerySet;
  api->destroyQuerySet = mt_destroyQuerySet;
  api->writeTimestamp = mt_writeTimestamp;
  api->resolveQuerySet = mt_resolveQuerySet;
}
