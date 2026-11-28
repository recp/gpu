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

/* frames and GPU work where the system doesn't show the view, on this build's
 * Apple backend (Metal, Vulkan on MoltenVK, WebGPU on Dawn). macOS covers the
 * window; iOS sends the app to the background (apple_window.m), where the
 * system refuses GPU work. Metal gives the system the work and reports its
 * refusal; Vulkan and WebGPU refuse it themselves, at once, since MoltenVK and
 * Dawn lose the device to one refused command buffer. Static builds also run
 * that refusal by hand (appleSetGPURefused), with the window shown: a frame cut
 * short gives its image to the next, a refused submit's signals are given, a
 * queue write waits for the first submit after */

#include "apple_window.h"
#include "test.h"
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>
#if TARGET_OS_OSX
#  import <AppKit/AppKit.h>
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if GPU_TEST_REFUSAL
/* GPU-C's own word on the background (src/backend/surface_apple.m) */
void appleSetGPURefused(bool refused);
#endif

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
    ok = false; \
  } \
} while (0)

typedef struct State {
  AppleWindow  *window;
  GPUDevice    *device;
  GPUQueue     *queue;
  GPUSwapchain *swapchain;
  atomic_uint   errors;
  _Atomic(int)  errorType;
  _Atomic(int)  errorResult;
  _Atomic(int)  errorLost;
  uint32_t      hiddenTicks;  /* the pacer's, with the window hidden */
  uint32_t      hiddenFrames;
  uint32_t      shownFrames;
  bool          hiding;
} State;

static void
on_error(GPUDevice *device, const GPUDeviceErrorInfo *error, void *user) {
  State *state;

  (void)device;
  state = user;
  atomic_store_explicit(&state->errorType, (int)error->type, memory_order_relaxed);
  atomic_store_explicit(&state->errorResult, (int)error->result, memory_order_relaxed);
  atomic_store_explicit(&state->errorLost, (int)error->lostReason, memory_order_relaxed);
  atomic_fetch_add_explicit(&state->errors, 1u, memory_order_release);
  printf("background: device error type %d result %d lost %d: %s\n", (int)error->type, (int)error->result,
         (int)error->lostReason, error->message ? error->message : "");
}

/* the device errors since `before`, once `expect` came or that long passed */
static uint32_t
errors_after(State *state, uint32_t before, uint32_t expect, double seconds) {
  double deadline;

  deadline = CACurrentMediaTime() + seconds;
  while (atomic_load_explicit(&state->errors, memory_order_acquire) - before < expect
         && CACurrentMediaTime() < deadline)
    appleWindowPump(0.005);

  return atomic_load_explicit(&state->errors, memory_order_acquire) - before;
}

/* the one error since `before` was a backend failure: refused, the queue kept */
static bool
refused_alone(State *state, uint32_t before) {
  return errors_after(state, before, 1u, 1.0) == 1u
         && atomic_load_explicit(&state->errorType, memory_order_relaxed) == GPU_DEVICE_ERROR_BACKEND
         && atomic_load_explicit(&state->errorResult, memory_order_relaxed) == GPU_ERROR_BACKEND_FAILURE
         && atomic_load_explicit(&state->errorLost, memory_order_relaxed) == GPU_DEVICE_LOST_REASON_UNKNOWN;
}

/* GPU work with no frame: a command buffer of its own and a fence; the
 * submit's result in *submitted, the fence's returned */
static GPUResult
submit_plain(State *state, GPUResult *submitted) {
  GPUQueueSubmitInfo info  = {0};
  GPUCommandBuffer  *command;
  GPUFence          *fence = NULL;
  GPUResult          result;

  *submitted = GPU_ERROR_BACKEND_FAILURE;
  if ((result = GPUCreateFence(state->device, NULL, &fence)) != GPU_OK)
    return result;

  if ((result = GPUAcquireCommandBuffer(state->queue, "background plain", &command)) != GPU_OK) {
    GPUDestroyFence(fence);
    return result;
  }

  info.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize   = sizeof(info);
  info.commandBufferCount = 1u;
  info.ppCommandBuffers   = &command;
  info.fence              = fence;
  *submitted              = GPUQueueSubmit(state->queue, &info);
  result                  = GPUWaitFence(fence, 5000000000ull);
  GPUDestroyFence(fence);
  return result;
}

