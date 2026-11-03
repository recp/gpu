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

#include <stdio.h>

typedef struct DrawConstants {
  float rect[4];
  float tint[4];
} DrawConstants;

typedef struct FetchSample {
  GPUInstance       *instance;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *layout;
  GPURenderPipeline *pipeline;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
} FetchSample;

static FetchSample app;

static int
prepare(FetchSample *state, GPUDevice *device) {
  GPURenderPipelineCreateInfo info       = {0};
  GPUColorTargetState         color      = {0};
  GPUShaderReflection         reflection = {0};
  void                       *artifact;
  uint64_t                    size;
  GPUResult                   result;

  if (!read_file("/framebuffer_fetch.us", &artifact, &size))
    return 0;

  result = GPUCreateShaderLibraryFromUSL(device, artifact, size, &state->library);
  free(artifact);

  if (result != GPU_OK
      || GPUGetShaderReflection(state->library, &reflection) != GPU_OK)
    return 0;

  result = reflection.pushConstantSizeBytes == sizeof(DrawConstants)
        && reflection.pushConstantStages == (GPU_SHADER_STAGE_VERTEX_BIT
                                             | GPU_SHADER_STAGE_FRAGMENT_BIT)
         ? GPU_OK : GPU_ERROR_INVALID_ARGUMENT;
  GPUFreeShaderReflection(&reflection);

  if (result != GPU_OK
      || GPUCreateShaderLayout(device, state->library, &state->layout) != GPU_OK)
    return 0;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "framebuffer-fetch";
  info.layout                  = state->layout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "fetch_vs";
  info.fragmentEntry           = "fetch_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;

  return GPUCreateRenderPipeline(device, &info, &state->pipeline) == GPU_OK;
}

static void
render(void *user) {
  GPURenderPassCreateInfo      info  = {0};
  GPURenderPassColorAttachment color = {0};
  const DrawConstants         draws[] = {
    {{-0.30f,  0.18f, 0.70f, 0.70f}, {0.50f, 0.0625f, 0.0625f, 0.50f}},
    {{ 0.30f,  0.18f, 0.70f, 0.70f}, {0.0625f, 0.50f, 0.125f,  0.50f}},
    {{ 0.00f, -0.28f, 0.70f, 0.70f}, {0.125f,  0.25f, 0.625f,  0.75f}}
  };
  FetchSample                *state;
  GPUFrame                   *frame;
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  uint32_t                    i;

  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)
      || !(frame = GPUBeginFrame(state->swapchain)))
    return;

  if (GPUAcquireCommandBuffer(state->queue, "framebuffer-fetch", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.03125f;
  color.clearColor.float32[1] = 0.03125f;
  color.clearColor.float32[2] = 0.0625f;
  color.clearColor.float32[3] = 1.0f;

  info.label                = "framebuffer-fetch";
  info.pColorAttachments    = &color;
  info.colorAttachmentCount = 1u;

  if (!(pass = GPUBeginRenderPass(command, &info))) {
    (void)GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  for (i = 0u; i < sizeof(draws) / sizeof(draws[0]); i++) {
    GPUSetRenderPushConstants(pass, 0u, sizeof(draws[i]), &draws[i]);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: framebuffer fetch presentation failed", 1);
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  FetchSample *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_FRAMEBUFFER_FETCH)) {
    set_status_notice("GPU: framebuffer fetch requires a supported Apple GPU");
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
    set_status("GPU: failed to prepare framebuffer fetch sample", 1);
    return;
  }

  set_status("GPU: ordered source-over compositing using framebuffer fetch", 0);
  emscripten_set_main_loop_arg(render, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature     features[] = {GPU_FEATURE_FRAMEBUFFER_FETCH};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 1u) == GPU_OK ? 0 : 1;
}
