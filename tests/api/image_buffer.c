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
 * read-write storage textures, native or emulated by a buffer (GPU_IMAGE_BUFFERS=1
 * emulates them where the device has them): an rgba32float image and an
 * rgba8unorm 2d array uploaded, read and written twice, sampled between the
 * two, copied out at the end. both paths must give the same texels.
 */

#include "test.h"
#include "../../src/api/texture_internal.h"

enum {
  IB_WIDTH       = 37u,
  IB_HEIGHT      = 19u,
  IB_LAYER_W     = 13u,
  IB_LAYER_H     = 7u,
  IB_LAYERS      = 2u,
  IB_ROW         = 1024u, /* readback rows: at least 37 * 16 and 256-aligned */
  IB_LAYER_ROW   = 256u,
  IB_COLOR_BYTES = IB_ROW * IB_HEIGHT,
  IB_ARRAY_BYTES = IB_LAYER_ROW * IB_LAYER_H * IB_LAYERS
};

typedef struct IBTest {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUShaderLibrary   *updateLibrary, *sampleLibrary;
  GPUShaderLayout    *updateLayout, *sampleLayout;
  GPUComputePipeline *update, *sample;
  GPUTexture         *color, *array;
  GPUTextureView     *colorStorage, *colorSampled, *arrayStorage;
  GPUBuffer          *sizes, *sampled, *readback;
  GPUBindGroup       *updateGroup, *sampleGroup;
} IBTest;

static float
ib_start(uint32_t x, uint32_t y, uint32_t c) {
  const float base[4] = {0.25f * (float)x, 0.5f * (float)y, 1.0f, 2.0f};

  return base[c];
}

static uint8_t
ib_byte(uint32_t x, uint32_t y, uint32_t layer, uint32_t c) {
  const uint8_t base[4] = {(uint8_t)(x * 3u + layer), (uint8_t)(y * 5u), (uint8_t)(7u * layer), 200u};

  return base[c];
}

static float
ib_updated(uint32_t x, uint32_t y, uint32_t c, uint32_t times) {
  const float add[4] = {(float)x, (float)y, 0.5f, 1.0f};
  float       v      = ib_start(x, y, c);

  while (times--)
    v = v * 2.0f + add[c];

  return v;
}

static GPUBuffer*
ib_buffer(GPUDevice *device, uint64_t size, GPUBufferUsageFlags usage) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer          *buffer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = size;
  info.usage            = usage;

  return GPUCreateBuffer(device, &info, &buffer) == GPU_OK ? buffer : NULL;
}

static GPUTexture*
ib_texture(GPUDevice *device, GPUFormat format, uint32_t w, uint32_t h, uint32_t layers, GPUTextureUsageFlags usage) {
  GPUTextureCreateInfo info    = {0};
  GPUTexture          *texture = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = format;
  info.width            = w;
  info.height           = h;
  info.depthOrLayers    = layers;
  info.mipLevelCount    = 1u;
  info.sampleCount      = 1u;
  info.usage            = usage;

  return GPUCreateTexture(device, &info, &texture) == GPU_OK ? texture : NULL;
}

static GPUTextureView*
ib_view(GPUTexture *texture, GPUFormat format, GPUTextureViewType type, uint32_t layers) {
  GPUTextureViewCreateInfo info = {0};
  GPUTextureView          *view = NULL;

  info.chain.sType     = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.viewType        = type;
  info.format          = format;
  info.mipLevelCount   = 1u;
  info.arrayLayerCount = layers;

  return GPUCreateTextureView(texture, &info, &view) == GPU_OK ? view : NULL;
}

static int
ib_pipeline(IBTest *t, const char *path, const char *entry, GPUShaderLibrary **library,
            GPUShaderLayout **layout, GPUComputePipeline **pipeline) {
  GPUComputePipelineCreateInfo info = {0};
  void                        *bytecode;
  uint64_t                     size;
  int                          ok;

  if (!(bytecode = gpu_test_read_file(path, &size)))
    return 0;

  ok = GPUCreateShaderLibraryFromUSL(t->device, bytecode, size, library) == GPU_OK
       && GPUCreateShaderLayout(t->device, *library, layout) == GPU_OK;
  free(bytecode);

  if (!ok)
    return 0;

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = (*layout)->pipelineLayout;
  info.library          = *library;
  info.entryPoint       = entry;

  return GPUCreateComputePipeline(t->device, &info, pipeline) == GPU_OK;
}

