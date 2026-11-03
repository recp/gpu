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

#include "../../src/backend/mt/common.h"
#include <math.h>

enum {
  FETCH_WIDTH  = 16,
  FETCH_HEIGHT = 8,
  FETCH_ROW    = FETCH_WIDTH * 4 * sizeof(float),
  FETCH_IMAGE  = FETCH_HEIGHT * FETCH_ROW
};

typedef struct Target {
  GPUTexture     *texture;
  GPUTextureView *view;
} Target;

static const float kClear[2][4] = {
  {0.125f, 0.25f, 0.375f, 0.5f},
  {0.625f, 0.75f, 0.875f, 1.0f}
};

static const GPUScissorRect kScissors[] = {
  {0, 0, 16, 8}, {0, 0, 9, 8}, {4, 2, 9, 4},
  {0, 4, 16, 4}, {7, 1, 9, 5}, {2, 0, 7, 8}
};

static bool
create_target(GPUDevice *device, uint32_t samples, Target *target) {
  GPUTextureCreateInfo     info = {0};
  GPUTextureViewCreateInfo view = {0};
  GPUResult               result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.dimension        = GPU_TEXTURE_DIMENSION_2D;
  info.format           = GPU_FORMAT_RGBA32_FLOAT;
  info.width            = FETCH_WIDTH;
  info.height           = FETCH_HEIGHT;
  info.depthOrLayers    = 1u;
  info.mipLevelCount    = 1u;
  info.sampleCount      = samples;
  info.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  if (samples == 1u)
    info.usage |= GPU_TEXTURE_USAGE_COPY_SRC;

  view.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  view.chain.structSize = sizeof(view);
  view.viewType         = GPU_TEXTURE_VIEW_2D;
  view.format           = info.format;
  view.mipLevelCount    = 1u;
  view.arrayLayerCount  = 1u;

  result = GPUCreateTexture(device, &info, &target->texture);
  if (result == GPU_OK)
    result = GPUCreateTextureView(target->texture, &view, &target->view);

  if (result != GPU_OK)
    fprintf(stderr, "fetch target samples=%u: result=%d\n", samples, result);
  return result == GPU_OK;
}

