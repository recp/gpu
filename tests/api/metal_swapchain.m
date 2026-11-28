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
#include "apple_window.h"
#import <QuartzCore/CATransaction.h>
#include <float.h>
#include <math.h>
#include <pthread.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
    ok = false; \
  } \
} while (0)

static bool
present(GPUDevice *device, GPUSwapchain *swapchain, AppleWindow *window, uint32_t index,
        id<CAMetalDrawable> *outDrawable) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUTransientBufferSlice      slice = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  GPUSwapchainMetal          *native;
  GPUFrame                   *frame;
  bool                        ok = true;

  native = swapchain->_priv;
  if (!(frame = GPUBeginFrame(swapchain))) {
    /* a window the system doesn't show gives no frame, and at once */
    return !appleWindowShown(window) && GPUGetSwapchainStatus(swapchain) == GPU_SWAPCHAIN_STATUS_UNAVAILABLE;
  }

  CHECK(frame->target->width == (uint32_t)native->layer.drawableSize.width
       && frame->target->height == (uint32_t)native->layer.drawableSize.height
       && GPUResizeSwapchain(swapchain, 96u, 80u) == GPU_ERROR_INVALID_ARGUMENT
       && GPUSetSwapchainScale(swapchain, 3.0f)
          == (GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_SCALE)
                ? GPU_ERROR_INVALID_ARGUMENT : GPU_ERROR_UNSUPPORTED));
  if (device->transientConfigured) {
    CHECK(GPUAllocateTransientBuffer(device, GPU_BUFFER_USAGE_UNIFORM,
                                     256u, 256u, &slice) == GPU_OK);
    if (slice.cpuPtr)
      memset(slice.cpuPtr, (int)index, 256u);
    else
      CHECK(false);
  }

  if (GPUAcquireCommandBuffer(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u),
                              "multiwindow clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = (float)(index % 3u) / 2.0f;
  color.clearColor.float32[1] = (float)((index + 1u) % 3u) / 2.0f;
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
  if (outDrawable)
    *outDrawable = [(id<CAMetalDrawable>)frame->drawable retain];

  return GPUFinishFrame(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u), command, frame) == GPU_OK && ok;
}

static void*
reject_resize(void *user) {
  GPUSwapchain      *swapchain;
  GPUSwapchainMetal *native;

  @autoreleasepool {
    swapchain = user;
    native    = swapchain->_priv;
    return (void *)(uintptr_t)(GPUSetSwapchainScale(swapchain, 2.0f) == GPU_ERROR_INVALID_ARGUMENT
                               && GPUResizeSwapchain(swapchain, 64u, 64u) == GPU_ERROR_INVALID_ARGUMENT
                               && (!native->layer.presentsWithTransaction || !GPUBeginFrame(swapchain)));
  }
}

static bool
check_options(GPUAdapter *adapter, GPUSurface *surface, AppleWindow *window) {
  GPUDeviceCreateInfo    deviceInfo = {0};
  GPUSwapchainCreateInfo info       = {0};
  GPUSwapchainOptions    options    = {0};
  GPUDevice            *device;
  GPUSwapchain         *swapchain = NULL;
  GPUViewHandle        *view;
  bool                  ok = true;

  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK)
    return false;

  info.surface          = surface;
  info.width            = 96u;
  info.height           = 80u;
  info.format           = GPU_FORMAT_BGRA8_UNORM;
  info.chain.pNext      = &options;
  options.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS;
  options.chain.structSize = sizeof(options);
  options.colorSpace       = GPU_COLOR_SPACE_SRGB;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  options.colorSpace = GPU_COLOR_SPACE_DEFAULT;
  options.presentsWithTransaction = true;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  options.presentsWithTransaction = false;
  options.colorSpace = (GPUColorSpace)99;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.colorSpace = GPU_COLOR_SPACE_DEFAULT;
  options.chain.structSize = sizeof(GPUChainedStruct);
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.chain.structSize = sizeof(options);
  options.chain.pNext = &options;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.chain.pNext = NULL;
  options.chain.sType = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  info.chain.pNext = NULL;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_OK && swapchain);
  if (swapchain) {
    CHECK(GPUSetSwapchainScale(swapchain, 2.0f) == GPU_ERROR_UNSUPPORTED);
    CHECK(!((GPUSwapchainMetal *)swapchain->_priv)->layer.presentsWithTransaction);
    CHECK(present(device, swapchain, window, 0u, NULL));
  }

  GPUDestroySwapchain(swapchain);
  view = surface->_priv;
