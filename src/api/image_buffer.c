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

/* the formats USL lowers (image_buffer.h there); texels under 4 bytes share words. */

typedef struct GPUImageBufferFormat {
  GPUFormat format;
  uint8_t   texel; /* USLRuntimeTexelFormat */
  uint8_t   bytes;
} GPUImageBufferFormat;

static const GPUImageBufferFormat gpu_imageBufferFormats[] = {
  {GPU_FORMAT_R8_UNORM,       USL_RUNTIME_TEXEL_FORMAT_R8_UNORM,      1u},
  {GPU_FORMAT_R8_SNORM,       USL_RUNTIME_TEXEL_FORMAT_R8_SNORM,      1u},
  {GPU_FORMAT_R8_UINT,        USL_RUNTIME_TEXEL_FORMAT_R8_UINT,       1u},
  {GPU_FORMAT_R8_SINT,        USL_RUNTIME_TEXEL_FORMAT_R8_SINT,       1u},
  {GPU_FORMAT_R16_UNORM,      USL_RUNTIME_TEXEL_FORMAT_R16_UNORM,     2u},
  {GPU_FORMAT_R16_SNORM,      USL_RUNTIME_TEXEL_FORMAT_R16_SNORM,     2u},
  {GPU_FORMAT_R16_UINT,       USL_RUNTIME_TEXEL_FORMAT_R16_UINT,      2u},
  {GPU_FORMAT_R16_SINT,       USL_RUNTIME_TEXEL_FORMAT_R16_SINT,      2u},
  {GPU_FORMAT_R16_FLOAT,      USL_RUNTIME_TEXEL_FORMAT_R16_FLOAT,     2u},
  {GPU_FORMAT_RG8_UNORM,      USL_RUNTIME_TEXEL_FORMAT_RG8_UNORM,     2u},
  {GPU_FORMAT_RG8_SNORM,      USL_RUNTIME_TEXEL_FORMAT_RG8_SNORM,     2u},
  {GPU_FORMAT_RG8_UINT,       USL_RUNTIME_TEXEL_FORMAT_RG8_UINT,      2u},
  {GPU_FORMAT_RG8_SINT,       USL_RUNTIME_TEXEL_FORMAT_RG8_SINT,      2u},
  {GPU_FORMAT_R32_UINT,       USL_RUNTIME_TEXEL_FORMAT_R32_UINT,      4u},
  {GPU_FORMAT_R32_SINT,       USL_RUNTIME_TEXEL_FORMAT_R32_SINT,      4u},
  {GPU_FORMAT_R32_FLOAT,      USL_RUNTIME_TEXEL_FORMAT_R32_FLOAT,     4u},
  {GPU_FORMAT_RG16_UNORM,     USL_RUNTIME_TEXEL_FORMAT_RG16_UNORM,    4u},
  {GPU_FORMAT_RG16_SNORM,     USL_RUNTIME_TEXEL_FORMAT_RG16_SNORM,    4u},
  {GPU_FORMAT_RG16_UINT,      USL_RUNTIME_TEXEL_FORMAT_RG16_UINT,     4u},
  {GPU_FORMAT_RG16_SINT,      USL_RUNTIME_TEXEL_FORMAT_RG16_SINT,     4u},
  {GPU_FORMAT_RG16_FLOAT,     USL_RUNTIME_TEXEL_FORMAT_RG16_FLOAT,    4u},
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
  {GPU_FORMAT_RG11B10_UFLOAT, USL_RUNTIME_TEXEL_FORMAT_RG11B10_UFLOAT, 4u},
  {GPU_FORMAT_RGBA16_UNORM,   USL_RUNTIME_TEXEL_FORMAT_RGBA16_UNORM,  8u},
  {GPU_FORMAT_RGBA16_SNORM,   USL_RUNTIME_TEXEL_FORMAT_RGBA16_SNORM,  8u},
  {GPU_FORMAT_RGBA16_UINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA16_UINT,   8u},
  {GPU_FORMAT_RGBA16_SINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA16_SINT,   8u},
  {GPU_FORMAT_RGBA16_FLOAT,   USL_RUNTIME_TEXEL_FORMAT_RGBA16_FLOAT,  8u},
  {GPU_FORMAT_RGBA32_UINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA32_UINT,   16u},
  {GPU_FORMAT_RGBA32_SINT,    USL_RUNTIME_TEXEL_FORMAT_RGBA32_SINT,   16u},
  {GPU_FORMAT_RGBA32_FLOAT,   USL_RUNTIME_TEXEL_FORMAT_RGBA32_FLOAT,  16u}
};

