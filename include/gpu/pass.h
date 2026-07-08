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

#ifndef gpu_pass_h
#define gpu_pass_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "buffer.h"
#include "swapchain.h"
#include "frame.h"

#ifndef GPU_RENDER_ENCODER_TYPES_DEFINED
#define GPU_RENDER_ENCODER_TYPES_DEFINED
typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;
typedef GPURenderCommandEncoder GPURenderPassEncoder;
#endif

typedef struct GPUCopyPassEncoder GPUCopyPassEncoder;

typedef enum GPULoadOp {
  GPU_LOAD_OP_LOAD      = 0,
  GPU_LOAD_OP_CLEAR     = 1,
  GPU_LOAD_OP_DONT_CARE = 2
} GPULoadOp;

typedef enum GPUStoreOp {
  GPU_STORE_OP_STORE     = 0,
  GPU_STORE_OP_DONT_CARE = 1
} GPUStoreOp;

typedef union GPUColorValue {
  float    float32[4];
  uint32_t uint32[4];
  int32_t  int32[4];
} GPUColorValue;

typedef struct GPURenderPassColorAttachment {
  GPUTextureView *view;
  GPUTextureView *resolveView;
  GPULoadOp       loadOp;
  GPUStoreOp      storeOp;
  GPUColorValue   clearColor;
} GPURenderPassColorAttachment;

typedef struct GPURenderPassDepthStencilAttachment {
  GPUTextureView *view;
  GPULoadOp       depthLoadOp;
  GPUStoreOp      depthStoreOp;
  GPULoadOp       stencilLoadOp;
  GPUStoreOp      stencilStoreOp;
  float           clearDepth;
  uint32_t        clearStencil;
} GPURenderPassDepthStencilAttachment;

typedef struct GPURenderPassCreateInfo {
  GPUChainedStruct                         chain;
  const char                                *label;
  uint32_t                                   colorAttachmentCount;
  const GPURenderPassColorAttachment        *pColorAttachments;
  const GPURenderPassDepthStencilAttachment *pDepthStencilAttachment;
} GPURenderPassCreateInfo;

GPU_EXPORT
GPURenderPassEncoder*
GPUBeginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info);

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassEncoder *pass);

typedef struct GPUBufferCopyRegion {
  uint64_t srcOffset;
  uint64_t dstOffset;
  uint64_t sizeBytes;
} GPUBufferCopyRegion;

typedef struct GPUTextureLocation {
  uint32_t x, y, z;
  uint32_t mipLevel;
  uint32_t baseArrayLayer;
} GPUTextureLocation;

typedef struct GPUTextureSubresourceRegion {
  GPUTextureLocation texture;
  uint32_t width, height, depth;
  uint32_t layerCount;
} GPUTextureSubresourceRegion;

typedef struct GPUBufferTextureCopyRegion {
  uint64_t bufferOffset;
  uint32_t bytesPerRow;
  uint32_t rowsPerImage;
  GPUTextureSubresourceRegion texture;
} GPUBufferTextureCopyRegion;

typedef struct GPUTextureToTextureCopyRegion {
  GPUTextureLocation src;
  GPUTextureLocation dst;
  uint32_t width, height, depth;
  uint32_t layerCount;
} GPUTextureToTextureCopyRegion;

GPU_EXPORT
GPUCopyPassEncoder*
GPUBeginCopyPass(GPUCommandBuffer *cmdb, const char *label);

GPU_EXPORT
void
GPUCopyBufferToBuffer(GPUCopyPassEncoder        *pass,
                      GPUBuffer                 *src,
                      GPUBuffer                 *dst,
                      const GPUBufferCopyRegion *region);

GPU_EXPORT
void
GPUCopyBufferToTexture(GPUCopyPassEncoder               *pass,
                       GPUBuffer                        *src,
                       GPUTexture                       *dst,
                       const GPUBufferTextureCopyRegion *region);

GPU_EXPORT
void
GPUCopyTextureToBuffer(GPUCopyPassEncoder               *pass,
                       GPUTexture                       *src,
                       GPUBuffer                        *dst,
                       const GPUBufferTextureCopyRegion *region);

GPU_EXPORT
void
GPUCopyTextureToTexture(GPUCopyPassEncoder                  *pass,
                        GPUTexture                          *src,
                        GPUTexture                          *dst,
                        const GPUTextureToTextureCopyRegion *region);

GPU_EXPORT
void
GPUEndCopyPass(GPUCopyPassEncoder *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_pass_h */
