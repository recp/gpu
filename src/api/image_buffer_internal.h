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
  uint64_t base;       /* its first header row */
  uint64_t layerBytes; /* a header row and 'height' rows */
  uint32_t pitchBytes;
  uint32_t width;
  uint32_t height;
  uint32_t count;      /* layers, or a 3d level's slices */
} GPUImageBufferMip;

typedef struct GPUImageBuffer {
  GPUBuffer             *buffer;
  GPUTexture            *texture;
  struct GPUImageBuffer *next;         /* the device's list */
  uint32_t               bufferNewer;  /* mip levels read-write bindings wrote last */
  uint32_t               textureNewer; /* mip levels something else wrote last (or never copied) */
  uint32_t               renderMips;   /* mip levels bound read-write to vertex or fragment shaders */
  uint32_t               mipCount;
  bool                   headers;      /* the header rows are written */
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

/* the texture of every buffer written last, before a render pass or a blit. */

GPU_HIDE
void
imageBufferFlush(GPUCommandBuffer *cmdb);

/* before a render pass: also the buffer of textures bound read-write to
 * vertex or fragment shaders, where the texture was written last. */

GPU_HIDE
void
imageBufferRenderSync(GPUCommandBuffer *cmdb);

/* a group bound in a render pass: its read-write bindings write the buffers. */

GPU_HIDE
void
imageBufferRenderBind(struct GPUBindGroup *group);

/* inside a transfer pass: a texture read by a copy gets the buffer's texels first. */

GPU_HIDE
void
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture);

/* a mip level of the texture written (UINT32_MAX: every level) */

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture, uint32_t mipLevel);

#endif /* gpu_image_buffer_internal_h */
