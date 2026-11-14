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

#ifndef gpu_texture_h
#define gpu_texture_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "cmdqueue.h"
#include "format.h"

typedef struct GPUTexture     GPUTexture;
typedef struct GPUTextureView GPUTextureView;
typedef struct GPUDevice      GPUDevice;
typedef struct GPUBuffer      GPUBuffer;

typedef uint32_t GPUTextureUsageFlags;
enum {
  GPU_TEXTURE_USAGE_SAMPLED       = 1u << 0,
  GPU_TEXTURE_USAGE_STORAGE       = 1u << 1,
  GPU_TEXTURE_USAGE_COLOR_TARGET  = 1u << 2,
  GPU_TEXTURE_USAGE_DEPTH_STENCIL = 1u << 3,
  GPU_TEXTURE_USAGE_COPY_SRC      = 1u << 4,
  GPU_TEXTURE_USAGE_COPY_DST      = 1u << 5,

  GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT = 1u << 6,

  /* pass-local 2D attachment, one mip; no sampling, storage or copying.
   * load must clear/discard, store must discard; MSAA may resolve to an ordinary
   * attachment. enable GPU_FEATURE_MEMORYLESS_ATTACHMENTS for native tile/lazy
   * storage; otherwise ordinary backing preserves the same pass-local contract.
   * incompatible native format/memory combinations return unsupported.
   * texture/view objects still outlive all GPU use; heaps, sparse resources
   * and cross-device sharing are unsupported for transient attachments.
   */
  GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT = 1u << 7
};

typedef enum GPUTextureDimension {
  GPU_TEXTURE_DIMENSION_1D = 0,
  GPU_TEXTURE_DIMENSION_2D = 1,
  GPU_TEXTURE_DIMENSION_3D = 2
} GPUTextureDimension;

typedef enum GPUTextureAspect {
  GPU_TEXTURE_ASPECT_ALL          = 0,
  GPU_TEXTURE_ASPECT_DEPTH_ONLY   = 1,
  GPU_TEXTURE_ASPECT_STENCIL_ONLY = 2
} GPUTextureAspect;

typedef enum GPUTextureViewType {
  GPU_TEXTURE_VIEW_1D         = 0,
  GPU_TEXTURE_VIEW_1D_ARRAY   = 1,
  GPU_TEXTURE_VIEW_2D         = 2,
  GPU_TEXTURE_VIEW_2D_ARRAY   = 3,
  GPU_TEXTURE_VIEW_CUBE       = 4,
  GPU_TEXTURE_VIEW_CUBE_ARRAY = 5,
  GPU_TEXTURE_VIEW_3D         = 6
} GPUTextureViewType;

typedef struct GPUTextureCreateInfo {
  GPUChainedStruct     chain;
  const char          *label;
  GPUTextureDimension  dimension;
  GPUFormat            format;
  uint32_t             width;
  uint32_t             height;
  uint32_t             depthOrLayers;
  uint32_t             mipLevelCount;
  uint32_t             sampleCount;
  GPUTextureUsageFlags usage;
} GPUTextureCreateInfo;

/* shared storage; the buffer and its host allocation outlive all texture uses.
 * CPU overwrites require completion of previous GPU reads. No implicit upload.
 */
typedef struct GPUBufferTextureCreateInfo {
  GPUChainedStruct     chain;
  const char          *label;
  GPUBuffer           *buffer;
  uint64_t             offset;
  uint32_t             bytesPerRow;
  GPUFormat            format;
  uint32_t             width;
  uint32_t             height;
  GPUTextureUsageFlags usage;
} GPUBufferTextureCreateInfo;

typedef struct GPUTextureInfo {
  GPUTextureDimension  dimension;
  GPUFormat            format;
  uint32_t             width;
  uint32_t             height;
  uint32_t             depthOrLayers;
  uint32_t             mipLevelCount;
  uint32_t             sampleCount;
  GPUTextureUsageFlags usage;
} GPUTextureInfo;

typedef struct GPUTextureViewCreateInfo {
  GPUChainedStruct   chain;
  const char        *label;
  GPUTextureViewType viewType;
  GPUFormat          format;
  uint32_t           baseMipLevel;
  uint32_t           mipLevelCount;
  uint32_t           baseArrayLayer;
  uint32_t           arrayLayerCount;
} GPUTextureViewCreateInfo;

/* absolute texture mip threshold; requires GPU_FEATURE_TEXTURE_VIEW_MIN_LOD for nonzero values. */
typedef struct GPUTextureViewMinLODEXT {
  GPUChainedStruct chain;
  float            minLOD;
} GPUTextureViewMinLODEXT;

typedef struct GPUTextureWriteRegion {
  GPUTextureAspect aspect;
  uint32_t         width;
  uint32_t         height;
  uint32_t         depth;
  uint32_t         mipLevel;
  uint32_t         baseArrayLayer;
  uint32_t         layerCount;
  uint32_t         bytesPerRow;
  uint32_t         rowsPerImage;
  uint32_t         x, y, z;
} GPUTextureWriteRegion;

GPU_EXPORT
GPUResult
GPUCreateTexture(GPUDevice                  *__restrict device,
                 const GPUTextureCreateInfo *__restrict info,
                 GPUTexture                **__restrict outTexture);

/* requires enabled GPU_FEATURE_BUFFER_TEXTURES; zero alignment means unsupported. */
GPU_EXPORT
uint32_t
GPUGetLinearTextureAlignment(GPUDevice *device, GPUFormat format);

GPU_EXPORT
GPUResult
GPUCreateTextureFromBuffer(GPUDevice                        *__restrict device,
                           const GPUBufferTextureCreateInfo *__restrict info,
                           GPUTexture                      **__restrict outTexture);

/* each holder of a texture releases it with GPUDestroyTexture: it is destroyed
 * with the last. a texture starts with one holder; GPURetainTexture adds one.
 * safe on any thread */
GPU_EXPORT
void
GPURetainTexture(GPUTexture *__restrict texture);

GPU_EXPORT
void
GPUDestroyTexture(GPUTexture *__restrict texture);

GPU_EXPORT
GPUResult
GPUGetTextureInfo(GPUTexture     *__restrict texture,
                  GPUTextureInfo *__restrict outInfo);

GPU_EXPORT
GPUResult
GPUCreateTextureView(GPUTexture                     *__restrict texture,
                     const GPUTextureViewCreateInfo *__restrict info,
                     GPUTextureView                **__restrict outView);

GPU_EXPORT
void
GPUDestroyTextureView(GPUTextureView *__restrict view);

GPU_EXPORT
GPUResult
GPUQueueWriteTexture(GPUQueue                    *__restrict queue,
                     GPUTexture                  *__restrict texture,
                     const GPUTextureWriteRegion *__restrict region,
                     const void                  *__restrict data,
                     uint64_t                                sizeBytes);

#ifdef __cplusplus
}
#endif
#endif /* gpu_texture_h */
