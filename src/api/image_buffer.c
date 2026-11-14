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

/* the stages a render pass runs */

#define GPU_IMAGE_BUFFER_RENDER_STAGES                                                                                \
  (GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_FRAGMENT_BIT | GPU_SHADER_STAGE_TASK_BIT | GPU_SHADER_STAGE_MESH_BIT)

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
  const GPUApi         *api;
  const char           *force;
  uint64_t              mask;
  void                 *lock;
  uint32_t              i;

  device->imageBufferFormats     = 0u;
  device->_imageBuffers          = NULL;
  device->_imageBufferLock       = NULL;
  device->_imageBufferGeneration = 0u;
  device->_imageBufferCounts     = NULL;

  /* GPU_IMAGE_BUFFERS=1 emulates every format it can, as a device without read-write textures would; =0 none */

  force = getenv("GPU_IMAGE_BUFFERS");
  mask  = 0u;

  if (force && force[0] == '0')
    return;

  /* nothing to find, and each format's query can be a driver round trip */
  if (!(force && force[0] == '1') && (api = deviceApi(device)) && api->storageAlwaysReadWrite)
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

  GPUDestroyBuffer(device->_imageBufferCounts);
  device->_imageBufferCounts = NULL;
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

/* every layer starts newer in the texture (its texels are there), each
 * header saying the layers from it on */

static bool
ib_track(GPUImageBuffer *ib, const GPUTexture *texture) {
  GPUImageBufferMip *m;
  uint64_t           words, counts, at;
  uint32_t           mip, i;
  uint8_t           *block;

  words  = 0u;
  counts = 0u;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    m        = &ib->mips[mip];
    m->units = texture->dimension == GPU_TEXTURE_DIMENSION_3D ? 1u : m->count;
    words   += 2u * ((m->units + 63u) / 64u);
    counts  += m->count;
  }

  if (!(block = calloc(1, words * sizeof(uint64_t) + 2u * counts * sizeof(uint32_t))))
    return false;

  ib->tracking = block;
  at           = 0u;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    m               = &ib->mips[mip];
    m->bufferNewer  = (uint64_t *)(block + at);
    at             += ((m->units + 63u) / 64u) * sizeof(uint64_t);
    m->textureNewer = (uint64_t *)(block + at);
    at             += ((m->units + 63u) / 64u) * sizeof(uint64_t);

    for (i = 0u; i < m->units; i++)
      m->textureNewer[i / 64u] |= UINT64_C(1) << (i % 64u);
  }

  for (mip = 0u; mip < ib->mipCount; mip++) {
    m              = &ib->mips[mip];
    m->layerCounts  = (uint32_t *)(block + at);
    at             += m->count * sizeof(uint32_t);
    m->renderCounts = (uint32_t *)(block + at);
    at             += m->count * sizeof(uint32_t);

    for (i = 0u; i < m->count; i++)
      m->layerCounts[i] = m->count - i;
  }

  return true;
}

/* the layers a view from 'layer' sees: 'count', or all from it on (0, or past the end) */

static uint32_t
ib_seen(const GPUImageBufferMip *m, uint32_t layer, uint32_t count) {
  return count && count < m->count - layer ? count : m->count - layer;
}

/* a view the shader sees as an array: only these read the layer count */