static GPUResult
create_pipeline(GPUDevice          *device,
                GPUShaderLibrary   *library,
                GPUShaderLayout    *layout,
                const char         *fragment,
                GPUFormat           format,
                uint32_t            count,
                uint32_t            samples,
                GPURenderPipeline **pipeline) {
  GPUColorTargetState         colors[2] = {{0}};
  GPURenderPipelineCreateInfo info      = {0};

  colors[0].format         = format;
  colors[0].blend.writeMask = GPU_COLOR_WRITE_ALL;
  colors[1]                 = colors[0];
  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.library             = library;
  info.layout              = layout->pipelineLayout;
  info.vertexEntry          = !strcmp(fragment, "fetch_seed_fs") ? "fetch_sample_vs" : "fetch_vs";
  info.fragmentEntry        = fragment;
  info.pColorTargets        = colors;
  info.colorTargetCount     = count;
  info.primitiveTopology   = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode            = GPU_CULL_MODE_NONE;
  info.frontFace           = GPU_FRONT_FACE_CCW;

  info.multisample.sampleCount = samples;
  info.multisample.sampleMask  = UINT32_MAX;

  return GPUCreateRenderPipeline(device, &info, pipeline);
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
  batch.srcStages            = GPU_STAGE_FRAGMENT;
  batch.dstStages            = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(command, &batch);
  if (!(pass = GPUBeginTransferPass(command, "framebuffer-fetch-readback")))
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
apply_pair(float values[2][4], bool second) {
  float a[4], b[4];

  memcpy(a, values[0], sizeof(a));
  memcpy(b, values[1], sizeof(b));
  if (!second) {
    values[0][0] = a[1] * 0.5f + 0.125f;
    values[0][1] = a[2] * 0.25f + 0.25f;
    values[0][2] = a[0] * 0.5f + 0.0625f;
    values[0][3] = a[3] * 0.5f + 0.25f;
    values[1][0] = b[3] * 0.5f + a[0] * 0.25f;
    values[1][1] = b[0] * 0.5f + a[2] * 0.25f;
    values[1][2] = b[1] * 0.5f + 0.125f;
    values[1][3] = b[2] * 0.25f + 0.5f;
  } else {
    values[0][0] = a[2] * 0.25f + b[1] * 0.5f;
    values[0][1] = a[3] * 0.5f + 0.0625f;
    values[0][2] = a[1] * 0.25f + 0.125f;
    values[0][3] = a[0] * 0.5f + 0.375f;
    values[1][0] = b[1] * 0.5f + 0.0625f;
    values[1][1] = b[2] * 0.25f + a[3] * 0.5f;
    values[1][2] = b[3] * 0.5f + 0.125f;
    values[1][3] = b[0] * 0.25f + 0.25f;
  }
}

static bool
check_order(GPUDevice *device, GPUShaderLibrary *library, GPUShaderLayout *layout) {
  float                       pixels[2][FETCH_HEIGHT][FETCH_WIDTH][4];
  float                       expected[2][4];
  GPURenderPassColorAttachment colors[2] = {{0}};
  GPURenderPassCreateInfo      info      = {0};
  GPUTextureBarrier           barriers[2] = {{0}};
  GPUBarrierBatch             batch       = {0};
  Target                      targets[2] = {{0}};
  GPURenderPipeline          *pipelines[3] = {NULL};
  GPURenderPassEncoder       *pass;
  GPUCommandBuffer           *command = NULL;
  GPUQueue                   *queue;
  const GPUScissorRect        *rect;
  uint32_t                    i, phase, x, y, attachment, channel;
  bool                        ok = false;

  if (!create_target(device, 1u, &targets[0])
      || !create_target(device, 1u, &targets[1])
      || create_pipeline(device, library, layout, "fetch_pair_a_fs",
                         GPU_FORMAT_RGBA32_FLOAT, 2u, 1u, &pipelines[0]) != GPU_OK
      || create_pipeline(device, library, layout, "fetch_pair_b_fs",
                         GPU_FORMAT_RGBA32_FLOAT, 2u, 1u, &pipelines[1]) != GPU_OK
      || create_pipeline(device, library, layout, "fetch_cross_fs",
                         GPU_FORMAT_RGBA32_FLOAT, 2u, 1u, &pipelines[2]) != GPU_OK)
    goto cleanup;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUAcquireCommandBuffer(queue, "framebuffer-fetch-order", &command) != GPU_OK)
    goto cleanup;

  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.pColorAttachments    = colors;
  info.colorAttachmentCount = 2u;
  for (i = 0u; i < 2u; i++) {
    colors[i].view    = targets[i].view;
    colors[i].loadOp  = GPU_LOAD_OP_CLEAR;
    colors[i].storeOp = GPU_STORE_OP_STORE;
    memcpy(colors[i].clearColor.float32, kClear[i], sizeof(kClear[i]));
    barriers[i].texture    = targets[i].texture;
    barriers[i].srcAccess  = GPU_ACCESS_COLOR_WRITE;
    barriers[i].dstAccess  = GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE;
    barriers[i].mipCount   = 1u;
    barriers[i].layerCount = 1u;
  }
  batch.pTextureBarriers     = barriers;
  batch.textureBarrierCount = 2u;
  batch.srcStages            = GPU_STAGE_FRAGMENT;
  batch.dstStages            = GPU_STAGE_FRAGMENT;

  for (phase = 0u; phase < 2u; phase++) {
    if (!(pass = GPUBeginRenderPass(command, &info)))
      goto cleanup;

    for (i = phase ? 3u : 0u; i < (phase ? 6u : 3u); i++) {
      GPUBindRenderPipeline(pass, pipelines[i == 5u ? 2u : i & 1u]);
      GPUSetScissor(pass, &kScissors[i]);
      GPUDraw(pass, 3u, 1u, 0u, 0u);
    }
    GPUEndRenderPass(pass);
    /* separate passes need a dependency even though fetch orders draws within a pass. */
    if (!phase)
      GPUEncodeBarriers(command, &batch);

    colors[0].loadOp = GPU_LOAD_OP_LOAD;
    colors[1].loadOp = GPU_LOAD_OP_LOAD;
  }

  ok      = read_targets(device, command, targets, 2u, &pixels[0][0][0][0]);
  command = NULL;
  if (!ok)
    goto cleanup;

  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      memcpy(expected, kClear, sizeof(expected));
      for (i = 0u; i < 6u; i++) {
        rect = &kScissors[i];
        if (x >= (uint32_t)rect->x && x < (uint32_t)rect->x + rect->width
            && y >= (uint32_t)rect->y && y < (uint32_t)rect->y + rect->height) {
          if (i == 5u) {
            expected[0][0] = expected[1][0] * 0.5f + 0.125f;
            expected[0][1] = expected[1][1] * 0.5f + 0.25f;
            expected[0][2] = expected[1][2] * 0.5f + 0.0625f;
            expected[0][3] = expected[1][3] * 0.5f + 0.125f;
          } else {
            apply_pair(expected, (i & 1u) != 0u);
          }
        }
      }
      for (attachment = 0u; attachment < 2u; attachment++) {
        for (channel = 0u; channel < 4u; channel++) {
          if (pixels[attachment][y][x][channel] != expected[attachment][channel]) {
            fprintf(stderr, "fetch order attachment=%u pixel=%u,%u channel=%u: %a != %a\n",
                    attachment, x, y, channel,
                    pixels[attachment][y][x][channel], expected[attachment][channel]);
            ok = false;
            goto cleanup;
          }
        }
      }
    }
  }

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);

  GPUDestroyRenderPipeline(pipelines[2]);
  for (i = 0u; i < 2u; i++) {
    GPUDestroyRenderPipeline(pipelines[i]);
    GPUDestroyTextureView(targets[i].view);
    GPUDestroyTexture(targets[i].texture);
  }
  printf("framebuffer fetch: ordered draws, MRT/read-only attachment, clear/load, exact CPU oracle=%s\n",
         ok ? "pass" : "fail");
  return ok;
}