static GPUResult
clear_frame(GPUQueue *queue, GPUFrame *frame) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info    = {0};
  GPUCommandBuffer            *command = NULL;
  GPURenderPassEncoder        *pass;

  if (GPUAcquireCommandBuffer(queue, "background clear", &command) != GPU_OK || !GPUFrameGetTargetView(frame)) {
    if (command)
      GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[2] = 1.0f;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount   = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  GPUEndRenderPass(pass);
  return GPUFinishFrame(queue, command, frame);
}

/* a pacer's frames: none with the window hidden, drawn once shown again */
static void
on_tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  State *state;

  (void)timing;
  state = user;
  if (state->hiding && !appleWindowShown(state->window)) {
    state->hiddenTicks++;
    state->hiddenFrames += frame != NULL;
    return; /* unconsumed: released, nothing drawn */
  }

  if (!frame)
    return;

  state->shownFrames++;
  (void)clear_frame(state->queue, frame);
}

static GPUBackend
backend_of(GPUAdapter *adapter) {
  GPUAdapterProperties props = {0};

  return GPUGetAdapterProperties(adapter, &props) == GPU_OK ? props.backend : GPU_BACKEND_DEFAULT;
}

static bool
open_state(State *state, GPUAdapter *adapter, GPUSurface *surface, AppleWindow *window, bool paced) {
  GPUDeviceCreateInfo deviceInfo = {0};
  const GPUFeature    features[] = {GPU_FEATURE_FRAME_PACER};

  memset(state, 0, sizeof(*state));
  atomic_init(&state->errors, 0u);
  atomic_init(&state->errorType, 0);
  atomic_init(&state->errorResult, 0);
  atomic_init(&state->errorLost, 0);
  state->window = window;
  if (paced) {
    deviceInfo.required.pFeatures    = features;
    deviceInfo.required.featureCount = 1u;
  }

  if (gpu_test_create_device(adapter, &deviceInfo, &state->device) != GPU_OK || !state->device
      || !(state->swapchain = GPUCreateSwapchainDefault(state->device, surface, 96u, 80u))) {
    GPUDestroyDevice(state->device);
    state->device = NULL;
    return false;
  }

  GPUSetDeviceErrorCallback(state->device, on_error, state);
  state->queue = GPUGetQueue(state->device, GPU_QUEUE_GRAPHICS, 0u);
  return state->queue != NULL;
}

static void
close_state(State *state) {
  GPUDestroySwapchain(state->swapchain);
  GPUDestroyDevice(state->device);
  state->swapchain = NULL;
  state->device    = NULL;
}

static GPUFramePacer*
open_pacer(State *state) {
  GPUFramePacerCreateInfo info  = {0};
  GPUFramePacer          *pacer = NULL;

  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.swapchain        = state->swapchain;
  info.callback         = on_tick;
  info.user             = state;
  info.latencyFrames    = 1u;
  return GPUCreateFramePacer(state->device, &info, &pacer) == GPU_OK ? pacer : NULL;
}

/* a window the system doesn't show (macOS: covered; iOS: the app in the
 * background) gives no frame, at once, and a pacer running across none. iOS
 * refuses GPU work from the background: there a submit fails alone (a
 * backend failure reported, nothing lost), at once on Vulkan and WebGPU, a
 * moment later on Metal, the system's; a frame begun before and submitted
 * there too. Shown again, frames come back and the queue runs */
static bool
check_hidden(GPUAdapter *adapter, GPUSurface *surface, AppleWindow *window) {
  State          state;
  GPUFramePacer *pacer = NULL;
  GPUFrame      *frame, *early = NULL;
  GPUBackend     backend;
  GPUResult      result, submitted;
  double         start, waited, deadline, unhide;
  uint32_t       before;
  bool           paced, atOnce, refused = false, ok = true;

  if (!appleWindowWaitShown(window, true, 1.0)) {
    printf("hidden window: not shown to begin with; skip\n");
    return true;
  }

  backend = backend_of(adapter);
  atOnce  = backend == GPU_BACKEND_VULKAN || backend == GPU_BACKEND_WEBGPU;
#if TARGET_OS_IOS
  paced  = GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_PACER);
  unhide = 20.0; /* the app is brought back from outside: that takes seconds */
#else
  paced  = false; /* a covered window's pacer frames draw fine there: nothing to check */
  unhide = 1.0;