static bool
ib_layered(const GPUTextureView *view) {
  return view->viewType == GPU_TEXTURE_VIEW_1D_ARRAY || view->viewType == GPU_TEXTURE_VIEW_2D_ARRAY;
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

  if (!ib_layout(ib, texture, &size) || !ib_track(ib, texture)) {
    ib_unlock(device);
    free(ib->tracking);
    free(ib);
    return NULL;
  }

  memset(&info, 0, sizeof(info));
  info.label     = "gpu image buffer";
  info.sizeBytes = size;
  info.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &info, &ib->buffer) != GPU_OK) {
    ib_unlock(device);
    free(ib->tracking);
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
  free(ib->tracking);
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

  /* exactly the view's layers (every slice of a 3d level) */

  offset = m->base + (uint64_t)textureView->baseArrayLayer * m->layerBytes;
  end    = textureView->_texture->dimension == GPU_TEXTURE_DIMENSION_3D || !textureView->arrayLayerCount
             ? m->base + m->layerBytes * m->count
             : offset + m->layerBytes * (textureView->arrayLayerCount < m->count - textureView->baseArrayLayer
                                           ? textureView->arrayLayerCount
                                           : m->count - textureView->baseArrayLayer);
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

/* ---------------------------------------------------------------------------
 * which layers are newer where
 * ------------------------------------------------------------------------- */

static bool
ib_bitsAny(const uint64_t *bits, uint32_t first, uint32_t end) {
  uint32_t i;

  for (i = first; i < end; i++) {
    if (!(i & 63u) && i + 64u <= end) {
      if (bits[i / 64u])
        return true;

      i += 63u;
      continue;
    }

    if (bits[i / 64u] & (UINT64_C(1) << (i & 63u)))
      return true;
  }

  return false;
}

static void
ib_bitsSet(uint64_t *bits, uint32_t first, uint32_t end, bool on) {
  uint32_t i;

  for (i = first; i < end; i++) {
    if (on)
      bits[i / 64u] |= UINT64_C(1) << (i & 63u);
    else
      bits[i / 64u] &= ~(UINT64_C(1) << (i & 63u));
  }
}

/* a level's tracked units [first, end) for layers [layer, layer + count):
 * a 3d level is one unit; count 0 or UINT32_MAX: to the last layer */

static void
ib_units(const GPUImageBufferMip *m, uint32_t layer, uint32_t count, uint32_t *first, uint32_t *end) {
  if (m->units == 1u && m->count != 1u) {
    *first = 0u;
    *end   = 1u;
    return;
  }

  *first = layer < m->units ? layer : m->units;
  *end   = count == 0u || count == UINT32_MAX || count > m->units - *first ? m->units : *first + count;
}

/* the level's summary bits follow its layers */

static void
ib_summarize(GPUImageBuffer *ib, uint32_t mip) {
  const GPUImageBufferMip *m = &ib->mips[mip];

  if (ib_bitsAny(m->bufferNewer, 0u, m->units))
    ib->bufferNewer |= 1u << mip;
  else
    ib->bufferNewer &= ~(1u << mip);

  if (ib_bitsAny(m->textureNewer, 0u, m->units))
    ib->textureNewer |= 1u << mip;
  else
    ib->textureNewer &= ~(1u << mip);
}

/* layers [layer, layer + count) of levels in 'mips' now newer in the buffer (or the texture) */

static void
ib_mark(GPUImageBuffer *ib, uint32_t mips, uint32_t layer, uint32_t count, bool buffer) {
  GPUImageBufferMip *m;
  uint32_t           mip, first, end;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    if (!(mips & (1u << mip)))
      continue;

    m = &ib->mips[mip];
    ib_units(m, layer, count, &first, &end);
    ib_bitsSet(buffer ? m->bufferNewer : m->textureNewer, first, end, true);
    ib_bitsSet(buffer ? m->textureNewer : m->bufferNewer, first, end, false);
    ib_summarize(ib, mip);
  }
}

/* whether any of layers [layer, layer + count) of levels in 'mips' is newer in the buffer (or the texture) */

static bool
ib_newer(const GPUImageBuffer *ib, uint32_t mips, uint32_t layer, uint32_t count, bool buffer) {
  const GPUImageBufferMip *m;
  uint32_t                 mip, first, end;

  if (!(mips & (buffer ? ib->bufferNewer : ib->textureNewer)))
    return false;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    if (!(mips & (1u << mip)))
      continue;

    m = &ib->mips[mip];
    ib_units(m, layer, count, &first, &end);

    if (ib_bitsAny(buffer ? m->bufferNewer : m->textureNewer, first, end))
      return true;
  }

  return false;
}

/* ---------------------------------------------------------------------------
 * bindings' uses
 * ------------------------------------------------------------------------- */

/* how a group binds an emulated texture */

typedef struct GPUImageBufferUse {
  uint32_t mips;       /* the levels it sees */
  uint32_t layer;      /* its first layer */
  uint32_t layerCount; /* and how many */
  bool     readWrite;  /* the buffer, at its base level */
  bool     writes;     /* write-only storage: the texture */
  bool     render;     /* visible to render stages */
  bool     layered;    /* an array view: its shader may ask its layer count */
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

    ib             = view->_texture->_imageBuffer;
    entry          = &layout->entries[binding->layoutEntryIndex];
    storage        = entry->bindingType == GPU_BINDING_STORAGE_TEXTURE;
    use.readWrite  = storage && entry->storageTexture.access == GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE;
    use.writes     = storage && entry->storageTexture.access != GPU_STORAGE_TEXTURE_ACCESS_READ_ONLY;
    use.render     = (entry->visibility & GPU_IMAGE_BUFFER_RENDER_STAGES) != 0u;
    use.mips       = ib_mipMask(ib, view->baseMipLevel, use.readWrite ? 1u : view->mipLevelCount);
    use.layer      = view->baseArrayLayer;
    use.layerCount = view->arrayLayerCount;
    use.layered    = ib_layered(view);

    if (!fn(ctx, ib, &use))
      return false;
  }

  return true;
}