#if TARGET_OS_OSX
  /* the view's own layer, the default swapchain's, is AppKit's again and in
   * the window's tree: what goes on the view next (check_windows' sublayer)
   * shows */
  CHECK(view.layer && ![view.layer isKindOfClass:[CAMetalLayer class]] && view.layer.superlayer);
#else
  /* the default swapchain's layer, the view's sublayer, is gone with it */
  CHECK(view.layer && ![view.layer isKindOfClass:[CAMetalLayer class]]);
  for (CALayer *sublayer in view.layer.sublayers)
    CHECK(![sublayer isKindOfClass:[CAMetalLayer class]]);
#endif
  GPUDestroyDevice(device);
  printf("options: default, disabled features, malformed chains=%s\n", ok ? "pass" : "fail");
  return ok;
}

/* frames shown on both windows' screens. Core Animation gives no
 * presentation time to a fresh layer's first presents (the epochs' bursts)
 * nor to the first after a second idle, and a refresh shows only its last
 * present: frames go on as an app's do, one a refresh, until one of each
 * window's has its time. A window hidden meanwhile leaves them unseen, not
 * failed */
static bool
check_presented(GPUDevice *device, GPUSwapchain **swapchains, AppleWindow **windows, bool *onscreen) {
  id<CAMetalDrawable> drawables[2] = {nil, nil};
  NSDate             *deadline;
  uint32_t            window, round;
  bool                seen[2] = {false, false};
  bool                ok = true;

  for (round = 0u; round < 30u && !(seen[0] && seen[1]); round++) {
    for (window = 0u; window < 2u; window++) {
      if (!appleWindowShown(windows[window])) {
        *onscreen = false;
        goto done;
      }

      [drawables[window] release];
      drawables[window] = nil;
      CHECK(present(device, swapchains[window], windows[window], round + window, &drawables[window])
            && drawables[window]);
    }

    [CATransaction flush];
    deadline = [NSDate dateWithTimeIntervalSinceNow:0.05];
    do {
      appleWindowPump(0.002);
      for (window = 0u; window < 2u; window++)
        seen[window] = seen[window] || drawables[window].presentedTime > 0.0;
    } while (!(seen[0] && seen[1]) && deadline.timeIntervalSinceNow > 0.0);
  }

  CHECK(seen[0] && seen[1]);

done:
  for (window = 0u; window < 2u; window++)
    [drawables[window] release];
  return ok;
}

