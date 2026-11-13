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
 * it, laid out as USL reads it: per layer a header row, then the rows.
 *
 * a read-write binding binds the buffer instead of the texture (made at the
 * texture's first read-write binding, so a texture never bound so costs
 * nothing). every other
 * use (sampled, read-only or write-only storage, copies, attachments) sees
 * the texture. the two are copied into each other only when one was written
 * and the other is used next, in command order: before a compute dispatch
 * (its pass is split around the copy), before a render pass, around transfer
 * copies and blits. devices with read-write access to every format, or
 * without a texture bound read-write that has none, pay a zero test.
 */

#ifndef gpu_image_buffer_internal_h
#define gpu_image_buffer_internal_h

#include "../common.h"

struct GPUBindGroupBindingView;
struct GPUComputePassEncoder;
struct GPUBindGroup;

typedef enum GPUImageBufferNewer {
  GPU_IMAGE_BUFFER_SYNCED  = 0, /* both hold the same texels (or neither was written) */
  GPU_IMAGE_BUFFER_BUFFER  = 1, /* read-write bindings wrote the buffer */
  GPU_IMAGE_BUFFER_TEXTURE = 2  /* something else wrote the texture */
} GPUImageBufferNewer;

typedef struct GPUImageBuffer {
  GPUBuffer             *buffer;
  GPUTexture            *texture;
  struct GPUImageBuffer *next;       /* the device's list */
  uint64_t               layerBytes; /* a header row and 'height' rows */
  uint32_t               pitchBytes;
  uint8_t                newer;      /* GPUImageBufferNewer */
  bool                   headers;    /* the header rows are written */
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

/* inside a transfer pass: a texture read by a copy gets the buffer's texels first. */

GPU_HIDE
void
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture);

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture);

#endif /* gpu_image_buffer_internal_h */
