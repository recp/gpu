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
  KERNEL_WIDTH  = 31,
  KERNEL_HEIGHT = 19,
  KERNEL_ROW    = 512,
  KERNEL_IMAGE  = KERNEL_HEIGHT * KERNEL_ROW
};

typedef struct Target {
  GPUTexture     *texture;
  GPUTextureView *view;
  GPUTexture     *multisample;
  GPUTextureView *multisampleView;
} Target;

static bool
create_target(GPUDevice *device, uint32_t samples, Target *target) {
  GPUTextureCreateInfo     info = {0};
  GPUTextureViewCreateInfo view = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = GPU_FORMAT_RGBA32_FLOAT;
  info.width            = KERNEL_WIDTH;
  info.height           = KERNEL_HEIGHT;
  info.depthOrLayers    = 1u;
  info.mipLevelCount     = 1u;
  info.sampleCount      = 1u;
  info.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC;

  view.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  view.chain.structSize = sizeof(view);
  view.viewType         = GPU_TEXTURE_VIEW_2D;
  view.format           = info.format;
  view.mipLevelCount     = 1u;
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
  info.sizeBytes        = (uint64_t)count * KERNEL_IMAGE;
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

  batch.pTextureBarriers    = barriers;
  batch.textureBarrierCount = count;
  batch.srcStages           = GPU_STAGE_FRAGMENT | GPU_STAGE_TILE;
  batch.dstStages           = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(command, &batch);
  if (!(pass = GPUBeginTransferPass(command, "tile-readback")))
    goto cleanup;

  region.bytesPerRow        = KERNEL_ROW;
  region.rowsPerImage       = KERNEL_HEIGHT;
  region.texture.width      = KERNEL_WIDTH;
  region.texture.height     = KERNEL_HEIGHT;
  region.texture.depth      = 1u;
  region.texture.layerCount = 1u;
  for (i = 0u; i < count; i++) {
    region.bufferOffset = (uint64_t)i * KERNEL_IMAGE;
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

static GPUResult
create_draw(GPUDevice          *device,
            GPUShaderLibrary   *library,
            GPUShaderLayout    *layout,
            const char         *fragment,
            uint32_t            samples,
            GPURenderPipeline **pipeline) {
  GPURenderPipelineCreateInfo info = {0};
  GPUColorTargetState         color = {0};

  color.format          = GPU_FORMAT_RGBA32_FLOAT;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.library                 = library;
  info.layout                  = layout->pipelineLayout;
  info.vertexEntry             = "kernel_vs";
  info.fragmentEntry           = fragment;
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.multisample.sampleCount = samples;
  info.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(device, &info, pipeline);
}

static void
expected_pixel(uint32_t x, uint32_t y, uint32_t samples, float expected[4]) {
  uint32_t groupX, groupY, neighborX, neighborY, sample, c;
  float    sign, values[4];

  groupX    = x / 16u;
  groupY    = y / 16u;
  neighborX = (x % 16u + 7u) % 16u;
  neighborY = (y % 16u + 5u) % 16u;
  sign      = groupX * 16u + neighborX < KERNEL_WIDTH
              && groupY * 16u + neighborY < KERNEL_HEIGHT ? 1.0f : -1.0f;
  memset(expected, 0, 4u * sizeof(*expected));
  for (sample = 0u; sample < samples; sample++) {
    values[0] = (sign * 0.125f + (float)neighborX / 32.0f) * 0.5f + 37.0f / 256.0f;
    values[1] = (sign * 0.25f + (float)neighborY / 64.0f) * 0.5f + 0.0625f;
    values[2] = (sign * 0.375f + (float)sample / 16.0f) * 0.5f + 0.03125f;
    values[3] = (sign * 0.5f + (float)(groupX + groupY * 2u) / 16.0f) * 0.5f + 0.125f;
    for (c = 0u; c < 4u; c++) {
      expected[c] += values[c] / (float)samples;
    }
  }
}

static bool
check_kernel(GPUDevice        *device,
             GPUShaderLibrary *library,
             uint32_t          samples,
             uint32_t          threads) {
  GPURenderPassColorAttachment color      = {0};
  GPURenderPassCreateInfo      passInfo   = {0};
  GPUTileRenderPassEXT         tilePass   = {0};
  GPUTilePipelineCreateInfo    tileInfo   = {0};
  GPUTilePipelineProperties    properties = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupCreateInfo       groupInfo  = {0};
  GPUBindGroupEntry            entry      = {0};
  GPUBufferBarrier            barrier    = {0};
  GPUBarrierBatch             barriers   = {0};
  Target                      target     = {0};
  float                       pixels[KERNEL_HEIGHT][KERNEL_ROW / sizeof(float)];
  float                       expected[4];
  GPUShaderLayout            *layout     = NULL;
  GPURenderPipeline          *init       = NULL;
  GPURenderPipeline          *resolve    = NULL;
  GPUTilePipeline            *kernel     = NULL;
  GPUBindGroup               *group      = NULL;
  GPUBuffer                  *buffer     = NULL;
  GPUCommandBuffer           *command    = NULL;
  GPURenderPassEncoder       *pass       = NULL;
  GPUQueue                   *queue;
  const GPUBindGroupLayoutEntry *layoutEntries;
  const GPUFormat             format     = GPU_FORMAT_RGBA32_FLOAT;
  uint32_t                    config[4]  = {KERNEL_WIDTH, KERNEL_HEIGHT, threads, 37u};
  uint32_t                    count, width, height, length, x, y, c;
  bool                        ok = false;

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "tile kernel samples=%u threads=%u line=%u: %s\n", \
            samples, threads, __LINE__, #expr); \
    goto cleanup; \
  } \
} while (0)

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  REQUIRE(create_target(device, samples, &target));
  REQUIRE(GPUCreateShaderLayout(device, library, &layout) == GPU_OK);
  REQUIRE(layout->bindGroupLayoutCount == 1u);
  layoutEntries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0], &count);
  REQUIRE(layoutEntries && count == 1u
          && layoutEntries[0].binding == 3u
          && layoutEntries[0].bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
          && layoutEntries[0].visibility == GPU_SHADER_STAGE_COMPUTE_BIT);
  REQUIRE(create_draw(device, library, layout, "kernel_init_fs", samples, &init) == GPU_OK);
  REQUIRE(create_draw(device, library, layout, "kernel_resolve_fs", samples, &resolve) == GPU_OK);

  tileInfo.chain.sType      = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tileInfo.chain.structSize = sizeof(tileInfo);
  tileInfo.library              = library;
  tileInfo.layout               = layout->pipelineLayout;
  tileInfo.entryPoint           = "kernel_update";
  tileInfo.pColorFormats        = &format;
  tileInfo.colorAttachmentCount = 1u;
  tileInfo.sampleCount          = samples;
  REQUIRE(GPUCreateTilePipeline(device, &tileInfo, &kernel) == GPU_OK);
  REQUIRE(GPUGetTilePipelineProperties(kernel, &properties) == GPU_OK);
  REQUIRE(properties.imageblockSampleLength && properties.maxThreadsPerThreadgroup >= threads * threads);

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = 256u + sizeof(config);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST;
  REQUIRE(GPUCreateBuffer(device, &bufferInfo, &buffer) == GPU_OK);
  REQUIRE(GPUQueueWriteBuffer(queue, buffer, 256u, config, sizeof(config)) == GPU_OK);
  entry.binding       = 3u;
  entry.bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  entry.buffer.buffer = buffer;
  entry.buffer.offset = 256u;
  entry.buffer.size   = sizeof(config);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = layout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  REQUIRE(GPUCreateBindGroup(device, &groupInfo, &group) == GPU_OK);

  REQUIRE(GPUAcquireCommandBuffer(queue, "tile-kernel-neighbors", &command) == GPU_OK);
  barrier.buffer    = buffer;
  barrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  barrier.dstAccess = GPU_ACCESS_SHADER_READ;
  barriers.srcStages          = GPU_STAGE_TRANSFER;
  barriers.dstStages          = GPU_STAGE_TILE;
  barriers.pBufferBarriers    = &barrier;
  barriers.bufferBarrierCount = 1u;
  GPUEncodeBarriers(command, &barriers);

  tilePass.chain.sType      = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tilePass.chain.structSize = sizeof(tilePass);
  tilePass.tileWidth              = 16u;
  tilePass.tileHeight             = 16u;
  tilePass.imageblockSampleLength = properties.imageblockSampleLength;
  length = GPUGetRenderPipelineImageblockSampleLength(init);
  if (length > tilePass.imageblockSampleLength)
    tilePass.imageblockSampleLength = length;
  length = GPUGetRenderPipelineImageblockSampleLength(resolve);
  if (length > tilePass.imageblockSampleLength)
    tilePass.imageblockSampleLength = length;
  color.view        = target.multisampleView ? target.multisampleView : target.view;
  color.resolveView = target.multisampleView ? target.view : NULL;
  color.loadOp      = GPU_LOAD_OP_CLEAR;
  color.storeOp     = GPU_STORE_OP_STORE;
  passInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize = sizeof(passInfo);
  passInfo.chain.pNext      = &tilePass;
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  REQUIRE((pass = GPUBeginRenderPass(command, &passInfo)));
  REQUIRE(GPUGetRenderPassTileSize(pass, &width, &height) == GPU_OK && width == 16u && height == 16u);
  GPUBindRenderPipeline(pass, init);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  REQUIRE(GPUBindTilePipeline(pass, kernel) == GPU_OK);
  REQUIRE(GPUDispatchThreadsPerTile(pass, threads, threads) == GPU_ERROR_INVALID_ARGUMENT);
  REQUIRE(GPUBindTileGroup(pass, 0u, group, 0u, NULL) == GPU_OK);
  REQUIRE(GPUDispatchThreadsPerTile(pass, threads, threads) == GPU_OK);
  GPUBindRenderPipeline(pass, resolve);
  REQUIRE(GPUDispatchThreadsPerTile(pass, threads, threads) == GPU_ERROR_INVALID_ARGUMENT);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  pass = NULL;
  ok = read_targets(device, command, &target, 1u, &pixels[0][0]);
  command = NULL;
  REQUIRE(ok);
  ok = false;
  for (y = 0u; y < KERNEL_HEIGHT; y++) {
    for (x = 0u; x < KERNEL_WIDTH; x++) {
      expected_pixel(x, y, samples, expected);
      for (c = 0u; c < 4u; c++) {
        if (pixels[y][x * 4u + c] != expected[c]) {
          fprintf(stderr, "kernel pixel=%u,%u channel=%u: %a != %a\n",
                  x, y, c, pixels[y][x * 4u + c], expected[c]);
          goto cleanup;
        }
      }
    }
  }
  ok = true;

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(buffer);
  GPUDestroyTilePipeline(kernel);
  GPUDestroyRenderPipeline(resolve);
  GPUDestroyRenderPipeline(init);
  GPUDestroyShaderLayout(layout);
  GPUDestroyTextureView(target.multisampleView);
  GPUDestroyTexture(target.multisample);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile kernel samples=%u dispatch=%ux%u neighbor/barrier exact2356=%s\n",
         samples, threads, threads, ok ? "pass" : "fail");
  return ok;
