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

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    GPUDeviceCreateInfo   info = {0};
    GPUFormatCapabilities caps = {0};
    const GPUFeature     features[] = {GPU_FEATURE_FRAMEBUFFER_FETCH,
                                     GPU_FEATURE_TILE_SHADERS,
                                     GPU_FEATURE_IMAGEBLOCKS};
    GPUInstance       *instance = NULL;
    GPUAdapter        *adapter;
    GPUDevice         *device = NULL, *disabled = NULL;
    GPUShaderLibrary  *libraries[2] = {NULL}, *disabledLibrary = NULL;
    GPUShaderLibrary  *rejectedLibrary = NULL;
    GPUTilePipeline   *rejected = NULL;
    NSData            *data[2];
    const char        *mode;
    uint32_t           i;
    bool               ok = false;

    if (argc != 3)
      return 1;
    for (i = 0u; i < 2u; i++) {
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

    info.required.featureCount = 3u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK)
      goto cleanup;
    for (i = 0u; i < 2u; i++) {
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
    for (i = 0u; i < 2u; i++) {
      GPUDestroyShaderLibrary(libraries[i]);
    }
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