#endif
  if (!open_state(&state, adapter, surface, window, paced))
    return false;

  if (paced && !(pacer = open_pacer(&state))) {
    close_state(&state);
    return false;
  }

  /* begun shown, submitted hidden */
  early = GPUBeginFrame(state.swapchain);
  CHECK(early != NULL);

  if (ok && (state.hiding = appleWindowHide(window, 2.0))) {
    /* the frame begun shown, submitted hidden: on iOS refused with its submit
     * (its image the next frame's) on Vulkan and WebGPU, the system's to
     * refuse or not on Metal (not drawn); drawn on macOS */
#if TARGET_OS_IOS
    if (atOnce) {
      before = atomic_load_explicit(&state.errors, memory_order_acquire);
      CHECK(early && clear_frame(state.queue, early) == GPU_ERROR_BACKEND_FAILURE && refused_alone(&state, before));
    } else {
      GPUEndFrame(early);
    }
#else
    if (early)
      CHECK(clear_frame(state.queue, early) == GPU_OK);
#endif
    early = NULL;

    start  = CACurrentMediaTime();
    frame  = GPUBeginFrame(state.swapchain);
    waited = CACurrentMediaTime() - start;
    CHECK(!frame && waited < 0.1 && GPUGetSwapchainStatus(state.swapchain) == GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    GPUEndFrame(frame);

#if TARGET_OS_IOS
    if (atOnce) {
      before = atomic_load_explicit(&state.errors, memory_order_acquire);
      start  = CACurrentMediaTime();
      result = submit_plain(&state, &submitted);
      waited = CACurrentMediaTime() - start;
      CHECK(submitted == GPU_ERROR_BACKEND_FAILURE && result == GPU_ERROR_BACKEND_FAILURE && waited < 0.1
            && refused_alone(&state, before));

      /* nothing in flight, nothing to submit: idle */
      CHECK(GPUDeviceWaitIdle(state.device) == GPU_OK && GPUQueueWaitIdle(state.queue) == GPU_OK);
      refused = true;
      printf("hidden window: submits refused at once (%.4f s), the frame begun before with them\n", waited);
    } else {
      /* the system takes the GPU away after the scene's notification: mostly
       * within a fraction of a second, at times not for seconds. Work it still
       * runs meanwhile is fine; a refusal never seen is the system's */
      start    = CACurrentMediaTime();
      deadline = start + 10.0;
      do {
        before = atomic_load_explicit(&state.errors, memory_order_acquire);
        result = submit_plain(&state, &submitted);
      } while (submitted == GPU_OK && result == GPU_OK && errors_after(&state, before, 1u, 0.1) == 0u
               && CACurrentMediaTime() < deadline);
      if (submitted == GPU_OK && result == GPU_OK
          && atomic_load_explicit(&state.errors, memory_order_acquire) == before) {
        printf("hidden window: the system ran GPU work %.1f s into the background; its refusal unseen\n",
               CACurrentMediaTime() - start);
      } else {
        refused = true;
        CHECK(submitted == GPU_OK && result == GPU_ERROR_BACKEND_FAILURE && refused_alone(&state, before));
        printf("hidden window: plain submit refused %.2f s in\n", CACurrentMediaTime() - start);
      }
    }
#else
    /* covered: frames wait, GPU work doesn't */
    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    CHECK(submit_plain(&state, &submitted) == GPU_OK && submitted == GPU_OK
          && errors_after(&state, before, 0u, 0.1) == 0u);
#endif

    if (pacer) {
      GPUSetFramePacerPaused(pacer, false);
      appleWindowPump(0.3);
      GPUSetFramePacerPaused(pacer, true);
      CHECK(state.hiddenFrames == 0u);
      printf("hidden window: pacer ticks hidden %u, frames %u\n", state.hiddenTicks, state.hiddenFrames);
    }
  } else if (ok) {
    printf("hidden window: it couldn't be hidden here; skip\n");
  }

  if (early)
    GPUEndFrame(early);

  /* a cover that didn't hide the window is taken away all the same */
  if (!appleWindowUnhide(window, unhide)) {
    printf("hidden window: not shown again; skip the rest\n");
  } else if (ok) {
    if (pacer) {
      GPUSetFramePacerPaused(pacer, false);
      deadline = CACurrentMediaTime() + 1.0;
      while (state.shownFrames < 3u && CACurrentMediaTime() < deadline)
        appleWindowPump(0.005);
      GPUSetFramePacerPaused(pacer, true);
      CHECK(state.shownFrames >= 3u && state.hiddenFrames == 0u);
      printf("hidden window: pacer frames shown again %u\n", state.shownFrames);
    }

    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    CHECK(submit_plain(&state, &submitted) == GPU_OK && submitted == GPU_OK);
    frame = GPUBeginFrame(state.swapchain);
    CHECK(frame && GPUGetSwapchainStatus(state.swapchain) == GPU_SWAPCHAIN_STATUS_READY);
    if (frame)
      CHECK(clear_frame(state.queue, frame) == GPU_OK);
    CHECK(GPUQueueWaitIdle(state.queue) == GPU_OK && errors_after(&state, before, 0u, 0.1) == 0u);
  }

  GPUDestroyFramePacer(pacer);
  close_state(&state);
#if TARGET_OS_IOS
  printf("hidden window (the background): no frame, at once; %s; no pacer frame; "
         "frames and the queue when shown again=%s\n", refused ? "GPU work refused alone" : "no GPU work refused",
         ok ? "pass" : "fail");
#else
  (void)refused;
  (void)atOnce;
  (void)result;
  printf("hidden window: no frame, at once; frames when shown again=%s\n", ok ? "pass" : "fail");
#endif
  return ok;
}

