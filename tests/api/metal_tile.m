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

#include <gpu/gpu.h>
#include <gpu/tile.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  FETCH_WIDTH  = 31,
  FETCH_HEIGHT = 19,
  FETCH_ROW    = 512,
  FETCH_IMAGE  = FETCH_HEIGHT * FETCH_ROW
};

typedef struct Target {
  GPUTexture     *texture;
  GPUTextureView *view;
  GPUTexture     *multisample;
  GPUTextureView *multisampleView;
} Target;

static const float kClear[4] = {0.125f, 0.25f, 0.375f, 0.5f};
static const GPUScissorRect kRects[3] = {
  {3, 2, 19, 12}, {0, 8, 31, 10}, {7, 0, 13, 19}
};

static bool
create_target(GPUDevice *device, uint32_t samples, Target *target) {
  GPUTextureCreateInfo     info = {0};
  GPUTextureViewCreateInfo view = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = GPU_FORMAT_RGBA32_FLOAT;
  info.width            = FETCH_WIDTH;
  info.height           = FETCH_HEIGHT;
  info.depthOrLayers    = 1u;
  info.mipLevelCount    = 1u;
  info.sampleCount      = 1u;
  info.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC;

  view.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  view.chain.structSize = sizeof(view);
  view.viewType         = GPU_TEXTURE_VIEW_2D;
  view.format           = info.format;
  view.mipLevelCount    = 1u;
  view.arrayLayerCount  = 1u;

  if (GPUCreateTexture(device, &info, &target->texture) != GPU_OK
      || GPUCreateTextureView(target->texture, &view, &target->view) != GPU_OK)
    return false;

  if (samples == 1u)
    return true;

  info.sampleCount = samples;
  info.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET;
  return GPUCreateTexture(device, &info, &target->multisample) == GPU_OK
         && GPUCreateTextureView(target->multisample, &view, &target->multisampleView) == GPU_OK;
}

static GPUResult
create_draw(GPUDevice *device, GPUShaderLibrary *library, GPUShaderLayout *layout,
            bool explicit, uint32_t samples, GPURenderPipeline **pipeline) {
  GPURenderPipelineCreateInfo info = {0};
  GPUColorTargetState         color = {0};

  color.format          = GPU_FORMAT_RGBA32_FLOAT;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.library                 = library;
  info.layout                  = layout->pipelineLayout;
  info.vertexEntry             = explicit ? "block_vs" : "tile_vs";
  info.fragmentEntry           = explicit ? "block_draw_fs" : "tile_draw_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.multisample.sampleCount = samples;
  info.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(device, &info, pipeline);
}

static GPUResult
create_tile(GPUDevice *device, GPUShaderLibrary *library, const char *entry,
            bool matches, uint32_t samples, GPUTilePipeline **pipeline) {
  GPUTilePipelineCreateInfo info = {0};
  const GPUFormat          format = GPU_FORMAT_RGBA32_FLOAT;

  info.chain.sType                   = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  info.chain.structSize              = sizeof(info);
  info.library                       = library;
  info.entryPoint                    = entry;
  info.pColorFormats                 = &format;
  info.colorAttachmentCount          = 1u;
  info.sampleCount                   = samples;
  info.threadgroupSizeMatchesTileSize = matches;
  return GPUCreateTilePipeline(device, &info, pipeline);
}

static bool
read_targets(GPUDevice        *device,
             GPUCommandBuffer *command,
             const Target     *targets,
             uint32_t          count,
             float            *pixels) {
  GPUTextureBarrier          barriers[2] = {{0}};
  GPUBufferTextureCopyRegion region      = {0};
  GPUBufferCreateInfo        info        = {0};
  GPUBarrierBatch            batch       = {0};
  GPUQueueSubmitInfo         submit      = {0};
  GPUTransferPassEncoder    *pass;
  GPUQueue                  *queue;
  GPUBuffer                 *buffer = NULL;
  GPUFence                  *fence  = NULL;
  uint32_t                   i;
  bool                       ok = false;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.sizeBytes        = (uint64_t)count * FETCH_IMAGE;
  info.usage            = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &info, &buffer) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < count; i++) {
    barriers[i].texture    = targets[i].texture;
    barriers[i].srcAccess  = GPU_ACCESS_COLOR_WRITE;
    barriers[i].dstAccess  = GPU_ACCESS_TRANSFER_READ;
    barriers[i].mipCount   = 1u;
    barriers[i].layerCount = 1u;
  }

  batch.pTextureBarriers     = barriers;
  batch.textureBarrierCount = count;
  batch.srcStages            = GPU_STAGE_FRAGMENT | GPU_STAGE_TILE;
  batch.dstStages            = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(command, &batch);
  if (!(pass = GPUBeginTransferPass(command, "tile-readback")))
    goto cleanup;

  region.bytesPerRow        = FETCH_ROW;
  region.rowsPerImage       = FETCH_HEIGHT;
  region.texture.width      = FETCH_WIDTH;
  region.texture.height     = FETCH_HEIGHT;
  region.texture.depth      = 1u;
  region.texture.layerCount = 1u;
  for (i = 0u; i < count; i++) {
    region.bufferOffset = (uint64_t)i * FETCH_IMAGE;
    GPUCopyTextureToBuffer(pass, targets[i].texture, buffer, &region);
  }
  GPUEndTransferPass(pass);

  submit.ppCommandBuffers   = &command;
  submit.commandBufferCount = 1u;
  submit.fence              = fence;
  if (GPUQueueSubmit(queue, &submit) != GPU_OK)
    goto cleanup;

  command = NULL;
  ok = GPUWaitFence(fence, UINT64_C(5000000000)) == GPU_OK
       && GPUQueueReadBuffer(queue, buffer, 0u, pixels, info.sizeBytes) == GPU_OK;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  return ok;
}

static void
apply_draw(float values[4]) {
  values[0] = values[0] * 0.5f + 0.125f;
  values[1] = values[1] * 0.5f + 0.25f;
  values[2] = values[2] * 0.5f + 0.0625f;
  values[3] = values[3] * 0.5f + 0.125f;
}

