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

typedef struct SwapchainSample {
  GPUInstance   *instance;
  GPUQueue      *queue;
  GPUSurface    *surface;
  GPUSwapchain  *swapchain;
  WebGPURequest  request;
  uint32_t       width;
  uint32_t       height;
  uint32_t       frames;
} SwapchainSample;

static SwapchainSample app;

static void
tick(void *user) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  const float scales[] = {1.0f, 2.0f, 1.5f};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  GPUFrame                   *frame;
  SwapchainSample            *state;

  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height))
    return;

  if (state->frames % 120u == 0u
      && GPUSetSwapchainScale(state->swapchain, scales[(state->frames / 120u) % 3u]) != GPU_OK) {
    set_status("GPU: swapchain scale change failed", 1);
    return;
  }

  if (!(frame = GPUBeginFrame(state->swapchain)))
    return;

  if (GPUAcquireCommandBuffer(state->queue, "swapchain options", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.15f;
  color.clearColor.float32[1] = 0.55f + (float)(state->frames % 120u) / 400.0f;
  color.clearColor.float32[2] = 0.25f;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount  = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    set_status("GPU: swapchain render pass failed", 1);
    return;
  }

  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, command, frame) != GPU_OK)
    set_status("GPU: swapchain presentation failed", 1);

  state->frames++;
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  GPUSwapchainCreateInfo info    = {0};
  GPUSwapchainOptions    options = {0};
  SwapchainSample      *state;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_SCALE)) {
    set_status_notice("GPU: swapchain scaling unavailable on this backend");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->surface = GPUCreateSurfaceFromNative(state->instance, adapter, (void *)"#canvas",
                                             GPU_SURFACE_WEB_CANVAS, 1.0f);
  if (!state->queue || !state->surface
      || !resize_webgpu_canvas(NULL, &state->width, &state->height)) {
    set_status("GPU: swapchain surface unavailable", 1);
    return;
  }

  /* the default path also provides the host's preferred surface format. */
  state->swapchain = GPUCreateSwapchainDefault(device, state->surface, state->width, state->height);
  if (!state->swapchain) {
    set_status("GPU: default swapchain unavailable", 1);
    return;
  }

  if (GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_COLOR_SPACE)) {
    options.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS;
    options.chain.structSize = sizeof(options);
    options.colorSpace       = GPU_COLOR_SPACE_DISPLAY_P3;
    options.presentsWithTransaction = GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_TRANSACTIONS);
    info.chain.pNext = &options;
    info.surface     = state->surface;
    info.width       = state->width;
    info.height      = state->height;
    info.format      = GPUGetSwapchainFormat(state->swapchain);
    if (GPUCreateSwapchain(device, &info, &state->swapchain) != GPU_OK) {
      set_status("GPU: explicit swapchain options failed", 1);
      return;
    }
    set_status("GPU: Display P3 + Core Animation transaction; scale 1, 2, 1.5", 0);
  } else {
    set_status("GPU: scale 1, 2, 1.5; explicit colour/transaction unavailable", 0);
  }

  emscripten_set_main_loop_arg(tick, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  const GPUFeature features[] = {GPU_FEATURE_SWAPCHAIN_SCALE, GPU_FEATURE_SWAPCHAIN_COLOR_SPACE,
                                 GPU_FEATURE_SWAPCHAIN_TRANSACTIONS};

  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 3u) == GPU_OK ? 0 : 1;
}