/* rows start on 256 bytes: buffer-texture copies and storage offsets everywhere take that. */

enum {
  GPU_IMAGE_BUFFER_ROW_ALIGN = 256u,
  GPU_IMAGE_BUFFER_MAX_MIPS  = 32u   /* a level is a bit of the masks */
};

static uint32_t
ib_extent(uint32_t extent, uint32_t mip) {
  return extent >> mip ? extent >> mip : 1u;
}

/* the levels [base, base + count) as bits, within the buffer's */

static uint32_t
ib_mipMask(const GPUImageBuffer *ib, uint32_t base, uint32_t count) {
  uint32_t all;

  all = ib->mipCount >= 32u ? UINT32_MAX : (1u << ib->mipCount) - 1u;

  if (base >= ib->mipCount)
    return 0u;

  if (count == 0u || count >= 32u)
    return all & (UINT32_MAX << base);

  return all & (((1u << count) - 1u) << base);
}

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
ib_eligible(const GPUDevice *device, GPUFormat format, GPUTextureUsageFlags usage, uint32_t mips, uint32_t samples) {
  return (usage & GPU_TEXTURE_USAGE_STORAGE) && mips <= GPU_IMAGE_BUFFER_MAX_MIPS && samples <= 1u
         && deviceEmulatesReadWrite(device, format);
}

GPU_HIDE
GPUTextureUsageFlags
imageBufferTextureUsage(const GPUDevice *device, const GPUTextureCreateInfo *info) {
  if (!info || !ib_eligible(device, info->format, info->usage, info->mipLevelCount, info->sampleCount))
    return info ? info->usage : 0u;

  return info->usage | GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
}

/* a header row's first texels: width, height, pitch in texels, layers (or
 * slices) from here on */

static void
ib_writeHeaders(GPUImageBuffer *ib, GPUQueue *queue) {
  const GPUImageBufferMip *m;
  uint64_t                 offset;
  uint32_t                 header[4];
  uint32_t                 mip, layer, bytes;
  uint8_t                 *host;

  if (ib->headers)
    return;

  host  = GPUGetBufferHostAddress(ib->buffer);
  bytes = ib_format(ib->texture->format)->bytes;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    m = &ib->mips[mip];

    for (layer = 0u; layer < m->count; layer++) {
      offset    = m->base + (uint64_t)layer * m->layerBytes;
      header[0] = m->width;
      header[1] = m->height;
      header[2] = m->pitchBytes / bytes;
      header[3] = m->count - layer;

      if (host)
        memcpy(host + offset, header, sizeof(header));
      else if (!queue || GPUQueueWriteBuffer(queue, ib->buffer, offset, header, sizeof(header)) != GPU_OK)
        return;
    }
  }

  ib->headers = true;
}

/* a texture's buffer, made with its first read-write binding: under the
 * device's lock, the texels still in the texture (copied over before use) */

/* each mip level's layers (a 3d level's slices) one after another, a level
 * after the one before; rows and so levels start on GPU_IMAGE_BUFFER_ROW_ALIGN */

static bool
ib_layout(GPUImageBuffer *ib, const GPUTexture *texture, uint64_t *outSize) {
  GPUImageBufferMip *m;
  uint64_t           pitch, total, bytes;
  uint32_t           mip;

  total = 0u;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    m         = &ib->mips[mip];
    m->width  = ib_extent(texture->width, mip);
    m->height = texture->dimension == GPU_TEXTURE_DIMENSION_1D ? 1u : ib_extent(texture->height, mip);
    m->count  = texture->dimension == GPU_TEXTURE_DIMENSION_3D ? ib_extent(texture->depthOrLayers, mip)
                                                               : texture->depthOrLayers;

    pitch = (uint64_t)m->width * ib_format(texture->format)->bytes;
    pitch = pitch < USL_IMAGE_BUFFER_HEADER_BYTES ? USL_IMAGE_BUFFER_HEADER_BYTES : pitch;
    pitch = (pitch + GPU_IMAGE_BUFFER_ROW_ALIGN - 1u) & ~(uint64_t)(GPU_IMAGE_BUFFER_ROW_ALIGN - 1u);

    if (pitch > UINT32_MAX)
      return false;

    m->pitchBytes = (uint32_t)pitch;
    m->layerBytes = pitch * ((uint64_t)m->height + 1u);
    m->base       = total;

    if (m->layerBytes > UINT64_MAX / m->count || (bytes = m->layerBytes * m->count) > UINT64_MAX - total)
      return false;

    total += bytes;
  }

  *outSize = total;

  return true;
}