static GPUBindGroup*
ib_group(IBTest *t, GPUShaderLayout *layout, const GPUBindGroupEntry *entries, uint32_t count) {
  GPUBindGroupCreateInfo info  = {0};
  GPUBindGroup          *group = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = layout->bindGroupLayouts[0];
  info.entryCount       = count;
  info.pEntries         = entries;

  return GPUCreateBindGroup(t->device, &info, &group) == GPU_OK ? group : NULL;
}

static void
ib_dispatch(GPUCommandBuffer *cmdb, GPUComputePipeline *pipeline, GPUBindGroup *group, uint32_t w, uint32_t h) {
  GPUComputePassEncoder *pass;

  if (!(pass = GPUBeginComputePass(cmdb, "image-buffer")))
    return;

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, (w + 7u) / 8u, (h + 7u) / 8u, 1u);
  GPUEndComputePass(pass);
}

static void
ib_barrier(GPUCommandBuffer *cmdb, GPUTexture *texture, GPUAccessMask src, GPUAccessMask dst) {
  GPUTransitionTexture(cmdb, texture, src, dst);
}

static int
ib_run(IBTest *t) {
  GPUBufferTextureCopyRegion region = {0};
  GPUTextureWriteRegion      write  = {0};
  GPUBindGroupEntry          entries[3];
  GPUQueueSubmitInfo         submit = {0};
  GPUTransferPassEncoder    *copy;
  GPUCommandBuffer          *cmdb   = NULL;
  GPUFence                  *fence  = NULL;
  float                      colors[IB_WIDTH * IB_HEIGHT * 4u];
  uint8_t                    bytes[IB_LAYER_W * IB_LAYER_H * IB_LAYERS * 4u];
  uint8_t                    readback[IB_COLOR_BYTES + IB_ARRAY_BYTES];
  float                      sampled[IB_WIDTH * IB_HEIGHT * 4u];
  uint32_t                   sizes[4];
  uint32_t                   x, y, c, l;
  const float               *row;
  int                        ok;

  for (y = 0u; y < IB_HEIGHT; y++)
    for (x = 0u; x < IB_WIDTH; x++)
      for (c = 0u; c < 4u; c++)
        colors[(y * IB_WIDTH + x) * 4u + c] = ib_start(x, y, c);

  for (l = 0u; l < IB_LAYERS; l++)
    for (y = 0u; y < IB_LAYER_H; y++)
      for (x = 0u; x < IB_LAYER_W; x++)
        for (c = 0u; c < 4u; c++)
          bytes[((l * IB_LAYER_H + y) * IB_LAYER_W + x) * 4u + c] = ib_byte(x, y, l, c);

  write.width        = IB_WIDTH;
  write.height       = IB_HEIGHT;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = IB_WIDTH * 16u;
  write.rowsPerImage = IB_HEIGHT;

  if (GPUQueueWriteTexture(t->queue, t->color, &write, colors, sizeof(colors)) != GPU_OK)
    return 0;

  write.width        = IB_LAYER_W;
  write.height       = IB_LAYER_H;
  write.layerCount   = IB_LAYERS;
  write.bytesPerRow  = IB_LAYER_W * 4u;
  write.rowsPerImage = IB_LAYER_H;

  if (GPUQueueWriteTexture(t->queue, t->array, &write, bytes, sizeof(bytes)) != GPU_OK)
    return 0;

  memset(entries, 0, sizeof(entries));
  entries[0].binding           = 0u;
  entries[0].bindingType       = GPU_BINDING_STORAGE_TEXTURE;
  entries[0].textureView       = t->colorStorage;
  entries[1].binding           = 1u;
  entries[1].bindingType       = GPU_BINDING_STORAGE_TEXTURE;
  entries[1].textureView       = t->arrayStorage;
  entries[2].binding           = 2u;
  entries[2].bindingType       = GPU_BINDING_STORAGE_BUFFER;
  entries[2].buffer.buffer     = t->sizes;
  entries[2].buffer.size       = 16u;

  if (!(t->updateGroup = ib_group(t, t->updateLayout, entries, 3u)))
    return 0;

  memset(entries, 0, sizeof(entries));
  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  entries[0].textureView   = t->colorSampled;
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = t->sampled;
  entries[1].buffer.size   = sizeof(sampled);

  if (!(t->sampleGroup = ib_group(t, t->sampleLayout, entries, 2u)))
    return 0;

  if (GPUAcquireCommandBuffer(t->queue, "image-buffer", &cmdb) != GPU_OK || !cmdb)
    return 0;

  /* written read-write, sampled, written again, copied out */

  ib_dispatch(cmdb, t->update, t->updateGroup, IB_WIDTH, IB_HEIGHT);
  ib_barrier(cmdb, t->color, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_SHADER_READ);
  ib_dispatch(cmdb, t->sample, t->sampleGroup, IB_WIDTH, IB_HEIGHT);
  ib_barrier(cmdb, t->color, GPU_ACCESS_SHADER_READ, GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE);
  ib_dispatch(cmdb, t->update, t->updateGroup, IB_WIDTH, IB_HEIGHT);
  ib_barrier(cmdb, t->color, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);
  ib_barrier(cmdb, t->array, GPU_ACCESS_SHADER_WRITE, GPU_ACCESS_TRANSFER_READ);

  if (!(copy = GPUBeginTransferPass(cmdb, "image-buffer-readback")))
    return 0;

  region.texture.width      = IB_WIDTH;
  region.texture.height     = IB_HEIGHT;
  region.texture.depth      = 1u;
  region.texture.layerCount = 1u;
  region.bytesPerRow        = IB_ROW;
  region.rowsPerImage       = IB_HEIGHT;
  GPUCopyTextureToBuffer(copy, t->color, t->readback, &region);

  region.texture.width      = IB_LAYER_W;
  region.texture.height     = IB_LAYER_H;
  region.texture.layerCount = IB_LAYERS;
  region.bufferOffset       = IB_COLOR_BYTES;
  region.bytesPerRow        = IB_LAYER_ROW;
  region.rowsPerImage       = IB_LAYER_H;
  GPUCopyTextureToBuffer(copy, t->array, t->readback, &region);
  GPUEndTransferPass(copy);

  if (GPUCreateFence(t->device, NULL, &fence) != GPU_OK)
    return 0;

  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &cmdb;
  submit.fence              = fence;

  ok = GPUQueueSubmit(t->queue, &submit) == GPU_OK && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
       && GPUQueueReadBuffer(t->queue, t->sizes, 0u, sizes, sizeof(sizes)) == GPU_OK
       && GPUQueueReadBuffer(t->queue, t->sampled, 0u, sampled, sizeof(sampled)) == GPU_OK
       && GPUQueueReadBuffer(t->queue, t->readback, 0u, readback, sizeof(readback)) == GPU_OK;
  GPUDestroyFence(fence);

  if (!ok) {
    fprintf(stderr, "image buffer: submission or readback failed\n");
    return 0;
  }

  if (sizes[0] != IB_WIDTH || sizes[1] != IB_HEIGHT || sizes[2] != IB_LAYER_W || sizes[3] != IB_LAYERS) {
    fprintf(stderr, "image buffer: sizes %u %u %u %u\n", sizes[0], sizes[1], sizes[2], sizes[3]);
    return 0;
  }

  for (y = 0u; y < IB_HEIGHT; y++) {
    row = (const float *)(readback + y * IB_ROW);

    for (x = 0u; x < IB_WIDTH; x++) {
      for (c = 0u; c < 4u; c++) {
        if (sampled[(y * IB_WIDTH + x) * 4u + c] != ib_updated(x, y, c, 1u) + 1.0f
            || row[x * 4u + c] != ib_updated(x, y, c, 2u)) {
          fprintf(stderr, "image buffer: rgba32float %u,%u.%u sampled %g read %g, expected %g and %g\n",
                  x, y, c, sampled[(y * IB_WIDTH + x) * 4u + c], row[x * 4u + c],
                  ib_updated(x, y, c, 1u) + 1.0f, ib_updated(x, y, c, 2u));
          return 0;
        }
      }
    }
  }

  /* yzwx twice: zwxy */

  for (l = 0u; l < IB_LAYERS; l++) {
    for (y = 0u; y < IB_LAYER_H; y++) {
      for (x = 0u; x < IB_LAYER_W; x++) {
        const uint8_t *texel = readback + IB_COLOR_BYTES + (l * IB_LAYER_H + y) * IB_LAYER_ROW + x * 4u;

        for (c = 0u; c < 4u; c++) {
          if (texel[c] != ib_byte(x, y, l, (c + 2u) & 3u)) {
            fprintf(stderr, "image buffer: rgba8unorm layer %u at %u,%u.%u is %u, expected %u\n",
                    l, x, y, c, texel[c], ib_byte(x, y, l, (c + 2u) & 3u));
            return 0;
          }
        }
      }
    }
  }

  return 1;
}