/* a render stage's read-write view of an array: the layer count its first
 * header must hold while a pass runs. a pass can't write headers once begun,
 * so render passes write these before they begin; two such views from one
 * layer that see different counts can't both be right (the later one is) */

static void
ib_renderView(GPUImageBuffer *ib, const GPUTextureView *view) {
  GPUImageBufferMip *m;
  uint32_t           count;

  if (!ib_layered(view) || ib->texture->dimension == GPU_TEXTURE_DIMENSION_3D
      || view->baseArrayLayer >= (m = &ib->mips[view->baseMipLevel])->count)
    return;

  count = ib_seen(m, view->baseArrayLayer, view->arrayLayerCount);

  if (m->renderCounts[view->baseArrayLayer] != count) {
    m->renderCounts[view->baseArrayLayer] = count;
    ib->renderStale                       = true;
  }
}

/* a group made or updated: the levels it binds read-write to render stages,
 * which render passes copy into the buffer before they begin (and the layer
 * counts those views need), and the textures render stages read otherwise,
 * which render passes bring up to date */

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

    if (!(entry->visibility & GPU_IMAGE_BUFFER_RENDER_STAGES) || !deviceEmulatesReadWrite(view->_texture->device, view->format))
      continue;

    if (entry->bindingType != GPU_BINDING_STORAGE_TEXTURE
        || entry->storageTexture.access != GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE) {
      view->_texture->_imageBufferRenderRead = true;
      continue;
    }

    if ((ib = view->_texture->_imageBuffer ? view->_texture->_imageBuffer : ib_create(view->_texture))
        && view->baseMipLevel < ib->mipCount) {
      /* render passes read these under the lock as they begin */
      ib_lock(view->_texture->device);
      ib->renderMips |= 1u << view->baseMipLevel;
      ib_renderView(ib, view);
      ib_unlock(view->_texture->device);
    }
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

/* a level's copy region for units [first, end): those layers (a 3d level's
 * every slice) from their first rows */

static void
ib_region(const GPUImageBuffer *ib, uint32_t mip, uint32_t first, uint32_t end, GPUBufferTextureCopyRegion *region) {
  const GPUImageBufferMip *m;
  bool                     volume;

  m      = &ib->mips[mip];
  volume = ib->texture->dimension == GPU_TEXTURE_DIMENSION_3D;

  memset(region, 0, sizeof(*region));
  region->texture.texture.mipLevel       = mip;
  region->texture.texture.baseArrayLayer = volume ? 0u : first;
  region->texture.width                  = m->width;
  region->texture.height                 = m->height;
  region->texture.depth                  = volume ? m->count : 1u;
  region->texture.layerCount             = volume ? 1u : end - first;
  region->bufferOffset                   = m->base + (uint64_t)(volume ? 0u : first) * m->layerBytes
                                           + m->pitchBytes; /* past the first header row */
  region->bytesPerRow                    = m->pitchBytes;
  region->rowsPerImage                   = m->height + 1u;  /* and every later one */
}

/* inside a transfer pass: each run of layers in range newer on one side
 * copied to the other, which then holds the same */

static void
ib_copyRuns(GPUTransferPassEncoder *pass, GPUApi *api, GPUImageBuffer *ib, bool toTexture, uint32_t mips, uint32_t layer,
            uint32_t count) {
  GPUBufferTextureCopyRegion region;
  GPUImageBufferMip         *m;
  uint64_t                  *bits;
  uint32_t                   mip, first, end, run;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    if (!(mips & (1u << mip)))
      continue;

    m    = &ib->mips[mip];
    bits = toTexture ? m->bufferNewer : m->textureNewer;
    ib_units(m, layer, count, &first, &end);

    while (first < end) {
      if (!(bits[first / 64u] & (UINT64_C(1) << (first & 63u)))) {
        first++;
        continue;
      }

      for (run = first + 1u; run < end && (bits[run / 64u] & (UINT64_C(1) << (run & 63u))); run++) {}

      ib_region(ib, mip, first, run, &region);

      if (toTexture && api->renderPass.copyBufferToTexture)
        api->renderPass.copyBufferToTexture(pass, ib->buffer, ib->texture, &region);
      else if (!toTexture && api->renderPass.copyTextureToBuffer)
        api->renderPass.copyTextureToBuffer(pass, ib->texture, ib->buffer, &region);

      ib_bitsSet(bits, first, run, false);
      first = run;
    }

    ib_summarize(ib, mip);
  }
}

