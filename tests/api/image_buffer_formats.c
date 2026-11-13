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
 * read-write storage textures of every shape a buffer stands in for, native
 * where the device has them and emulated where not (GPU_IMAGE_BUFFERS=1
 * emulates all): r8unorm, rg8snorm, r16sint, r16float, rgba16float and
 * rg11b10ufloat written by one kernel (8- and 16-bit texels share words with
 * their neighbours), a 3d image, two mip levels of a texture bound one after
 * the other, a 1d image and a 1d array, and a fragment shader writing an
 * image in two render passes. every texel is checked exactly.
 */

#include "test.h"
#include "../../src/api/texture_internal.h"

#include <math.h>

enum {
  IBF_W       = 13u, /* odd: words hold texels of two rows' ends */
  IBF_H       = 5u,
  IBF_ROW     = 256u,
  IBF_VOL_W   = 5u,
  IBF_VOL_H   = 4u,
  IBF_VOL_D   = 3u,
  IBF_MIP_W   = 16u,
  IBF_MIP_H   = 8u,
  IBF_MIPS    = 3u,
  IBF_LINE_W  = 20u,
  IBF_LINES_W = 9u,
  IBF_LINES   = 3u,
  IBF_RT      = 8u,
  IBF_SLOT    = IBF_ROW * 16u /* a readback slot: 16 rows */
};

/* a library each: a library's entries share its one layout */

enum {
  IBF_FORMATS,
  IBF_VOLUME,
  IBF_LEVEL,
  IBF_LINE,
  IBF_RENDER,
  IBF_LAYERS,
  IBF_LIBRARIES
};

typedef struct IBFTest {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUShaderLibrary   *libraries[IBF_LIBRARIES];
  GPUShaderLayout    *layouts[IBF_LIBRARIES];
  GPUComputePipeline *formats, *volume, *level, *line, *layers;
  GPURenderPipeline  *render;
  GPUBuffer          *sizes, *readback;
  GPUTexture         *textures[16];
  GPUTextureView     *views[20];
  GPUBindGroup       *groups[10];
  uint32_t            textureCount, viewCount, groupCount;
} IBFTest;

/* ---------------------------------------------------------------------------
 * small floats on the host, for values they hold exactly
 * ------------------------------------------------------------------------- */

static uint32_t
ibf_small(float value, uint32_t mant, bool sign) {
  uint32_t bits, s, e, m;
  int      exp;
  float    frac;

  s = sign && signbit(value) ? 1u : 0u;

  if (value == 0.0f)
    return s << 15;

  value = fabsf(value);
  frac  = frexpf(value, &exp); /* value = frac * 2^exp, frac in [0.5, 1) */
  exp  -= 1;

  if (exp < -14) { /* subnormal */
    e = 0u;
    m = (uint32_t)ldexpf(value, 14 + (int)mant);
  } else {
    e = (uint32_t)(exp + 15);
    m = (uint32_t)ldexpf(frac * 2.0f - 1.0f, (int)mant);
  }

  bits = (e << mant) | m;

  return sign ? bits | (s << 15) : bits;
}

static float
ibf_byteValue(uint32_t x, uint32_t y) {
  return (float)((x * 19u + y * 7u) & 0xffu);
}

static float
ibf_half(uint32_t x, uint32_t y, uint32_t c) {
  const float v[4] = {0.25f * (float)x, 0.5f * (float)y, -1.5f, 2.0f};

  return v[c];
}

static float
ibf_one(uint32_t x, uint32_t y) {
  return x == 0u && y == 0u ? ldexpf(1.0f, -20) : ((float)x - 6.0f) * 0.125f * (float)(y + 1u);
}

static float
ibf_packed(uint32_t x, uint32_t y, uint32_t c) {
  const float v[3] = {0.125f * (float)(x + y), 0.25f * (float)(x + 2u * y), 0.5f * (float)y};

  return v[c];
}

static int8_t
ibf_short(uint32_t x, uint32_t y, uint32_t c) {
  return c == 0u ? (int8_t)((int)x * 9 - 50) : (int8_t)((int)y * 20 - 40);
}

static int16_t
ibf_word(uint32_t x, uint32_t y) {
  return (int16_t)((int)x * 1000 - (int)y * 3000);
}

static uint32_t
ibf_packedBits(float r, float g, float b) {
  return ibf_small(r, 6u, false) | (ibf_small(g, 6u, false) << 11) | (ibf_small(b, 5u, false) << 22);
}

/* ---------------------------------------------------------------------------
 * setup
 * ------------------------------------------------------------------------- */

static GPUBuffer*
ibf_buffer(GPUDevice *device, uint64_t size, GPUBufferUsageFlags usage) {
  GPUBufferCreateInfo info   = {0};
  GPUBuffer          *buffer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = size;
  info.usage            = usage;

  return GPUCreateBuffer(device, &info, &buffer) == GPU_OK ? buffer : NULL;
}

