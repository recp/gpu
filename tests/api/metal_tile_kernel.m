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

typedef struct AttachmentBudgetCase {
  uint32_t samples, colors, width, height, sampleLength, threadgroupLength;
  bool     accepted;
} AttachmentBudgetCase;

typedef enum KernelKind {
  KERNEL_NEIGHBOR,
  KERNEL_UNIFORM,
  KERNEL_LOOP,
  KERNEL_MIXED,
  KERNEL_MIXED_SMALL
} KernelKind;

static const char *kernelEntries[] = {"kernel_update", "kernel_uniform", "kernel_loop",
                                    "kernel_mixed", "kernel_mixed_small"};
static const char *kernelLabels[]  = {"", " uniform", " loop", " mixed", " mixed small"};

static bool
create_target(GPUDevice *device, GPUFormat format, uint32_t samples, Target *target) {
  GPUTextureCreateInfo     info = {0};
  GPUTextureViewCreateInfo view = {0};
  bool                     depth = format == GPU_FORMAT_DEPTH32_FLOAT;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = format;
  info.width            = KERNEL_WIDTH;
  info.height           = KERNEL_HEIGHT;
  info.depthOrLayers    = 1u;
  info.mipLevelCount     = 1u;
  info.sampleCount      = depth ? samples : 1u;
  info.usage            = depth ? GPU_TEXTURE_USAGE_DEPTH_STENCIL
                          : GPU_TEXTURE_USAGE_COLOR_TARGET | GPU_TEXTURE_USAGE_COPY_SRC;

  view.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  view.chain.structSize = sizeof(view);
  view.viewType         = GPU_TEXTURE_VIEW_2D;
  view.format           = info.format;
  view.mipLevelCount     = 1u;
  view.arrayLayerCount  = 1u;

  if (GPUCreateTexture(device, &info, &target->texture) != GPU_OK
      || GPUCreateTextureView(target->texture, &view, &target->view) != GPU_OK)
    return false;

  if (samples == 1u || depth)
    return true;

  info.sampleCount = samples;
  info.usage       = GPU_TEXTURE_USAGE_COLOR_TARGET;
  return GPUCreateTexture(device, &info, &target->multisample) == GPU_OK
         && GPUCreateTextureView(target->multisample, &view, &target->multisampleView) == GPU_OK;
}