/* outside a pass: the layers in range of levels in 'mips' newer on one side
 * copied to the other, between barriers over both */

static void
ib_copy(GPUCommandBuffer *cmdb, GPUImageBuffer *ib, bool toTexture, uint32_t mips, uint32_t layer, uint32_t count) {
  GPUTransferPassEncoder *pass;
  GPUTextureBarrier       texture;
  GPUBufferBarrier        buffer;
  GPUBarrierBatch         batch;
  GPUTexture             *t;
  GPUApi                 *api;

  t = ib->texture;

  if (!mips || !ib_newer(ib, mips, layer, count, toTexture) || !(api = commandBufferApi(cmdb)))
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
    ib_copyRuns(pass, api, ib, toTexture, mips, layer, count);
    GPUEndTransferPass(pass);
  }

  buffer.srcAccess  = buffer.dstAccess;
  buffer.dstAccess  = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
  texture.srcAccess = texture.dstAccess;
  texture.dstAccess = ib_textureAccess(t, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_TRANSFER_READ);
  batch.srcStages   = GPU_STAGE_TRANSFER;
  batch.dstStages   = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT | GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);
}

/* the device's buffer of 0, 1, 2... a header's layer count is copied from */

enum {
  GPU_IMAGE_BUFFER_COUNTS = 2048u
};

static GPUBuffer *
ib_counts(GPUDevice *device, GPUQueue *queue) {
  GPUBufferCreateInfo info;
  uint32_t           *values;
  uint32_t            i;

  if (device->_imageBufferCounts || !queue)
    return device->_imageBufferCounts;

  if (!(values = malloc(GPU_IMAGE_BUFFER_COUNTS * sizeof(*values))))
    return NULL;

  for (i = 0u; i < GPU_IMAGE_BUFFER_COUNTS; i++)
    values[i] = i;

  memset(&info, 0, sizeof(info));
  info.label     = "gpu image buffer layer counts";
  info.sizeBytes = GPU_IMAGE_BUFFER_COUNTS * sizeof(*values);
  info.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &info, &device->_imageBufferCounts) == GPU_OK
      && GPUQueueWriteBuffer(queue, device->_imageBufferCounts, 0u, values, info.sizeBytes) != GPU_OK) {
    GPUDestroyBuffer(device->_imageBufferCounts);
    device->_imageBufferCounts = NULL;
  }

  free(values);

  return device->_imageBufferCounts;
}

/* the layer count a read-write view of an array needs in its first header,
 * where it differs from the one there; 0: none to write */

static uint32_t
ib_viewCount(const GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  const GPUImageBufferMip *m;
  uint32_t                 mip, count;

  if (!use->readWrite || !use->layered || ib->texture->dimension == GPU_TEXTURE_DIMENSION_3D)
    return 0u;

  for (mip = 0u; mip < ib->mipCount && !(use->mips & (1u << mip)); mip++) {}

  if (mip == ib->mipCount || use->layer >= (m = &ib->mips[mip])->count)
    return 0u;

  count = ib_seen(m, use->layer, use->layerCount);

  return m->layerCounts[use->layer] != count && count < GPU_IMAGE_BUFFER_COUNTS ? count : 0u;
}

/* a layer count to put in a header */

typedef struct GPUImageBufferCount {
  uint32_t mip;
  uint32_t layer;
  uint32_t count;
} GPUImageBufferCount;

/* outside a pass: counts copied into their layers' headers, one transfer
 * pass for them all; false if they couldn't be */