static GPUTexture*
ibf_texture(IBFTest *t, GPUTextureDimension dimension, GPUFormat format, uint32_t w, uint32_t h, uint32_t depth,
            uint32_t mips, GPUTextureUsageFlags usage) {
  GPUTextureCreateInfo info    = {0};
  GPUTexture          *texture = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = dimension;
  info.format           = format;
  info.width            = w;
  info.height           = h;
  info.depthOrLayers    = depth;
  info.mipLevelCount    = mips;
  info.sampleCount      = 1u;
  info.usage            = usage | GPU_TEXTURE_USAGE_STORAGE | GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;

  if (GPUCreateTexture(t->device, &info, &texture) != GPU_OK || !texture)
    return NULL;

  t->textures[t->textureCount++] = texture;

  return texture;
}

static GPUTextureView*
ibf_view(IBFTest *t, GPUTexture *texture, GPUTextureViewType type, uint32_t mip, uint32_t layers) {
  GPUTextureViewCreateInfo info = {0};
  GPUTextureView          *view = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.viewType         = type;
  info.format           = texture->format;
  info.baseMipLevel     = mip;
  info.mipLevelCount    = 1u;
  info.arrayLayerCount  = layers;

  if (GPUCreateTextureView(texture, &info, &view) != GPU_OK || !view)
    return NULL;

  t->views[t->viewCount++] = view;

  return view;
}

static bool
ibf_library(IBFTest *t, uint32_t index, const char *path) {
  void    *bytecode;
  uint64_t size;
  bool     ok;

  if (!(bytecode = gpu_test_read_file(path, &size)))
    return false;

  ok = GPUCreateShaderLibraryFromUSL(t->device, bytecode, size, &t->libraries[index]) == GPU_OK
       && GPUCreateShaderLayout(t->device, t->libraries[index], &t->layouts[index]) == GPU_OK;
  free(bytecode);

  return ok;
}

/* a 2d array view of 'count' layers from 'layer' */

static GPUTextureView*
ibf_layers(IBFTest *t, GPUTexture *texture, uint32_t layer, uint32_t count) {
  GPUTextureViewCreateInfo info = {0};
  GPUTextureView          *view = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  info.format           = texture->format;
  info.mipLevelCount    = 1u;
  info.baseArrayLayer   = layer;
  info.arrayLayerCount  = count;

  if (GPUCreateTextureView(texture, &info, &view) != GPU_OK || !view)
    return NULL;

  t->views[t->viewCount++] = view;

  return view;
}

static GPUComputePipeline*
ibf_compute(IBFTest *t, uint32_t index, const char *entry) {
  GPUComputePipelineCreateInfo info     = {0};
  GPUComputePipeline          *pipeline = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = t->layouts[index]->pipelineLayout;
  info.library          = t->libraries[index];
  info.entryPoint       = entry;

  return GPUCreateComputePipeline(t->device, &info, &pipeline) == GPU_OK ? pipeline : NULL;
}

static GPURenderPipeline*
ibf_renderPipeline(IBFTest *t) {
  GPURenderPipelineCreateInfo info     = {0};
  GPUColorTargetState         target   = {0};
  GPURenderPipeline          *pipeline = NULL;

  target.format                = GPU_FORMAT_RGBA8_UNORM;
  target.blend.writeMask       = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "image-buffer-render";
  info.layout                  = t->layouts[IBF_RENDER]->pipelineLayout;
  info.library                 = t->libraries[IBF_RENDER];
  info.vertexEntry             = "image_buffer_render_vs";
  info.fragmentEntry           = "image_buffer_render_fs";
  info.colorTargetCount        = 1u;
  info.pColorTargets           = &target;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;

  return GPUCreateRenderPipeline(t->device, &info, &pipeline) == GPU_OK ? pipeline : NULL;
}

/* a group of storage texture views at bindings 0.., and the sizes buffer after them when asked */

static GPUBindGroup*
ibf_group(IBFTest *t, uint32_t index, GPUTextureView *const *views, uint32_t count, bool sizes) {
  GPUBindGroupCreateInfo info  = {0};
  GPUBindGroupEntry      entries[8];
  GPUBindGroup          *group = NULL;
  uint32_t               i;

  memset(entries, 0, sizeof(entries));

  for (i = 0u; i < count; i++) {
    entries[i].binding     = i;
    entries[i].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    entries[i].textureView = views[i];
  }

  if (sizes) {
    entries[count].binding       = count;
    entries[count].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[count].buffer.buffer = t->sizes;
    entries[count].buffer.size   = 16u;
    count++;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = t->layouts[index]->bindGroupLayouts[0];
  info.entryCount       = count;
  info.pEntries         = entries;

  if (GPUCreateBindGroup(t->device, &info, &group) != GPU_OK || !group)
    return NULL;

  t->groups[t->groupCount++] = group;

  return group;
}

static bool
ibf_upload(IBFTest *t, GPUTexture *texture, uint32_t mip, uint32_t w, uint32_t h, uint32_t depth, uint32_t layers,
           uint32_t texelBytes, const void *data) {
  GPUTextureWriteRegion write = {0};

  write.width        = w;
  write.height       = h;
  write.depth        = depth;
  write.mipLevel     = mip;
  write.layerCount   = layers;
  write.bytesPerRow  = w * texelBytes;
  write.rowsPerImage = h;

  return GPUQueueWriteTexture(t->queue, texture, &write, data, (uint64_t)w * h * depth * layers * texelBytes) == GPU_OK;
}

/* ---------------------------------------------------------------------------
 * commands
 * ------------------------------------------------------------------------- */

static void
ibf_dispatch(GPUCommandBuffer *cmdb, GPUComputePipeline *pipeline, GPUBindGroup *group, uint32_t x, uint32_t y,
             uint32_t z) {
  GPUComputePassEncoder *pass;

  if (!(pass = GPUBeginComputePass(cmdb, "image-buffer-formats")))
    return;

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, x, y, z);
  GPUEndComputePass(pass);
}

