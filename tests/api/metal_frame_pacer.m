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
#include <pthread.h>

typedef struct PacerTest {
  GPUDevice     *device;
  GPUQueue      *queue;
  GPUSwapchain  *swapchain;
  GPUFramePacer *pacer;
  GPUFrame      *manual;
  NSThread      *thread;
  uint32_t       frames;
  uint32_t       ticks;
  uint32_t       phase;
  bool           failed;
} PacerTest;

typedef struct PacerThreadTest {
  GPUSwapchain *swapchain;
  bool          rejected;
} PacerThreadTest;

static bool
begin_rejected(GPUSwapchain *swapchain) {
  GPUFrame *frame;

  if (!(frame = GPUBeginFrame(swapchain)))
    return true;

  GPUEndFrame(frame);
  return false;
}

static void*
pacer_wrong_thread(void *user) {
  PacerThreadTest *test;

  test = user;
  @autoreleasepool {
    test->rejected = begin_rejected(test->swapchain);
  }
  return NULL;
}

static bool
check_pacer_thread(GPUSwapchain *swapchain) {
  PacerThreadTest test = {0};
  pthread_t      thread;

  test.swapchain = swapchain;
  if (pthread_create(&thread, NULL, pacer_wrong_thread, &test) != 0)
    return false;

  if (pthread_join(thread, NULL) != 0)
    abort();

  return test.rejected;
}

static bool
finish_clear(GPUQueue *queue, GPUFrame *frame, float green) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder        *pass;

  if (!frame || !frame->drawable)
    return false;

  if (GPUAcquireCommandBuffer(queue, "pacer clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[1] = green;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount  = 1u;
  pass = GPUBeginRenderPass(command, &info);
  if (!pass) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return false;
  }

  GPUEndRenderPass(pass);
  return GPUFinishFrame(queue, command, frame) == GPU_OK;
}

static bool
check_paused_clear(PacerTest *state, uint32_t width, uint32_t height) {
  GPUFrame   *frame;
  GPUTexture *target;
  bool        rejected;
  bool        extent;
  bool        submitted;

  if (!(frame = GPUBeginFrame(state->swapchain)))
    return false;

  target   = GPUFrameGetTarget(frame);
  extent   = target && target->width == width && target->height == height;
  rejected = begin_rejected(state->swapchain);
  if (!rejected)
    return false;

  submitted = finish_clear(state->queue, frame, 0.5f);
  return extent && submitted;
}

static void
tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  GPUFramePacerCreateInfo info = {0};
  PacerTest              *state;

  state = user;
  state->ticks++;
  if ([NSThread currentThread] != state->thread || !timing
      || !isfinite(timing->deadline) || !isfinite(timing->targetPresentTime)
      || timing->targetPresentTime <= 0.0 || timing->refreshInterval < 0.0)
    state->failed = true;

  if (!frame)
    return;

  state->frames++;
  if (!begin_rejected(state->swapchain)) {
    state->failed = true;
    return;
  }

  if (state->phase == 1u) {
    GPUDestroyFramePacer(state->pacer);
    state->pacer = NULL;
    return;
  }

  if (state->phase == 2u) {
    GPUDestroySwapchain(state->swapchain);
    state->swapchain = NULL;
    state->pacer     = NULL;
    return;
  }

  if (state->phase == 3u) {
    GPUSetFramePacerPaused(state->pacer, true);
    if (!begin_rejected(state->swapchain))
      state->failed = true;
    return;
  }

  if (!finish_clear(state->queue, frame, (float)(state->frames & 7u) / 8.0f))
    state->failed = true;

  if (state->phase == 5u) {
    GPUDestroyFramePacer(state->pacer);
    state->pacer = NULL;

    info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.swapchain        = state->swapchain;
    info.callback         = tick;
    info.user             = state;
    info.latencyFrames    = 1u;
    if (GPUCreateFramePacer(state->device, &info, &state->pacer) != GPU_OK
        || !(state->manual = GPUBeginFrame(state->swapchain)))
      state->failed = true;
    return;
  }

  if (state->frames == 8u || state->phase == 4u) {
    GPUSetFramePacerPaused(state->pacer, true);
    if (!begin_rejected(state->swapchain))
      state->failed = true;
  }
}

static void
pump(double seconds) {
  NSDate *deadline;

  deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while ([deadline timeIntervalSinceNow] > 0.0) {
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                           beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
  }
}