static bool
ib_writeCounts(GPUCommandBuffer *cmdb, GPUImageBuffer *ib, const GPUImageBufferCount *writes, uint32_t n) {
  GPUTransferPassEncoder *pass;
  GPUImageBufferMip      *m;
  GPUBufferCopyRegion     region;
  GPUBufferBarrier        buffer;
  GPUBarrierBatch         batch;
  GPUBuffer              *counts;
  GPUApi                 *api;
  uint32_t                i;

  if (!n)
    return true;

  if (!(api = commandBufferApi(cmdb)) || !api->renderPass.copyBufferToBuffer
      || !(counts = ib_counts(ib->texture->device, cmdb->_queue)))
    return false;

  memset(&buffer, 0, sizeof(buffer));
  memset(&batch, 0, sizeof(batch));
  buffer.buffer             = ib->buffer;
  buffer.srcAccess          = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
  buffer.dstAccess          = GPU_ACCESS_TRANSFER_WRITE;
  buffer.sizeBytes          = ib->buffer->sizeBytes;
  batch.pBufferBarriers     = &buffer;
  batch.bufferBarrierCount  = 1u;
  batch.srcStages           = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT;
  batch.dstStages           = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &batch);

  if ((pass = GPUBeginTransferPass(cmdb, "gpu image buffer layer count"))) {
    region.sizeBytes = sizeof(uint32_t);

    for (i = 0u; i < n; i++) {
      m                = &ib->mips[writes[i].mip];
      region.srcOffset = (uint64_t)writes[i].count * sizeof(uint32_t);
      region.dstOffset = m->base + (uint64_t)writes[i].layer * m->layerBytes + 3u * sizeof(uint32_t);
      api->renderPass.copyBufferToBuffer(pass, counts, ib->buffer, &region);
      m->layerCounts[writes[i].layer] = writes[i].count;
    }

    GPUEndTransferPass(pass);
  }

  buffer.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  buffer.dstAccess = GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
  batch.srcStages  = GPU_STAGE_TRANSFER;
  batch.dstStages  = GPU_STAGE_COMPUTE | GPU_STAGE_FRAGMENT;
  GPUEncodeBarriers(cmdb, &batch);

  return pass != NULL;
}

/* outside a pass: the count a view needs in its first header, where it
 * differs (a render pass's view there wants its own back after) */

static void
ib_writeViewCount(GPUCommandBuffer *cmdb, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPUImageBufferCount write;

  if (!(write.count = ib_viewCount(ib, use)))
    return;

  for (write.mip = 0u; !(use->mips & (1u << write.mip)); write.mip++) {}

  write.layer = use->layer;

  if (ib_writeCounts(cmdb, ib, &write, 1u) && ib->mips[write.mip].renderCounts[write.layer])
    ib->renderStale = true;
}

/* before a render pass begins: the counts render stages' read-write views
 * of arrays need, where a dispatch or another view left others */

static void
ib_renderCounts(GPUCommandBuffer *cmdb, GPUImageBuffer *ib) {
  GPUImageBufferCount      writes[16];
  const GPUImageBufferMip *m;
  uint32_t                 mip, layer, want, n;
  bool                     ok;

  if (!ib->renderStale)
    return;

  if (!ib->headers)
    ib_writeHeaders(ib, cmdb->_queue);

  ok = true;
  n  = 0u;

  for (mip = 0u; mip < ib->mipCount; mip++) {
    if (!(ib->renderMips & (1u << mip)))
      continue;

    m = &ib->mips[mip];

    for (layer = 0u; layer < m->count; layer++) {
      if (!(want = m->renderCounts[layer]) || want == m->layerCounts[layer] || want >= GPU_IMAGE_BUFFER_COUNTS)
        continue;

      writes[n].mip   = mip;
      writes[n].layer = layer;
      writes[n].count = want;

      if (++n == GPU_ARRAY_LEN(writes)) {
        ok = ib_writeCounts(cmdb, ib, writes, n) && ok;
        n  = 0u;
      }
    }
  }

  ok              = ib_writeCounts(cmdb, ib, writes, n) && ok;
  ib->renderStale = !ok;
}

typedef struct GPUImageBufferSync {
  GPUCommandBuffer *cmdb;
  bool              needed;
} GPUImageBufferSync;

/* what a use needs first: its layers copied into the buffer for a read-write
 * one (and its layer count in the header), into the texture for any other */

static bool
ib_needs(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPUImageBufferSync *sync = ctx;

  if (!ib->headers)
    ib_writeHeaders(ib, sync->cmdb ? sync->cmdb->_queue : NULL);

  if (ib_newer(ib, use->mips, use->layer, use->layerCount, !use->readWrite) || ib_viewCount(ib, use))
    sync->needed = true;

  return true;
}

static bool
ib_sync(void *ctx, GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  GPUImageBufferSync *sync = ctx;

  ib_copy(sync->cmdb, ib, !use->readWrite, use->mips, use->layer, use->layerCount);
  ib_writeViewCount(sync->cmdb, ib, use);

  return true;
}