static bool
check_windows(GPUAdapter *adapter, GPUSurface **surfaces, AppleWindow **windows, bool *onscreen) {
  const GPUFeature features[] = {GPU_FEATURE_SWAPCHAIN_SCALE, GPU_FEATURE_SWAPCHAIN_COLOR_SPACE,
                                 GPU_FEATURE_SWAPCHAIN_TRANSACTIONS};
  const float scales[] = {1.0f, 2.0f, 1.5f, 0.5f};
  const float invalid[] = {0.0f, -1.0f, NAN, INFINITY, FLT_MAX, FLT_MIN};
  GPUDeviceCreateInfo          deviceInfo = {0};
  GPUTransientAllocatorConfig transient  = {0};
  GPUSwapchainCreateInfo       info      = {0};
  GPUSwapchainOptions          options   = {0};
  GPUSwapchain               *swapchains[2] = {NULL, NULL};
  GPUDevice                  *device;
  GPUSwapchainMetal          *native;
  CGColorSpaceRef             expected;
  pthread_t                   worker;
  void                       *workerResult;
  uint32_t                    window, epoch, value;
  bool                        ok = true;

  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = GPU_ARRAY_LEN(features);
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK)
    return false;

  transient.chain.sType      = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  transient.chain.structSize = sizeof(transient);
  transient.ringBytesPerFrame = 4096u;
  transient.chunkBytes        = 4096u;
  transient.framesInFlight    = 3u;
  CHECK(GPUConfigureTransientAllocator(device, &transient) == GPU_OK);
  info.chain.pNext          = &options;
  info.width                = 96u;
  info.height               = 80u;
  info.format               = GPU_FORMAT_BGRA8_UNORM;
  options.chain.sType       = GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS;
  options.chain.structSize  = sizeof(options);

  for (window = 0u; window < 2u; window++) {
    info.surface       = surfaces[window];
    options.colorSpace = window ? GPU_COLOR_SPACE_DISPLAY_P3 : GPU_COLOR_SPACE_SRGB;
    options.presentsWithTransaction = window != 0u;
    if (GPUCreateSwapchain(device, &info, &swapchains[window]) != GPU_OK) {
      CHECK(false);
      goto cleanup;
    }

    native = swapchains[window]->_priv;
    expected = CGColorSpaceCreateWithName(window ? kCGColorSpaceDisplayP3 : kCGColorSpaceSRGB);
    CHECK(expected && CFEqual(native->layer.colorspace, expected)
         && native->layer.presentsWithTransaction == (window != 0u));
    if (expected)
      CGColorSpaceRelease(expected);

    for (value = 0u; value < GPU_ARRAY_LEN(invalid); value++) {
      CHECK(GPUSetSwapchainScale(swapchains[window], invalid[value]) == GPU_ERROR_INVALID_ARGUMENT
           && swapchains[window]->backingScaleFactor == 1.0f);
    }

    CHECK(GPUResizeSwapchain(swapchains[window], 0u, 80u) == GPU_ERROR_INVALID_ARGUMENT
         && GPUResizeSwapchain(swapchains[window], UINT32_MAX, 80u) != GPU_OK
         && GPUSetSwapchainScale(swapchains[window], 4096.0f) == GPU_ERROR_UNSUPPORTED
         && swapchains[window]->backingScaleFactor == 1.0f
         && native->layer.contentsScale == 1.0f
         && native->layer.drawableSize.width == 96.0);
    if (pthread_create(&worker, NULL, reject_resize, swapchains[window]) != 0) {
      CHECK(false);
      goto cleanup;
    }

    pthread_join(worker, &workerResult);
    CHECK((uintptr_t)workerResult == 1u && swapchains[window]->backingScaleFactor == 1.0f);
  }

  for (epoch = 0u; epoch < GPU_ARRAY_LEN(scales); epoch++) {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (window = 0u; window < 2u; window++) {
      native = swapchains[window]->_priv;
      CHECK(GPUSetSwapchainScale(swapchains[window], scales[epoch]) == GPU_OK
           && GPUResizeSwapchain(swapchains[window], 96u, 80u) == GPU_OK
           && native->layer.contentsScale == scales[epoch]
           && native->layer.drawableSize.width == 96.0 * scales[epoch]
           && native->layer.drawableSize.height == 80.0 * scales[epoch]
           && surfaces[window]->scale == 1.0f);
      /* exercise the observer after changing the scale, without the API path. */
      ((GPUViewHandle *)surfaces[window]->_priv).layer.bounds = CGRectMake(0, 0, 100 + epoch, 84 + epoch);
      CHECK(native->layer.drawableSize.width == ceil((100.0 + epoch) * scales[epoch])
           && native->layer.drawableSize.height == ceil((84.0 + epoch) * scales[epoch]));
      CHECK(GPUResizeSwapchain(swapchains[window], 96u, 80u) == GPU_OK);
      CHECK(present(device, swapchains[window], windows[window], epoch + window, NULL));
    }
    [CATransaction commit];
  }

  [CATransaction flush];
  appleWindowPump(0.0);
  CHECK(deviceApi(device)->device.waitIdle(device) == GPU_OK);
  *onscreen = true;
  CHECK(check_presented(device, swapchains, windows, onscreen));
  if (!*onscreen)
    printf("onscreen timestamps unavailable: test windows are occluded\n");