int
main(int argc, char **argv) {
  IBTest       t = {0};
  GPUInstance *instance = NULL;
  GPUAdapter  *adapter  = NULL;
  const char  *forced;
  int          ok;

  if (argc < 3) {
    fprintf(stderr, "usage: %s <image_buffer.us> <image_buffer_sample.us>\n", argv[0]);
    return 2;
  }

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !(adapter = GPUGetAutoSelectedAdapter(instance))
      || gpu_test_create_device(adapter, NULL, &t.device) != GPU_OK || !t.device) {
    GPUDestroyInstance(instance);
    puts("image buffer: no device");
    return 77;
  }

  t.queue = GPUGetQueue(t.device, GPU_QUEUE_GRAPHICS, 0u);
  ok      = t.queue
            && ib_pipeline(&t, argv[1], "image_buffer_update_cs", &t.updateLibrary, &t.updateLayout, &t.update)
            && ib_pipeline(&t, argv[2], "image_buffer_sample_cs", &t.sampleLibrary, &t.sampleLayout, &t.sample)
            && (t.color = ib_texture(t.device, GPU_FORMAT_RGBA32_FLOAT, IB_WIDTH, IB_HEIGHT, 1u,
                                     GPU_TEXTURE_USAGE_STORAGE | GPU_TEXTURE_USAGE_SAMPLED
                                     | GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST))
            && (t.array = ib_texture(t.device, GPU_FORMAT_RGBA8_UNORM, IB_LAYER_W, IB_LAYER_H, IB_LAYERS,
                                     GPU_TEXTURE_USAGE_STORAGE | GPU_TEXTURE_USAGE_COPY_SRC
                                     | GPU_TEXTURE_USAGE_COPY_DST))
            && (t.colorStorage = ib_view(t.color, GPU_FORMAT_RGBA32_FLOAT, GPU_TEXTURE_VIEW_2D, 1u))
            && (t.colorSampled = ib_view(t.color, GPU_FORMAT_RGBA32_FLOAT, GPU_TEXTURE_VIEW_2D, 1u))
            && (t.arrayStorage = ib_view(t.array, GPU_FORMAT_RGBA8_UNORM, GPU_TEXTURE_VIEW_2D_ARRAY, IB_LAYERS))
            && (t.sizes = ib_buffer(t.device, 16u, GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC))
            && (t.sampled = ib_buffer(t.device, IB_WIDTH * IB_HEIGHT * 16u,
                                      GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC))
            && (t.readback = ib_buffer(t.device, IB_COLOR_BYTES + IB_ARRAY_BYTES,
                                       GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC));

  if (!ok) {
    fprintf(stderr, "image buffer: setup failed\n");
  } else {
    ok = ib_run(&t);

    /* the emulated path when asked for (a buffer each, made by its read-write binding), the native one otherwise */

    forced = getenv("GPU_IMAGE_BUFFERS");

    if (forced && forced[0] == '1' && (!t.color->_imageBuffer || !t.array->_imageBuffer)) {
      fprintf(stderr, "image buffer: textures not emulated\n");
      ok = 0;
    } else if ((!forced || forced[0] != '1') && t.color->_imageBuffer) {
      fprintf(stderr, "image buffer: native rgba32float got a buffer\n");
      ok = 0;
    }
  }

  printf("image buffer (%s): %s\n", t.color && t.color->_imageBuffer ? "emulated" : "native", ok ? "ok" : "FAILED");

  GPUDestroyBindGroup(t.updateGroup);
  GPUDestroyBindGroup(t.sampleGroup);
  GPUDestroyBuffer(t.readback);
  GPUDestroyBuffer(t.sampled);
  GPUDestroyBuffer(t.sizes);
  GPUDestroyTextureView(t.arrayStorage);
  GPUDestroyTextureView(t.colorSampled);
  GPUDestroyTextureView(t.colorStorage);
  GPUDestroyTexture(t.array);
  GPUDestroyTexture(t.color);
  GPUDestroyComputePipeline(t.sample);
  GPUDestroyComputePipeline(t.update);
  GPUDestroyShaderLayout(t.sampleLayout);
  GPUDestroyShaderLayout(t.updateLayout);
  GPUDestroyShaderLibrary(t.sampleLibrary);
  GPUDestroyShaderLibrary(t.updateLibrary);
  GPUDestroyDevice(t.device);
  GPUDestroyInstance(instance);

  return ok ? 0 : 1;
}