static bool
check_msaa(GPUDevice *device, GPUShaderLibrary *library, GPUShaderLayout *layout) {
  float                       pixels[FETCH_HEIGHT][FETCH_WIDTH][4];
  float                       expected[4] = {0}, sample[4];
  const float                 bias[4] = {0.0625f, 0.125f, 0.25f, 0.125f};
  MTLSamplePosition           positions[4];
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUFormatCapabilities       caps  = {0};
  Target                      source = {0}, resolve = {0};
  GPURenderPipeline          *seed = NULL, *square = NULL;
  GPURenderPassEncoder       *pass;
  GPUCommandBuffer           *command = NULL;
  GPUDeviceMT                *native;
  GPUQueue                   *queue;
  uint32_t                    s, c, x, y;
  bool                        ok = false;

  if (GPUGetFormatCapabilities(device->adapter, GPU_FORMAT_RGBA32_FLOAT, &caps) != GPU_OK)
    return false;

  if (!(caps.supportedSampleCounts & GPU_SAMPLE_COUNT_4_BIT)) {
    printf("framebuffer fetch: RGBA32_FLOAT 4x MSAA unsupported, MSAA subcase skipped\n");
    return true;
  }

  /* the quantized interpolant distinguishes per-sample fetch from pixel broadcast. */
  native = device->_priv;
  [native->device getDefaultSamplePositions:positions count:4u];
  for (s = 0u; s < 4u; s++) {
    sample[0] = (floorf(positions[s].x * 4.0f) + 1.0f) * 0.125f;
    sample[1] = sample[0] + 0.125f;
    sample[2] = sample[0] * 0.5f;
    sample[3] = 0.5f;
    for (c = 0u; c < 4u; c++) {
      expected[c] += (sample[c] * sample[c] + bias[c]) * 0.25f;
    }
  }

  if (!create_target(device, 4u, &source)
      || !create_target(device, 1u, &resolve)
      || create_pipeline(device, library, layout, "fetch_seed_fs",
                         GPU_FORMAT_RGBA32_FLOAT, 1u, 4u, &seed) != GPU_OK
      || create_pipeline(device, library, layout, "fetch_square_fs",
                         GPU_FORMAT_RGBA32_FLOAT, 1u, 4u, &square) != GPU_OK)
    goto cleanup;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUAcquireCommandBuffer(queue, "framebuffer-fetch-msaa", &command) != GPU_OK)
    goto cleanup;

  color.view        = source.view;
  color.resolveView = resolve.view;
  color.loadOp      = GPU_LOAD_OP_CLEAR;
  color.storeOp     = GPU_STORE_OP_STORE;
  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.pColorAttachments    = &color;
  info.colorAttachmentCount = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info)))
    goto cleanup;

  GPUBindRenderPipeline(pass, seed);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUBindRenderPipeline(pass, square);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  ok      = read_targets(device, command, &resolve, 1u, &pixels[0][0][0]);
  command = NULL;
  if (!ok)
    goto cleanup;

  for (y = 0u; y < FETCH_HEIGHT; y++) {
    for (x = 0u; x < FETCH_WIDTH; x++) {
      for (c = 0u; c < 4u; c++) {
        if (pixels[y][x][c] != expected[c]) {
          fprintf(stderr, "fetch MSAA pixel=%u,%u channel=%u: %a != %a\n",
                  x, y, c, pixels[y][x][c], expected[c]);
          ok = false;
          goto cleanup;
        }
      }
    }
  }

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyRenderPipeline(square);
  GPUDestroyRenderPipeline(seed);
  GPUDestroyTextureView(resolve.view);
  GPUDestroyTexture(resolve.texture);
  GPUDestroyTextureView(source.view);
  GPUDestroyTexture(source.texture);
  printf("framebuffer fetch: 4x MSAA nonlinear per-sample fetch/resolve=%s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    GPUDeviceCreateInfo info = {0};
    const GPUFeature   feature = GPU_FEATURE_FRAMEBUFFER_FETCH;
    GPUInstance       *instance = NULL;
    GPUAdapter        *adapter;
    GPUDevice         *device = NULL, *disabled = NULL;
    GPUShaderLibrary  *library = NULL, *rejected = NULL;
    GPUShaderLayout   *layout = NULL;
    GPURenderPipeline *pipeline = NULL;
    NSData            *data;
    GPUResult          result;
    bool               ok = false;

    if (argc != 2 || !(data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]]))
      return 1;

    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      goto cleanup;

    if (!GPUIsFeatureSupported(adapter, feature)) {
      GPUDestroyInstance(instance);
      return 77;
    }

    if (GPUCreateDevice(adapter, NULL, &disabled) != GPU_OK
        || GPUIsFeatureEnabled(disabled, feature))
      goto cleanup;

    result = GPUCreateShaderLibraryFromUSL(disabled, [data bytes], [data length], &rejected);
    if (result != GPU_ERROR_UNSUPPORTED || rejected) {
      fprintf(stderr, "disabled framebuffer fetch accepted: %d\n", result);
      goto cleanup;
    }
    GPUDestroyDevice(disabled);
    disabled = NULL;

    info.required.pFeatures    = &feature;
    info.required.featureCount = 1u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK
        || !GPUIsFeatureEnabled(device, feature)
        || GPUCreateShaderLibraryFromUSL(device, [data bytes], [data length], &library) != GPU_OK
        || GPUCreateShaderLayout(device, library, &layout) != GPU_OK)
      goto cleanup;

    result = create_pipeline(device, library, layout, "fetch_cross_fs",
                             GPU_FORMAT_RGBA32_FLOAT, 1u, 1u, &pipeline);
    if (result != GPU_ERROR_INVALID_ARGUMENT || pipeline) {
      fprintf(stderr, "missing fetched attachment accepted: %d\n", result);
      goto cleanup;
    }

    result = create_pipeline(device, library, layout, "fetch_single_fs",
                             GPU_FORMAT_RGBA32_UINT, 1u, 1u, &pipeline);
    if (result != GPU_ERROR_BACKEND_FAILURE || pipeline) {
      fprintf(stderr, "incompatible fetched attachment format accepted: %d\n", result);
      goto cleanup;
    }

    result = create_pipeline(device, library, layout, "fetch_uint_fs",
                             GPU_FORMAT_RGBA32_UINT, 2u, 1u, &pipeline);
    if (result != GPU_ERROR_BACKEND_FAILURE || pipeline) {
      fprintf(stderr, "incompatible read-only fetch format accepted: %d\n", result);
      goto cleanup;
    }

    ok = check_order(device, library, layout);
    ok = check_msaa(device, library, layout) && ok;

cleanup:
    GPUDestroyRenderPipeline(pipeline);
    GPUDestroyShaderLayout(layout);
    GPUDestroyShaderLibrary(library);
    GPUDestroyShaderLibrary(rejected);
    GPUDestroyDevice(device);
    GPUDestroyDevice(disabled);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