static GPUImageBuffer*
ib_create(GPUTexture *texture) {
  GPUBufferCreateInfo info;
  GPUImageBuffer     *ib;
  GPUDevice          *device;
  uint64_t            size;
  uint32_t            mips;

  device = texture->device;
  mips   = texture->mipLevelCount ? texture->mipLevelCount : 1u;

  if (!ib_eligible(device, texture->format, texture->usage, mips, texture->sampleCount)
      || !(texture->usage & GPU_TEXTURE_USAGE_COPY_SRC) || !(texture->usage & GPU_TEXTURE_USAGE_COPY_DST))
    return NULL;

  ib_lock(device);

  if ((ib = texture->_imageBuffer)) {
    ib_unlock(device);
    return ib;
  }

  if (!(ib = calloc(1, sizeof(*ib) + mips * sizeof(ib->mips[0])))) {
    ib_unlock(device);
    return NULL;
  }

  ib->mipCount = mips;

  if (!ib_layout(ib, texture, &size)) {
    ib_unlock(device);
    free(ib);
    return NULL;
  }

  memset(&info, 0, sizeof(info));
  info.label     = "gpu image buffer";
  info.sizeBytes = size;
  info.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &info, &ib->buffer) != GPU_OK) {
    ib_unlock(device);
    free(ib);
    return NULL;
  }

  ib->texture      = texture;
  ib->textureNewer = ib_mipMask(ib, 0u, mips);

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
  const GPUImageBufferMip *m;
  GPUTextureView          *textureView;
  GPUImageBuffer          *ib;
  uint64_t                 offset, end;
  uint32_t                 bytes;

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

  /* the view's level, from its layer on (a 3d view holds every slice) */

  if (textureView->baseMipLevel >= ib->mipCount
      || textureView->baseArrayLayer >= (m = &ib->mips[textureView->baseMipLevel])->count)
    return false;

  offset = m->base + (uint64_t)textureView->baseArrayLayer * m->layerBytes;
  end    = m->base + m->layerBytes * m->count;
  bytes  = ib_format(textureView->_texture->format)->bytes;

  view->buffer                    = ib->buffer;
  view->offset                    = offset;
  view->size                      = end - offset;
  view->kind                      = GPUBindKindBuffer;
  view->bindingType               = GPU_BINDING_STORAGE_BUFFER;
  view->bufferLayout.strideBytes  = bytes < 4u ? 4u : bytes;
  view->bufferLayout.byteAddress  = false;
  view->bufferLayout.minBindingSize = 0u;

  return true;
}

/* how a group binds an emulated texture */

typedef struct GPUImageBufferUse {
  uint32_t mips;      /* the levels it sees */
  bool     readWrite; /* the buffer, at its base level */
  bool     writes;    /* write-only storage: the texture */
  bool     render;    /* visible to vertex or fragment shaders */
} GPUImageBufferUse;

typedef bool (*GPUImageBufferVisitFn)(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use);

/* every emulated texture a group binds, read-write (its buffer) or otherwise */

static bool
ib_visitGroup(GPUBindGroup *group, GPUImageBufferVisitFn fn, void *ctx) {
  const GPUBindGroupLayoutEntry *entry;
  GPUBindGroupLayoutPriv        *layout;
  GPUBindGroupBindingPriv       *binding;
  GPUBindGroupPriv              *priv;
  GPUTextureView                *view;
  GPUImageBufferUse              use;
  GPUImageBuffer                *ib;
  uint32_t                       i;
  bool                           storage;

  if (!group || !(priv = group->_priv) || !priv->layout || !(layout = priv->layout->_priv))
    return true;

  for (i = 0u; i < priv->count; i++) {
    binding = &priv->bindings[i];

    if (binding->kind != GPUBindKindTexture || !(view = binding->textureView) || !view->_texture
        || !view->_texture->_imageBuffer || binding->layoutEntryIndex >= layout->count)
      continue;

    ib            = view->_texture->_imageBuffer;
    entry         = &layout->entries[binding->layoutEntryIndex];
    storage       = entry->bindingType == GPU_BINDING_STORAGE_TEXTURE;
    use.readWrite = storage && entry->storageTexture.access == GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE;
    use.writes    = storage && entry->storageTexture.access != GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    use.render    = (entry->visibility & (GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_FRAGMENT_BIT)) != 0u;
    use.mips      = ib_mipMask(ib, view->baseMipLevel, use.readWrite ? 1u : view->mipLevelCount);

    if (!fn(ctx, ib, &use))
      return false;
  }

  return true;
}