cleanup:
  for (window = 0u; window < 2u; window++)
    GPUDestroySwapchain(swapchains[window]);
  GPUDestroyDevice(device);
  printf("multiwindow: sRGB/P3, scale/KVO, transaction, thread rules, transient ring=%s\n", ok ? "pass" : "fail");
  return ok;
}

typedef struct HiddenState {
  AppleWindow   *window;
  GPUQueue      *queue;
  GPUSwapchain  *swapchain;
  atomic_uint    errors;
  _Atomic(int)   errorType;
  _Atomic(int)   errorResult;
  _Atomic(int)   errorLost;
  uint32_t       hiddenTicks;  /* the pacer's, with the window hidden */
  uint32_t       hiddenFrames;
  uint32_t       shownFrames;
  bool           hiding;
} HiddenState;

static void
hidden_error(GPUDevice *device, const GPUDeviceErrorInfo *error, void *user) {
  HiddenState *state;

  (void)device;
  state = user;
  atomic_store_explicit(&state->errorType, (int)error->type, memory_order_relaxed);
  atomic_store_explicit(&state->errorResult, (int)error->result, memory_order_relaxed);
  atomic_store_explicit(&state->errorLost, (int)error->lostReason, memory_order_relaxed);
  atomic_fetch_add_explicit(&state->errors, 1u, memory_order_release);
  printf("hidden window: device error type %d result %d lost %d: %s\n", (int)error->type, (int)error->result,
         (int)error->lostReason, error->message ? error->message : "");
}

/* the system gave up on the queue: nothing submitted after runs */
static bool
queue_lost(GPUQueue *queue) {
  MTCommandQueue *native;

  native = mt_commandQueue(queue);
  return !native || atomic_load_explicit(&native->lost, memory_order_acquire);
}

/* GPU work with no frame: a command buffer of its own, submitted and waited for */
static GPUResult
submit_plain(GPUQueue *queue) {
  GPUQueueSubmitInfo info = {0};
  GPUCommandBuffer  *command;
  GPUResult          result;

  if ((result = GPUAcquireCommandBuffer(queue, "hidden plain", &command)) != GPU_OK)
    return result;

  info.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize   = sizeof(info);
  info.commandBufferCount = 1u;
  info.ppCommandBuffers   = &command;
  if ((result = GPUQueueSubmit(queue, &info)) != GPU_OK)
    return result;

  return GPUQueueWaitIdle(queue);
}

/* the device errors since `before`, once `expect` came or that long passed */
static uint32_t
errors_after(HiddenState *state, uint32_t before, uint32_t expect, double seconds) {
  double deadline;

  deadline = CACurrentMediaTime() + seconds;
  while (atomic_load_explicit(&state->errors, memory_order_acquire) - before < expect
         && CACurrentMediaTime() < deadline)
    appleWindowPump(0.005);

  return atomic_load_explicit(&state->errors, memory_order_acquire) - before;
}

static bool
clear_frame(GPUQueue *queue, GPUFrame *frame) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;

  if (GPUAcquireCommandBuffer(queue, "hidden clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return false;
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
    return false;
  }

  GPUEndRenderPass(pass);
  return GPUFinishFrame(queue, command, frame) == GPU_OK;
}

/* a pacer's frames: none with the window hidden, drawn once shown again */
static void
hidden_tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  HiddenState *state;

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

/* a window the system doesn't show (macOS: covered; iOS: the app in the
 * background) gives no frame, at once and not after nextDrawable's second.
 * iOS refuses GPU work from the background: a submit there fails alone (a
 * backend failure reported, the queue not lost), and a pacer running across
 * gives no frame there. Shown again, frames come back and the queue runs */
