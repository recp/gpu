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

#ifndef metal_common_h
#define metal_common_h

#include "../common.h"
#include "../../api/adapter_internal.h"
#include "../../api/buffer_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/device_internal.h"
#include "../../api/frame_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/query_internal.h"
#include "../../api/sampler_internal.h"
#include "../../api/surface_internal.h"
#include "../../api/swapchain_internal.h"
#include "../../api/texture_internal.h"

#if defined(__APPLE__) && defined(__OBJC__)
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>
#import <os/lock.h>

typedef CALayer GPUViewLayer;

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
typedef UIView GPUViewHandle;
#elif TARGET_OS_MAC
#import <AppKit/AppKit.h>
typedef NSView GPUViewHandle;
#else
#error "Unsupported platform"
#endif

@class GPUSwapChainObjc;

typedef enum MTCommandMode {
  MTCommandModeClassic = 0,
  MTCommandMode4
} MTCommandMode;

typedef struct GPUSwapChainMetal {
  CAMetalLayer  *layer;
  void          *objc;
  GPUFrame       frame;
  GPUTexture     target;
  GPUTextureView targetView;
  bool           frameActive;
} GPUSwapChainMetal;

typedef struct GPUDeviceMT {
  id<MTLDevice>     device;
  GPUCommandQueue **createdQueues;
  uint32_t          nCreatedQueues;
  MTCommandMode     commandMode;
} GPUDeviceMT;

typedef struct GPUTextureMT {
  id<MTLTexture> texture;
  id<MTLTexture> stencilCopyView;
} GPUTextureMT;

enum {
  MT_ARGUMENT_BUFFER_COUNT  = 31u,
  MT_ARGUMENT_TEXTURE_COUNT = 128u,
  MT_ARGUMENT_SAMPLER_COUNT = 16u,
  MT_PUSH_CONSTANT_INDEX    = 30u
};

typedef struct MTArgumentState {
  id       table;
  uint64_t textureMask[2];
  uint32_t bufferMask;
  uint16_t samplerMask;
} MTArgumentState;

typedef struct MTUploadChunk {
  id<MTLBuffer>          buffer;
  struct MTUploadChunk  *next;
  uint64_t               capacity;
  uint64_t               offset;
} MTUploadChunk;

typedef struct MTRenderPass {
  MTLRenderPassDescriptor *classic;
  id                       modern;
} MTRenderPass;

typedef struct MTRenderEncoder {
  id<MTLRenderCommandEncoder> classic;
  id                          modern;
  MTArgumentState            *vertexArguments;
  MTArgumentState            *fragmentArguments;
} MTRenderEncoder;

typedef struct MTRenderPipelineState {
  id<MTLRenderPipelineState> render;
  id<MTLDepthStencilState>   depthStencil;
} MTRenderPipelineState;

typedef struct MTComputeEncoder {
  id<MTLComputeCommandEncoder> classic;
  id                           modern;
  MTArgumentState             *arguments;
} MTComputeEncoder;

typedef struct MTCopyEncoder {
  id<MTLBlitCommandEncoder> classic;
  id                       modern;
} MTCopyEncoder;

typedef struct MTCommandBuffer MTCommandBuffer;

typedef struct MTCommandQueue {
  id<MTLCommandQueue>  classic;
  id<MTLCommandQueue>  upload;
  id                    modern;
  dispatch_group_t      inFlightGroup;
  MTCommandBuffer      *commands;
  MTCommandBuffer      *freeCommands;
  os_unfair_lock        poolLock;
  MTCommandMode         mode;
} MTCommandQueue;