/* a group made or updated: the levels it binds read-write to vertex or
 * fragment shaders, which render passes copy into the buffer before they begin */

GPU_HIDE
void
imageBufferGroupCreated(GPUBindGroup *group) {
  const GPUBindGroupLayoutEntry *entry;
  GPUBindGroupLayoutPriv        *layout;
  GPUBindGroupBindingPriv       *binding;
  GPUBindGroupPriv              *priv;
  GPUTextureView                *view;
  GPUImageBuffer                *ib;
  uint32_t                       i;

  if (!group || !(priv = group->_priv) || !priv->layout || !(layout = priv->layout->_priv))
    return;

  for (i = 0u; i < priv->count; i++) {
    binding = &priv->bindings[i];

    if (binding->kind != GPUBindKindTexture || !(view = binding->textureView) || !view->_texture
        || binding->layoutEntryIndex >= layout->count)
      continue;

    entry = &layout->entries[binding->layoutEntryIndex];

    if (entry->bindingType != GPU_BINDING_STORAGE_TEXTURE
        || entry->storageTexture.access != GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE
        || !(entry->visibility & (GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_FRAGMENT_BIT))
        || !deviceEmulatesReadWrite(view->_texture->device, view->format))
      continue;

    if ((ib = view->_texture->_imageBuffer ? view->_texture->_imageBuffer : ib_create(view->_texture))
        && view->baseMipLevel < ib->mipCount)
      ib->renderMips |= 1u << view->baseMipLevel;
  }
}

static bool
ib_found(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPU__UNUSED(ctx);
  GPU__UNUSED(ib);
  GPU__UNUSED(use);

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

/* a level's copy region: every layer (or slice) from its first row */

static void
ib_region(const GPUImageBuffer *ib, uint32_t mip, GPUBufferTextureCopyRegion *region) {
  const GPUImageBufferMip *m;
  bool                     volume;

  m      = &ib->mips[mip];
  volume = ib->texture->dimension == GPU_TEXTURE_DIMENSION_3D;

  memset(region, 0, sizeof(*region));
  region->texture.texture.mipLevel = mip;
  region->texture.width            = m->width;
  region->texture.height           = m->height;
  region->texture.depth            = volume ? m->count : 1u;
  region->texture.layerCount       = volume ? 1u : m->count;
  region->bufferOffset             = m->base + m->pitchBytes; /* past the first header row */
  region->bytesPerRow              = m->pitchBytes;
  region->rowsPerImage             = m->height + 1u;          /* and every later one */
}

/* the levels in 'mips' copied one way, between barriers over the texture */

static void
ib_copy(GPUCommandBuffer *cmdb, GPUImageBuffer *ib, bool toTexture, uint32_t mips) {
  GPUBufferTextureCopyRegion region;
  GPUTransferPassEncoder    *pass;
  GPUTextureBarrier          texture;
  GPUBufferBarrier           buffer;
  GPUBarrierBatch            batch;
  GPUTexture                *t;
  GPUApi                    *api;
  uint32_t                   mip;

  t = ib->texture;

  if (!mips || !(api = commandBufferApi(cmdb)))
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
  texture.mipCount    = ib->mipCount;
  texture.layerCount  = t->dimension == GPU_TEXTURE_DIMENSION_3D ? 1u : t->depthOrLayers;
  batch.pBufferBarriers     = &buffer;
  batch.pTextureBarriers    = &texture;
  batch.bufferBarrierCount  = 1u;
  batch.textureBarrierCount = 1u;
  batch.srcStages           = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  batch.dstStages           = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);

  /* the backend's copies: the public ones would sync this texture again */

  if ((pass = GPUBeginTransferPass(cmdb, "gpu image buffer"))) {
    for (mip = 0u; mip < ib->mipCount; mip++) {
      if (!(mips & (1u << mip)))
        continue;

      ib_region(ib, mip, &region);

      if (toTexture && api->renderPass.copyBufferToTexture)
        api->renderPass.copyBufferToTexture(pass, ib->buffer, t, &region);
      else if (!toTexture && api->renderPass.copyTextureToBuffer)
        api->renderPass.copyTextureToBuffer(pass, t, ib->buffer, &region);
    }

    GPUEndTransferPass(pass);
  }

  buffer.srcAccess  = buffer.dstAccess;
  buffer.dstAccess  = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
  texture.srcAccess = texture.dstAccess;
  texture.dstAccess = ib_textureAccess(t, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_TRANSFER_READ);
  batch.srcStages   = GPU_STAGE_TRANSFER;
  batch.dstStages   = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);

  if (toTexture)
    ib->bufferNewer &= ~mips;
  else
    ib->textureNewer &= ~mips;
}