static void
apply_tile(float values[4]) {
  float old[4];

  memcpy(old, values, sizeof(old));
  values[0] = old[2] * 0.5f + 0.25f;
  values[1] = old[0] * 0.5f + 0.125f;
  values[2] = old[3] * 0.5f + 0.0625f;
  values[3] = old[1] * 0.5f + 0.25f;
}

static bool
covered(uint32_t x, uint32_t y, const GPUScissorRect *rect) {
  return x >= (uint32_t)rect->x && x < (uint32_t)rect->x + rect->width
         && y >= (uint32_t)rect->y && y < (uint32_t)rect->y + rect->height;
}

static bool
check_tiles(GPUDevice *device, GPUShaderLibrary *library, bool explicit,
            uint32_t requestedTile, bool matches, uint32_t samples) {
  float                       pixels[FETCH_HEIGHT][FETCH_ROW / sizeof(float)];
  float                       expected[4];
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUTileRenderPassEXT         tileInfo = {0};
  GPUTilePipelineProperties    initProps = {0}, resolveProps = {0};
  Target                      target = {0};
  GPUShaderLayout            *layout = NULL;
  GPURenderPipeline          *draw = NULL;
  GPUTilePipeline            *init = NULL, *resolve = NULL, *rejected = NULL;
  const char                 *mode;
  GPUCommandBuffer           *command = NULL;
  GPURenderPassEncoder       *pass = NULL;
  GPUQueue                   *queue;
  uint32_t                    width, height, i, x, y, c, renderBytes;
  bool                        ok = false;

  if (!create_target(device, samples, &target)
      || GPUCreateShaderLayout(device, library, &layout) != GPU_OK
      || create_draw(device, library, layout, explicit, samples, &draw) != GPU_OK
      || create_tile(device, library, explicit ? "block_init_fs" : "tile_transform_fs", matches, samples, &init) != GPU_OK
      || create_tile(device, library, explicit ? "block_resolve_fs" : "tile_transform_fs", matches, samples, &resolve) != GPU_OK
      || GPUGetTilePipelineProperties(init, &initProps) != GPU_OK
      || GPUGetTilePipelineProperties(resolve, &resolveProps) != GPU_OK
      || !initProps.maxThreadsPerThreadgroup || !resolveProps.maxThreadsPerThreadgroup)
    goto cleanup;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUAcquireCommandBuffer(queue, "tile-order", &command) != GPU_OK)
    goto cleanup;

  tileInfo.chain.sType          = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tileInfo.chain.structSize     = sizeof(tileInfo);
  tileInfo.tileWidth            = requestedTile;
  tileInfo.tileHeight           = requestedTile;
  tileInfo.imageblockSampleLength = initProps.imageblockSampleLength;
  if (resolveProps.imageblockSampleLength > tileInfo.imageblockSampleLength)
    tileInfo.imageblockSampleLength = resolveProps.imageblockSampleLength;
  renderBytes = GPUGetRenderPipelineImageblockSampleLength(draw);
  if (renderBytes > tileInfo.imageblockSampleLength)
    tileInfo.imageblockSampleLength = renderBytes;
  if (explicit && tileInfo.imageblockSampleLength == 0u)
    goto cleanup;

  color.view        = target.multisampleView ? target.multisampleView : target.view;
  color.resolveView = target.multisampleView ? target.view : NULL;
  color.loadOp  = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  memcpy(color.clearColor.float32, kClear, sizeof(kClear));

  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.chain.pNext          = &tileInfo;
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  if (!(pass = GPUBeginRenderPass(command, &passInfo))
      || GPUGetRenderPassTileSize(pass, &width, &height) != GPU_OK
      || !width || !height
      || GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK
      || GPUBindTilePipeline(pass, init) != GPU_OK
      || GPUDispatchThreadsPerTile(pass, 0u, height) == GPU_OK
      || GPUDispatchThreadsPerTile(pass, UINT32_MAX, height) == GPU_OK)
    goto cleanup;

  mode = getenv("GPU_METAL_MODE");
  if (explicit && !requestedTile && mode && strcmp(mode, "metal4") == 0) {
    if (create_tile(device, library, "block_init_fs", true, samples, &rejected) != GPU_OK
        || GPUBindTilePipeline(pass, rejected) != GPU_ERROR_UNSUPPORTED)
      goto cleanup;
  }

  if (explicit && GPUDispatchThreadsPerTile(pass, width, height) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < (explicit ? 3u : 2u); i++) {
    GPUBindRenderPipeline(pass, draw);
    if (GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK)
      goto cleanup;

    GPUSetScissor(pass, &kRects[i]);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
    if (!explicit) {
      if (GPUBindTilePipeline(pass, resolve) != GPU_OK
          || GPUDispatchThreadsPerTile(pass, width, height) != GPU_OK)
        goto cleanup;
    }
  }
  if (explicit
      && (GPUBindTilePipeline(pass, resolve) != GPU_OK
          || GPUDispatchThreadsPerTile(pass, width, height) != GPU_OK))
    goto cleanup;

  GPUEndRenderPass(pass);
  pass = NULL;
  ok = read_targets(device, command, &target, 1u, &pixels[0][0]);
  command = NULL;
  if (!ok)
    goto cleanup;

  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      memcpy(expected, kClear, sizeof(expected));
      for (i = 0u; i < (explicit ? 3u : 2u); i++) {
        if (covered(x, y, &kRects[i]))
          apply_draw(expected);
        if (!explicit)
          apply_tile(expected);
      }
      if (explicit)
        apply_tile(expected);
      for (c = 0u; c < 4u; c++) {
        if (pixels[y][x * 4u + c] != expected[c]) {
          fprintf(stderr, "tile explicit=%u requested=%u pixel=%u,%u channel=%u: %a != %a\n",
                  explicit, requestedTile, x, y, c, pixels[y][x * 4u + c], expected[c]);
          ok = false;
          goto cleanup;
        }
      }
    }
  }

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyTilePipeline(resolve);
  GPUDestroyTilePipeline(rejected);
  GPUDestroyTilePipeline(init);
  GPUDestroyRenderPipeline(draw);
  GPUDestroyShaderLayout(layout);
  GPUDestroyTextureView(target.multisampleView);
  GPUDestroyTexture(target.multisample);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile explicit=%u requested=%u samples=%u exact CPU oracle=%s\n",
         explicit, requestedTile, samples, ok ? "pass" : "fail");
  return ok;
}