#undef REQUIRE
}

static bool
check_entry_contract(GPUDevice *device, GPUShaderLibrary *library) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPUTilePipelineCreateInfo    tileInfo    = {0};
  GPUShaderReflection         reflection  = {0};
  GPUShaderLayout            *layout      = NULL;
  GPUComputePipeline         *compute     = NULL;
  GPUTilePipeline            *tile        = NULL;
  const GPUFormat             format      = GPU_FORMAT_RGBA32_FLOAT;
  bool                        ok = false;

  if (GPUGetShaderReflection(library, &reflection) != GPU_OK
      || reflection.resourceCount != 1u || reflection.pushConstantSizeBytes
      || reflection.pResources[0].binding != 3u
      || reflection.pResources[0].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER
      || reflection.pResources[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT
      || GPUCreateShaderLayout(device, library, &layout) != GPU_OK)
    goto cleanup;

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.library         = library;
  computeInfo.layout          = layout->pipelineLayout;
  computeInfo.entryPoint      = "kernel_update";
  if (GPUCreateComputePipeline(device, &computeInfo, &compute) != GPU_ERROR_INVALID_ARGUMENT || compute)
    goto cleanup;
  computeInfo.entryPoint = "ordinary_kernel";
  if (GPUCreateComputePipeline(device, &computeInfo, &compute) != GPU_OK)
    goto cleanup;

  tileInfo.chain.sType      = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tileInfo.chain.structSize = sizeof(tileInfo);
  tileInfo.library              = library;
  tileInfo.layout               = layout->pipelineLayout;
  tileInfo.entryPoint           = "ordinary_kernel";
  tileInfo.pColorFormats        = &format;
  tileInfo.colorAttachmentCount = 1u;
  tileInfo.sampleCount          = 1u;
  if (GPUCreateTilePipeline(device, &tileInfo, &tile) != GPU_ERROR_UNSUPPORTED || tile)
    goto cleanup;
  tileInfo.entryPoint = "kernel_vs";
  if (GPUCreateTilePipeline(device, &tileInfo, &tile) != GPU_ERROR_UNSUPPORTED || tile)
    goto cleanup;
  tileInfo.entryPoint = "kernel_readonly";
  if (GPUCreateTilePipeline(device, &tileInfo, &tile) != GPU_OK)
    goto cleanup;
  ok = true;

cleanup:
  GPUDestroyTilePipeline(tile);
  GPUDestroyComputePipeline(compute);
  GPUDestroyShaderLayout(layout);
  GPUFreeShaderReflection(&reflection);
  printf("tile kernel reflection/stage guards=%s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    GPUDeviceCreateInfo   info = {0};
    GPUFormatCapabilities caps = {0};
    const GPUFeature      features[] = {GPU_FEATURE_COMPUTE,
                                       GPU_FEATURE_TILE_SHADERS,
                                       GPU_FEATURE_IMAGEBLOCKS};
    GPUInstance          *instance = NULL;
    GPUAdapter           *adapter;
    GPUDevice            *device = NULL;
    GPUShaderLibrary     *library = NULL;
    NSData               *data;
    uint32_t              i;
    bool                  ok = false;

    if (argc != 2
        || !(data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]]))
      return 1;
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
    info.required.featureCount = 3u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK
        || GPUCreateShaderLibraryFromUSL(device, data.bytes, data.length, &library) != GPU_OK)
      goto cleanup;
    ok = check_entry_contract(device, library);
    ok = check_kernel(device, library, 1u, 16u) && ok;
    ok = check_kernel(device, library, 1u, 8u) && ok;
    if (GPUGetFormatCapabilities(adapter, GPU_FORMAT_RGBA32_FLOAT, &caps) != GPU_OK) {
      ok = false;
    } else if (caps.supportedSampleCounts & 4u) {
      ok = check_kernel(device, library, 4u, 16u) && ok;
      ok = check_kernel(device, library, 4u, 8u) && ok;
    } else {
      printf("tile kernel4x MSAA: RGBA32_FLOAT unsupported, subcase skipped\n");
    }

cleanup:
    GPUDestroyShaderLibrary(library);
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