typedef struct GPUImageBufferSync {
  GPUCommandBuffer *cmdb;
  bool              needed;
} GPUImageBufferSync;

/* the levels a use needs copied first: into the buffer for a read-write one,
 * into the texture for any other */

static uint32_t
ib_stale(const GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  return use->mips & (use->readWrite ? ib->textureNewer : ib->bufferNewer);
}

static bool
ib_needs(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPUImageBufferSync *sync = ctx;

  if (!ib->headers)
    ib_writeHeaders(ib, sync->cmdb ? sync->cmdb->_queue : NULL);

  if (ib_stale(ib, use))
    sync->needed = true;

  return true;
}

static bool
ib_sync(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPUImageBufferSync *sync = ctx;

  ib_copy(sync->cmdb, ib, !use->readWrite, ib_stale(ib, use));

  return true;
}

static void
ib_written(GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  if (use->readWrite) {
    ib->bufferNewer  |= use->mips;
    ib->textureNewer &= ~use->mips;
  } else if (use->writes) {
    ib->textureNewer |= use->mips;
    ib->bufferNewer  &= ~use->mips;
  }
}

static bool
ib_wrote(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPU__UNUSED(ctx);

  ib_written(ib, use);

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

static void
ib_flush(GPUCommandBuffer *cmdb, bool render) {
  GPUImageBuffer *ib;
  GPUDevice      *device;

  if (!cmdb || !(device = commandBufferDevice(cmdb)) || !device->_imageBuffers)
    return;

  ib_lock(device);

  for (ib = device->_imageBuffers; ib; ib = ib->next) {
    ib_copy(cmdb, ib, true, ib->bufferNewer);

    if (render)
      ib_copy(cmdb, ib, false, ib->textureNewer & ib->renderMips);
  }

  ib_unlock(device);
}

GPU_HIDE
void
imageBufferFlush(GPUCommandBuffer *cmdb) {
  ib_flush(cmdb, false);
}

GPU_HIDE
void
imageBufferRenderSync(GPUCommandBuffer *cmdb) {
  ib_flush(cmdb, true);
}

static bool
ib_renderWrote(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPU__UNUSED(ctx);

  if (use->readWrite && use->render)
    ib_written(ib, use);

  return true;
}

GPU_HIDE
void
imageBufferRenderBind(GPUBindGroup *group) {
  const GPUBindGroupPriv *priv;

  if (!group || !group->_device || !(priv = group->_priv))
    return;

  if (priv->imageBufferGeneration == group->_device->_imageBufferGeneration ? priv->imageBuffers
                                                                            : bindGroupImageBuffersFind(group))
    (void)ib_visitGroup(group, ib_renderWrote, NULL);
}

GPU_HIDE
void
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture) {
  GPUBufferTextureCopyRegion region;
  GPUImageBuffer            *ib;
  GPUApi                    *api;
  uint32_t                   mip;

  if (!pass || !texture || !(ib = texture->_imageBuffer) || !ib->bufferNewer
      || !(api = commandBufferApi(pass->_cmdb)) || !api->renderPass.copyBufferToTexture)
    return;

  /* already inside a transfer pass: the copies go first in it */

  for (mip = 0u; mip < ib->mipCount; mip++) {
    if (ib->bufferNewer & (1u << mip)) {
      ib_region(ib, mip, &region);
      api->renderPass.copyBufferToTexture(pass, ib->buffer, texture, &region);
    }
  }

  ib->bufferNewer = 0u;
}

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture, uint32_t mipLevel) {
  GPUImageBuffer *ib;
  uint32_t        mips;

  if (!texture || !(ib = texture->_imageBuffer))
    return;

  mips = mipLevel == UINT32_MAX ? ib_mipMask(ib, 0u, 0u) : ib_mipMask(ib, mipLevel, 1u);

  ib->textureNewer |= mips;
  ib->bufferNewer  &= ~mips;
}
