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

#include "../common.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "device_internal.h"
#include "image_buffer_internal.h"
#include "texture_internal.h"
#include "descr/descriptor_internal.h"

#include <us/compiler.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* the formats USL lowers: whole 32-bit words per texel (image_buffer.h there). */

typedef struct GPUImageBufferFormat {
  GPUFormat format;
  uint8_t   texel; /* USLRuntimeTexelFormat */
  uint8_t   bytes;
} GPUImageBufferFormat;

static const GPUImageBufferFormat gpu_imageBufferFormats[] = {
  {GPU_FORMAT_R32_UINT,       USL_RUNTIME_TEXEL_FORMAT_R32_UINT,      4u},
  {GPU_FORMAT_R32_SINT,       USL_RUNTIME_TEXEL_FORMAT_R32_SINT,      4u},
  {GPU_FORMAT_R32_FLOAT,      USL_RUNTIME_TEXEL_FORMAT_R32_FLOAT,     4u},
  {GPU_FORMAT_RG16_UNORM,     USL_RUNTIME_TEXEL_FORMAT_RG16_UNORM,    4u},
  {GPU_FORMAT_RG16_SNORM,     USL_RUNTIME_TEXEL_FORMAT_RG16_SNORM,    4u},
  {GPU_FORMAT_RG16_UINT,      USL_RUNTIME_TEXEL_FORMAT_RG16_UINT,     4u},
  {GPU_FORMAT_RG16_SINT,      USL_RUNTIME_TEXEL_FORMAT_RG16_SINT,     4u},
  {GPU_FORMAT_RG32_UINT,      USL_RUNTIME_TEXEL_FORMAT_RG32_UINT,     8u},
  {GPU_FORMAT_RG32_SINT,      USL_RUNTIME_TEXEL_FORMAT_RG32_SINT,     8u},
  {GPU_FORMAT_RG32_FLOAT,     USL_RUNTIME_TEXEL_FORMAT_RG32_FLOAT,    8u},
  {GPU_FORMAT_RGBA8_UNORM,    USL_RUNTIME_TEXEL_FORMAT_RGBA8_UNORM,   4u},
  {GPU_FORMAT_RGBA8_SNORM,    USL_RUNTIME_TEXEL_FORMAT_RGBA8_SNORM,   4u},
  {GPU_FORMAT_RGBA8_UINT,     USL_RUNTIME_TEXEL_FORMAT_RGBA8_UINT,    4u},
  {GPU_FORMAT_RGBA8_SINT,     USL_RUNTIME_TEXEL_FORMAT_RGBA8_SINT,    4u},
  {GPU_FORMAT_BGRA8_UNORM,    USL_RUNTIME_TEXEL_FORMAT_BGRA8_UNORM,   4u},
  {GPU_FORMAT_RGB10A2_UNORM,  USL_RUNTIME_TEXEL_FORMAT_RGB10A2_UNORM, 4u},
  {GPU_FORMAT_RGB10A2_UINT,   USL_RUNTIME_TEXEL_FORMAT_RGB10A2_UINT,  4u},
  {GPU_FORMAT_RGBA16_UNORM,   USL_RUNTIME_TEXEL_FORMAT_RGBA16_UNORM,  8u},
  {GPU_FORMAT_RGBA16_SNORM,   USL_RUNTIME_TEXEL_FORMAT_RGBA16_SNORM,  8u},
  {GPU_FORMAT_RGBA16_UINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA16_UINT,   8u},
  {GPU_FORMAT_RGBA16_SINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA16_SINT,   8u},
  {GPU_FORMAT_RGBA32_UINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA32_UINT,   16u},
  {GPU_FORMAT_RGBA32_SINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA32_SINT,   16u},
  {GPU_FORMAT_RGBA32_FLOAT,   USL_RUNTIME_TEXEL_FORMAT_RGBA32_FLOAT,  16u}
};

/* rows start on 256 bytes: buffer-texture copies and storage offsets everywhere take that. */

enum {
  GPU_IMAGE_BUFFER_ROW_ALIGN = 256u
};

