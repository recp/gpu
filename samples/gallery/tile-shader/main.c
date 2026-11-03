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

#include "../../common/sample_platform.h"
#include <gpu/tile.h>

typedef struct TileSample {
  GPUInstance       *instance;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *layout;
  GPURenderPipeline *draw;
  GPUTilePipeline   *initialize;
  GPUTilePipeline   *resolve;
  GPUBuffer         *resolveBuffer;
  GPUBindGroup      *resolveGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           imageblockBytes;
} TileSample;

static TileSample app;

static int
prepare(TileSample *state, GPUDevice *device) {
  GPURenderPipelineCreateInfo render = {0};
  GPUTilePipelineCreateInfo   tile   = {0};
  GPUTilePipelineProperties   properties;
  GPUBufferCreateInfo         buffer = {0};
  GPUBindGroupCreateInfo      group  = {0};
  GPUBindGroupEntry           entry  = {0};
  GPUColorTargetState         color  = {0};
  const float                gain[4] = {1.0f, 0.875f, 1.0f, 1.0f};
  void                      *artifact;
  uint64_t                   size;
  GPUFormat                  format;
  uint32_t                   bytes;
  GPUResult                  result;

  if (!read_file("/tile.us", &artifact, &size))
    return 0;

  result = GPUCreateShaderLibraryFromUSL(device, artifact, size, &state->library);
  free(artifact);
  if (result != GPU_OK
      || GPUCreateShaderLayout(device, state->library, &state->layout) != GPU_OK)
    return 0;

  format                = GPUGetSwapchainFormat(state->swapchain);
  color.format          = format;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  render.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  render.chain.structSize        = sizeof(render);
  render.layout                  = state->layout->pipelineLayout;
  render.library                 = state->library;
  render.vertexEntry             = "scene_vs";
  render.fragmentEntry           = "scene_fs";
  render.pColorTargets           = &color;
  render.colorTargetCount        = 1u;
  render.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  render.cullMode                = GPU_CULL_MODE_NONE;
  render.multisample.sampleCount = 1u;
  render.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &render, &state->draw) != GPU_OK)
    return 0;

  tile.chain.sType                    = GPU_STRUCTURE_TYPE_TILE_PIPELINE_CREATE_INFO;
  tile.chain.structSize               = sizeof(tile);
  tile.library                        = state->library;
  tile.pColorFormats                  = &format;
  tile.colorAttachmentCount           = 1u;
  tile.sampleCount                    = 1u;
  tile.threadgroupSizeMatchesTileSize = false;
  tile.entryPoint                     = "init_tile";
  if (GPUCreateTilePipeline(device, &tile, &state->initialize) != GPU_OK
      || GPUGetTilePipelineProperties(state->initialize, &properties) != GPU_OK)
    return 0;

  state->imageblockBytes = properties.imageblockSampleLength;
  tile.entryPoint       = "resolve_tile";
  tile.layout           = state->layout->pipelineLayout;
  if (GPUCreateTilePipeline(device, &tile, &state->resolve) != GPU_OK
      || GPUGetTilePipelineProperties(state->resolve, &properties) != GPU_OK)
    return 0;

  bytes = GPUGetRenderPipelineImageblockSampleLength(state->draw);
  if (bytes > state->imageblockBytes)
    state->imageblockBytes = bytes;
  if (properties.imageblockSampleLength > state->imageblockBytes)
    state->imageblockBytes = properties.imageblockSampleLength;

  buffer.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer.chain.structSize = sizeof(buffer);
  buffer.sizeBytes        = sizeof(gain);
  buffer.usage            = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &buffer, &state->resolveBuffer) != GPU_OK
      || GPUQueueWriteBuffer(state->queue, state->resolveBuffer, 0u, gain, sizeof(gain)) != GPU_OK)
    return 0;

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entry.buffer.buffer = state->resolveBuffer;
  entry.buffer.size   = sizeof(gain);
  group.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  group.chain.structSize = sizeof(group);
  group.layout           = state->layout->bindGroupLayouts[0];
  group.pEntries         = &entry;
  group.entryCount       = 1u;
  return state->imageblockBytes != 0u
         && GPUCreateBindGroup(device, &group, &state->resolveGroup) == GPU_OK;
}

static void
render(void *user) {
  GPURenderPassCreateInfo      info = {0};
  GPURenderPassColorAttachment color = {0};
  GPUTileRenderPassEXT         tile = {0};
  const float                 bias[4] = {0.015625f, 0.0f, 0.03125f, 0.0f};
  TileSample                 *state;
  GPUFrame                   *frame;
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  uint32_t                    width, height;
  bool                        ok;

  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)
      || !(frame = GPUBeginFrame(state->swapchain)))
    return;
  if (GPUAcquireCommandBuffer(state->queue, "tile-shader", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  tile.chain.sType            = GPU_STRUCTURE_TYPE_TILE_RENDER_PASS_EXT;
  tile.chain.structSize       = sizeof(tile);
  tile.imageblockSampleLength = state->imageblockBytes;

  color.view    = GPUFrameGetTargetView(frame);
  color.loadOp  = GPU_LOAD_OP_DONT_CARE;
  color.storeOp = GPU_STORE_OP_STORE;

  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.chain.pNext          = &tile;
  info.pColorAttachments    = &color;
  info.colorAttachmentCount = 1u;

  if (!(pass = GPUBeginRenderPass(command, &info))) {
    (void)GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return;
  }

  ok = GPUGetRenderPassTileSize(pass, &width, &height) == GPU_OK
       && GPUBindTilePipeline(pass, state->initialize) == GPU_OK
       && GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK;
  if (ok) {
    GPUBindRenderPipeline(pass, state->draw);
    GPUDraw(pass, 6u, 1u, 0u, 0u);
    ok = GPUBindTilePipeline(pass, state->resolve) == GPU_OK
         && GPUBindTileGroup(pass, 0u, state->resolveGroup, 0u, NULL) == GPU_OK
         && GPUSetTilePushConstants(pass, 0u, sizeof(bias), bias) == GPU_OK
         && GPUDispatchThreadsPerTile(pass, width, height) == GPU_OK;
  }
  GPUEndRenderPass(pass);

  if (!ok) {
    (void)GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    set_status("GPU: tile shading failed", 1);
    return;
  }
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: tile presentation failed", 1);
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  TileSample *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_TILE_SHADERS)
      || !GPUIsFeatureEnabled(device, GPU_FEATURE_IMAGEBLOCKS)) {
    set_status_notice("GPU: tile shaders and imageblocks require a supported Apple GPU");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->queue || !state->surface
      || !resize_webgpu_canvas(NULL, &state->width, &state->height)
      || !(state->swapchain = GPUCreateSwapchainDefault(device, state->surface,
                                                        state->width, state->height))
      || !prepare(state, device)) {
    set_status("GPU: failed to prepare tile shader sample", 1);
    return;
  }

  set_status("GPU: tile initialization, ordered imageblock compositing and tile resolve", 0);
  emscripten_set_main_loop_arg(render, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature     features[] = {GPU_FEATURE_TILE_SHADERS, GPU_FEATURE_IMAGEBLOCKS};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 2u) == GPU_OK ? 0 : 1;
}