static bool
check_hidden(GPUAdapter *adapter, GPUSurface *surface, AppleWindow *window) {
  GPUDeviceCreateInfo     deviceInfo = {0};
  GPUFramePacerCreateInfo pacerInfo  = {0};
  HiddenState             state      = {0};
  GPUFramePacer          *pacer      = NULL;
  GPUDevice              *device     = NULL;
  GPUFrame               *frame;
  GPUResult               result;
  const GPUFeature        features[] = {GPU_FEATURE_FRAME_PACER};
  double                  start, waited, deadline, unhide;
  uint32_t                before, errors;
  bool                    paced, refused = false, ok = true;

  if (!appleWindowWaitShown(window, true, 1.0)) {
    printf("hidden window: not shown to begin with; skip\n");
    return true;
  }

  atomic_init(&state.errors, 0u);
  atomic_init(&state.errorType, 0);
  atomic_init(&state.errorResult, 0);
  atomic_init(&state.errorLost, 0);
  state.window = window;
#if TARGET_OS_IOS
  paced  = GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_PACER);
  unhide = 20.0; /* the app is brought back from outside: that takes seconds */
#else
  paced  = false; /* a covered window's pacer frames draw fine there: nothing to check */
  unhide = 1.0;
#endif
  if (paced) {
    deviceInfo.required.pFeatures    = features;
    deviceInfo.required.featureCount = 1u;
  }

  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK
      || !(state.swapchain = GPUCreateSwapchainDefault(device, surface, 96u, 80u))) {
    GPUDestroyDevice(device);
    return false;
  }

  GPUSetDeviceErrorCallback(device, hidden_error, &state);
  state.queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (paced) {
    pacerInfo.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
    pacerInfo.chain.structSize = sizeof(pacerInfo);
    pacerInfo.swapchain        = state.swapchain;
    pacerInfo.callback         = hidden_tick;
    pacerInfo.user             = &state;
    pacerInfo.latencyFrames    = 1u;
    CHECK(GPUCreateFramePacer(device, &pacerInfo, &pacer) == GPU_OK);
  }

  if (ok && (state.hiding = appleWindowHide(window, 2.0))) {
    start  = CACurrentMediaTime();
    frame  = GPUBeginFrame(state.swapchain);
    waited = CACurrentMediaTime() - start;
    CHECK(!frame && waited < 0.1 && GPUGetSwapchainStatus(state.swapchain) == GPU_SWAPCHAIN_STATUS_UNAVAILABLE);
    GPUEndFrame(frame);

#if TARGET_OS_IOS
    /* the system takes the GPU away after the scene's notification (the
     * frames above were refused from the notification on): mostly within a
     * fraction of a second, at times not for seconds. Work it still runs
     * meanwhile is fine; a refusal never seen is the system's, not failed */
    start    = CACurrentMediaTime();
    deadline = start + 10.0;
    do {
      before = atomic_load_explicit(&state.errors, memory_order_acquire);
      result = submit_plain(state.queue);
      errors = errors_after(&state, before, 1u, 0.1);
    } while (result == GPU_OK && errors == 0u && CACurrentMediaTime() < deadline);
    if (result == GPU_OK && errors == 0u) {
      printf("hidden window: the system ran GPU work %.1f s into the background; its refusal unseen\n",
             CACurrentMediaTime() - start);
    } else {
      refused = true;
      CHECK(result == GPU_OK && errors == 1u
            && atomic_load_explicit(&state.errorType, memory_order_relaxed) == GPU_DEVICE_ERROR_BACKEND
            && atomic_load_explicit(&state.errorResult, memory_order_relaxed) == GPU_ERROR_BACKEND_FAILURE
            && atomic_load_explicit(&state.errorLost, memory_order_relaxed) == GPU_DEVICE_LOST_REASON_UNKNOWN
            && !queue_lost(state.queue));
      printf("hidden window: plain submit refused %.2f s in: result %d, errors %u, queue lost %d\n",
             CACurrentMediaTime() - start, (int)result, errors, (int)queue_lost(state.queue));
    }
#else
    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    result = submit_plain(state.queue);
    errors = errors_after(&state, before, 0u, 0.1);
    CHECK(result == GPU_OK && errors == 0u);
#endif

    if (pacer) {
      GPUSetFramePacerPaused(pacer, false);
      appleWindowPump(0.3);
      CHECK(state.hiddenFrames == 0u);
      printf("hidden window: pacer ticks hidden %u, frames %u\n", state.hiddenTicks, state.hiddenFrames);
    }
  } else if (ok) {
    printf("hidden window: it couldn't be hidden here; skip\n");
  }

  /* a cover that didn't hide the window is taken away all the same; a failed
   * check above doesn't keep it hidden */
  if (!appleWindowUnhide(window, unhide)) {
    printf("hidden window: not shown again; skip the rest\n");
  } else if (ok) {
    if (pacer) {
      /* the pacer's frames come back */
      GPUSetFramePacerPaused(pacer, false);
      deadline = CACurrentMediaTime() + 1.0;
      while (state.shownFrames < 3u && CACurrentMediaTime() < deadline)
        appleWindowPump(0.005);
      GPUSetFramePacerPaused(pacer, true);
      CHECK(state.shownFrames >= 3u && state.hiddenFrames == 0u);
      printf("hidden window: pacer frames shown again %u\n", state.shownFrames);
    }

    before = atomic_load_explicit(&state.errors, memory_order_acquire);
    CHECK(submit_plain(state.queue) == GPU_OK);
    frame = GPUBeginFrame(state.swapchain);
    CHECK(frame && GPUGetSwapchainStatus(state.swapchain) == GPU_SWAPCHAIN_STATUS_READY);
    if (frame)
      CHECK(clear_frame(state.queue, frame));
    CHECK(GPUQueueWaitIdle(state.queue) == GPU_OK && errors_after(&state, before, 0u, 0.1) == 0u
          && !queue_lost(state.queue));
  }

  GPUDestroyFramePacer(pacer);
  GPUDestroySwapchain(state.swapchain);
  GPUDestroyDevice(device);