static void
ibf_draw(GPUCommandBuffer *cmdb, IBFTest *t, GPUTextureView *target, GPUBindGroup *group) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPURenderPassEncoder        *pass;
  GPUViewport                  viewport = {0};

  color.view                  = target;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  info.chain.sType            = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize       = sizeof(info);
  info.label                  = "image-buffer-render";
  info.colorAttachmentCount   = 1u;
  info.pColorAttachments      = &color;
  viewport.width              = (float)IBF_RT;
  viewport.height             = (float)IBF_RT;
  viewport.maxDepth           = 1.0f;

  if (!(pass = GPUBeginRenderPass(cmdb, &info)))
    return;

  GPUBindRenderPipeline(pass, t->render);
  GPUBindRenderGroup(pass, 0u, group, 0u, NULL);
  GPUSetViewport(pass, &viewport);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
}

/* a mip level's texels (every layer or slice) into readback slot 'slot', rows IBF_ROW apart */

static void
ibf_copyOut(GPUTransferPassEncoder *copy, IBFTest *t, GPUTexture *texture, uint32_t slot, uint32_t mip, uint32_t w,
            uint32_t h, uint32_t depth, uint32_t layers) {
  GPUBufferTextureCopyRegion region = {0};

  region.texture.texture.mipLevel = mip;
  region.texture.width            = w;
  region.texture.height           = h;
  region.texture.depth            = depth;
  region.texture.layerCount       = layers;
  region.bufferOffset             = (uint64_t)slot * IBF_SLOT;
  region.bytesPerRow              = IBF_ROW;
  region.rowsPerImage             = h;
  GPUCopyTextureToBuffer(copy, texture, t->readback, &region);
}

static const uint8_t*
ibf_texel(const uint8_t *readback, uint32_t slot, uint32_t row, uint32_t x, uint32_t bytes) {
  return readback + (uint64_t)slot * IBF_SLOT + (uint64_t)row * IBF_ROW + (uint64_t)x * bytes;
}

#define IBF_EXPECT(cond, ...)                                                                                          \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "image buffer formats: " __VA_ARGS__);                                                           \
      fputc('\n', stderr);                                                                                             \
      return 0;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int