static void
ib_written(GPUImageBuffer *ib, const GPUImageBufferUse *use) {
  if (use->readWrite)
    ib_mark(ib, use->mips, use->layer, use->layerCount, true);
  else if (use->writes)
    ib_mark(ib, use->mips, use->layer, use->layerCount, false);
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

GPU_HIDE
void
imageBufferFlushTexture(GPUCommandBuffer *cmdb, GPUTexture *texture, uint32_t mipLevel) {
  GPUImageBuffer *ib;

  if (cmdb && texture && (ib = texture->_imageBuffer))
    ib_copy(cmdb, ib, true, ib_mipMask(ib, mipLevel, 1u), 0u, UINT32_MAX);
}

static bool
ib_attachment(const GPURenderPassCreateInfo *info, const GPUTexture *texture) {
  uint32_t i;

  for (i = 0u; info && i < info->colorAttachmentCount; i++) {
    if ((info->pColorAttachments[i].view && info->pColorAttachments[i].view->_texture == texture)
        || (info->pColorAttachments[i].resolveView && info->pColorAttachments[i].resolveView->_texture == texture))
      return true;
  }

  return false;
}

GPU_HIDE
void
imageBufferRenderSync(GPUCommandBuffer *cmdb, const GPURenderPassCreateInfo *info) {
  GPUImageBuffer *ib;
  GPUDevice      *device;

  if (!cmdb || !(device = commandBufferDevice(cmdb)) || !device->_imageBuffers)
    return;

  ib_lock(device);

  for (ib = device->_imageBuffers; ib; ib = ib->next) {
    if (ib->bufferNewer && (ib->texture->_imageBufferRenderRead || ib_attachment(info, ib->texture)))
      ib_copy(cmdb, ib, true, ib->bufferNewer, 0u, UINT32_MAX);

    ib_copy(cmdb, ib, false, ib->textureNewer & ib->renderMips, 0u, UINT32_MAX);
    ib_renderCounts(cmdb, ib);
  }

  ib_unlock(device);
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
imageBufferTransferRead(GPUTransferPassEncoder *pass, GPUTexture *texture, uint32_t mipLevel, uint32_t layer,
                        uint32_t layerCount) {
  GPUImageBuffer *ib;
  GPUApi         *api;
  uint32_t        mips;

  if (!pass || !texture || !(ib = texture->_imageBuffer) || !(api = commandBufferApi(pass->_cmdb)))
    return;

  /* already inside a transfer pass: the copies go first in it */

  mips = ib_mipMask(ib, mipLevel, 1u);

  if (ib_newer(ib, mips, layer, layerCount, true))
    ib_copyRuns(pass, api, ib, true, mips, layer, layerCount);
}

/* where a texel of a level sits in the buffer: its layer's (a 3d level's
 * slice's) rows start one row past the header */

static uint64_t
ib_texelOffset(const GPUImageBuffer *ib, const GPUTextureLocation *at) {
  const GPUImageBufferMip *m;
  uint32_t                 layer;

  m     = &ib->mips[at->mipLevel];
  layer = ib->texture->dimension == GPU_TEXTURE_DIMENSION_3D ? at->z : at->baseArrayLayer;

  return m->base + (uint64_t)layer * m->layerBytes + ((uint64_t)at->y + 1u) * m->pitchBytes
         + (uint64_t)at->x * ib_format(ib->texture->format)->bytes;
}

/* a copy reads the buffer when it holds the newest texels of every layer the
 * copy reads and the newer ones of some */

static bool
ib_readsBuffer(const GPUTexture *src, uint32_t mipLevel, uint32_t layer, uint32_t layerCount) {
  const GPUImageBuffer *ib = src ? src->_imageBuffer : NULL;
  uint32_t              mips;

  if (!ib || mipLevel >= ib->mipCount)
    return false;

  mips = 1u << mipLevel;

  return ib_newer(ib, mips, layer, layerCount, true) && !ib_newer(ib, mips, layer, layerCount, false);
}

GPU_HIDE
bool
imageBufferCopyToTexture(GPUTransferPassEncoder              *pass,
                         GPUTexture                          *src,
                         GPUTexture                          *dst,
                         const GPUTextureToTextureCopyRegion *region) {
  GPUBufferTextureCopyRegion copy;
  GPUImageBuffer            *ib;
  GPUApi                    *api;

  if (!pass || !region || !ib_readsBuffer(src, region->src.mipLevel, region->src.baseArrayLayer, region->layerCount)
      || !(api = commandBufferApi(pass->_cmdb)) || !api->renderPass.copyBufferToTexture)
    return false;

  ib = src->_imageBuffer;

  memset(&copy, 0, sizeof(copy));
  copy.texture.texture    = region->dst;
  copy.texture.width      = region->width;
  copy.texture.height     = region->height;
  copy.texture.depth      = region->depth;
  copy.texture.layerCount = region->layerCount;
  copy.bufferOffset       = ib_texelOffset(ib, &region->src);
  copy.bytesPerRow        = ib->mips[region->src.mipLevel].pitchBytes;
  copy.rowsPerImage       = ib->mips[region->src.mipLevel].height + 1u;

  api->renderPass.copyBufferToTexture(pass, ib->buffer, dst, &copy);

  return true;
}

GPU_HIDE
bool
imageBufferCopyToBuffer(GPUTransferPassEncoder           *pass,
                        GPUTexture                       *src,
                        GPUBuffer                        *dst,
                        const GPUBufferTextureCopyRegion *region) {
  const GPUTextureSubresourceRegion *at;
  const GPUImageBufferMip           *m;
  GPUBufferCopyRegion                copy;
  GPUImageBuffer                    *ib;
  GPUApi                            *api;
  uint64_t                           first, rowBytes, rows, images, image, row, dstRows;
  uint32_t                           bytes;

  if (!pass || !region
      || !ib_readsBuffer(src, region->texture.texture.mipLevel, region->texture.texture.baseArrayLayer,
                         region->texture.layerCount)
      || !(api = commandBufferApi(pass->_cmdb)) || !api->renderPass.copyBufferToBuffer)
    return false;

  ib       = src->_imageBuffer;
  at       = &region->texture;
  m        = &ib->mips[at->texture.mipLevel];
  bytes    = ib_format(src->format)->bytes;
  rowBytes = (uint64_t)at->width * bytes;
  rows     = at->height;
  images   = src->dimension == GPU_TEXTURE_DIMENSION_3D ? at->depth : at->layerCount;
  dstRows  = region->rowsPerImage ? region->rowsPerImage : at->height;
  first    = ib_texelOffset(ib, &at->texture);

  /* buffer copies take 4-byte offsets and sizes everywhere */

  if (((first | rowBytes | region->bufferOffset | region->bytesPerRow | m->pitchBytes) & 3u) != 0u)
    return false;

  for (image = 0u; image < images; image++) {
    /* rows the same length on both sides with nothing between them: one copy */

    if (rowBytes == m->pitchBytes && region->bytesPerRow == m->pitchBytes) {
      copy.srcOffset = first + image * m->layerBytes;
      copy.dstOffset = region->bufferOffset + image * dstRows * region->bytesPerRow;
      copy.sizeBytes = rows * rowBytes;
      api->renderPass.copyBufferToBuffer(pass, ib->buffer, dst, &copy);
      continue;
    }

    for (row = 0u; row < rows; row++) {
      copy.srcOffset = first + image * m->layerBytes + row * m->pitchBytes;
      copy.dstOffset = region->bufferOffset + (image * dstRows + row) * region->bytesPerRow;
      copy.sizeBytes = rowBytes;
      api->renderPass.copyBufferToBuffer(pass, ib->buffer, dst, &copy);
    }
  }

  return true;
}

/* the buffer barrier of an emulated texture's barrier: shader and transfer
 * accesses carry over, attachment ones have no buffer side */

static GPUAccessMask
ib_bufferAccess(GPUAccessMask access) {
  return (GPUAccessMask)(access & (GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_TRANSFER_READ
                                   | GPU_ACCESS_TRANSFER_WRITE));
}

GPU_HIDE
const GPUBarrierBatch *
imageBufferBarriers(const GPUBarrierBatch *batch, GPUBarrierBatch *out, GPUBufferBarrier *storage, uint32_t capacity,
                    GPUBufferBarrier **allocated) {
  const GPUTextureBarrier *t;
  GPUBufferBarrier        *barriers;
  uint32_t                 i, extra, count;

  *allocated = NULL;
  extra      = 0u;

  for (i = 0u; i < batch->textureBarrierCount; i++) {
    t = &batch->pTextureBarriers[i];

    if (t->texture && t->texture->_imageBuffer && (ib_bufferAccess(t->srcAccess) || ib_bufferAccess(t->dstAccess)))
      extra++;
  }

  if (!extra)
    return NULL;

  count    = batch->bufferBarrierCount + extra;
  barriers = count <= capacity ? storage : (*allocated = malloc(count * sizeof(*barriers)));

  if (!barriers)
    return NULL;

  if (batch->bufferBarrierCount)
    memcpy(barriers, batch->pBufferBarriers, batch->bufferBarrierCount * sizeof(*barriers));

  count = batch->bufferBarrierCount;

  for (i = 0u; i < batch->textureBarrierCount; i++) {
    t = &batch->pTextureBarriers[i];

    if (!t->texture || !t->texture->_imageBuffer || !(ib_bufferAccess(t->srcAccess) || ib_bufferAccess(t->dstAccess)))
      continue;

    memset(&barriers[count], 0, sizeof(barriers[count]));
    barriers[count].buffer    = t->texture->_imageBuffer->buffer;
    barriers[count].srcAccess = ib_bufferAccess(t->srcAccess);
    barriers[count].dstAccess = ib_bufferAccess(t->dstAccess);
    barriers[count].sizeBytes = t->texture->_imageBuffer->buffer->sizeBytes;
    count++;
  }

  *out                    = *batch;
  out->pBufferBarriers    = barriers;
  out->bufferBarrierCount = count;

  return out;
}

GPU_HIDE
bool
imageBufferTextureUploaded(GPUQueue *queue, GPUTexture *texture, const GPUTextureWriteRegion *region, const void *data) {
  const GPUImageBufferMip *m;
  const uint8_t           *src;
  GPUImageBuffer          *ib;
  uint64_t                 offset, rowBytes, srcRows;
  uint32_t                 bytes, images, image, first, row;
  uint8_t                 *rows;
  bool                     whole, ok;

  if (!queue || !texture || !region || !data || !(ib = texture->_imageBuffer) || region->mipLevel >= ib->mipCount)
    return false;

  m        = &ib->mips[region->mipLevel];
  bytes    = ib_format(texture->format)->bytes;
  rowBytes = (uint64_t)region->width * bytes;
  srcRows  = region->rowsPerImage ? region->rowsPerImage : region->height;
  images   = texture->dimension == GPU_TEXTURE_DIMENSION_3D ? region->depth : region->layerCount;
  first    = texture->dimension == GPU_TEXTURE_DIMENSION_3D ? region->z : region->baseArrayLayer;
  whole    = region->x == 0u && region->width == m->width;

  /* buffer writes take 4-byte offsets and sizes everywhere */

  if (!whole && ((rowBytes | ((uint64_t)region->x * bytes)) & 3u))
    return false;

  ib_writeHeaders(ib, queue);

  rows = NULL;
  ok   = true;

  /* whole rows: one write a layer in the buffer's layout (its row ends padded) */

  if (whole && (region->bytesPerRow != m->pitchBytes || rowBytes != m->pitchBytes)
      && !(rows = calloc(1, (size_t)region->height * m->pitchBytes)))
    return false;

  for (image = 0u; image < images && ok; image++) {
    src    = (const uint8_t *)data + (uint64_t)image * srcRows * region->bytesPerRow;
    offset = m->base + (uint64_t)(first + image) * m->layerBytes + ((uint64_t)region->y + 1u) * m->pitchBytes
             + (uint64_t)region->x * bytes;

    if (whole) {
      if (rows) {
        for (row = 0u; row < region->height; row++)
          memcpy(rows + (uint64_t)row * m->pitchBytes, src + (uint64_t)row * region->bytesPerRow, rowBytes);
      }

      ok = GPUQueueWriteBuffer(queue, ib->buffer, offset, rows ? rows : src, (uint64_t)region->height * m->pitchBytes)
           == GPU_OK;
      continue;
    }

    for (row = 0u; row < region->height && ok; row++)
      ok = GPUQueueWriteBuffer(queue, ib->buffer, offset + (uint64_t)row * m->pitchBytes,
                               src + (uint64_t)row * region->bytesPerRow, rowBytes) == GPU_OK;
  }

  free(rows);

  return ok;
}

GPU_HIDE
void
imageBufferTextureWritten(GPUTexture *texture, uint32_t mipLevel, uint32_t layer, uint32_t layerCount) {
  GPUImageBuffer *ib;

  if (!texture || !(ib = texture->_imageBuffer))
    return;

  if (mipLevel == UINT32_MAX)
    ib_mark(ib, ib_mipMask(ib, 0u, 0u), 0u, UINT32_MAX, false);
  else
    ib_mark(ib, ib_mipMask(ib, mipLevel, 1u), layer, layerCount, false);
}