static bool
check_invalid_passes(GPUDevice *device, GPUShaderLibrary *library,
                     bool explicit, bool multisampleSupported) {
  float                       pixels[FETCH_HEIGHT][FETCH_ROW / sizeof(float)];
  float                       expected[4];
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUTileRenderPassEXT         tileInfo = {0}, duplicate;
  GPUTilePipelineCreateInfo    tileCreate = {0};
  GPUTilePipelineProperties    properties;
  const GPUFormat             formats[2] = {GPU_FORMAT_RGBA32_FLOAT,
                                            GPU_FORMAT_RGBA32_FLOAT};
  Target                      target = {0};
  GPUShaderLayout            *layout = NULL;
  GPURenderPipeline          *draw = NULL;
  GPUTilePipeline            *tile = NULL, *wrongCount = NULL, *wrongSample = NULL;
  GPUCommandBuffer           *command = NULL;
  GPURenderPassEncoder       *pass = NULL;
  GPUQueue                   *queue;
  const char                 *entry;
  uint32_t                    storage, width, height, i, x, y, c;
  bool                        ok = false;

  entry = explicit ? "block_init_fs" : "tile_transform_fs";
  if (!create_target(device, 1u, &target)
      || GPUCreateShaderLayout(device, library, &layout) != GPU_OK
      || create_draw(device, library, layout, explicit, 1u, &draw) != GPU_OK
      || create_tile(device, library, entry, true, 1u, &tile) != GPU_OK
      || GPUGetTilePipelineProperties(tile, &properties) != GPU_OK)
    goto cleanup;

  storage = GPUGetRenderPipelineImageblockSampleLength(draw);
  if (properties.imageblockSampleLength > storage)
    storage = properties.imageblockSampleLength;
  if (explicit && !storage)
    goto cleanup;

  tileCreate.chain.sType                   = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tileCreate.chain.structSize              = sizeof(tileCreate);
  tileCreate.library                       = library;
  tileCreate.entryPoint                    = entry;
  tileCreate.pColorFormats                 = formats;
  tileCreate.colorAttachmentCount          = 2u;
  tileCreate.sampleCount                   = 1u;
  tileCreate.threadgroupSizeMatchesTileSize = true;
  if (GPUCreateTilePipeline(device, &tileCreate, &wrongCount) != GPU_OK
      || (multisampleSupported
          && create_tile(device, library, entry, true, 4u, &wrongSample) != GPU_OK))
    goto cleanup;

  tileInfo.chain.sType          = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tileInfo.chain.structSize     = sizeof(tileInfo);
  tileInfo.tileWidth            = 16u;
  tileInfo.tileHeight           = 16u;
  tileInfo.imageblockSampleLength = storage;
  color.view                   = target.view;
  color.loadOp                 = GPU_LOAD_OP_CLEAR;
  color.storeOp                = GPU_STORE_OP_STORE;
  memcpy(color.clearColor.float32, kClear, sizeof(kClear));
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);

  /* failed binds must not issue an imageblock draw against absent storage. */
  for (i = 0u; i < (explicit ? 2u : 1u); i++) {
    if (GPUAcquireCommandBuffer(queue, "tile-invalid-storage", &command) != GPU_OK)
      goto cleanup;

    passInfo.chain.pNext = i ? &tileInfo : NULL;
    tileInfo.imageblockSampleLength = 0u;
    if (!(pass = GPUBeginRenderPass(command, &passInfo)))
      goto cleanup;

    if (GPUBindTilePipeline(pass, tile) != (i ? GPU_ERROR_INVALID_ARGUMENT : GPU_ERROR_UNSUPPORTED))
      goto cleanup;
    if (!i && GPUGetRenderPassTileSize(pass, &width, &height) != GPU_ERROR_UNSUPPORTED)
      goto cleanup;
    if (explicit) {
      GPUBindRenderPipeline(pass, draw);
      GPUDraw(pass, 3u, 1u, 0u, 0u);
    }
    GPUEndRenderPass(pass);
    pass = NULL;
    ok = read_targets(device, command, &target, 1u, &pixels[0][0]);
    command = NULL;
    if (!ok)
      goto cleanup;

    for (y = 0u; y < FETCH_HEIGHT; y++) {
      for (x = 0u; x < FETCH_WIDTH; x++) {
        for (c = 0u; c < 4u; c++) {
          if (pixels[y][x * 4u + c] != kClear[c]) {
            ok = false;
            goto cleanup;
          }
        }
      }
    }
    ok = false;
  }

  tileInfo.imageblockSampleLength = storage;
  passInfo.chain.pNext = &tileInfo;
  if (GPUAcquireCommandBuffer(queue, "tile-invalid-config", &command) != GPU_OK)
    goto cleanup;

  duplicate = tileInfo;
  tileInfo.chain.pNext = &duplicate;
  if ((pass = GPUBeginRenderPass(command, &passInfo)))
    goto cleanup;
  tileInfo.chain.pNext = &tileInfo;
  if ((pass = GPUBeginRenderPass(command, &passInfo)))
    goto cleanup;
  tileInfo.chain.pNext = NULL;
  tileInfo.chain.structSize = sizeof(tileInfo) - 1u;
  if ((pass = GPUBeginRenderPass(command, &passInfo)))
    goto cleanup;
  tileInfo.chain.structSize = sizeof(tileInfo);
  tileInfo.tileHeight = 32u;
  if ((pass = GPUBeginRenderPass(command, &passInfo)))
    goto cleanup;
  tileInfo.tileHeight = 16u;
  tileInfo.threadgroupMemoryLength = 1u;
  if ((pass = GPUBeginRenderPass(command, &passInfo)))
    goto cleanup;
  tileInfo.threadgroupMemoryLength = 0u;

  if (!(pass = GPUBeginRenderPass(command, &passInfo))
      || GPUGetRenderPassTileSize(pass, &width, &height) != GPU_OK
      || GPUBindTilePipeline(pass, tile) != GPU_OK
      || GPUBindTilePipeline(pass, wrongCount) != GPU_ERROR_INVALID_ARGUMENT
      || (wrongSample && GPUBindTilePipeline(pass, wrongSample) != GPU_ERROR_INVALID_ARGUMENT)
      || GPUDispatchThreadsPerTile(pass, width, height) != GPU_OK)
    goto cleanup;

  GPUEndRenderPass(pass);
  pass = NULL;
  ok = read_targets(device, command, &target, 1u, &pixels[0][0]);
  command = NULL;
  if (!ok)
    goto cleanup;

  memcpy(expected, kClear, sizeof(expected));
  if (!explicit)
    apply_tile(expected);
  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      for (c = 0u; c < 4u; c++) {
        if (pixels[y][x * 4u + c] != expected[c]) {
          ok = false;
          goto cleanup;
        }
      }
    }
  }

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyTilePipeline(wrongSample);
  GPUDestroyTilePipeline(wrongCount);
  GPUDestroyTilePipeline(tile);
  GPUDestroyRenderPipeline(draw);
  GPUDestroyShaderLayout(layout);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile negative explicit=%u exact CPU oracle=%s\n", explicit, ok ? "pass" : "fail");
  return ok;
}