#if TARGET_OS_IOS
  printf("hidden window (the background): no frame, at once; %s; no pacer frame; "
         "frames and the queue when shown again=%s\n", refused ? "GPU work refused alone" : "no GPU work refused",
         ok ? "pass" : "fail");
#else
  (void)refused;
  printf("hidden window: no frame, at once; frames when shown again=%s\n", ok ? "pass" : "fail");
#endif
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUInstance *instance = NULL;
    GPUSurface  *surfaces[2] = {NULL, NULL};
    AppleWindow *windows[2]  = {NULL, NULL};
    GPUAdapter  *adapter;
    uint32_t     i;
    bool         ok = false;
    bool         onscreen = false;

#if TARGET_OS_OSX
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
#endif
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      return 1;

    /* only frames of a window the system shows reach the screen */
    for (i = 0u; i < 2u; i++) {
      windows[i]  = appleWindowOpen(40.0 + i * 128.0, 40.0, 96u, 80u);
      surfaces[i] = appleWindowSurface(windows[i], instance, adapter);
      if (!surfaces[i])
        goto cleanup;
    }

    for (i = 0u; i < 2u; i++)
      appleWindowWaitShown(windows[i], true, 0.5);

    ok = check_options(adapter, surfaces[0], windows[0]) && check_windows(adapter, surfaces, windows, &onscreen)
         && check_hidden(adapter, surfaces[0], windows[0]);

cleanup:
    for (i = 0u; i < 2u; i++) {
      GPUDestroySurface(surfaces[i]);
      appleWindowClose(windows[i]);
    }
    GPUDestroyInstance(instance);
    if (ok && getenv("GPU_SWAPCHAIN_ONSCREEN") && !onscreen)
      return 77;

    return ok ? 0 : 1;
  }
}