static bool
check_storage_budget(GPUDevice *device, uint32_t samples, bool depth) {
  const struct {
    uint32_t            width, height, sampleLength, threadgroupLength;
    GPUSampleCountFlags validSamples;
  } cases[] = {
    {16u, 16u, 64u,          0u,                    GPU_SAMPLE_COUNT_1_BIT},
    { 0u,  0u, 64u,          0u,                    GPU_SAMPLE_COUNT_1_BIT},
    {32u, 16u, 64u,          0u,                    GPU_SAMPLE_COUNT_1_BIT},
    {32u, 32u, 32u,          0u,                    GPU_SAMPLE_COUNT_1_BIT},
    {16u, 16u, 32u,          0u,                    GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT},
    { 0u,  0u, 32u,          0u,                    GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT},
    {16u, 16u, 64u,      16384u,                    GPU_SAMPLE_COUNT_1_BIT},
    {16u, 16u, 64u,      16400u,                    0u},
    {16u, 16u, 32u,         16u,                    GPU_SAMPLE_COUNT_1_BIT},
    {16u, 16u, 80u,          0u,                    0u},
    { 0u,  0u, 80u,          0u,                    0u},
    {16u, 16u, UINT32_MAX,   0u,                    0u},
    {16u, 16u, 64u,      32768u,                    0u},
    {16u, 16u, 32u, UINT32_MAX & ~15u,              0u},
    { 0u,  0u, 32u, UINT32_MAX & ~15u,              0u},
    {32u, 32u,  0u,          0u,                    GPU_SAMPLE_COUNT_1_BIT},
    {16u, 16u,  0u,          0u,
      GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT},
    {16u, 16u,  0u,      32768u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT : 0u},
    { 0u,  0u,  0u,      32768u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT : 0u},
    {16u, 16u,  0u,      32784u,                    0u},
    { 0u,  0u,  0u, UINT32_MAX & ~15u,              0u},
    {16u, 16u, 16u,          0u,
      GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT},
    {16u, 16u, 16u,         16u,                    GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT},
    {32u, 16u,  0u,          0u,                    GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT},
    {16u, 16u,  0u,      28672u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT : GPU_SAMPLE_COUNT_1_BIT},
    {16u, 16u,  0u,      16384u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT
            : GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT},
    {16u, 16u,  0u,      16400u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT : GPU_SAMPLE_COUNT_1_BIT},
    { 0u,  0u,  0u,      16384u,
      depth ? GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT | GPU_SAMPLE_COUNT_8_BIT
            : GPU_SAMPLE_COUNT_1_BIT | GPU_SAMPLE_COUNT_4_BIT}
  };
  GPURenderPassColorAttachment        color     = {0};
  GPURenderPassDepthStencilAttachment attachment = {0};
  GPURenderPassCreateInfo             info      = {0};
  GPUTileRenderPassEXT                tile      = {0};
  Target                             target    = {0};
  GPUCommandBuffer                  *command   = NULL;
  GPURenderPassEncoder              *pass      = NULL;
  GPUQueue                          *queue;
  uint32_t                           i, checks = 0u;
  bool                               accepted;
  bool                               ok = false;

  if (!create_target(device,
                     depth ? GPU_FORMAT_DEPTH32_FLOAT : GPU_FORMAT_RGBA32_FLOAT,
                     samples,
                     &target))
    goto cleanup;

  color.view        = target.multisampleView ? target.multisampleView : target.view;
  color.resolveView = target.multisampleView ? target.view : NULL;
  color.loadOp      = GPU_LOAD_OP_CLEAR;
  color.storeOp     = GPU_STORE_OP_STORE;

  attachment.view           = target.view;
  attachment.depthLoadOp    = GPU_LOAD_OP_CLEAR;
  attachment.depthStoreOp   = GPU_STORE_OP_STORE;
  attachment.stencilLoadOp  = GPU_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  attachment.clearDepth     = 1.0;

  tile.chain.sType      = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tile.chain.structSize = sizeof(tile);
  info.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.chain.pNext      = &tile;
  info.pColorAttachments       = depth ? NULL : &color;
  info.colorAttachmentCount    = depth ? 0u : 1u;
  info.pDepthStencilAttachment = depth ? &attachment : NULL;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUAcquireCommandBuffer(queue, "tile-storage-budget", &command) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
    tile.tileWidth               = cases[i].width;
    tile.tileHeight              = cases[i].height;
    tile.imageblockSampleLength  = cases[i].sampleLength;
    tile.threadgroupMemoryLength = cases[i].threadgroupLength;
    accepted = (cases[i].validSamples & samples) != 0u;
    pass = GPUBeginRenderPass(command, &info);
    if ((pass != NULL) != accepted) {
      fprintf(stderr, "tile storage case=%u depth=%u samples=%u accepted=%u expected=%u\n",
              i, depth, samples, pass != NULL, accepted);
      goto cleanup;
    }
    if (pass) {
      GPUEndRenderPass(pass);
      pass = NULL;
    } else {
      /* a rejected reservation must leave this command usable. */
      tile.tileWidth               = 16u;
      tile.tileHeight              = 16u;
      tile.imageblockSampleLength  = samples == 8u ? 16u : 32u;
      tile.threadgroupMemoryLength = 0u;
      if (!(pass = GPUBeginRenderPass(command, &info))) {
        fprintf(stderr, "tile storage recovery case=%u depth=%u samples=%u\n", i, depth, samples);
        goto cleanup;
      }
      GPUEndRenderPass(pass);
      pass = NULL;
    }
    checks++;
  }
  ok = true;

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyTextureView(target.multisampleView);
  GPUDestroyTexture(target.multisample);
  GPUDestroyTextureView(target.view);
  GPUDestroyTexture(target.texture);
  printf("tile storage depth=%u samples=%u: %u cases %s\n", depth, samples, checks, ok ? "pass" : "fail");
  return ok;
}

