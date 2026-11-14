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

typedef struct BufferSample {
  GPUInstance       *instance;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUBuffer         *buffer;
  GPUTexture        *texture;
  GPUTextureView    *view;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *layout;
  GPURenderPipeline *pipeline;
  GPUBindGroup      *group;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
} BufferSample;

static BufferSample app;

static int
prepare(BufferSample *state, GPUDevice *device) {
  GPUBufferTextureCreateInfo textureInfo  = {0};
  GPUBufferCreateInfo        bufferInfo   = {0};
  GPUTextureViewCreateInfo   viewInfo     = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  GPUColorTargetState        color        = {0};
  GPUBindGroupCreateInfo     groupInfo    = {0};
  GPUBindGroupEntry          entry        = {0};
  void                     *artifact;
  uint8_t                  *pixels;
  uint64_t                  size;
  uint32_t                  alignment, pitch, x, y;
  GPUResult                 result;

  alignment = GPUGetLinearTextureAlignment(device, GPU_FORMAT_BGRA8_UNORM);
  if (!alignment)
    return 0;

  pitch = ((256u + alignment - 1u) / alignment) * alignment;
  bufferInfo.sizeBytes = (uint64_t)pitch * 64u;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &state->buffer) != GPU_OK
      || !(pixels = GPUGetBufferHostAddress(state->buffer)))
    return 0;

  /* write the final sampled storage directly, before any GPU use. */
  for (y = 0u; y < 64u; y++) {
    for (x = 0u; x < 64u; x++) {
      pixels[y * pitch + x * 4u + 0u] = (uint8_t)(x * 4u);
      pixels[y * pitch + x * 4u + 1u] = (uint8_t)(y * 4u);
      pixels[y * pitch + x * 4u + 2u] = ((x >> 3u) ^ (y >> 3u)) & 1u ? 220u : 40u;
      pixels[y * pitch + x * 4u + 3u] = 255u;
    }
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.buffer           = state->buffer;
  textureInfo.bytesPerRow      = pitch;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = 64u;
  textureInfo.height           = 64u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  viewInfo.format              = textureInfo.format;
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
  if (GPUCreateTextureFromBuffer(device, &textureInfo, &state->texture) != GPU_OK
      || GPUCreateTextureView(state->texture, &viewInfo, &state->view) != GPU_OK
      || !read_file("/buffer_texture.us", &artifact, &size))
    return 0;

  result = GPUCreateShaderLibraryFromUSL(device, artifact, size, &state->library);
  free(artifact);
  if (result != GPU_OK || GPUCreateShaderLayout(device, state->library, &state->layout) != GPU_OK)
    return 0;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize        = sizeof(pipelineInfo);
  pipelineInfo.layout                  = state->layout->pipelineLayout;
  pipelineInfo.library                 = state->library;
  pipelineInfo.vertexEntry             = "buffer_vs";
  pipelineInfo.fragmentEntry           = "buffer_fs";
  pipelineInfo.pColorTargets           = &color;
  pipelineInfo.colorTargetCount        = 1u;
  pipelineInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &state->pipeline) != GPU_OK)
    return 0;

  entry.textureView = state->view;
  entry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = state->layout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  return GPUCreateBindGroup(device, &groupInfo, &state->group) == GPU_OK;
}

static void
render(void *user) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  BufferSample               *state;
  GPUFrame                   *frame;
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;

  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)
      || !(frame = GPUBeginFrame(state->swapchain)))
    return;

  if (GPUAcquireCommandBuffer(state->queue, "sample shared bytes", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view     = GPUFrameGetTargetView(frame);
  color.loadOp   = GPU_LOAD_OP_CLEAR;
  color.storeOp  = GPU_STORE_OP_STORE;
  info.pColorAttachments     = &color;
  info.colorAttachmentCount = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->group, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: buffer-texture presentation failed", 1);
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  BufferSample *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_TEXTURES)) {
    set_status_notice("GPU: buffer-backed sampled textures unavailable on this backend");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->surface || !resize_webgpu_canvas(NULL, &state->width, &state->height)
      || !(state->swapchain = GPUCreateSwapchainDefault(device, state->surface, state->width, state->height))
      || !prepare(state, device)) {
    set_status("GPU: failed to prepare buffer-texture sample", 1);
    return;
  }

  set_status("GPU: BGRA pixels sampled directly from padded shared buffer; no texture upload", 0);
  emscripten_set_main_loop_arg(render, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature features[] = {GPU_FEATURE_BUFFER_TEXTURES};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 1u) == GPU_OK ? 0 : 1;
}