static bool
check_resources(GPUDevice *device, GPUShaderLibrary *library, uint32_t samples) {
  GPURenderPipelineCreateInfo render = {0};
  GPUTilePipelineCreateInfo   tile = {0};
  GPURenderPassCreateInfo     passInfo = {0};
  GPURenderPassColorAttachment attachment = {0};
  GPUTileRenderPassEXT        tileInfo = {0};
  GPUBindGroupCreateInfo      groupInfo = {0};
  GPUBindGroupEntry           entries[2] = {{0}};
  GPUBufferCreateInfo         bufferInfo = {0};
  GPUTextureCreateInfo        textureInfo = {0};
  GPUTextureViewCreateInfo    viewInfo = {0};
  GPUTextureWriteRegion       write = {0};
  GPUSamplerCreateInfo        samplerInfo = {0};
  GPUColorTargetState         color = {0};
  GPUBarrierBatch             barriers = {0};
  GPUBufferBarrier            bufferBarrier = {0};
  GPUTextureBarrier           textureBarrier = {0};
  Target                      target = {0};
  float                       pixels[FETCH_HEIGHT][FETCH_ROW / sizeof(float)];
  float                       bufferData[64][4] = {{0}};
  float                       expected[4], push[4], copied[4];
  const float                 weights[2][4] = {
    {0.03125f, 0.0625f, 0.125f, 0.25f}, {0.25f, 0.125f, 0.0625f, 0.03125f}
  };
  const float                 values[2][4] = {
    {0.0625f, 0.03125f, 0.015625f, 0.125f}, {0.125f, 0.015625f, 0.03125f, 0.0625f}
  };
  const float                 texels[2][4] = {
    {0.0625f, 0.125f, 0.25f, 0.5f}, {0.25f, 0.0625f, 0.125f, 0.03125f}
  };
  const float                 basePush[4] = {0.015625f, 0.03125f, 0.0625f, 0.125f};
  const uint16_t              halfTexels[2][4] = {
    {0x2c00, 0x3000, 0x3400, 0x3800}, {0x3400, 0x2c00, 0x3000, 0x2800}
  };
  const uint32_t              choices[5] = {0u, 1u, 0u, 0u, 1u};
  GPUShaderLayout            *layout = NULL, *other = NULL;
  GPURenderPipeline          *draw = NULL;
  GPUTilePipeline            *pipeline = NULL, *idle = NULL, *rejected = NULL;
  GPUTilePipelineCreateInfo  *shortInfo = NULL;
  GPUBindGroup               *buffers = NULL, *wrongBuffers = NULL, *textures[2] = {NULL};
  GPUSampler                 *samplers[2] = {NULL};
  GPUBuffer                  *buffer = NULL;
  GPUTexture                 *texture = NULL;
  GPUTextureView             *view = NULL;
  GPUCommandBuffer           *command = NULL;
  GPURenderPassEncoder       *pass = NULL;
  GPUQueue                   *queue;
  GPUFormat                   format = GPU_FORMAT_RGBA32_FLOAT;
  uint32_t                    offsets[2], invalid[2];
  uint32_t                    width, height, i, j, x, y, c, choice;
  bool                        ok = false;

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "tile resource check failed at line %u: %s\n", __LINE__, #expr); \
    goto cleanup; \
  } \
} while (0)

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  REQUIRE(create_target(device, samples, &target));
  REQUIRE(GPUCreateShaderLayout(device, library, &layout) == GPU_OK);
  REQUIRE(GPUCreateShaderLayout(device, library, &other) == GPU_OK);
  REQUIRE(layout->bindGroupLayoutCount == 3u);

  tile.chain.sType                    = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tile.chain.structSize               = sizeof(tile);
  tile.library                        = library;
  tile.entryPoint                     = "resource_fs";
  tile.pColorFormats                  = &format;
  tile.colorAttachmentCount           = 1u;
  tile.sampleCount                    = samples;
  tile.threadgroupSizeMatchesTileSize = true;
  REQUIRE(GPUCreateTilePipeline(device, &tile, &rejected) == GPU_ERROR_INVALID_ARGUMENT);
  REQUIRE(!rejected);
  REQUIRE((shortInfo = malloc(offsetof(GPUTilePipelineCreateInfo, layout))));
  memcpy(shortInfo, &tile, offsetof(GPUTilePipelineCreateInfo, layout));
  ((GPUChainedStruct *)shortInfo)->structSize = offsetof(GPUTilePipelineCreateInfo, layout);
  REQUIRE(GPUCreateTilePipeline(device, shortInfo, &rejected) == GPU_ERROR_INVALID_ARGUMENT);
  REQUIRE(!rejected);
  tile.layout = layout->pipelineLayout;
  REQUIRE(GPUCreateTilePipeline(device, &tile, &pipeline) == GPU_OK);
  tile.entryPoint = "resource_idle_fs";
  REQUIRE(GPUCreateTilePipeline(device, &tile, &idle) == GPU_OK);

  color.format          = format;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  render.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  render.chain.structSize        = sizeof(render);
  render.library                = library;
  render.layout                 = layout->pipelineLayout;
  render.vertexEntry            = "resource_vs";
  render.fragmentEntry          = "resource_fs";
  render.pColorTargets          = &color;
  render.colorTargetCount       = 1u;
  render.primitiveTopology      = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  render.cullMode               = GPU_CULL_MODE_NONE;
  render.multisample.sampleCount = samples;
  render.multisample.sampleMask  = UINT32_MAX;
  REQUIRE(GPUCreateRenderPipeline(device, &render, &draw) == GPU_OK);

  memcpy(bufferData[0], weights[0], sizeof(weights[0]));
  memcpy(bufferData[16], weights[1], sizeof(weights[1]));
  memcpy(bufferData[32], values[0], sizeof(values[0]));
  memcpy(bufferData[48], values[1], sizeof(values[1]));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(bufferData);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_STORAGE
                               | GPU_BUFFER_USAGE_COPY_DST;
  REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffer) == GPU_OK);
  REQUIRE(GPUQueueWriteBuffer(queue, buffer, 0u, bufferData, sizeof(bufferData)) == GPU_OK);

  entries[0].binding       = 5u;
  entries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[0].buffer.buffer = buffer;
  entries[0].buffer.size   = sizeof(weights[0]);
  entries[1].binding       = 7u;
  entries[1].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  entries[1].buffer.buffer = buffer;
  entries[1].buffer.offset = 512u;
  entries[1].buffer.size   = sizeof(values[0]);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[1];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 2u;
  REQUIRE(GPUCreateBindGroup(device, &groupInfo, &buffers) == GPU_OK);
  groupInfo.layout = other->bindGroupLayouts[1];
  REQUIRE(GPUCreateBindGroup(device, &groupInfo, &wrongBuffers) == GPU_OK);

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width            = 2u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  REQUIRE(GPUCreateTexture(device, &textureInfo, &texture) == GPU_OK);
  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = textureInfo.format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  REQUIRE(GPUCreateTextureView(texture, &viewInfo, &view) == GPU_OK);
  write.width        = 2u;
  write.height       = 1u;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = sizeof(halfTexels);
  write.rowsPerImage = 1u;
  REQUIRE(GPUQueueWriteTexture(queue, texture, &write, halfTexels, sizeof(halfTexels)) == GPU_OK);

  memset(entries, 0, sizeof(entries));
  entries[0].binding     = 9u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[0].textureView = view;
  entries[1].binding     = 11u;
  entries[1].bindingType = GPU_BINDING_SAMPLER;
  groupInfo.layout      = layout->bindGroupLayouts[2];
  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.desc.maxAnisotropy = 1u;
  for (i = 0u; i < 2u; i++) {
    samplerInfo.desc.addressU = i ? GPU_ADDRESS_MODE_CLAMP_TO_EDGE : GPU_ADDRESS_MODE_REPEAT;
    REQUIRE(GPUCreateSampler(device, &samplerInfo, false, &samplers[i]) == GPU_OK);
    entries[1].sampler = samplers[i];
    REQUIRE(GPUCreateBindGroup(device, &groupInfo, &textures[i]) == GPU_OK);
  }

  REQUIRE(GPUAcquireCommandBuffer(queue, "tile-resources", &command) == GPU_OK);
  bufferBarrier.buffer    = buffer;
  bufferBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  bufferBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  textureBarrier.texture    = texture;
  textureBarrier.srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barriers.srcStages           = GPU_STAGE_TRANSFER;
  barriers.dstStages           = GPU_STAGE_FRAGMENT | GPU_STAGE_TILE;
  barriers.pBufferBarriers     = &bufferBarrier;
  barriers.bufferBarrierCount = 1u;
  barriers.pTextureBarriers    = &textureBarrier;
  barriers.textureBarrierCount = 1u;
  GPUEncodeBarriers(command, &barriers);

  tileInfo.chain.sType      = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tileInfo.chain.structSize = sizeof(tileInfo);
  tileInfo.tileWidth        = 16u;
  tileInfo.tileHeight       = 16u;
  attachment.view          = target.multisampleView ? target.multisampleView : target.view;
  attachment.resolveView   = target.multisampleView ? target.view : NULL;
  attachment.loadOp        = GPU_LOAD_OP_CLEAR;
  attachment.storeOp       = GPU_STORE_OP_STORE;
  memcpy(attachment.clearColor.float32, kClear, sizeof(kClear));
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.chain.pNext          = &tileInfo;
  passInfo.pColorAttachments    = &attachment;
  passInfo.colorAttachmentCount = 1u;
  REQUIRE((pass = GPUBeginRenderPass(command, &passInfo)));
  REQUIRE(GPUGetRenderPassTileSize(pass, &width, &height) == GPU_OK);
  offsets[0] = offsets[1] = 0u;
  REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 2u, offsets) == GPU_ERROR_INVALID_ARGUMENT);
  REQUIRE(GPUSetTilePushConstants(pass, 0u, sizeof(push), basePush) == GPU_ERROR_INVALID_ARGUMENT);
  memcpy(expected, kClear, sizeof(expected));

  REQUIRE(GPUBindTilePipeline(pass, idle) == GPU_OK);
  REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 2u, offsets) == GPU_OK);
  REQUIRE(GPUBindTileGroup(pass, 2u, textures[0], 0u, NULL) == GPU_OK);
  REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK);
  for (c = 0u; c < 4u; c++) {
    expected[c] *= 0.5f;
  }

  for (i = 0u; i < 5u; i++) {
    choice = choices[i];
    offsets[0] = offsets[1] = choice * 256u;
    if (i != 2u) {
      memcpy(copied, basePush, sizeof(copied));
      if (i == 0u || i == 3u) {
        GPUBindRenderPipeline(pass, draw);
        GPUBindRenderGroup(pass, 1u, buffers, 2u, offsets);
        GPUBindRenderGroup(pass, 2u, textures[1u - choice], 0u, NULL);
        GPUSetRenderPushConstants(pass, 0u, sizeof(copied), copied);
        GPUDraw(pass, 3u, 1u, 0u, 0u);
        REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_ERROR_INVALID_ARGUMENT);
      } else {
        REQUIRE(GPUBindTilePipeline(pass, pipeline) == GPU_OK);
        REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_ERROR_INVALID_ARGUMENT);
        REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 2u, offsets) == GPU_OK);
        REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_ERROR_INVALID_ARGUMENT);
        REQUIRE(GPUBindTileGroup(pass, 2u, textures[1u - choice], 0u, NULL) == GPU_OK);
        REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_ERROR_INVALID_ARGUMENT);
        if (i == 4u)
          copied[2] = copied[3] = 0.0f;
        memcpy(push, copied, sizeof(push));
        REQUIRE(GPUSetTilePushConstants(pass, 0u, (i == 4u ? 2u : 4u) * sizeof(float), push) == GPU_OK);
        REQUIRE(GPUSetTilePushConstants(pass, 0u, (i == 4u ? 2u : 4u) * sizeof(float), push) == GPU_OK);
        for (j = 0u; j < 4u; j++) {
          push[j] = -99.0f;
        }
        REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK);
      }
    } else {
      REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 2u, offsets) == GPU_OK);
      REQUIRE(GPUBindTileGroup(pass, 2u, textures[1u - choice], 0u, NULL) == GPU_OK);
      copied[0] = 0.25f;
      copied[1] = 0.5f;
      REQUIRE(GPUSetTilePushConstants(pass, 0u, 2u * sizeof(float), copied) == GPU_OK);
      invalid[0] = 256u;
      invalid[1] = UINT32_MAX;
      REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 2u, invalid) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUBindTileGroup(pass, 1u, buffers, 1u, offsets) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUBindTileGroup(pass, 1u, wrongBuffers, 2u, offsets) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUBindTileGroup(pass, 2u, buffers, 2u, offsets) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUBindTileGroup(pass, 32u, buffers, 2u, offsets) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUSetTilePushConstants(pass, 1u, sizeof(float), basePush) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUSetTilePushConstants(pass, sizeof(push), sizeof(float), basePush) == GPU_ERROR_INVALID_ARGUMENT);
      REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK);
    }

    for (c = 0u; c < 4u; c++) {
      expected[c] = expected[c] * 0.5f + weights[choice][c] + values[choice][c]
                    + texels[1u - choice][c] + copied[c];
    }
  }

  GPUEndRenderPass(pass);
  pass = NULL;
  ok = read_targets(device, command, &target, 1u, &pixels[0][0]);
  command = NULL;
  REQUIRE(ok);
  ok = false;
  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      for (c = 0u; c < 4u; c++) {
        REQUIRE(pixels[y][x * 4u + c] == expected[c]);
      }
    }
  }
  ok = true;