ibf_run(IBFTest *t) {
  GPUTexture             *bytes, *halves, *ones, *packed, *shorts, *words, *volume, *mipped, *line, *lines, *counts,
                         *target;
  GPUTextureView         *views[6], *rtView;
  GPUBindGroup           *formats, *volumeGroup, *levels[2], *lineGroup, *render;
  GPUQueueSubmitInfo      submit = {0};
  GPUTransferPassEncoder *copy;
  GPUCommandBuffer       *cmdb   = NULL;
  GPUFence               *fence  = NULL;
  static uint8_t          readback[IBF_SLOT * 16u];
  uint8_t                 u8[IBF_W * IBF_H];
  uint16_t                h4[IBF_W * IBF_H * 4u], h1[IBF_W * IBF_H];
  uint32_t                k[IBF_W * IBF_H], vol[IBF_VOL_W * IBF_VOL_H * IBF_VOL_D];
  int8_t                  s2[IBF_W * IBF_H * 2u];
  int16_t                 w1[IBF_W * IBF_H];
  uint8_t                 mipData[IBF_MIP_W * IBF_MIP_H * 4u], lineData[IBF_LINE_W * 4u];
  uint32_t                linesData[IBF_LINES_W * IBF_LINES], countData[IBF_RT * IBF_RT], sizes[4];
  uint32_t                x, y, z, c, m, i, mw, mh;
  float                   f;
  int                     ok;

  /* textures and their first texels */

  ok = (bytes = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_R8_UNORM, IBF_W, IBF_H, 1u, 1u, 0u))
       && (halves = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_RGBA16_FLOAT, IBF_W, IBF_H, 1u, 1u, 0u))
       && (ones = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_R16_FLOAT, IBF_W, IBF_H, 1u, 1u, 0u))
       && (packed = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_RG11B10_UFLOAT, IBF_W, IBF_H, 1u, 1u, 0u))
       && (shorts = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_RG8_SNORM, IBF_W, IBF_H, 1u, 1u, 0u))
       && (words = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_R16_SINT, IBF_W, IBF_H, 1u, 1u, 0u))
       && (volume = ibf_texture(t, GPU_TEXTURE_DIMENSION_3D, GPU_FORMAT_R32_FLOAT, IBF_VOL_W, IBF_VOL_H, IBF_VOL_D, 1u,
                                0u))
       && (mipped = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_RGBA8_UNORM, IBF_MIP_W, IBF_MIP_H, 1u, IBF_MIPS,
                                0u))
       && (line = ibf_texture(t, GPU_TEXTURE_DIMENSION_1D, GPU_FORMAT_RGBA8_UINT, IBF_LINE_W, 1u, 1u, 1u, 0u))
       && (lines = ibf_texture(t, GPU_TEXTURE_DIMENSION_1D, GPU_FORMAT_R32_UINT, IBF_LINES_W, 1u, IBF_LINES, 1u, 0u))
       && (counts = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_R32_FLOAT, IBF_RT, IBF_RT, 1u, 1u, 0u))
       && (target = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_RGBA8_UNORM, IBF_RT, IBF_RT, 1u, 1u,
                                GPU_TEXTURE_USAGE_COLOR_TARGET));
  IBF_EXPECT(ok, "textures");

  for (y = 0u; y < IBF_H; y++) {
    for (x = 0u; x < IBF_W; x++) {
      i        = y * IBF_W + x;
      u8[i]    = (uint8_t)ibf_byteValue(x, y);
      h1[i]    = (uint16_t)ibf_small(ibf_one(x, y), 10u, true);
      k[i]     = ibf_packedBits(ibf_packed(x, y, 0u), ibf_packed(x, y, 1u), ibf_packed(x, y, 2u));
      w1[i]    = ibf_word(x, y);
      s2[i * 2u]      = ibf_short(x, y, 0u);
      s2[i * 2u + 1u] = ibf_short(x, y, 1u);

      for (c = 0u; c < 4u; c++)
        h4[i * 4u + c] = (uint16_t)ibf_small(ibf_half(x, y, c), 10u, true);
    }
  }

  for (i = 0u; i < IBF_VOL_W * IBF_VOL_H * IBF_VOL_D; i++) {
    f = 0.5f * (float)i;
    memcpy(&vol[i], &f, 4u);
  }

  for (i = 0u; i < sizeof(mipData); i++)
    mipData[i] = (uint8_t)(i * 7u + 1u);

  for (i = 0u; i < sizeof(lineData); i++)
    lineData[i] = (uint8_t)(i * 3u);

  for (i = 0u; i < IBF_LINES_W * IBF_LINES; i++)
    linesData[i] = i + 10u;

  for (i = 0u; i < IBF_RT * IBF_RT; i++) {
    f = (float)i;
    memcpy(&countData[i], &f, 4u);
  }

  ok = ibf_upload(t, bytes, 0u, IBF_W, IBF_H, 1u, 1u, 1u, u8) && ibf_upload(t, halves, 0u, IBF_W, IBF_H, 1u, 1u, 8u, h4)
       && ibf_upload(t, ones, 0u, IBF_W, IBF_H, 1u, 1u, 2u, h1) && ibf_upload(t, packed, 0u, IBF_W, IBF_H, 1u, 1u, 4u, k)
       && ibf_upload(t, shorts, 0u, IBF_W, IBF_H, 1u, 1u, 2u, s2) && ibf_upload(t, words, 0u, IBF_W, IBF_H, 1u, 1u, 2u, w1)
       && ibf_upload(t, volume, 0u, IBF_VOL_W, IBF_VOL_H, IBF_VOL_D, 1u, 4u, vol)
       && ibf_upload(t, line, 0u, IBF_LINE_W, 1u, 1u, 1u, 4u, lineData)
       && ibf_upload(t, lines, 0u, IBF_LINES_W, 1u, 1u, IBF_LINES, 4u, linesData)
       && ibf_upload(t, counts, 0u, IBF_RT, IBF_RT, 1u, 1u, 4u, countData);

  for (m = 0u; ok && m < IBF_MIPS; m++)
    ok = ibf_upload(t, mipped, m, IBF_MIP_W >> m, IBF_MIP_H >> m, 1u, 1u, 4u, mipData);

  IBF_EXPECT(ok, "uploads");

  /* groups */

  ok = (views[0] = ibf_view(t, bytes, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (views[1] = ibf_view(t, halves, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (views[2] = ibf_view(t, ones, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (views[3] = ibf_view(t, packed, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (views[4] = ibf_view(t, shorts, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (views[5] = ibf_view(t, words, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (formats = ibf_group(t, IBF_FORMATS, views, 6u, false));
  IBF_EXPECT(ok, "format group");

  ok = (views[0] = ibf_view(t, volume, GPU_TEXTURE_VIEW_3D, 0u, 1u))
       && (volumeGroup = ibf_group(t, IBF_VOLUME, views, 1u, true))
       && (views[0] = ibf_view(t, mipped, GPU_TEXTURE_VIEW_2D, 1u, 1u))
       && (levels[0] = ibf_group(t, IBF_LEVEL, views, 1u, true))
       && (views[0] = ibf_view(t, mipped, GPU_TEXTURE_VIEW_2D, 2u, 1u))
       && (levels[1] = ibf_group(t, IBF_LEVEL, views, 1u, true))
       && (views[0] = ibf_view(t, line, GPU_TEXTURE_VIEW_1D, 0u, 1u))
       && (views[1] = ibf_view(t, lines, GPU_TEXTURE_VIEW_1D_ARRAY, 0u, IBF_LINES))
       && (lineGroup = ibf_group(t, IBF_LINE, views, 2u, false))
       && (views[0] = ibf_view(t, counts, GPU_TEXTURE_VIEW_2D, 0u, 1u))
       && (render = ibf_group(t, IBF_RENDER, views, 1u, false))
       && (rtView = ibf_view(t, target, GPU_TEXTURE_VIEW_2D, 0u, 1u));
  IBF_EXPECT(ok, "groups");

  /* the commands: each kernel once, the 3d one after the sizes are read below; two levels in turn; two draws */

  IBF_EXPECT(GPUAcquireCommandBuffer(t->queue, "image-buffer-formats", &cmdb) == GPU_OK && cmdb, "command buffer");

  ibf_dispatch(cmdb, t->formats, formats, (IBF_W + 7u) / 8u, (IBF_H + 7u) / 8u, 1u);
  ibf_dispatch(cmdb, t->level, levels[0], 1u, 1u, 1u);
  ibf_dispatch(cmdb, t->level, levels[1], 1u, 1u, 1u);
  ibf_dispatch(cmdb, t->line, lineGroup, (IBF_LINE_W + 15u) / 16u, 1u, 1u);
  ibf_draw(cmdb, t, rtView, render);
  GPUTransitionTexture(cmdb, counts, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE);
  ibf_draw(cmdb, t, rtView, render);
  ibf_dispatch(cmdb, t->volume, volumeGroup, (IBF_VOL_W + 3u) / 4u, 1u, 1u);

  GPUTransitionTexture(cmdb, counts, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);

  IBF_EXPECT((copy = GPUBeginTransferPass(cmdb, "image-buffer-formats-readback")), "transfer pass");
  ibf_copyOut(copy, t, bytes, 0u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, halves, 1u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, ones, 2u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, packed, 3u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, shorts, 4u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, words, 5u, 0u, IBF_W, IBF_H, 1u, 1u);
  ibf_copyOut(copy, t, volume, 6u, 0u, IBF_VOL_W, IBF_VOL_H, IBF_VOL_D, 1u);

  for (m = 0u; m < IBF_MIPS; m++)
    ibf_copyOut(copy, t, mipped, 7u + m, m, IBF_MIP_W >> m, IBF_MIP_H >> m, 1u, 1u);

  ibf_copyOut(copy, t, line, 10u, 0u, IBF_LINE_W, 1u, 1u, 1u);
  ibf_copyOut(copy, t, lines, 11u, 0u, IBF_LINES_W, 1u, 1u, IBF_LINES);
  ibf_copyOut(copy, t, counts, 12u, 0u, IBF_RT, IBF_RT, 1u, 1u);
  GPUEndTransferPass(copy);

  IBF_EXPECT(GPUCreateFence(t->device, NULL, &fence) == GPU_OK, "fence");

  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &cmdb;
  submit.fence              = fence;

  ok = GPUQueueSubmit(t->queue, &submit) == GPU_OK && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
       && GPUQueueReadBuffer(t->queue, t->sizes, 0u, sizes, sizeof(sizes)) == GPU_OK
       && GPUQueueReadBuffer(t->queue, t->readback, 0u, readback, sizeof(readback)) == GPU_OK;
  GPUDestroyFence(fence);
  IBF_EXPECT(ok, "submission or readback");

  /* the format kernel */

  for (y = 0u; y < IBF_H; y++) {
    for (x = 0u; x < IBF_W; x++) {
      uint16_t hv;
      uint32_t kv;
      int16_t  wv;

      IBF_EXPECT(*ibf_texel(readback, 0u, y, x, 1u) == 255u - (uint32_t)ibf_byteValue(x, y), "r8unorm %u,%u is %u, expected %u",
                 x, y, *ibf_texel(readback, 0u, y, x, 1u), 255u - (uint32_t)ibf_byteValue(x, y));

      for (c = 0u; c < 4u; c++) {
        memcpy(&hv, ibf_texel(readback, 1u, y, x, 8u) + c * 2u, 2u);
        IBF_EXPECT(hv == ibf_small(2.0f * ibf_half(x, y, c), 10u, true), "rgba16float %u,%u.%u is %04x, expected %04x",
                   x, y, c, hv, ibf_small(2.0f * ibf_half(x, y, c), 10u, true));
      }

      memcpy(&hv, ibf_texel(readback, 2u, y, x, 2u), 2u);
      IBF_EXPECT(hv == ibf_small(-ibf_one(x, y), 10u, true), "r16float %u,%u is %04x, expected %04x", x, y, hv,
                 ibf_small(-ibf_one(x, y), 10u, true));

      memcpy(&kv, ibf_texel(readback, 3u, y, x, 4u), 4u);
      IBF_EXPECT(kv == ibf_packedBits(ibf_packed(x, y, 2u), ibf_packed(x, y, 0u), ibf_packed(x, y, 1u)),
                 "rg11b10ufloat %u,%u is %08x, expected %08x", x, y, kv,
                 ibf_packedBits(ibf_packed(x, y, 2u), ibf_packed(x, y, 0u), ibf_packed(x, y, 1u)));

      IBF_EXPECT((int8_t)ibf_texel(readback, 4u, y, x, 2u)[0] == ibf_short(x, y, 1u)
                 && (int8_t)ibf_texel(readback, 4u, y, x, 2u)[1] == ibf_short(x, y, 0u),
                 "rg8snorm %u,%u is %d,%d, expected %d,%d", x, y, (int8_t)ibf_texel(readback, 4u, y, x, 2u)[0],
                 (int8_t)ibf_texel(readback, 4u, y, x, 2u)[1], ibf_short(x, y, 1u), ibf_short(x, y, 0u));

      memcpy(&wv, ibf_texel(readback, 5u, y, x, 2u), 2u);
      IBF_EXPECT(wv == (int16_t)(3 - ibf_word(x, y)), "r16sint %u,%u is %d, expected %d", x, y, wv, 3 - ibf_word(x, y));
    }
  }

  /* the 3d image and its sizes */

  IBF_EXPECT(sizes[0] == IBF_VOL_W && sizes[1] == IBF_VOL_H && sizes[2] == IBF_VOL_D, "3d sizes %u %u %u", sizes[0],
             sizes[1], sizes[2]);

  for (z = 0u; z < IBF_VOL_D; z++) {
    for (y = 0u; y < IBF_VOL_H; y++) {
      for (x = 0u; x < IBF_VOL_W; x++) {
        i = (z * IBF_VOL_H + y) * IBF_VOL_W + x;
        memcpy(&f, ibf_texel(readback, 6u, z * IBF_VOL_H + y, x, 4u), 4u);
        IBF_EXPECT(f == 0.5f * (float)i * 2.0f + (float)z, "3d %u,%u,%u is %g, expected %g", x, y, z, f,
                   0.5f * (float)i * 2.0f + (float)z);
      }
    }
  }

  /* levels 1 and 2 swizzled once each, level 0 as uploaded */

  for (m = 0u; m < IBF_MIPS; m++) {
    mw = IBF_MIP_W >> m;
    mh = IBF_MIP_H >> m;

    for (y = 0u; y < mh; y++) {
      for (x = 0u; x < mw; x++) {
        const uint8_t *texel = ibf_texel(readback, 7u + m, y, x, 4u);

        for (c = 0u; c < 4u; c++) {
          uint8_t expected = mipData[(y * mw + x) * 4u + (m ? (c + 1u) & 3u : c)];

          IBF_EXPECT(texel[c] == expected, "mip %u at %u,%u.%u is %u, expected %u", m, x, y, c, texel[c], expected);
        }
      }
    }
  }

  /* 1d: wzyx, and a 1d array's texels times 3 plus their layer */

  for (x = 0u; x < IBF_LINE_W; x++)
    for (c = 0u; c < 4u; c++)
      IBF_EXPECT(ibf_texel(readback, 10u, 0u, x, 4u)[c] == lineData[x * 4u + 3u - c], "1d %u.%u is %u, expected %u", x,
                 c, ibf_texel(readback, 10u, 0u, x, 4u)[c], lineData[x * 4u + 3u - c]);

  for (y = 0u; y < IBF_LINES; y++) {
    for (x = 0u; x < IBF_LINES_W; x++) {
      uint32_t v;

      memcpy(&v, ibf_texel(readback, 11u, y, x, 4u), 4u);
      IBF_EXPECT(v == linesData[y * IBF_LINES_W + x] * 3u + y, "1d array %u layer %u is %u, expected %u", x, y, v,
                 linesData[y * IBF_LINES_W + x] * 3u + y);
    }
  }

  /* two draws: v * 2 + 1, twice */

  for (y = 0u; y < IBF_RT; y++) {
    for (x = 0u; x < IBF_RT; x++) {
      float start = (float)(y * IBF_RT + x);

      memcpy(&f, ibf_texel(readback, 12u, y, x, 4u), 4u);
      IBF_EXPECT(f == (start * 2.0f + 1.0f) * 2.0f + 1.0f, "fragment %u,%u is %g, expected %g", x, y, f,
                 (start * 2.0f + 1.0f) * 2.0f + 1.0f);
    }
  }

  /* uploads on the queue while the buffers are newer: rows 1-2 of the
   * float16 image whole, four texels of the r8unorm one; the kernel again */

  {
    GPUTextureWriteRegion write = {0};
    uint16_t              rows[IBF_W * 2u * 4u];
    uint8_t               four[4] = {10u, 20u, 30u, 40u};

    for (i = 0u; i < IBF_W * 2u * 4u; i++)
      rows[i] = (uint16_t)ibf_small(0.25f + 0.5f * (float)(i & 3u), 10u, true);

    write.width = IBF_W, write.height = 2u, write.depth = 1u, write.layerCount = 1u, write.y = 1u;
    write.bytesPerRow = IBF_W * 8u;
    IBF_EXPECT(GPUQueueWriteTexture(t->queue, halves, &write, rows, sizeof(rows)) == GPU_OK, "upload rows");

    memset(&write, 0, sizeof(write));
    write.width = 4u, write.height = 1u, write.depth = 1u, write.layerCount = 1u, write.x = 4u;
    write.bytesPerRow = 4u;
    IBF_EXPECT(GPUQueueWriteTexture(t->queue, bytes, &write, four, sizeof(four)) == GPU_OK, "upload texels");

    cmdb = NULL;
    IBF_EXPECT(GPUAcquireCommandBuffer(t->queue, "image-buffer-formats-2", &cmdb) == GPU_OK && cmdb, "command buffer 2");

    /* the copies out left them for transfers; back to read-write use */

    for (i = 0u; i < 6u; i++)
      GPUTransitionTexture(cmdb, t->textures[i], GPU_ACCESS_TRANSFER_READ, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE);

    ibf_dispatch(cmdb, t->formats, formats, (IBF_W + 7u) / 8u, (IBF_H + 7u) / 8u, 1u);
    GPUTransitionTexture(cmdb, halves, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);
    GPUTransitionTexture(cmdb, bytes, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);
    IBF_EXPECT((copy = GPUBeginTransferPass(cmdb, "image-buffer-formats-readback-2")), "transfer pass 2");
    ibf_copyOut(copy, t, bytes, 0u, 0u, IBF_W, IBF_H, 1u, 1u);
    ibf_copyOut(copy, t, halves, 1u, 0u, IBF_W, IBF_H, 1u, 1u);
    GPUEndTransferPass(copy);
    IBF_EXPECT(GPUCreateFence(t->device, NULL, &fence) == GPU_OK, "fence 2");
    submit.ppCommandBuffers = &cmdb;
    submit.fence            = fence;
    ok = GPUQueueSubmit(t->queue, &submit) == GPU_OK && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
         && GPUQueueReadBuffer(t->queue, t->readback, 0u, readback, IBF_SLOT * 2u) == GPU_OK;
    GPUDestroyFence(fence);
    IBF_EXPECT(ok, "submission or readback 2");

    for (y = 0u; y < IBF_H; y++) {
      for (x = 0u; x < IBF_W; x++) {
        uint32_t expected = y == 0u && x >= 4u && x < 8u ? 255u - four[x - 4u] : (uint32_t)ibf_byteValue(x, y);

        IBF_EXPECT(*ibf_texel(readback, 0u, y, x, 1u) == expected, "r8unorm after upload %u,%u is %u, expected %u", x, y,
                   *ibf_texel(readback, 0u, y, x, 1u), expected);

        for (c = 0u; c < 4u; c++) {
          uint16_t hv;
          float    v = y == 1u || y == 2u ? 2.0f * (0.25f + 0.5f * (float)c) : 4.0f * ibf_half(x, y, c);

          memcpy(&hv, ibf_texel(readback, 1u, y, x, 8u) + c * 2u, 2u);
          IBF_EXPECT(hv == ibf_small(v, 10u, true), "rgba16float after upload %u,%u.%u is %04x, expected %04x", x, y, c,
                     hv, ibf_small(v, 10u, true));
        }
      }
    }
  }

  /* views of some layers of a 3-layer array: layer 1 alone, all three, layers
   * 1-2; each adds 1000 times the layers it reports to its first layer */

  {
    GPUTexture     *array;
    GPUTextureView *one, *all, *two;
    GPUBindGroup   *gOne, *gAll, *gTwo;
    uint32_t        start[5u * 4u * 3u], v, expected;

    for (i = 0u; i < 5u * 4u * 3u; i++)
      start[i] = i;

    ok = (array = ibf_texture(t, GPU_TEXTURE_DIMENSION_2D, GPU_FORMAT_R32_UINT, 5u, 4u, 3u, 1u, 0u))
         && ibf_upload(t, array, 0u, 5u, 4u, 1u, 3u, 4u, start) && (one = ibf_layers(t, array, 1u, 1u))
         && (all = ibf_layers(t, array, 0u, 3u)) && (two = ibf_layers(t, array, 1u, 2u))
         && (gOne = ibf_group(t, IBF_LAYERS, &one, 1u, false)) && (gAll = ibf_group(t, IBF_LAYERS, &all, 1u, false))
         && (gTwo = ibf_group(t, IBF_LAYERS, &two, 1u, false));
    IBF_EXPECT(ok, "layer views");

    cmdb = NULL;
    IBF_EXPECT(GPUAcquireCommandBuffer(t->queue, "image-buffer-layers", &cmdb) == GPU_OK && cmdb, "command buffer 3");
    ibf_dispatch(cmdb, t->layers, gOne, 1u, 1u, 1u);
    ibf_dispatch(cmdb, t->layers, gAll, 1u, 1u, 1u);
    ibf_dispatch(cmdb, t->layers, gTwo, 1u, 1u, 1u);
    GPUTransitionTexture(cmdb, array, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);
    IBF_EXPECT((copy = GPUBeginTransferPass(cmdb, "image-buffer-layers-readback")), "transfer pass 3");
    ibf_copyOut(copy, t, array, 0u, 0u, 5u, 4u, 1u, 3u);
    GPUEndTransferPass(copy);
    IBF_EXPECT(GPUCreateFence(t->device, NULL, &fence) == GPU_OK, "fence 3");
    submit.ppCommandBuffers = &cmdb;
    submit.fence            = fence;
    ok = GPUQueueSubmit(t->queue, &submit) == GPU_OK && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
         && GPUQueueReadBuffer(t->queue, t->readback, 0u, readback, IBF_SLOT) == GPU_OK;
    GPUDestroyFence(fence);
    IBF_EXPECT(ok, "submission or readback 3");

    for (z = 0u; z < 3u; z++) {
      for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 5u; x++) {
          expected = (z * 4u + y) * 5u + x + (z < 2u ? 3000u : 0u);
          memcpy(&v, ibf_texel(readback, 0u, z * 4u + y, x, 4u), 4u);
          IBF_EXPECT(v == expected, "layer %u at %u,%u is %u, expected %u", z, x, y, v, expected);
        }
      }
    }
  }

  return 1;
}

int
main(int argc, char **argv) {
  IBFTest      t = {0};
  GPUInstance *instance = NULL;
  GPUAdapter  *adapter  = NULL;
  const char  *forced;
  uint32_t     i, emulated;
  int          ok;

  if (argc < 1 + IBF_LIBRARIES) {
    fprintf(stderr, "usage: %s <image_buffer_{formats,volume,level,line,render,layers}.us>\n", argv[0]);
    return 2;
  }

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !(adapter = GPUGetAutoSelectedAdapter(instance))
      || gpu_test_create_device(adapter, NULL, &t.device) != GPU_OK || !t.device) {
    GPUDestroyInstance(instance);
    puts("image buffer formats: no device");
    return 77;
  }

  t.queue = GPUGetQueue(t.device, GPU_QUEUE_GRAPHICS, 0u);
  ok      = t.queue != NULL;

  for (i = 0u; ok && i < IBF_LIBRARIES; i++)
    ok = ibf_library(&t, i, argv[1 + i]);

  ok = ok && (t.formats = ibf_compute(&t, IBF_FORMATS, "image_buffer_formats_cs"))
       && (t.volume = ibf_compute(&t, IBF_VOLUME, "image_buffer_volume_cs"))
       && (t.level = ibf_compute(&t, IBF_LEVEL, "image_buffer_level_cs"))
       && (t.line = ibf_compute(&t, IBF_LINE, "image_buffer_line_cs")) && (t.render = ibf_renderPipeline(&t))
       && (t.layers = ibf_compute(&t, IBF_LAYERS, "image_buffer_layers_cs"))
       && (t.sizes = ibf_buffer(t.device, 16u, GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC))
       && (t.readback = ibf_buffer(t.device, IBF_SLOT * 16u, GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC));

  if (!ok)
    fprintf(stderr, "image buffer formats: setup failed\n");
  else
    ok = ibf_run(&t);

  /* forced: every storage texture emulated (the color target has no buffer) */

  forced   = getenv("GPU_IMAGE_BUFFERS");
  emulated = 0u;

  for (i = 0u; i < t.textureCount; i++)
    emulated += t.textures[i]->_imageBuffer != NULL;

  if (ok && forced && forced[0] == '1' && emulated + 1u != t.textureCount) {
    fprintf(stderr, "image buffer formats: %u of %u textures emulated\n", emulated, t.textureCount - 1u);
    ok = 0;
  }

  printf("image buffer formats (%u emulated): %s\n", emulated, ok ? "ok" : "FAILED");

  for (i = 0u; i < t.groupCount; i++)
    GPUDestroyBindGroup(t.groups[i]);

  for (i = 0u; i < t.viewCount; i++)
    GPUDestroyTextureView(t.views[i]);

  for (i = 0u; i < t.textureCount; i++)
    GPUDestroyTexture(t.textures[i]);

  GPUDestroyBuffer(t.readback);
  GPUDestroyBuffer(t.sizes);
  GPUDestroyRenderPipeline(t.render);
  GPUDestroyComputePipeline(t.layers);
  GPUDestroyComputePipeline(t.line);
  GPUDestroyComputePipeline(t.level);
  GPUDestroyComputePipeline(t.volume);
  GPUDestroyComputePipeline(t.formats);
  for (i = 0u; i < IBF_LIBRARIES; i++) {
    GPUDestroyShaderLayout(t.layouts[i]);
    GPUDestroyShaderLibrary(t.libraries[i]);
  }

  GPUDestroyDevice(t.device);
  GPUDestroyInstance(instance);

  return ok ? 0 : 1;
}
