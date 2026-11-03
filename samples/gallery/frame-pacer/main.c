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
#include <stdatomic.h>
#include <stdio.h>

typedef struct FrameObservation {
  GPUFrameFeedback *feedback;
  double            deadline;
  atomic_uint       received;
  bool              attempted;
  GPUFrameEvent     gpu;
  GPUFrameEvent     presentation;
} FrameObservation;

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
  bool           feedbackEnabled;
  FrameObservation observations[2];
} PacerSample;

static PacerSample app;

static void
feedback(const GPUFrameEvent *event, void *user) {
  FrameObservation *observation;

  observation = user;
  if (event->kind == GPU_FRAME_EVENT_GPU_COMPLETE || event->kind == GPU_FRAME_EVENT_DISCARDED) {
    observation->gpu = *event;
    atomic_fetch_or_explicit(&observation->received, 1u, memory_order_release);
  } else {
    observation->presentation = *event;
    atomic_fetch_or_explicit(&observation->received, 2u, memory_order_release);
  }
}

static void
observe_frame(PacerSample *state, GPUFrame *frame, bool paced) {
  GPUFrameFeedbackCreateInfo info = {0};
  FrameObservation         *observation;

  observation = &state->observations[paced ? 1u : 0u];
  if (!state->feedbackEnabled || observation->attempted)
    return;

  observation->attempted = true;
  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_FEEDBACK_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.callback         = feedback;
  info.user             = observation;
  info.frameId          = paced ? 2u : 1u;
  if (GPUCreateFrameFeedback(frame, &info, &observation->feedback) == GPU_OK)
    observation->deadline = emscripten_get_now() + 2000.0;
  else
    set_status_notice("GPU: frame feedback unavailable; pacing continues");
}

static void
stop_feedback(void) {
  uint32_t i;

  for (i = 0u; i < 2u; i++) {
    GPUDestroyFrameFeedback(app.observations[i].feedback);
    app.observations[i].feedback = NULL;
  }
}

static void
tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  PacerSample                *state;

  state = user;
  if (!resize_webgpu_canvas(state->swapchain, &state->width, &state->height)) {
    GPUSetFramePacerPaused(state->pacer, true);
    GPUEndFrame(frame);
    return;
  }

  if (!frame)
    return;

  observe_frame(state, frame, timing != NULL);
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
  char              message[192];
  char              gpuTime[32], presentTime[80];
  PacerSample      *state;
  FrameObservation *observation;
  const char       *gpu, *presentation;
  double            now, milliseconds;
  uint32_t          i, received;
  int               length;

  state = user;
  if (!state->observations[0].feedback && !state->observations[1].feedback)
    return;

  now = emscripten_get_now();
  for (i = 0u; i < 2u; i++) {
    observation = &state->observations[i];
    if (!observation->feedback)
      continue;

    received = atomic_load_explicit(&observation->received, memory_order_acquire);
    if (received != 3u && now < observation->deadline
        && (!(received & 1u) || observation->gpu.kind != GPU_FRAME_EVENT_DISCARDED))
      continue;

    /* stop observing after two seconds; that does not imply the frame was dropped. */
    GPUDestroyFrameFeedback(observation->feedback);
    observation->feedback = NULL;
    received = atomic_load_explicit(&observation->received, memory_order_acquire);
    presentation = received & 2u ? "presentation time unavailable"
                                  : "presentation pending; observation stopped";
    if ((received & 2u) && observation->presentation.kind == GPU_FRAME_EVENT_PRESENTED) {
      if (observation->presentation.flags & GPU_FRAME_EVENT_TARGET_TIME_VALID) {
        milliseconds = (observation->presentation.presentedTime
                        - observation->presentation.targetPresentTime) * 1000.0;
        length = snprintf(presentTime, sizeof(presentTime), "shown %.6f s (%+.3f ms from target)",
                          observation->presentation.presentedTime, milliseconds);
      } else {
        length = snprintf(presentTime, sizeof(presentTime), "shown %.6f s",
                          observation->presentation.presentedTime);
      }
      if (length > 0 && (size_t)length < sizeof(presentTime))
        presentation = presentTime;
    }

    gpu = "time unavailable";
    if ((received & 1u) && (observation->gpu.flags & GPU_FRAME_EVENT_GPU_TIME_VALID)) {
      milliseconds = (observation->gpu.gpuEndTime - observation->gpu.gpuStartTime) * 1000.0;
      length = snprintf(gpuTime, sizeof(gpuTime), "%.3f ms", milliseconds);
      if (length > 0 && (size_t)length < sizeof(gpuTime))
        gpu = gpuTime;
    } else if (received & 1u) {
      gpu = observation->gpu.kind == GPU_FRAME_EVENT_DISCARDED ? "discarded before submit" : "time unavailable";
      if (observation->gpu.result != GPU_OK)
        gpu = "failed";
    }

    length = snprintf(message, sizeof(message), "GPU: %s frame %u, GPU %s, %s",
                      i ? "paced" : "manual", i + 1u, gpu, presentation);
    if (length > 0 && (size_t)length < sizeof(message))
      set_status(message, 0);
  }
}

static void
ready(GPUResult result, GPUAdapter *adapter, GPUDevice *device, void *user) {
  GPUFramePacerCreateInfo info = {0};
  PacerSample           *state;
  uint32_t               i;

  state = user;
  if (result != GPU_OK || !GPUIsFeatureEnabled(device, GPU_FEATURE_FRAME_PACER)) {
    set_status_notice("GPU: native frame pacer unavailable on this backend");
    return;
  }

  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->feedbackEnabled = GPUIsFeatureEnabled(device, GPU_FEATURE_FRAME_FEEDBACK);
  for (i = 0u; i < 2u; i++)
    atomic_init(&state->observations[i].received, 0u);

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
  const GPUFeature features[] = {GPU_FEATURE_FRAME_PACER, GPU_FEATURE_FRAME_FEEDBACK};

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  if (GPUCreateInstance(&info, &app.instance) != GPU_OK)
    return 1;

  if (atexit(stop_feedback) != 0)
    return 1;

  return request_webgpu_device_features(app.instance, &app.request, ready, &app,
                                        features, 2u) == GPU_OK ? 0 : 1;
}
