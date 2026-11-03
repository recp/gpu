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

typedef struct PacerSample {
  GPUInstance   *instance;
  GPUQueue      *queue;
  GPUSurface    *surface;
  GPUSwapchain  *swapchain;
  GPUFramePacer *pacer;
  WebGPURequest  request;
  uint32_t       width;
  uint32_t       height;
  uint32_t       frames;
} PacerSample;

static PacerSample app;

static void
tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  PacerSample                *state;

  (void)timing;
  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)) {
    GPUSetFramePacerPaused(state->pacer, true);
    GPUEndFrame(frame);
    return;
  }

  if (!frame)
    return;

  if (GPUAcquireCommandBuffer(state->queue, "paced clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.08f;
  color.clearColor.float32[1] = 0.25f + (float)(state->frames % 120u) / 240.0f;
  color.clearColor.float32[2] = 0.65f;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount  = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return;
  }

  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK) {
    set_status("GPU: paced presentation failed", 1);
    GPUSetFramePacerPaused(state->pacer, true);
  }
  state->frames++;
}

static void
host_tick(void *user) {
  /* the gallery host uses its own tick to update status and window controls. */
  (void)user;
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  GPUFramePacerCreateInfo info = {0};
  PacerSample           *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_FRAME_PACER)) {
    set_status_notice("GPU: native frame pacer unavailable on this backend");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->surface || !resize_webgpu_canvas(NULL, &state->width, &state->height)
      || !(state->swapchain = GPUCreateSwapchainDefault(device, state->surface, state->width, state->height))) {
    set_status("GPU: failed to create paced surface", 1);
    return;
  }

  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.swapchain        = state->swapchain;
  info.callback         = tick;
  info.user             = state;
  info.latencyFrames    = 1u;
  if (GPUCreateFramePacer(device, &info, &state->pacer) != GPU_OK) {
    set_status("GPU: failed to create frame pacer", 1);
    return;
  }

  set_status("GPU: paused startup, then native display-link frames", 0);
  tick(GPUBeginFrame(state->swapchain), NULL, state);
  GPUSetFramePacerPaused(state->pacer, false);
  emscripten_set_main_loop_arg(host_tick, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature features[] = {GPU_FEATURE_FRAME_PACER};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 1u) == GPU_OK ? 0 : 1;
}