static bool
check_attachment_cases(GPUDevice                  *device,
                       GPUAdapter                 *adapter,
                       const GPUFormat            *formats,
                       const AttachmentBudgetCase *cases,
                       const char                 *label,
                       uint32_t                    samples,
                       uint32_t                    colorCount,
                       uint32_t                    caseCount) {
  GPURenderPassColorAttachment colors[8]  = {{0}};
  Target                      targets[8] = {{0}};
  GPURenderPassCreateInfo      info = {0};
  GPUTileRenderPassEXT         tile = {0};
  GPUFormatCapabilities       caps;
  GPUCommandBuffer           *command = NULL;
  GPURenderPassEncoder        *pass    = NULL;
  GPUQueue                   *queue;
  GPUFormat                   format;
  uint32_t                    i, checks = 0u;
  bool                        ok = false;

  if (formats) {
    for (i = 0u; i < colorCount; i++) {
      if (GPUGetFormatCapabilities(adapter, formats[i], &caps) != GPU_OK)
        return false;
      if (!caps.colorAttachment || !(caps.supportedSampleCounts & samples)) {
        printf("tile attachment%s storage samples=%u format=%u unsupported, subcase skipped\n",
               label, samples, formats[i]);
        return true;
      }
    }
  }

  for (i = 0u; i < colorCount; i++) {
    format = formats ? formats[i] : GPU_FORMAT_RGBA32_FLOAT;
    if (!create_target(device, format, samples, &targets[i]))
      goto cleanup;

    colors[i].view    = samples == 1u ? targets[i].view : targets[i].multisampleView;
    colors[i].loadOp  = GPU_LOAD_OP_CLEAR;
    colors[i].storeOp = GPU_STORE_OP_DONT_CARE;
  }

  tile.chain.sType      = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tile.chain.structSize = sizeof(tile);
  info.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.chain.pNext      = &tile;

  info.pColorAttachments = colors;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUAcquireCommandBuffer(queue, "tile-attachment-budget", &command) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < caseCount; i++) {
    if (cases[i].samples != samples)
      continue;

    info.colorAttachmentCount = cases[i].colors;

    tile.tileWidth               = cases[i].width;
    tile.tileHeight              = cases[i].height;
    tile.imageblockSampleLength  = cases[i].sampleLength;
    tile.threadgroupMemoryLength = cases[i].threadgroupLength;

    pass = GPUBeginRenderPass(command, &info);
    if ((pass != NULL) != cases[i].accepted) {
      fprintf(stderr, "tile attachment%s samples=%u case=%u accepted=%u expected=%u\n",
              label, samples, i, pass != NULL, cases[i].accepted);
      goto cleanup;
    }
    if (pass) {
      GPUEndRenderPass(pass);
      pass = NULL;
    } else {
      /* a rejected attachment layout must leave this command usable. */
      info.colorAttachmentCount    = formats ? 1u : 4u;
      tile.tileWidth               = 16u;
      tile.tileHeight              = 16u;
      tile.imageblockSampleLength  = formats ? 16u : 64u;
      tile.threadgroupMemoryLength = 0u;
      if (!(pass = GPUBeginRenderPass(command, &info)))
        goto cleanup;

      GPUEndRenderPass(pass);
      pass = NULL;
    }
    checks++;
  }
  ok = true;

cleanup:
  if (pass)
    GPUEndRenderPass(pass);
  if (command)
    GPUDiscardCommandBuffer(command);
  for (i = 0u; i < colorCount; i++) {
    GPUDestroyTextureView(targets[i].multisampleView);
    GPUDestroyTexture(targets[i].multisample);
    GPUDestroyTextureView(targets[i].view);
    GPUDestroyTexture(targets[i].texture);
  }
  printf("tile attachment%s storage samples=%u: %u cases %s\n",
         label, samples, checks, ok ? "pass" : "fail");
  return ok;
}