static const GPUImageBufferFormat*
ib_format(GPUFormat format) {
  uint32_t i;

  for (i = 0u; i < GPU_ARRAY_LEN(gpu_imageBufferFormats); i++) {
    if (gpu_imageBufferFormats[i].format == format)
      return &gpu_imageBufferFormats[i];
  }

  return NULL;
}

static void
ib_lock(const GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(device->_imageBufferLock);
#else
  pthread_mutex_lock(device->_imageBufferLock);
#endif
}

static void
ib_unlock(const GPUDevice *device) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(device->_imageBufferLock);
#else
  pthread_mutex_unlock(device->_imageBufferLock);
#endif
}

GPU_HIDE
uint32_t
imageBufferTexelFormat(GPUFormat format) {
  const GPUImageBufferFormat *f;

  return (f = ib_format(format)) ? f->texel : 0u;
}

GPU_HIDE
bool
deviceEmulatesReadWrite(const GPUDevice *device, GPUFormat format) {
  uint32_t texel;

  return device && device->imageBufferFormats && (texel = imageBufferTexelFormat(format))
         && (device->imageBufferFormats & (UINT64_C(1) << texel));
}

GPU_HIDE
void
initImageBufferDevice(GPUDevice *device) {
  GPUFormatCapabilities caps;
  const char           *force;
  uint64_t              mask;
  void                 *lock;
  uint32_t              i;

  device->imageBufferFormats     = 0u;
  device->_imageBuffers          = NULL;
  device->_imageBufferLock       = NULL;
  device->_imageBufferGeneration = 0u;

  /* GPU_IMAGE_BUFFERS=1 emulates every format it can, as a device without read-write textures would; =0 none */

  force = getenv("GPU_IMAGE_BUFFERS");
  mask  = 0u;

  if (force && force[0] == '0')
    return;

  for (i = 0u; i < GPU_ARRAY_LEN(gpu_imageBufferFormats); i++) {
    if (GPUGetFormatCapabilities(device->adapter, gpu_imageBufferFormats[i].format, &caps) == GPU_OK && caps.storage
        && (!caps.storageReadWrite || (force && force[0] == '1'))) {
      mask |= UINT64_C(1) << gpu_imageBufferFormats[i].texel;
    }
  }

  if (!mask)
    return;
#if defined(_WIN32) || defined(WIN32)
  if (!(lock = calloc(1, sizeof(CRITICAL_SECTION))))
    return;

  InitializeCriticalSection(lock);
#else
  if (!(lock = calloc(1, sizeof(pthread_mutex_t))))
    return;

  if (pthread_mutex_init(lock, NULL) != 0) {
    free(lock);
    return;
  }
#endif
  device->_imageBufferLock   = lock;
  device->imageBufferFormats = mask;
}

GPU_HIDE
void
destroyImageBufferDevice(GPUDevice *device) {
  if (!device || !device->_imageBufferLock)
    return;
#if defined(_WIN32) || defined(WIN32)
  DeleteCriticalSection(device->_imageBufferLock);
#else
  pthread_mutex_destroy(device->_imageBufferLock);
#endif
  free(device->_imageBufferLock);
  device->_imageBufferLock   = NULL;
  device->imageBufferFormats = 0u;
}

static bool
ib_eligible(const GPUDevice *device, GPUFormat format, GPUTextureDimension dimension,
            GPUTextureUsageFlags usage, uint32_t mips, uint32_t samples) {
  return (usage & GPU_TEXTURE_USAGE_STORAGE) && dimension == GPU_TEXTURE_DIMENSION_2D && mips <= 1u
         && samples <= 1u && deviceEmulatesReadWrite(device, format);
}

GPU_HIDE
GPUTextureUsageFlags
imageBufferTextureUsage(const GPUDevice *device, const GPUTextureCreateInfo *info) {
  if (!info || !ib_eligible(device, info->format, info->dimension, info->usage, info->mipLevelCount, info->sampleCount))
    return info ? info->usage : 0u;

  return info->usage | GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
}

/* a header row's first texels: width, height, pitch in texels, layers from here on */

