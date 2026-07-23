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
typedef struct GPURenderPassEncoder GPURenderPassEncoder;
#endif

typedef struct GPUCopyPassEncoder GPUCopyPassEncoder;
typedef struct GPUQuerySet        GPUQuerySet;

typedef enum GPULoadOp {
  GPU_LOAD_OP_LOAD      = 0,
  GPU_LOAD_OP_CLEAR     = 1,
  GPU_LOAD_OP_DONT_CARE = 2
} GPULoadOp;

typedef enum GPUStoreOp {
  GPU_STORE_OP_STORE     = 0,
  GPU_STORE_OP_DONT_CARE = 1
} GPUStoreOp;

typedef union GPUClearColorValue {
  float    float32[4];
  uint32_t uint32[4];
  int32_t  sint32[4];
} GPUClearColorValue;

typedef struct GPURenderPassColorAttachment {
  GPUTextureView     *view;
  GPUTextureView     *resolveView;
  GPULoadOp           loadOp;
  GPUStoreOp          storeOp;
  GPUClearColorValue  clearColor;
} GPURenderPassColorAttachment;

typedef struct GPURenderPassDepthStencilAttachment {
  GPUTextureView *view;
  GPULoadOp       depthLoadOp;
  GPUStoreOp      depthStoreOp;
  GPULoadOp       stencilLoadOp;
  GPUStoreOp      stencilStoreOp;
  uint32_t        clearStencil;
  float           clearDepth;
} GPURenderPassDepthStencilAttachment;

/* Writes one timestamp at pass begin and one at pass end. */
typedef struct GPUPassTimestampWrites {
  GPUQuerySet *querySet;
  uint32_t     beginIndex;
  uint32_t     endIndex;
} GPUPassTimestampWrites;

typedef struct GPURenderPassCreateInfo {
  GPUChainedStruct                           chain;
  const char                                *label;
  GPUQuerySet                               *occlusionQuerySet;
  const GPUPassTimestampWrites              *timestampWrites;
  const GPURenderPassColorAttachment        *pColorAttachments;
  const GPURenderPassDepthStencilAttachment *pDepthStencilAttachment;
  uint32_t                                   colorAttachmentCount;
} GPURenderPassCreateInfo;

typedef struct GPUBufferCopyRegion {
  uint64_t srcOffset;
  uint64_t dstOffset;
  uint64_t sizeBytes;
} GPUBufferCopyRegion;

typedef struct GPUTextureLocation {
  GPUTextureAspect aspect;
  uint32_t         x, y, z;
  uint32_t         mipLevel;
  uint32_t         baseArrayLayer;
} GPUTextureLocation;

typedef struct GPUTextureSubresourceRegion {
  GPUTextureLocation texture;
  uint32_t           width, height, depth;
  uint32_t           layerCount;
} GPUTextureSubresourceRegion;

typedef struct GPUBufferTextureCopyRegion {
  GPUTextureSubresourceRegion texture;
  uint64_t                    bufferOffset;
  uint32_t                    bytesPerRow;
  uint32_t                    rowsPerImage;
} GPUBufferTextureCopyRegion;

typedef struct GPUTextureToTextureCopyRegion {
  GPUTextureLocation src;
  GPUTextureLocation dst;
  uint32_t           width, height, depth;
  uint32_t           layerCount;
} GPUTextureToTextureCopyRegion;

typedef uint32_t GPUAddressCopyFlagsEXT;
enum {
  GPU_ADDRESS_COPY_DEVICE_LOCAL_BIT_EXT = 1u << 0,
  GPU_ADDRESS_COPY_SPARSE_BIT_EXT       = 1u << 1,
  GPU_ADDRESS_COPY_PROTECTED_BIT_EXT    = 1u << 2
};

typedef uint32_t GPUIndirectTextureAspectFlagsEXT;
enum {
  GPU_INDIRECT_TEXTURE_ASPECT_COLOR_BIT_EXT   = 1u << 0,
  GPU_INDIRECT_TEXTURE_ASPECT_DEPTH_BIT_EXT   = 1u << 1,
  GPU_INDIRECT_TEXTURE_ASPECT_STENCIL_BIT_EXT = 1u << 2
};

typedef struct GPUIndirectMemoryCopyCommandEXT {
  uint64_t srcAddress;
  uint64_t dstAddress;
  uint64_t sizeBytes;
} GPUIndirectMemoryCopyCommandEXT;

typedef struct GPUIndirectTextureSubresourceEXT {
  GPUIndirectTextureAspectFlagsEXT aspectMask;
  uint32_t                         mipLevel;
  uint32_t                         baseArrayLayer;
  uint32_t                         layerCount;
} GPUIndirectTextureSubresourceEXT;

typedef struct GPUIndirectMemoryToTextureCommandEXT {
  uint64_t                         srcAddress;
  uint32_t                         bufferRowLength;
  uint32_t                         bufferImageHeight;
  GPUIndirectTextureSubresourceEXT texture;
  int32_t                          x, y, z;
  uint32_t                         width, height, depth;
} GPUIndirectMemoryToTextureCommandEXT;

typedef struct GPUIndirectCommandRangeEXT {
  GPUBuffer *buffer;
  uint64_t   offset;
  uint64_t   sizeBytes;
  uint64_t   strideBytes;
} GPUIndirectCommandRangeEXT;

typedef struct GPUIndirectMemoryCopyInfoEXT {
  GPUIndirectCommandRangeEXT commands;
  GPUAddressCopyFlagsEXT     srcFlags;
  GPUAddressCopyFlagsEXT     dstFlags;
  uint32_t                   commandCount;
} GPUIndirectMemoryCopyInfoEXT;

typedef struct GPUIndirectMemoryToTextureCopyInfoEXT {
  GPUTexture                                  *dst;
  const GPUIndirectTextureSubresourceEXT      *pTextureSubresources;
  GPUIndirectCommandRangeEXT                   commands;
  GPUAddressCopyFlagsEXT                       srcFlags;
  uint32_t                                     commandCount;
} GPUIndirectMemoryToTextureCopyInfoEXT;

GPU_EXPORT
GPURenderPassEncoder*
GPUBeginRenderPass(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info);

GPU_EXPORT
void
GPUEndRenderPass(GPURenderPassEncoder *pass);

GPU_EXPORT
void
GPUSetRenderPushConstants(GPURenderPassEncoder *pass,
                          uint32_t              offset,
                          uint32_t              sizeBytes,
                          const void           *data);

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
GPUCopyMemoryIndirectEXT(GPUCopyPassEncoder                  *pass,
                         const GPUIndirectMemoryCopyInfoEXT *info);

GPU_EXPORT
void
GPUCopyMemoryToTextureIndirectEXT(GPUCopyPassEncoder                          *pass,
                                  const GPUIndirectMemoryToTextureCopyInfoEXT *info);

GPU_EXPORT
void
GPUEndCopyPass(GPUCopyPassEncoder *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_pass_h */