static bool
check_attachment_budget(GPUDevice *device, GPUAdapter *adapter, uint32_t samples) {
  const GPUFormat mixedFormats[] = {GPU_FORMAT_RGBA16_FLOAT, GPU_FORMAT_RGBA8_UNORM,
                                    GPU_FORMAT_RGBA32_FLOAT};
  const GPUFormat msaa8Formats[] = {GPU_FORMAT_RGBA8_UNORM, GPU_FORMAT_R8_UNORM};
  const AttachmentBudgetCase cases[] = {
    {1u, 4u, 16u, 16u,  0u,   512u, true},
    {1u, 5u, 16u, 16u,  0u,   512u, false},
    {1u, 7u, 16u, 16u,  0u,   512u, false},
    {1u, 8u, 16u, 16u,  0u,   512u, false},
    {1u, 5u, 16u, 16u, 32u,     0u, false},
    {1u, 5u, 16u, 16u, 32u,   512u, false},
    {1u, 4u, 16u, 16u, 32u, 16384u, true},
    {1u, 4u, 16u, 16u, 32u, 16400u, false},
    {1u, 4u, 16u, 16u, 16u, 16384u, true},
    {1u, 4u, 16u, 16u, 16u, 16400u, false},
    {1u, 4u, 32u, 16u, 16u,     0u, true},
    {1u, 4u, 32u, 16u, 16u,    16u, false},
    {1u, 4u, 32u, 32u, 16u,     0u, false},
    {1u, 4u, 16u, 16u, 64u, 16384u, true},
    {1u, 4u, 16u, 16u, 64u, 16400u, false},
    {1u, 4u, 16u, 16u,  0u, 16384u, true},
    {1u, 4u, 16u, 16u,  0u, 16400u, false},
    {1u, 1u, 16u, 16u, 17u, 26624u, true},
    {1u, 1u, 16u, 16u, 17u, 26640u, false},

    /* the 12- and 28-byte layouts reserve 16 and 32 bytes per sample. */
    {2u, 2u, 32u, 32u,  8u,     0u, true},
    {2u, 2u, 32u, 32u,  8u,    16u, false},
    {2u, 3u, 32u, 16u, 16u,     0u, true},
    {2u, 3u, 32u, 16u, 16u,    16u, false},
    {2u, 3u, 16u, 16u, 33u, 12288u, true},
    {2u, 3u, 16u, 16u, 33u, 12304u, false},
    {4u, 2u, 16u, 16u,  0u, 16384u, true},
    {4u, 2u, 16u, 16u,  0u, 16400u, false},
    {4u, 3u, 16u, 16u, 16u,     0u, true},
    {4u, 3u, 16u, 16u, 16u,    16u, false},
    {4u, 2u, 32u, 16u, 12u,     0u, true},
    {4u, 2u, 32u, 16u, 12u,    16u, false},

    /* the 5-byte layout reserves 8 bytes unless the declaration is larger. */
    {8u, 2u, 16u, 16u,  0u, 16384u, true},
    {8u, 2u, 16u, 16u,  0u, 16400u, false},
    {8u, 2u,  0u,  0u,  0u, 16384u, true},
    {8u, 2u,  0u,  0u,  0u, 16400u, false},
    {8u, 2u, 16u, 16u,  9u,     0u, true},
    {8u, 2u, 16u, 16u,  9u,    16u, false}
  };
  const GPUFormat *formats;
  uint32_t         colorCount;

  formats    = mixedFormats;
  colorCount = GPU_ARRAY_LEN(mixedFormats);
  if (samples == 1u) {
    formats    = NULL;
    colorCount = 8u;
  } else if (samples == 8u) {
    formats    = msaa8Formats;
    colorCount = GPU_ARRAY_LEN(msaa8Formats);
  }

  return check_attachment_cases(device, adapter, formats, cases, "", samples,
                                colorCount, GPU_ARRAY_LEN(cases));
}