static void
ib_header(const GPUImageBuffer *ib, uint32_t layer, uint32_t out[4]) {
  const GPUTexture *t = ib->texture;

  out[0] = t->width;
  out[1] = t->height;
  out[2] = ib->pitchBytes / ib_format(t->format)->bytes;
  out[3] = t->depthOrLayers - layer;
}

static void
ib_writeHeaders(GPUImageBuffer *ib, GPUQueue *queue) {
  uint32_t header[4];
  uint32_t layer;
  uint8_t *host;

  if (ib->headers)
    return;

  host = GPUGetBufferHostAddress(ib->buffer);

  for (layer = 0u; layer < ib->texture->depthOrLayers; layer++) {
    ib_header(ib, layer, header);

    if (host)
      memcpy(host + (uint64_t)layer * ib->layerBytes, header, sizeof(header));
    else if (!queue || GPUQueueWriteBuffer(queue, ib->buffer, (uint64_t)layer * ib->layerBytes, header,
                                           sizeof(header)) != GPU_OK)
      return;
  }

  ib->headers = true;
}

/* a texture's buffer, made with its first read-write binding: under the
 * device's lock, the texels still in the texture (copied over before use) */

static GPUImageBuffer*
ib_create(GPUTexture *texture) {
  const GPUImageBufferFormat *f;
  GPUBufferCreateInfo         info;
  GPUImageBuffer             *ib;
  GPUDevice                  *device;
  uint64_t                    pitch;
  uint64_t                    layerBytes;

  device = texture->device;

  if (!ib_eligible(device, texture->format, texture->dimension, texture->usage, texture->mipLevelCount,
                   texture->sampleCount)
      || !(texture->usage & GPU_TEXTURE_USAGE_COPY_SRC) || !(texture->usage & GPU_TEXTURE_USAGE_COPY_DST))
    return NULL;

  ib_lock(device);

  if ((ib = texture->_imageBuffer)) {
    ib_unlock(device);
    return ib;
  }

  f     = ib_format(texture->format);
  pitch = (uint64_t)texture->width * f->bytes;
  pitch = pitch < USL_IMAGE_BUFFER_HEADER_BYTES ? USL_IMAGE_BUFFER_HEADER_BYTES : pitch;
  pitch = (pitch + GPU_IMAGE_BUFFER_ROW_ALIGN - 1u) & ~(uint64_t)(GPU_IMAGE_BUFFER_ROW_ALIGN - 1u);

  layerBytes = pitch * ((uint64_t)texture->height + 1u);

  if (pitch > UINT32_MAX || layerBytes > UINT64_MAX / texture->depthOrLayers || !(ib = calloc(1, sizeof(*ib)))) {
    ib_unlock(device);
    return NULL;
  }

  memset(&info, 0, sizeof(info));
  info.label     = "gpu image buffer";
  info.sizeBytes = layerBytes * texture->depthOrLayers;
  info.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &info, &ib->buffer) != GPU_OK) {
    ib_unlock(device);
    free(ib);
    return NULL;
  }

  ib->texture    = texture;
  ib->pitchBytes = (uint32_t)pitch;
  ib->layerBytes = layerBytes;
  ib->newer      = GPU_IMAGE_BUFFER_TEXTURE;

  ib_writeHeaders(ib, NULL);

  ib->next              = device->_imageBuffers;
  device->_imageBuffers = ib;
  texture->_imageBuffer = ib;
  device->_imageBufferGeneration++;

  ib_unlock(device);

  return ib;
}

GPU_HIDE
void
destroyImageBuffer(GPUTexture *texture) {
  GPUImageBuffer **link;
  GPUImageBuffer  *ib;
  GPUDevice       *device;

  if (!texture || !(ib = texture->_imageBuffer))
    return;

  device = texture->device;

  ib_lock(device);

  for (link = &device->_imageBuffers; *link; link = &(*link)->next) {
    if (*link == ib) {
      *link = ib->next;
      break;
    }
  }

  ib_unlock(device);

  texture->_imageBuffer = NULL;
  GPUDestroyBuffer(ib->buffer);
  free(ib);
}