#if GPU_TEST_REFUSAL
/* buffer bytes after the GPU: a host-read copy read back */
static bool
read_back(State *state, GPUBuffer *buffer, uint8_t *out, uint32_t size) {
  return GPUQueueReadBuffer(state->queue, buffer, 0u, out, size) == GPU_OK;
}

/* GPU-C's own refusal (Vulkan, WebGPU), set by hand with the window shown. A
 * frame begun and refused its submit gives its image to the next frame:
 * rounds past the swapchain's image count would wait for good otherwise. A
 * refused submit's signal is given; a queue write waits for the first submit
 * once back; waits for idle return, nothing submitted */
static bool
check_refusal(GPUAdapter *adapter, GPUSurface *surface, AppleWindow *window) {
  GPUBufferCreateInfo     bufferInfo = {0};
  GPUSemaphoreCreateInfo  semaInfo   = {0};
  GPUQueueSubmitExInfo    exInfo     = {0};
  GPUQueueSemaphoreSignal signal     = {0};
  GPUQueueSemaphoreWait   wait       = {0};
  State                   state;
  GPUBuffer              *buffer     = NULL;
  GPUSemaphore           *semaphore  = NULL;
  GPUCommandBuffer       *command;
  GPUFence               *fence      = NULL;
  GPUFrame               *frame;
  GPUResult               result, submitted;
  uint8_t                 bytes[256], back[256];
  uint32_t                round, before, i;
  bool                    ok = true;

  if (!appleWindowWaitShown(window, true, 1.0)) {
    printf("refusal: window not shown; skip\n");
    return true;
  }

  if (!open_state(&state, adapter, surface, window, false))
    return false;

  for (round = 0u; round < 5u; round++) {
    CHECK((frame = GPUBeginFrame(state.swapchain)) != NULL);
    appleSetGPURefused(true);
    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    if (frame)
      CHECK(clear_frame(state.queue, frame) == GPU_ERROR_BACKEND_FAILURE && refused_alone(&state, before));

    CHECK(!GPUBeginFrame(state.swapchain)
          && GPUGetSwapchainStatus(state.swapchain) == GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    CHECK(submit_plain(&state, &submitted) == GPU_ERROR_BACKEND_FAILURE && submitted == GPU_ERROR_BACKEND_FAILURE
          && refused_alone(&state, before));
    CHECK(GPUDeviceWaitIdle(state.device) == GPU_OK);
    appleSetGPURefused(false);

    /* back: the held image drawn, then the next acquired */
    for (i = 0u; i < 2u; i++) {
      CHECK((frame = GPUBeginFrame(state.swapchain)) != NULL);
      if (frame)
        CHECK(clear_frame(state.queue, frame) == GPU_OK);
    }
  }
  CHECK(GPUQueueWaitIdle(state.queue) == GPU_OK);

  /* a write from the background waits for the first submit after */
  bufferInfo.sizeBytes = sizeof(bytes);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_STORAGE;
  CHECK(GPUCreateBuffer(state.device, &bufferInfo, &buffer) == GPU_OK);
  for (i = 0u; i < sizeof(bytes); i++)
    bytes[i] = (uint8_t)(i * 7u + 3u);
  if (buffer) {
    appleSetGPURefused(true);
    CHECK(GPUQueueWriteBuffer(state.queue, buffer, 0u, bytes, sizeof(bytes)) == GPU_OK);
    /* read through a copy: refused; a buffer the host reads itself: there */
    result = GPUQueueReadBuffer(state.queue, buffer, 0u, back, sizeof(back));
    CHECK(result == GPU_ERROR_BACKEND_FAILURE || (result == GPU_OK && memcmp(bytes, back, sizeof(bytes)) == 0));
    appleSetGPURefused(false);
    memset(back, 0, sizeof(back));
    CHECK(submit_plain(&state, &submitted) == GPU_OK && submitted == GPU_OK);
    CHECK(read_back(&state, buffer, back, sizeof(back)) && memcmp(bytes, back, sizeof(bytes)) == 0);
  }

  /* a refused submit's signal is given: what waits on it goes once back */
  if (GPUCreateSemaphore(state.device, &semaInfo, &semaphore) == GPU_OK
      && GPUCreateFence(state.device, NULL, &fence) == GPU_OK) {
    exInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
    exInfo.chain.structSize   = sizeof(exInfo);
    exInfo.commandBufferCount = 1u;
    signal.semaphore          = semaphore;
    signal.value              = 1u;
    appleSetGPURefused(true);
    if (GPUAcquireCommandBuffer(state.queue, "refused signal", &command) == GPU_OK) {
      exInfo.ppCommandBuffers = &command;
      exInfo.pSignals         = &signal;
      exInfo.signalCount      = 1u;
      CHECK(GPUQueueSubmitEx(state.queue, &exInfo) == GPU_ERROR_BACKEND_FAILURE);
    }
    appleSetGPURefused(false);
    if (GPUAcquireCommandBuffer(state.queue, "waits for the refused", &command) == GPU_OK) {
      wait.semaphore          = semaphore;
      wait.value              = 1u;
      wait.waitStages         = GPU_STAGE_TOP;
      exInfo.ppCommandBuffers = &command;
      exInfo.pSignals         = NULL;
      exInfo.signalCount      = 0u;
      exInfo.pWaits           = &wait;
      exInfo.waitCount        = 1u;
      exInfo.fence            = fence;
      CHECK(GPUQueueSubmitEx(state.queue, &exInfo) == GPU_OK && GPUWaitFence(fence, 5000000000ull) == GPU_OK);
    }
  } else {
    printf("refusal: no semaphores here; the signal not checked\n");
  }

  CHECK(GPUDeviceWaitIdle(state.device) == GPU_OK);
  GPUDestroyFence(fence);
  GPUDestroySemaphore(semaphore);
  GPUDestroyBuffer(buffer);
  close_state(&state);
  printf("refusal by hand: frames and submits refused at once and alone, the frame's image the next one's, "
         "signals given, a write kept for after=%s\n", ok ? "pass" : "fail");
  return ok;
}
#endif

int
main(void) {
  @autoreleasepool {
    GPUInstance *instance = NULL;
    GPUSurface  *surface  = NULL;
    AppleWindow *window;
    GPUAdapter  *adapter;
    GPUBackend   backend;
    bool         ok = false;

#if TARGET_OS_OSX
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
#endif
    adapter = NULL;
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || gpu_test_request_adapter(instance, &adapter) != GPU_OK || !adapter) {
      GPUDestroyInstance(instance);
      return 77;
    }

    backend = backend_of(adapter);
    window  = appleWindowOpen(40.0, 40.0, 96u, 80u);
    surface = appleWindowSurface(window, instance, adapter);
    if (!surface)
      goto cleanup;

    appleWindowWaitShown(window, true, 0.5);
    ok = true;
#if GPU_TEST_REFUSAL
    if (backend == GPU_BACKEND_VULKAN || backend == GPU_BACKEND_WEBGPU)
      ok = check_refusal(adapter, surface, window);
#else
    (void)backend;
#endif
    ok = ok && check_hidden(adapter, surface, window);

cleanup:
    GPUDestroySurface(surface);
    appleWindowClose(window);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