static bool
check_packed_attachment_budget(GPUDevice *device, GPUAdapter *adapter, uint32_t samples) {
  const struct {
    const char *label;
    GPUFormat   formats[4];
    uint32_t    colorCount, sampleBytes;
  } layouts[] = {
    {" packed uint", {GPU_FORMAT_RGB10A2_UINT, GPU_FORMAT_R8_UINT}, 2u, 16u},
    {" packed first", {GPU_FORMAT_RGB10A2_UNORM, GPU_FORMAT_RG8_UNORM, GPU_FORMAT_R8_UNORM}, 3u, 8u},
    {" packed after r8", {GPU_FORMAT_R8_UNORM, GPU_FORMAT_RGB10A2_UNORM, GPU_FORMAT_RG8_UNORM}, 3u, 16u},
    {" rg16 after r8", {GPU_FORMAT_R8_UNORM, GPU_FORMAT_RG16_FLOAT, GPU_FORMAT_RG8_UNORM,
                        GPU_FORMAT_R8_UNORM}, 4u, 16u}
  };
  const GPUFormat limitFormats[] = {
    GPU_FORMAT_R8_UNORM, GPU_FORMAT_RGBA32_FLOAT, GPU_FORMAT_R8_UNORM, GPU_FORMAT_RGBA32_FLOAT,
    GPU_FORMAT_R8_UNORM, GPU_FORMAT_RGBA32_FLOAT, GPU_FORMAT_RGBA16_FLOAT
  };
  const AttachmentBudgetCase limitCases[] = {
    {1u, 7u, 16u, 16u, 0u, 0u, true},
    {1u, 7u, 16u, 16u, 8u, 0u, false}
  };
  AttachmentBudgetCase cases[2];
  uint32_t             i, persistent;
  bool                 ok = true;

  for (i = 0u; i < GPU_ARRAY_LEN(layouts); i++) {
    persistent = 32768u - 16u * 16u * samples * layouts[i].sampleBytes;
    cases[0]   = (AttachmentBudgetCase){
      samples, layouts[i].colorCount, 16u, 16u, persistent ? 0u : 8u, persistent, true
    };
    cases[1] = cases[0];
    cases[1].threadgroupLength += 16u;
    cases[1].accepted           = false;

    ok = check_attachment_cases(device, adapter, layouts[i].formats, cases, layouts[i].label, samples,
                                layouts[i].colorCount, GPU_ARRAY_LEN(cases)) && ok;
  }

  if (samples == 1u)
    ok = check_attachment_cases(device, adapter, limitFormats, limitCases, " padded limit", samples,
                                GPU_ARRAY_LEN(limitFormats), GPU_ARRAY_LEN(limitCases)) && ok;

  return ok;
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

static void
expected_uniform_pixel(uint32_t x, uint32_t y, uint32_t samples, float expected[4]) {
  float    values[4];
  uint32_t groupX, groupY, steps, source, sample, c;
  float    scale, gain;

  groupX = x / 16u;
  groupY = y / 16u;
  steps  = ((groupX + groupY) & 1u) ? 1u : 2u;
  source = (x % 16u + (y % 16u) * 16u + 17u * steps) % 256u;
  scale  = steps == 2u ? 0.25f : 0.5f;
  gain   = steps == 2u ? 1.5f : 1.0f;

  memset(expected, 0, 4u * sizeof(*expected));
  for (sample = 0u; sample < samples; sample++) {
    values[0] = (float)(source % 16u) / 32.0f * scale + 0.125f * gain;
    values[1] = (float)(source / 16u) / 64.0f * scale + 0.0625f * gain;
    values[2] = (0.25f + (float)sample / 16.0f) * scale + 0.03125f * gain;
    values[3] = (0.5f + (float)(groupX + groupY * 2u) / 16.0f) * scale + 0.25f * gain;
    for (c = 0u; c < 4u; c++) {
      expected[c] += values[c] / (float)samples;
    }
  }
}

static void
expected_loop_pixel(uint32_t x, uint32_t y, uint32_t samples, float expected[4]) {
  float    values[4];
  uint32_t groupX, groupY, steps, source, sample, c;
  float    scale, gain;

  groupX = x / 16u;
  groupY = y / 16u;
  steps  = (groupX + groupY * 2u) & 3u;
  source = (x % 16u + (y % 16u) * 16u + 17u * steps) % 256u;
  scale  = 1.0f / (float)(1u << steps);
  gain   = 2.0f * (1.0f - scale);

  memset(expected, 0, 4u * sizeof(*expected));
  for (sample = 0u; sample < samples; sample++) {
    values[0] = (float)(source % 16u) / 32.0f * scale + 0.125f * gain;
    values[1] = (float)(source / 16u) / 64.0f * scale + 0.0625f * gain;
    values[2] = (0.25f + (float)sample / 16.0f) * scale + 0.03125f * gain;
    values[3] = (0.5f + (float)(groupX + groupY * 2u) / 16.0f) * scale + 0.25f * gain;
    for (c = 0u; c < 4u; c++) {
      expected[c] += values[c] / (float)samples;
    }
  }
}

static void
expected_mixed_pixel(uint32_t x, uint32_t y, uint32_t samples, float expected[4]) {
  float    values[4];
  uint32_t groupX, groupY, source, neighborX, neighborY, sample, c;

  groupX    = x / 16u;
  groupY    = y / 16u;
  source    = (x % 16u + (y % 16u) * 16u + 17u) % 256u;
  neighborX = source % 16u;
  neighborY = source / 16u;

  memset(expected, 0, 4u * sizeof(*expected));
  for (sample = 0u; sample < samples; sample++) {
    values[0] = 0.125f + (float)neighborX / 32.0f + (float)(1u + neighborX) / 512.0f
                + ((float)neighborX - 8.0f) / 8192.0f;
    values[1] = 0.25f + (float)neighborY / 64.0f + (float)(2u + neighborY) / 1024.0f
                + (float)(7u + groupX) / 16384.0f;
    values[2] = 0.375f + (float)sample / 16.0f + (float)(3u + sample) / 128.0f
                + (float)(4u + neighborY) / 4096.0f + (float)(11u + groupY) / 16384.0f;
    values[3] = 0.5f + (float)(groupX + groupY * 2u) / 16.0f
                + (float)(1234u + neighborX + neighborY * 16u + sample * 256u) / 65536.0f;
    for (c = 0u; c < 4u; c++) {
      expected[c] += values[c] / (float)samples;
    }
  }
}

static void
expected_small_pixel(uint32_t x, uint32_t y, uint32_t samples, float expected[4]) {
  float    values[4];
  uint32_t groupX, groupY, source, neighborX, neighborY, sample, c;

  groupX    = x / 16u;
  groupY    = y / 16u;
  source    = (x % 16u + (y % 16u) * 16u + 17u) % 256u;
  neighborX = source % 16u;
  neighborY = source / 16u;

  memset(expected, 0, 4u * sizeof(*expected));
  for (sample = 0u; sample < samples; sample++) {
    values[0] = 0.125f + (float)neighborX / 32.0f + ((float)neighborX - 8.0f) / 8192.0f;
    values[1] = 0.25f + (float)neighborY / 64.0f;
    values[2] = 0.375f + (float)sample / 16.0f + (float)(groupX + groupY * 2u) / 128.0f;
    values[3] = (float)(1234u + neighborX + neighborY * 16u + sample * 256u) / 65536.0f;
    for (c = 0u; c < 4u; c++) {
      expected[c] += values[c] / (float)samples;
    }
  }
}

static bool
check_kernel(GPUDevice        *device,
             GPUShaderLibrary *library,
             uint32_t          samples,
             uint32_t          threads,
             KernelKind        kind) {
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
  const char                 *initEntry;
  const char                 *resolveEntry;
  const GPUBindGroupLayoutEntry *layoutEntries;
  const GPUFormat             format     = GPU_FORMAT_RGBA32_FLOAT;
  uint32_t                    config[4]  = {KERNEL_WIDTH, KERNEL_HEIGHT, threads, 37u};
  uint32_t                    count, width, height, length, x, y, c;
  bool                        resources;
  bool                        ok = false;

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "tile kernel%s samples=%u threads=%u line=%u: %s\n", \
            kernelLabels[kind], samples, threads, __LINE__, #expr); \
    goto cleanup; \
  } \
} while (0)

  queue     = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  resources = kind == KERNEL_NEIGHBOR || kind == KERNEL_MIXED || kind == KERNEL_MIXED_SMALL;

  if (kind == KERNEL_MIXED) {
    initEntry    = "kernel_mixed_init_fs";
    resolveEntry = "kernel_mixed_resolve_fs";
  } else if (kind == KERNEL_MIXED_SMALL) {
    initEntry    = "kernel_mixed_small_init_fs";
    resolveEntry = "kernel_mixed_small_resolve_fs";
  } else {
    initEntry    = "kernel_init_fs";
    resolveEntry = "kernel_resolve_fs";
  }

  REQUIRE(kind == KERNEL_NEIGHBOR || threads == 16u);
  REQUIRE(create_target(device, GPU_FORMAT_RGBA32_FLOAT, samples, &target));
  REQUIRE(GPUCreateShaderLayout(device, library, &layout) == GPU_OK);
  REQUIRE(layout->bindGroupLayoutCount == 1u);
  layoutEntries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0], &count);
  REQUIRE(layoutEntries && count == 1u
          && layoutEntries[0].binding == 3u
          && layoutEntries[0].bindingType == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
          && layoutEntries[0].visibility == GPU_SHADER_STAGE_COMPUTE_BIT);
  REQUIRE(create_draw(device, library, layout, initEntry, samples, &init) == GPU_OK);
  REQUIRE(create_draw(device, library, layout, resolveEntry, samples, &resolve) == GPU_OK);

  tileInfo.chain.sType      = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tileInfo.chain.structSize = sizeof(tileInfo);
  tileInfo.library              = library;
  tileInfo.layout               = resources ? layout->pipelineLayout : NULL;
  tileInfo.entryPoint           = kernelEntries[kind];
  tileInfo.pColorFormats        = &format;
  tileInfo.colorAttachmentCount = 1u;
  tileInfo.sampleCount          = samples;
  REQUIRE(GPUCreateTilePipeline(device, &tileInfo, &kernel) == GPU_OK);
  REQUIRE(GPUGetTilePipelineProperties(kernel, &properties) == GPU_OK);
  REQUIRE(properties.imageblockSampleLength && properties.maxThreadsPerThreadgroup >= threads * threads);
  if (kind == KERNEL_MIXED || kind == KERNEL_MIXED_SMALL)
    printf("tile kernel%s imageblock sample length=%u\n", kernelLabels[kind], properties.imageblockSampleLength);

  if (resources) {
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
  }

  REQUIRE(GPUAcquireCommandBuffer(queue, "tile-kernel-neighbors", &command) == GPU_OK);
  if (resources) {
    barrier.buffer    = buffer;
    barrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
    barrier.dstAccess = GPU_ACCESS_SHADER_READ;
    barriers.srcStages          = GPU_STAGE_TRANSFER;
    barriers.dstStages          = GPU_STAGE_TILE;
    barriers.pBufferBarriers    = &barrier;
    barriers.bufferBarrierCount = 1u;
    GPUEncodeBarriers(command, &barriers);
  }

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

  /* the 2x fixture combines 16 KiB of imageblock and 16 KiB of persistent memory. */
  if (samples == 2u && tilePass.imageblockSampleLength == 32u)
    tilePass.threadgroupMemoryLength = 16384u;

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
  if (resources) {
    REQUIRE(GPUDispatchThreadsPerTile(pass, threads, threads) == GPU_ERROR_INVALID_ARGUMENT);
    REQUIRE(GPUBindTileGroup(pass, 0u, group, 0u, NULL) == GPU_OK);
  }
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
      if (kind == KERNEL_MIXED_SMALL)
        expected_small_pixel(x, y, samples, expected);
      else if (kind == KERNEL_MIXED)
        expected_mixed_pixel(x, y, samples, expected);
      else if (kind == KERNEL_LOOP)
        expected_loop_pixel(x, y, samples, expected);
      else if (kind == KERNEL_UNIFORM)
        expected_uniform_pixel(x, y, samples, expected);
      else
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
  printf("tile kernel%s samples=%u dispatch=%ux%u neighbor/barrier exact2356=%s"
         " sampleBytes=%u threadgroupBytes=%u\n",
         kernelLabels[kind], samples, threads, threads, ok ? "pass" : "fail",
         tilePass.imageblockSampleLength, tilePass.threadgroupMemoryLength);
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
    for (i = 1u; i <= 8u; i *= 2u) {
      ok = check_attachment_budget(device, adapter, i) && ok;
      ok = check_packed_attachment_budget(device, adapter, i) && ok;
    }
    ok = check_storage_budget(device, 1u, false) && ok;
    ok = check_storage_budget(device, 1u, true) && ok;
    ok = check_kernel(device, library, 1u, 16u, KERNEL_NEIGHBOR) && ok;
    ok = check_kernel(device, library, 1u, 8u, KERNEL_NEIGHBOR) && ok;
    ok = check_kernel(device, library, 1u, 16u, KERNEL_UNIFORM) && ok;
    ok = check_kernel(device, library, 1u, 16u, KERNEL_LOOP) && ok;
    ok = check_kernel(device, library, 1u, 16u, KERNEL_MIXED) && ok;
    ok = check_kernel(device, library, 1u, 16u, KERNEL_MIXED_SMALL) && ok;
    if (GPUGetFormatCapabilities(adapter, GPU_FORMAT_RGBA32_FLOAT, &caps) != GPU_OK) {
      ok = false;
      goto cleanup;
    }

    if (caps.supportedSampleCounts & GPU_SAMPLE_COUNT_2_BIT) {
      ok = check_kernel(device, library, 2u, 16u, KERNEL_NEIGHBOR) && ok;
      ok = check_kernel(device, library, 2u, 8u, KERNEL_NEIGHBOR) && ok;
    } else {
      printf("tile kernel2x MSAA: RGBA32_FLOAT unsupported, subcase skipped\n");
    }

    if (caps.supportedSampleCounts & GPU_SAMPLE_COUNT_4_BIT) {
      ok = check_storage_budget(device, 4u, false) && ok;
      ok = check_storage_budget(device, 4u, true) && ok;
      ok = check_kernel(device, library, 4u, 16u, KERNEL_NEIGHBOR) && ok;
      ok = check_kernel(device, library, 4u, 8u, KERNEL_NEIGHBOR) && ok;
      ok = check_kernel(device, library, 4u, 16u, KERNEL_UNIFORM) && ok;
      ok = check_kernel(device, library, 4u, 16u, KERNEL_LOOP) && ok;
      /* the wide master exceeds the native 4x tile budget at 16x16. */
      ok = check_kernel(device, library, 4u, 16u, KERNEL_MIXED_SMALL) && ok;
    } else {
      printf("tile kernel4x MSAA: RGBA32_FLOAT unsupported, subcase skipped\n");
    }

    if (caps.supportedSampleCounts & GPU_SAMPLE_COUNT_8_BIT)
      ok = check_storage_budget(device, 8u, false) && ok;

cleanup:
    GPUDestroyShaderLibrary(library);
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