/* ---------------------------------------------------------------------------
 * bindings
 * ------------------------------------------------------------------------- */

GPU_HIDE
bool
imageBufferBindingView(GPUBindGroupBindingView *view) {
  GPUTextureView *textureView;
  GPUImageBuffer *ib;
  uint64_t        offset;

  if (!view || view->kind != GPUBindKindTexture || view->bindingType != GPU_BINDING_STORAGE_TEXTURE
      || view->storageTextureAccess != GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE || !(textureView = view->textureView)
      || !textureView->_texture)
    return true;

  if (!(ib = textureView->_texture->_imageBuffer)) {
    if (!deviceEmulatesReadWrite(textureView->_texture->device, textureView->format))
      return true;

    if (!(ib = ib_create(textureView->_texture)))
      return false;
  }

  if (textureView->baseMipLevel != 0u || textureView->baseArrayLayer >= textureView->_texture->depthOrLayers)
    return false;

  offset = (uint64_t)textureView->baseArrayLayer * ib->layerBytes;

  view->buffer                    = ib->buffer;
  view->offset                    = offset;
  view->size                      = ib->buffer->sizeBytes - offset;
  view->kind                      = GPUBindKindBuffer;
  view->bindingType               = GPU_BINDING_STORAGE_BUFFER;
  view->bufferLayout.strideBytes  = ib_format(textureView->_texture->format)->bytes;
  view->bufferLayout.byteAddress  = false;
  view->bufferLayout.minBindingSize = 0u;

  return true;
}

typedef bool (*GPUImageBufferVisitFn)(void *ctx, GPUImageBuffer *ib, bool readWrite, bool writes);

/* every emulated texture a group binds, read-write (its buffer) or otherwise */

static bool
ib_visitGroup(GPUBindGroup *group, GPUImageBufferVisitFn fn, void *ctx) {
  const GPUBindGroupLayoutEntry *entry;
  GPUBindGroupLayoutPriv        *layout;
  GPUBindGroupBindingPriv       *binding;
  GPUBindGroupPriv              *priv;
  GPUTextureView                *view;
  uint32_t                       i;
  bool                           storage;

  if (!group || !(priv = group->_priv) || !priv->layout || !(layout = priv->layout->_priv))
    return true;

  for (i = 0u; i < priv->count; i++) {
    binding = &priv->bindings[i];

    if (binding->kind != GPUBindKindTexture || !(view = binding->textureView) || !view->_texture
        || !view->_texture->_imageBuffer || binding->layoutEntryIndex >= layout->count)
      continue;

    entry   = &layout->entries[binding->layoutEntryIndex];
    storage = entry->bindingType == GPU_BINDING_STORAGE_TEXTURE;

    if (!fn(ctx,
            view->_texture->_imageBuffer,
            storage && entry->storageTexture.access == GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE,
            storage && entry->storageTexture.access != GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY))
      return false;
  }

  return true;
}

static bool
ib_found(void *ctx, GPUImageBuffer *ib, bool readWrite, bool writes) {
  GPU__UNUSED(ctx);
  GPU__UNUSED(ib);
  GPU__UNUSED(readWrite);
  GPU__UNUSED(writes);

  return false;
}

GPU_HIDE
bool
bindGroupImageBuffersFind(GPUBindGroup *group) {
  GPUBindGroupPriv *priv;

  if (!group || !group->_device || !(priv = group->_priv))
    return false;

  priv->imageBuffers          = !ib_visitGroup(group, ib_found, NULL);
  priv->imageBufferGeneration = group->_device->_imageBufferGeneration;

  return priv->imageBuffers;
}

/* ---------------------------------------------------------------------------
 * keeping the buffer and the texture coherent
 * ------------------------------------------------------------------------- */

/* the accesses a texture's usage allows: a barrier naming others is dropped whole */