cleanup:
  free(shortInfo);
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyTilePipeline(rejected);
  GPUDestroyTilePipeline(pipeline);
  GPUDestroyTilePipeline(idle);
  GPUDestroyRenderPipeline(draw);
  GPUDestroyBindGroup(wrongBuffers);
  GPUDestroyBindGroup(buffers);
  for (i = 0u; i < 2u; i++) {
    GPUDestroyBindGroup(textures[i]);
    GPUDestroySampler(samplers[i]);
  }
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(buffer);
  GPUDestroyShaderLayout(other);
  GPUDestroyShaderLayout(layout);
  GPUDestroyTextureView(target.multisampleView);
  GPUDestroyTexture(target.multisample);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile resources samples=%u exact CPU oracle=%s\n", samples, ok ? "pass" : "fail");
  return ok;
#undef REQUIRE
}

static bool
check_array_resources(GPUDevice *device, GPUShaderLibrary *library) {
  GPUTilePipelineCreateInfo    tileInfo      = {0};
  GPURenderPassCreateInfo      passInfo      = {0};
  GPURenderPassColorAttachment attachment    = {0};
  GPUTileRenderPassEXT         tile          = {0};
  GPUBindGroupCreateInfo       groupInfo     = {0};
  GPUBindGroupEntry            entries[6]    = {{0}};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUTextureCreateInfo         textureInfo   = {0};
  GPUTextureViewCreateInfo     viewInfo      = {0};
  GPUTextureWriteRegion        write         = {0};
  GPUSamplerCreateInfo         samplerInfo   = {0};
  GPUBarrierBatch              barriers      = {0};
  GPUBufferBarrier             bufferBarriers[2]  = {{0}};
  GPUTextureBarrier            textureBarriers[2] = {{0}};
  Target                       target        = {0};
  float                        pixels[FETCH_HEIGHT][FETCH_ROW / sizeof(float)];
  float                        bufferData[64][4];
  float                        expected[4];
  const float                  values[2][2][4] = {
    {{0.03125f, 0.0625f, 0.125f, 0.25f}, {0.25f, 0.125f, 0.0625f, 0.03125f}},
    {{0.0625f, 0.03125f, 0.015625f, 0.125f}, {0.125f, 0.015625f, 0.03125f, 0.0625f}}
  };
  const float                  sampled[2][4] = {
    {0.25f, 0.125f, 0.0625f, 0.03125f}, {0.0625f, 0.015625f, 0.03125f, 0.125f}
  };
  const uint16_t               halfTexels[2][2][4] = {
    {{0x2800, 0x2c00, 0x3000, 0x3400}, {0x3400, 0x3000, 0x2c00, 0x2800}},
    {{0x2c00, 0x2400, 0x2800, 0x3000}, {0x3000, 0x2800, 0x2400, 0x2c00}}
  };
  const uint32_t               offsets[2][2] = {{256u, 512u}, {512u, 768u}};
  const uint32_t               invalid[2]    = {512u, 1024u};
  const uint32_t               choices[4]    = {0u, 0u, 1u, 0u};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUShaderLayout             *layout       = NULL;
  GPUTilePipeline             *pipeline     = NULL;
  GPUBindGroup                *group        = NULL;
  GPUSampler                  *samplers[2]  = {NULL};
  GPUBuffer                   *buffers[2]   = {NULL};
  GPUTexture                  *textures[2]  = {NULL};
  GPUTextureView              *views[2]     = {NULL};
  GPUCommandBuffer            *command      = NULL;
  GPURenderPassEncoder        *pass         = NULL;
  GPUQueue                    *queue;
  const GPUFormat              format       = GPU_FORMAT_RGBA32_FLOAT;
  uint32_t                     count, width, height, i, j, x, y, c, choice;
  bool                         ok           = false;

#define ARRAY_REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "tile array check failed at line %u: %s\n", __LINE__, #expr); \
    goto cleanup; \
  } \
} while (0)

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  ARRAY_REQUIRE(create_target(device, 1u, &target));
  ARRAY_REQUIRE(GPUCreateShaderLayout(device, library, &layout) == GPU_OK);
  ARRAY_REQUIRE(layout->bindGroupLayoutCount == 2u);
  layoutEntries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[1], &count);
  ARRAY_REQUIRE(layoutEntries && count == 3u
                && layoutEntries[0].binding == 5u
                && layoutEntries[0].bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                && layoutEntries[0].hasDynamicOffset
                && layoutEntries[0].buffer.strideBytes == sizeof(values[0][0])
                && layoutEntries[0].buffer.minBindingSize == sizeof(values[0][0])
                && layoutEntries[1].binding == 9u
                && layoutEntries[1].bindingType == GPU_BINDING_SAMPLED_TEXTURE
                && layoutEntries[2].binding == 11u
                && layoutEntries[2].bindingType == GPU_BINDING_SAMPLER);
  for (i = 0u; i < count; i++) {
    ARRAY_REQUIRE(layoutEntries[i].arrayCount == 2u);
  }

  tileInfo.chain.sType                    = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tileInfo.chain.structSize               = sizeof(tileInfo);
  tileInfo.library                        = library;
  tileInfo.layout                         = layout->pipelineLayout;
  tileInfo.entryPoint                     = "array_resource_fs";
  tileInfo.pColorFormats                  = &format;
  tileInfo.colorAttachmentCount           = 1u;
  tileInfo.sampleCount                    = 1u;
  tileInfo.threadgroupSizeMatchesTileSize = true;
  ARRAY_REQUIRE(GPUCreateTilePipeline(device, &tileInfo, &pipeline) == GPU_OK);

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(bufferData);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension       = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width            = 2u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = textureInfo.format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  write.width        = 2u;
  write.height       = 1u;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = sizeof(halfTexels[0]);
  write.rowsPerImage = 1u;
  samplerInfo.chain.sType        = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize   = sizeof(samplerInfo);
  samplerInfo.desc.minFilter     = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter     = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter     = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.maxAnisotropy = 1u;

  for (i = 0u; i < 2u; i++) {
    for (j = 0u; j < 64u; j++) {
      for (c = 0u; c < 4u; c++) {
        bufferData[j][c] = -8.0f;
      }
    }
    memcpy(bufferData[16u + i * 16u], values[i][0], sizeof(values[i][0]));
    memcpy(bufferData[32u + i * 16u], values[i][1], sizeof(values[i][1]));
    ARRAY_REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffers[i]) == GPU_OK);
    ARRAY_REQUIRE(GPUQueueWriteBuffer(queue, buffers[i], 0u, bufferData, sizeof(bufferData)) == GPU_OK);
    ARRAY_REQUIRE(GPUCreateTexture(device, &textureInfo, &textures[i]) == GPU_OK);
    ARRAY_REQUIRE(GPUCreateTextureView(textures[i], &viewInfo, &views[i]) == GPU_OK);
    ARRAY_REQUIRE(GPUQueueWriteTexture(queue, textures[i], &write,
                                      halfTexels[i], sizeof(halfTexels[i])) == GPU_OK);
    samplerInfo.desc.addressU = i ? GPU_ADDRESS_MODE_REPEAT : GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
    ARRAY_REQUIRE(GPUCreateSampler(device, &samplerInfo, false, &samplers[i]) == GPU_OK);

    entries[i].binding       = 5u;
    entries[i].arrayIndex    = i;
    entries[i].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.size   = sizeof(values[i][0]);
    entries[2u + i].binding     = 9u;
    entries[2u + i].arrayIndex  = i;
    entries[2u + i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[2u + i].textureView = views[i];
    entries[4u + i].binding     = 11u;
    entries[4u + i].arrayIndex  = i;
    entries[4u + i].bindingType = GPU_BINDING_SAMPLER;
    entries[4u + i].sampler     = samplers[i];

    bufferBarriers[i].buffer    = buffers[i];
    bufferBarriers[i].srcAccess = GPU_ACCESS_TRANSFER_WRITE;
    bufferBarriers[i].dstAccess = GPU_ACCESS_SHADER_READ;
    textureBarriers[i].texture    = textures[i];
    textureBarriers[i].srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
    textureBarriers[i].dstAccess  = GPU_ACCESS_SHADER_READ;
    textureBarriers[i].mipCount   = 1u;
    textureBarriers[i].layerCount = 1u;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[1];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 6u;
  ARRAY_REQUIRE(GPUCreateBindGroup(device, &groupInfo, &group) == GPU_OK);
  ARRAY_REQUIRE(GPUAcquireCommandBuffer(queue, "tile-array-resources", &command) == GPU_OK);
  barriers.srcStages           = GPU_STAGE_TRANSFER;
  barriers.dstStages           = GPU_STAGE_TILE;
  barriers.pBufferBarriers      = bufferBarriers;
  barriers.bufferBarrierCount  = 2u;
  barriers.pTextureBarriers     = textureBarriers;
  barriers.textureBarrierCount = 2u;
  GPUEncodeBarriers(command, &barriers);

  tile.chain.sType      = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tile.chain.structSize = sizeof(tile);
  tile.tileWidth        = 16u;
  tile.tileHeight       = 16u;
  attachment.view      = target.view;
  attachment.loadOp    = GPU_LOAD_OP_CLEAR;
  attachment.storeOp   = GPU_STORE_OP_STORE;
  memcpy(attachment.clearColor.float32, kClear, sizeof(kClear));
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.chain.pNext          = &tile;
  passInfo.pColorAttachments    = &attachment;
  passInfo.colorAttachmentCount = 1u;
  ARRAY_REQUIRE((pass = GPUBeginRenderPass(command, &passInfo)));
  ARRAY_REQUIRE(GPUGetRenderPassTileSize(pass, &width, &height) == GPU_OK);
  ARRAY_REQUIRE(GPUBindTilePipeline(pass, pipeline) == GPU_OK);
  ARRAY_REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) != GPU_OK);
  memcpy(expected, kClear, sizeof(expected));

  for (i = 0u; i < 4u; i++) {
    choice = choices[i];
    if (i == 1u) {
      /* a rejected second offset must preserve both previous descriptor addresses. */
      ARRAY_REQUIRE(GPUBindTileGroup(pass, 1u, group, 2u, invalid) != GPU_OK);
      ARRAY_REQUIRE(GPUBindTileGroup(pass, 1u, group, 1u, offsets[choice]) != GPU_OK);
    } else {
      ARRAY_REQUIRE(GPUBindTileGroup(pass, 1u, group, 2u, offsets[choice]) == GPU_OK);
    }
    ARRAY_REQUIRE(GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK);
    for (c = 0u; c < 4u; c++) {
      expected[c] = expected[c] * 0.5f + values[0][choice][c] + values[1][choice][c] * 0.5f
                    + sampled[0][c] + sampled[1][c] * 0.5f;
    }
  }

  GPUEndRenderPass(pass);
  pass = NULL;
  ok   = read_targets(device, command, &target, 1u, &pixels[0][0]);
  command = NULL;
  ARRAY_REQUIRE(ok);
  ok = false;
  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      for (c = 0u; c < 4u; c++) {
        ARRAY_REQUIRE(pixels[y][x * 4u + c] == expected[c]);
      }
    }
  }
  ok = true;

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyTilePipeline(pipeline);
  GPUDestroyBindGroup(group);
  for (i = 0u; i < 2u; i++) {
    GPUDestroySampler(samplers[i]);
    GPUDestroyTextureView(views[i]);
    GPUDestroyTexture(textures[i]);
    GPUDestroyBuffer(buffers[i]);
  }
  GPUDestroyShaderLayout(layout);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile resource arrays exact CPU oracle=%s\n", ok ? "pass" : "fail");
  return ok;
