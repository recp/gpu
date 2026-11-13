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

/*
 * read-write storage textures a device can't hold natively
 *
 * a format with storage (write) access but no read-write access
 * (GPUFormatCapabilities.storageReadWrite) has its read-write bindings
 * emulated: USL lowers those images to storage buffers (its image_buffer
 * atom), and every storage texture of such a format keeps a buffer beside
 * it, laid out as USL reads it: per mip level and layer (or 3d slice) a
 * header row, then the rows; a view binds at its level's and layer's header.
 *
 * a read-write binding binds the buffer instead of the texture (made at the
 * texture's first read-write binding, so a texture never bound so costs
 * nothing). every other use (sampled, read-only or write-only storage,
 * copies, attachments) sees the texture. a mip level is copied from one to
 * the other only when it was written there and is used here next, in
 * command order: before a compute dispatch (its pass is split around the
 * copy), before a render pass (also into the buffer for read-write render
 * bindings), around transfer copies and blits. devices with read-write
 * access to every format, or without a texture bound read-write that has
 * none, pay a zero test.
 */

#ifndef gpu_image_buffer_internal_h
#define gpu_image_buffer_internal_h

#include "../common.h"

struct GPUBindGroupBindingView;
struct GPUComputePassEncoder;
struct GPUBindGroup;

/* a mip level's part of the buffer */

typedef struct GPUImageBufferMip {
  uint64_t  base;         /* its first header row */
  uint64_t  layerBytes;   /* a header row and 'height' rows */
  uint64_t *bufferNewer;  /* a bit a layer (one for a 3d level): read-write bindings wrote it last */
  uint64_t *textureNewer; /* a bit a layer: something else wrote it last, or it was never copied */
  uint32_t *layerCounts;  /* header word 3 of each layer now: the layers a binding there sees */
  uint32_t *renderCounts; /* the word a render pass's read-write array view from a layer needs (0: none) */
  uint32_t  pitchBytes;
  uint32_t  width;
  uint32_t  height;
  uint32_t  count;        /* layers, or a 3d level's slices */
  uint32_t  units;        /* tracked apart: the layers, or 1 for a 3d level */
} GPUImageBufferMip;

typedef struct GPUImageBuffer {
  GPUBuffer             *buffer;
  GPUTexture            *texture;
  struct GPUImageBuffer *next;         /* the device's list */
  void                  *tracking;     /* the levels' bits and layer counts */
  uint32_t               bufferNewer;  /* levels with a layer read-write bindings wrote last */
  uint32_t               textureNewer; /* levels with a layer something else wrote last */
  uint32_t               renderMips;   /* levels bound read-write to vertex or fragment shaders */
  uint32_t               mipCount;
  bool                   headers;      /* the header rows are written */
  bool                   renderStale;  /* a layer count may differ from its renderCounts */
  GPUImageBufferMip      mips[];
} GPUImageBuffer;

/* the read-write formats this device emulates, as USL texel format bits (0: none). */

GPU_HIDE
void
initImageBufferDevice(GPUDevice *device);

GPU_HIDE
void
destroyImageBufferDevice(GPUDevice *device);

/* the USL texel format of a format USL can lower, or 0. */

GPU_HIDE
uint32_t
imageBufferTexelFormat(GPUFormat format);

GPU_HIDE
bool
deviceEmulatesReadWrite(const GPUDevice *device, GPUFormat format);

/* a texture's usage with what its buffer copies need, before it is created;
 * the buffer itself comes with the texture's first read-write binding. */

GPU_HIDE
GPUTextureUsageFlags
imageBufferTextureUsage(const GPUDevice *device, const GPUTextureCreateInfo *info);

GPU_HIDE
void
destroyImageBuffer(GPUTexture *texture);

/* a read-write binding of an emulated texture becomes its buffer (false: it can't be bound). */

GPU_HIDE
bool
imageBufferBindingView(struct GPUBindGroupBindingView *view);

/* a group made or updated (with emulated formats on the device) */

GPU_HIDE
void
imageBufferGroupCreated(struct GPUBindGroup *group);

/* whether a group binds emulated textures, kept with the device's generation of buffers */

GPU_HIDE
bool
bindGroupImageBuffersFind(struct GPUBindGroup *group);

/* the copies a dispatch with the pass's groups needs first; true when any. */

GPU_HIDE
bool
imageBufferComputeNeedsSync(struct GPUComputePassEncoder *pass);

/* with no encoder open: the copies, then the states the dispatch leaves. */

GPU_HIDE
void
imageBufferComputeSync(struct GPUComputePassEncoder *pass, GPUCommandBuffer *cmdb);

GPU_HIDE
void
imageBufferComputeWrote(struct GPUComputePassEncoder *pass);

/* outside a pass: a texture's level as read-write bindings left it. */

GPU_HIDE
void
imageBufferFlushTexture(GPUCommandBuffer *cmdb, GPUTexture *texture, uint32_t mipLevel);

/* before a render pass: the textures it may read (its attachments, those
 * vertex or fragment shaders read) as read-write bindings left them, and the
 * buffers of textures it binds read-write as everything else left them. */

GPU_HIDE
void
imageBufferRenderSync(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info);

/* a group bound in a render pass: its read-write bindings write the buffers. */

GPU_HIDE
void
imageBufferRenderBind(struct GPUBindGroup *group);

/* inside a transfer pass: a texture's level read by a copy gets the buffer's texels first. */

GPU_HIDE
void
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture, uint32_t mipLevel, uint32_t layer,
                        uint32_t layerCount);

/* inside a transfer pass: a copy out of a texture whose buffer holds the
 * newer texels of the level reads the buffer instead, touching neither;
 * false when it can't (the caller syncs and copies the texture) */

GPU_HIDE
bool
imageBufferCopyToTexture(GPUTransferPassEncoder              *pass,
                         GPUTexture                          *src,
                         GPUTexture                          *dst,
                         const GPUTextureToTextureCopyRegion *region);

GPU_HIDE
bool
imageBufferCopyToBuffer(GPUTransferPassEncoder           *pass,
                        GPUTexture                       *src,
                        GPUBuffer                        *dst,
                        const GPUBufferTextureCopyRegion *region);

/* a barrier batch with each emulated texture's buffer added as the texture
 * (its reads and writes), in 'storage' when it fits; NULL when none */

GPU_HIDE
const GPUBarrierBatch *
imageBufferBarriers(const GPUBarrierBatch *batch, GPUBarrierBatch *out, GPUBufferBarrier *storage, uint32_t capacity,
                    GPUBufferBarrier **allocated);

/* texels written to the texture on the queue, written to its buffer too in
 * its layout (neither is then newer for them); false when they can't be */

GPU_HIDE
bool
imageBufferTextureUploaded(GPUQueue *queue, GPUTexture *texture, const GPUTextureWriteRegion *region, const void *data);

/* layers of a mip level of the texture written (mipLevel UINT32_MAX: all of it) */

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture, uint32_t mipLevel, uint32_t layer, uint32_t layerCount);

#endif /* gpu_image_buffer_internal_h */