static GPUAccessMask
ib_textureAccess(const GPUTexture *texture, uint32_t access) {
  GPUTextureUsageFlags usage = texture->usage;

  if (!(usage & (GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_STORAGE)))
    access &= ~(uint32_t)GPU_ACCESS_SHADER_READ;

  if (!(usage & GPU_TEXTURE_USAGE_STORAGE))
    access &= ~(uint32_t)GPU_ACCESS_SHADER_WRITE;

  if (!(usage & GPU_TEXTURE_USAGE_COLOR_TARGET))
    access &= ~(uint32_t)(GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE);

  if (!(usage & GPU_TEXTURE_USAGE_COPY_SRC))
    access &= ~(uint32_t)GPU_ACCESS_TRANSFER_READ;

  if (!(usage & GPU_TEXTURE_USAGE_COPY_DST))
    access &= ~(uint32_t)GPU_ACCESS_TRANSFER_WRITE;

  return (GPUAccessMask)access;
}

static void
ib_copy(GPUCommandBuffer *cmdb, GPUImageBuffer *ib, bool toTexture) {
  GPUBufferTextureCopyRegion region;
  GPUTransferPassEncoder    *pass;
  GPUTextureBarrier          texture;
  GPUBufferBarrier           buffer;
  GPUBarrierBatch            batch;
  GPUTexture                *t;
  GPUApi                    *api;

  t = ib->texture;

  if (!(api = commandBufferApi(cmdb)))
    return;


  ib_writeHeaders(ib, cmdb->_queue);

  memset(&buffer, 0, sizeof(buffer));
  memset(&texture, 0, sizeof(texture));
  memset(&batch, 0, sizeof(batch));
  buffer.buffer       = ib->buffer;
  buffer.srcAccess    = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_TRANSFER_READ;
  buffer.dstAccess    = toTexture ? GPU_ACCESS_TRANSFER_READ : GPU_ACCESS_TRANSFER_WRITE;
  buffer.sizeBytes    = ib->buffer->sizeBytes;
  texture.texture     = t;
  texture.srcAccess   = ib_textureAccess(t, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_COLOR_WRITE
                                            | GPU_ACCESS_TRANSFER_WRITE);
  texture.dstAccess   = toTexture ? GPU_ACCESS_TRANSFER_WRITE : GPU_ACCESS_TRANSFER_READ;
  texture.mipCount    = 1u;
  texture.layerCount  = t->depthOrLayers;
  batch.pBufferBarriers     = &buffer;
  batch.pTextureBarriers    = &texture;
  batch.bufferBarrierCount  = 1u;
  batch.textureBarrierCount = 1u;
  batch.srcStages           = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  batch.dstStages           = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);

  memset(&region, 0, sizeof(region));
  region.texture.width      = t->width;
  region.texture.height     = t->height;
  region.texture.depth      = 1u;
  region.texture.layerCount = t->depthOrLayers;
  region.bufferOffset       = ib->pitchBytes; /* past the first header row */
  region.bytesPerRow        = ib->pitchBytes;
  region.rowsPerImage       = t->height + 1u; /* and every later one */

  /* the backend's copies: the public ones would sync this texture again */

  if ((pass = GPUBeginTransferPass(cmdb, "gpu image buffer"))) {
    if (toTexture && api->renderPass.copyBufferToTexture)
      api->renderPass.copyBufferToTexture(pass, ib->buffer, t, &region);
    else if (!toTexture && api->renderPass.copyTextureToBuffer)
      api->renderPass.copyTextureToBuffer(pass, t, ib->buffer, &region);

    GPUEndTransferPass(pass);
  }

  buffer.srcAccess  = buffer.dstAccess;
  buffer.dstAccess  = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
  texture.srcAccess = texture.dstAccess;
  texture.dstAccess = ib_textureAccess(t, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_TRANSFER_READ);
  batch.srcStages   = GPU_STAGE_TRANSFER;
  batch.dstStages   = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);

  ib->newer = GPU_IMAGE_BUFFER_SYNCED;
}

typedef struct GPUImageBufferSync {
  GPUCommandBuffer *cmdb;
  bool              needed;
} GPUImageBufferSync;