#undef ARRAY_REQUIRE
}

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    GPUDeviceCreateInfo   info = {0};
    GPUFormatCapabilities caps = {0};
    const GPUFeature     features[] = {GPU_FEATURE_FRAMEBUFFER_FETCH,
                                     GPU_FEATURE_TILE_SHADERS,
                                     GPU_FEATURE_IMAGEBLOCKS,
                                     GPU_FEATURE_DESCRIPTOR_INDEXING};
    GPUInstance       *instance = NULL;
    GPUAdapter        *adapter;
    GPUDevice         *device = NULL, *disabled = NULL;
    GPUShaderLibrary  *libraries[4] = {NULL}, *disabledLibrary = NULL;
    GPUShaderLibrary  *rejectedLibrary = NULL;
    GPUTilePipeline   *rejected = NULL;
    NSData            *data[4];
    const char        *mode;
    uint32_t           i, featureCount = 3u;
    bool               ok = false;

    if (argc != 5)
      return 1;
    for (i = 0u; i < 4u; i++) {
      if (!(data[i] = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[i + 1u]]]))
        return 1;
    }
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      goto cleanup;
    for (i = 0u; i < 3u; i++) {
      if (!GPUIsFeatureSupported(adapter, features[i])) {
        GPUDestroyInstance(instance);
        return 77;
      }
    }

    info.required.pFeatures    = features;
    info.required.featureCount = 1u;
    if (GPUCreateDevice(adapter, &info, &disabled) != GPU_OK
        || GPUCreateShaderLibraryFromUSL(disabled, [data[0] bytes], [data[0] length], &disabledLibrary) != GPU_OK
        || create_tile(disabled, disabledLibrary, "tile_transform_fs", true, 1u, &rejected) != GPU_ERROR_UNSUPPORTED
        || rejected)
      goto cleanup;

    if (GPUCreateShaderLibraryFromUSL(disabled, [data[1] bytes], [data[1] length],
                                      &rejectedLibrary) != GPU_ERROR_UNSUPPORTED
        || rejectedLibrary)
      goto cleanup;

    if (GPUIsFeatureSupported(adapter, GPU_FEATURE_DESCRIPTOR_INDEXING))
      featureCount = 4u;
    info.required.featureCount = featureCount;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK)
      goto cleanup;
    for (i = 0u; i < featureCount; i++) {
      if (GPUCreateShaderLibraryFromUSL(device, [data[i] bytes], [data[i] length], &libraries[i]) != GPU_OK)
        goto cleanup;
    }

    ok = check_tiles(device, libraries[0], false, 16u, true, 1u);
    ok = check_tiles(device, libraries[0], false, 0u, false, 1u) && ok;
    ok = check_tiles(device, libraries[1], true, 16u, true, 1u) && ok;
    ok = check_tiles(device, libraries[1], true, 0u, false, 1u) && ok;
    if (GPUGetFormatCapabilities(adapter, GPU_FORMAT_RGBA32_FLOAT, &caps) != GPU_OK) {
      ok = false;
    } else if ((caps.supportedSampleCounts & 4u) != 0u) {
      ok = check_tiles(device, libraries[0], false, 16u, true, 4u) && ok;
      ok = check_tiles(device, libraries[1], true, 16u, true, 4u) && ok;
    } else {
      printf("tile 4x MSAA: RGBA32_FLOAT unsupported, subcase skipped\n");
    }
    if (featureCount == 4u)
      ok = check_array_resources(device, libraries[3]) && ok;
    else
      printf("tile descriptor arrays: descriptor indexing unsupported, subcase skipped\n");
    ok = check_resources(device, libraries[2], 1u) && ok;
    if ((caps.supportedSampleCounts & 4u) != 0u)
      ok = check_resources(device, libraries[2], 4u) && ok;
    ok = check_invalid_passes(device, libraries[0], false, (caps.supportedSampleCounts & 4u) != 0u) && ok;
    ok = check_invalid_passes(device, libraries[1], true, (caps.supportedSampleCounts & 4u) != 0u) && ok;
    mode = getenv("GPU_METAL_MODE");
    if (mode && strcmp(mode, "classic") == 0)
      ok = check_tiles(device, libraries[0], false, 0u, true, 1u) && ok;

cleanup:
    GPUDestroyTilePipeline(rejected);
    GPUDestroyShaderLibrary(rejectedLibrary);
    GPUDestroyShaderLibrary(disabledLibrary);
    GPUDestroyDevice(disabled);
    for (i = 0u; i < 4u; i++) {
      GPUDestroyShaderLibrary(libraries[i]);
    }
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