static bool
check_pacer(GPUInstance *instance, GPUAdapter *adapter, GPUDevice *device) {
  GPUFramePacerCreateInfo info = {0};
  PacerTest              state = {0};
  GPUSurface            *surface;
  NSWindow              *window;
  uint32_t               frames, ticks;
  bool                   ok;

  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 96, 96)
                                      styleMask:NSWindowStyleMaskTitled
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  [window setReleasedWhenClosed:NO];
  [window orderFront:nil];
  surface = GPUCreateSurfaceFromNative(instance, adapter, [window contentView], GPU_SURFACE_APPLE_NSVIEW, 1.0f);
  state.device    = device;
  state.swapchain = GPUCreateSwapchainDefault(device, surface, 96u, 96u);
  state.queue     = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state.thread    = [NSThread currentThread];
  ok = surface && state.swapchain && state.queue;
  if (!ok)
    goto cleanup;

  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.swapchain        = state.swapchain;
  info.callback         = tick;
  info.user             = &state;
  info.latencyFrames    = 1u;
  ok = GPUCreateFramePacer(device, &info, &state.pacer) == GPU_OK;
  if (!ok)
    goto cleanup;

  pump(0.08);
  ok = state.frames == 0u && state.ticks == 0u && check_pacer_thread(state.swapchain)
       && check_paused_clear(&state, 96u, 96u);
  if (!ok)
    goto cleanup;

  [window setContentSize:NSMakeSize(128, 80)];
  ok = GPUResizeSwapchain(state.swapchain, 128u, 80u) == GPU_OK
       && check_paused_clear(&state, 128u, 80u);
  if (!ok)
    goto cleanup;

  GPUSetFramePacerPaused(state.pacer, false);
  ok = begin_rejected(state.swapchain);
  pump(0.6);
  frames = state.frames;
  pump(0.08);
  ok = ok && frames == 8u && state.frames == frames && !state.failed
       && check_paused_clear(&state, 128u, 80u);
  if (!ok)
    goto cleanup;

  state.phase  = 3u;
  state.frames = 0u;
  GPUSetFramePacerPaused(state.pacer, false);
  pump(0.2);
  ok = state.frames == 1u && !state.failed && check_paused_clear(&state, 128u, 80u);
  if (!ok)
    goto cleanup;

  state.phase  = 4u;
  state.frames = 0u;
  GPUSetFramePacerPaused(state.pacer, false);
  pump(0.2);
  ok = state.frames == 1u && !state.failed && check_paused_clear(&state, 128u, 80u);
  if (!ok)
    goto cleanup;

  state.manual = GPUBeginFrame(state.swapchain);
  if (!state.manual) {
    ok = false;
    goto cleanup;
  }

  ticks = state.ticks;
  GPUSetFramePacerPaused(state.pacer, false);
  pump(0.08);
  ok = state.frames == 1u && state.ticks == ticks && !state.failed
       && begin_rejected(state.swapchain);
  if (!ok)
    goto cleanup;

  GPUDestroyFramePacer(state.pacer);
  state.pacer = NULL;
  ok = finish_clear(state.queue, state.manual, 0.75f);
  state.manual = NULL;
  if (!ok)
    goto cleanup;

  state.phase  = 5u;
  state.frames = 0u;
  ok = GPUCreateFramePacer(device, &info, &state.pacer) == GPU_OK;
  if (!ok)
    goto cleanup;

  GPUSetFramePacerPaused(state.pacer, false);
  pump(0.2);
  ok = state.frames == 1u && state.pacer && state.manual && !state.failed;
  if (!ok)
    goto cleanup;

  ok = finish_clear(state.queue, state.manual, 0.25f);
  state.manual = NULL;
  if (!ok)
    goto cleanup;

  GPUDestroyFramePacer(state.pacer);
  state.pacer = NULL;

  state.phase  = 1u;
  state.frames = 0u;
  ok = GPUCreateFramePacer(device, &info, &state.pacer) == GPU_OK && ok;
  if (state.pacer)
    GPUSetFramePacerPaused(state.pacer, false);

  pump(0.2);
  ok = ok && state.frames == 1u && state.pacer == NULL && !state.failed;
  if (!ok)
    goto cleanup;

  state.phase  = 2u;
  state.frames = 0u;
  ok = GPUCreateFramePacer(device, &info, &state.pacer) == GPU_OK && ok;
  if (state.pacer)
    GPUSetFramePacerPaused(state.pacer, false);

  pump(0.2);
  ok = ok && state.frames == 1u && !state.swapchain && !state.pacer && !state.failed;
  if (!ok)
    goto cleanup;

  state.swapchain = GPUCreateSwapchainDefault(device, surface, 128u, 80u);
  info.swapchain  = state.swapchain;
  ok = state.swapchain && GPUCreateFramePacer(device, &info, &state.pacer) == GPU_OK;
  if (!ok)
    goto cleanup;

  state.manual = GPUBeginFrame(state.swapchain);
  ok = state.manual != NULL;
  if (!ok)
    goto cleanup;

  GPUDestroySwapchain(state.swapchain);
  state.swapchain = NULL;
  state.pacer     = NULL;
  state.manual    = NULL;

cleanup:
  GPUEndFrame(state.manual);
  GPUDestroyFramePacer(state.pacer);
  GPUDestroySwapchain(state.swapchain);
  GPUDestroySurface(surface);
  [window close];
  [window release];
  if (!ok)
    fprintf(stderr, "pacer failed: phase=%u frames=%u ticks=%u\n", state.phase, state.frames, state.ticks);

  printf("pacer: paused draws/resize, owner thread, callback rejection, unconsumed cleanup, resume, manual ownership, destruction=%s\n",
         ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUDeviceCreateInfo info = {0};
    const GPUFeature    features[] = {GPU_FEATURE_FRAME_PACER};
    GPUInstance        *instance = NULL;
    GPUAdapter         *adapter;
    GPUDevice          *device = NULL;
    int                 status;

    if (GPUCreateInstance(NULL, &instance) != GPU_OK)
      return 1;

    if (!(adapter = GPUGetAutoSelectedAdapter(instance))) {
      GPUDestroyInstance(instance);
      return 1;
    }

    if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_PACER)) {
      GPUDestroyInstance(instance);
      return 77;
    }

    info.required.pFeatures    = features;
    info.required.featureCount = 1u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK) {
      GPUDestroyInstance(instance);
      return 1;
    }

    status = check_pacer(instance, adapter, device) ? 0 : 1;
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return status;
  }
}