static bool
ib_needs(void *ctx, GPUImageBuffer *ib, bool readWrite, bool writes) {
  GPUImageBufferSync *sync = ctx;

  GPU__UNUSED(writes);

  if (!ib->headers)
    ib_writeHeaders(ib, sync->cmdb ? sync->cmdb->_queue : NULL);

  if (readWrite ? ib->newer == GPU_IMAGE_BUFFER_TEXTURE : ib->newer == GPU_IMAGE_BUFFER_BUFFER)
    sync->needed = true;

  return true;
}

static bool
ib_sync(void *ctx, GPUImageBuffer *ib, bool readWrite, bool writes) {
  GPUImageBufferSync *sync = ctx;

  GPU__UNUSED(writes);

  if (readWrite ? ib->newer == GPU_IMAGE_BUFFER_TEXTURE : ib->newer == GPU_IMAGE_BUFFER_BUFFER)
    ib_copy(sync->cmdb, ib, !readWrite);

  return true;
}

static bool
ib_wrote(void *ctx, GPUImageBuffer *ib, bool readWrite, bool writes) {
  GPU__UNUSED(ctx);

  if (readWrite)
    ib->newer = GPU_IMAGE_BUFFER_BUFFER;
  else if (writes)
    ib->newer = GPU_IMAGE_BUFFER_TEXTURE;

  return true;
}

static void
ib_visitPass(GPUComputePassEncoder *pass, GPUImageBufferVisitFn fn, void *ctx) {
  uint32_t mask;
  uint32_t group;

  mask = pass->_imageBufferGroups & pass->_requiredBindGroupMask;

  for (group = 0u; mask; group++, mask >>= 1u) {
    if (mask & 1u)
      (void)ib_visitGroup(pass->_boundGroups[group], fn, ctx);
  }
}

GPU_HIDE
bool
imageBufferComputeNeedsSync(GPUComputePassEncoder *pass) {
  GPUImageBufferSync sync = {pass->_cmdb, false};

  ib_visitPass(pass, ib_needs, &sync);

  return sync.needed;
}

GPU_HIDE
void
imageBufferComputeSync(GPUComputePassEncoder *pass, GPUCommandBuffer *cmdb) {
  GPUImageBufferSync sync = {cmdb, false};

  ib_visitPass(pass, ib_sync, &sync);
}

GPU_HIDE
void
imageBufferComputeWrote(GPUComputePassEncoder *pass) {
  ib_visitPass(pass, ib_wrote, NULL);
}

GPU_HIDE
void
imageBufferFlush(GPUCommandBuffer *cmdb) {
  GPUImageBuffer *ib;
  GPUDevice      *device;

  if (!cmdb || !(device = commandBufferDevice(cmdb)) || !device->_imageBuffers)
    return;

  ib_lock(device);

  for (ib = device->_imageBuffers; ib; ib = ib->next) {
    if (ib->newer == GPU_IMAGE_BUFFER_BUFFER)
      ib_copy(cmdb, ib, true);
  }

  ib_unlock(device);
}

GPU_HIDE
void
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture) {
  GPUBufferTextureCopyRegion region;
  GPUImageBuffer            *ib;
  GPUApi                    *api;

  if (!pass || !texture || !(ib = texture->_imageBuffer) || ib->newer != GPU_IMAGE_BUFFER_BUFFER
      || !(api = commandBufferApi(pass->_cmdb)) || !api->renderPass.copyBufferToTexture)
    return;

  /* already inside a transfer pass: the copy goes first in it */

  memset(&region, 0, sizeof(region));
  region.texture.width      = texture->width;
  region.texture.height     = texture->height;
  region.texture.depth      = 1u;
  region.texture.layerCount = texture->depthOrLayers;
  region.bufferOffset       = ib->pitchBytes;
  region.bytesPerRow        = ib->pitchBytes;
  region.rowsPerImage       = texture->height + 1u;

  api->renderPass.copyBufferToTexture(pass, ib->buffer, texture, &region);
  ib->newer = GPU_IMAGE_BUFFER_SYNCED;
}

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture) {
  if (texture && texture->_imageBuffer)
    texture->_imageBuffer->newer = GPU_IMAGE_BUFFER_TEXTURE;
}