struct MTCommandBuffer {
  id<MTLCommandBuffer>   classic;
  id                     modern;
  id                     allocator;
  id                     residency;
  id<CAMetalDrawable>    drawable;
  MTCommandQueue        *owner;
  MTCommandBuffer       *next;
  MTCommandBuffer       *poolNext;
  MTUploadChunk         *uploads;
  MTArgumentState        vertexArguments;
  MTArgumentState        fragmentArguments;
  MTArgumentState        computeArguments;
  GPURenderPassDesc       renderPass;
  MTRenderPass            renderPassState;
  GPURenderCommandEncoder renderEncoder;
  MTRenderEncoder         renderState;
  GPUComputePassEncoder   computeEncoder;
  MTComputeEncoder        computeState;
  GPUCopyPassEncoder      copyEncoder;
  MTCopyEncoder           copyState;
  GPUCommandBuffer        commandBuffer;
  uint64_t                pendingAfterStages;
  uint64_t                pendingBeforeStages;
  uint64_t                pendingVisibility;
  MTCommandMode           mode;
};

typedef struct MTQuerySet {
  id<MTLCounterSampleBuffer> classic;
  id<MTLBuffer>              visibility;
  id                          modern;
  MTCommandMode              mode;
} MTQuerySet;

GPU_HIDE MTLPixelFormat mt_format(GPUFormat format);
GPU_HIDE GPUFormat mt_formatFromNative(MTLPixelFormat format);
GPU_HIDE id<MTLTexture> mt_nativeTexture(GPUTexture *texture);
GPU_HIDE id<MTLTexture> mt_copyTexture(GPUTexture *texture,
                                       GPUTextureAspect aspect);
GPU_HIDE MTLBlitOption mt_copyOption(GPUFormat format,
                                     GPUTextureAspect aspect);

static inline MTCommandQueue *
mt_commandQueue(GPUCommandQueue *queue) {
  return queue ? queue->_priv : NULL;
}

static inline MTCommandBuffer *
mt_commandBuffer(GPUCommandBuffer *cmdb) {
  return cmdb ? cmdb->_priv : NULL;
}

static inline id<MTLCommandBuffer>
mt_classicCommandBuffer(GPUCommandBuffer *cmdb) {
  MTCommandBuffer *native;

  native = mt_commandBuffer(cmdb);
  return native ? native->classic : nil;
}

static inline id
mt_modernCommandBuffer(GPUCommandBuffer *cmdb) {
  MTCommandBuffer *native;

  native = mt_commandBuffer(cmdb);
  return native ? native->modern : nil;
}

static inline bool
mt_commandBufferIsModern(GPUCommandBuffer *cmdb) {
  MTCommandBuffer *native;

  native = mt_commandBuffer(cmdb);
  return native && native->mode == MTCommandMode4;
}

GPU_HIDE
bool
mt_prepareArgumentState(GPUCommandBuffer *cmdb,
                        MTArgumentState  *state,
                        const char       *label);

GPU_HIDE
void
mt_setArgumentBuffer(GPUCommandBuffer *cmdb,
                     MTArgumentState  *state,
                     id<MTLBuffer>      buffer,
                     uint64_t           offset,
                     uint32_t           index);

GPU_HIDE
void
mt_setArgumentTexture(GPUCommandBuffer *cmdb,
                      MTArgumentState  *state,
                      id<MTLTexture>     texture,
                      uint32_t           index);

GPU_HIDE
void
mt_setArgumentSampler(MTArgumentState   *state,
                      id<MTLSamplerState> sampler,
                      uint32_t            index);

GPU_HIDE
void
mt_useAllocation(GPUCommandBuffer *cmdb, id allocation);

GPU_HIDE
bool
mt_reserveUpload(GPUCommandBuffer *cmdb,
                 uint64_t          sizeBytes,
                 uint64_t          alignment,
                 id<MTLBuffer>     *outBuffer,
                 uint64_t         *outOffset);

GPU_HIDE
bool
mt_uploadConstants(GPUCommandBuffer *cmdb,
                   const void       *data,
                   uint32_t          sizeBytes,
                   uint64_t         *outAddress);

GPU_HIDE
void
mt_applyPendingBarrier(GPUCommandBuffer *cmdb, id encoder);

GPU_HIDE
void
mt_destroyCommandBufferState(MTCommandBuffer *native);

GPU_HIDE
void
mt_recycleCommandBuffer(GPUCommandBuffer *cmdb);

#endif
#endif /* metal_common_h */
