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
#import <QuartzCore/CATransaction.h>
#include <math.h>
#include <pthread.h>

typedef enum PresentationStatus {
  PRESENTATION_FAILED,
  PRESENTATION_OBSERVED,
  PRESENTATION_UNAVAILABLE
} PresentationStatus;

typedef struct ObservationData {
  GPUFrameEvent events[4];
  double        nativeTime;
  uint32_t      eventMask;
  uint32_t      eventsCount;
  uint32_t      completions;
  uint32_t      presentations;
  bool          failed;
  bool          selfDestroyed;
} ObservationData;

@interface Observation : NSObject {
@public
  GPUFrameFeedback *token;
  pthread_mutex_t   lock;
  ObservationData   data;
  uint64_t          frameId;
  double            target;
  bool              lockReady;
  bool              selfDestroy;
  bool              cancelled;
}
@end

typedef struct Fixture {
  GPUDevice    *device;
  GPUQueue     *queue;
  GPUSurface   *surface;
  GPUSwapchain *swapchain;
  NSWindow     *window;
  bool          unavailable;
} Fixture;

typedef struct PacedCase {
  Fixture       *fixture;
  Observation   *observation;
  GPUFramePacer *pacer;
  uint32_t       ticks;
  uint32_t       attempt;
  bool           failed;
} PacedCase;

static atomic_uint kCallbacks;

@implementation Observation
- (id)init {
  self = [super init];
  if (self && pthread_mutex_init(&lock, NULL) != 0) {
    [self release];
    return nil;
  }
  if (self)
    lockReady = true;
  return self;
}

- (void)dealloc {
  if (lockReady)
    pthread_mutex_destroy(&lock);
  [super dealloc];
}
@end

static void
snapshot(Observation *observation, ObservationData *data) {
  pthread_mutex_lock(&observation->lock);
  *data = observation->data;
  pthread_mutex_unlock(&observation->lock);
}

static void
feedback(const GPUFrameEvent *event, void *user) {
  Observation      *observation;
  GPUFrameFeedback *destroy = NULL;
  uint32_t          bit;

  atomic_fetch_add_explicit(&kCallbacks, 1u, memory_order_relaxed);
  observation = user;
  pthread_mutex_lock(&observation->lock);
  if (!event || observation->cancelled || event->kind < GPU_FRAME_EVENT_GPU_COMPLETE
      || event->kind > GPU_FRAME_EVENT_DISCARDED) {
    observation->data.failed = true;
  } else {
    bit = 1u << event->kind;
    if ((observation->data.eventMask & bit) || observation->data.eventsCount >= 4u
        || event->frameId != observation->frameId || event->targetPresentTime != observation->target
        || !!(event->flags & GPU_FRAME_EVENT_TARGET_TIME_VALID) != (observation->target > 0.0)) {
      observation->data.failed = true;
    } else {
      observation->data.events[observation->data.eventsCount++] = *event;
      observation->data.eventMask |= bit;
    }
  }

  if (observation->selfDestroy && observation->token) {
    destroy = observation->token;
    observation->token = NULL;
  }
  pthread_mutex_unlock(&observation->lock);

  if (destroy) {
    GPUDestroyFrameFeedback(destroy);
    pthread_mutex_lock(&observation->lock);
    observation->data.selfDestroyed = true;
    pthread_mutex_unlock(&observation->lock);
  }
}

static void
completed(void *user, GPUCommandBuffer *command) {
  Observation *observation;

  (void)command;
  observation = user;
  pthread_mutex_lock(&observation->lock);
  observation->data.completions++;
  pthread_mutex_unlock(&observation->lock);
}

static void
cancel(Observation *observation) {
  GPUFrameFeedback *token;

  if (!observation)
    return;

  pthread_mutex_lock(&observation->lock);
  token = observation->token;
  observation->token = NULL;
  pthread_mutex_unlock(&observation->lock);
  GPUDestroyFrameFeedback(token);
  pthread_mutex_lock(&observation->lock);
  observation->cancelled = true;
  pthread_mutex_unlock(&observation->lock);
}

static GPUResult
create_feedback(GPUFrame *frame, Observation *observation, GPUFrameFeedback **out) {
  GPUFrameFeedbackCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_FEEDBACK_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.callback         = feedback;
  info.user             = observation;
  info.frameId          = observation->frameId;
  return GPUCreateFrameFeedback(frame, &info, out);
}

static bool
attach(GPUFrame *frame, Observation *observation, bool nativeOracle) {
  GPUFrameFeedback *duplicate = (GPUFrameFeedback *)(uintptr_t)1u;

  if (create_feedback(frame, observation, &observation->token) != GPU_OK)
    return false;

  if (create_feedback(frame, observation, &duplicate) != GPU_ERROR_INVALID_ARGUMENT || duplicate) {
    if (duplicate && duplicate != (GPUFrameFeedback *)(uintptr_t)1u)
      GPUDestroyFrameFeedback(duplicate);
    return false;
  }

  if (nativeOracle) {
    [(id<CAMetalDrawable>)frame->drawable addPresentedHandler:^(id<MTLDrawable> drawable) {
      pthread_mutex_lock(&observation->lock);
      observation->data.nativeTime = drawable.presentedTime;
      observation->data.presentations++;
      pthread_mutex_unlock(&observation->lock);
    }];
  }
  return true;
}

static void
pump(double seconds) {
  NSDate  *deadline;
  NSEvent *event;

  deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while (deadline.timeIntervalSinceNow > 0.0) {
    event = [NSApp nextEventMatchingMask:NSEventMaskAny
                             untilDate:[NSDate dateWithTimeIntervalSinceNow:0.003]
                                inMode:NSDefaultRunLoopMode
                               dequeue:YES];
    if (event)
      [NSApp sendEvent:event];
    [NSApp updateWindows];
  }
}

static void
drain(GPUQueue *queue) {
  MTCommandQueue *native;

  native = mt_commandQueue(queue);
  if (!native)
    return;

  if (dispatch_group_wait(native->inFlightGroup,
                          dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0) {
    fprintf(stderr, "frame feedback: GPU completion timed out\n");
    abort();
  }
}

static bool
wait_observation(Observation *observation, uint32_t mask, bool nativeOracle, bool selfDestroy) {
  ObservationData data;
  NSDate         *deadline;

  deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
  do {
    snapshot(observation, &data);
    if (data.failed)
      return false;

    if ((data.eventMask & mask) == mask
        && (!nativeOracle || (data.presentations == 1u && data.completions == 1u
            && (selfDestroy || (data.eventMask & ((1u << GPU_FRAME_EVENT_PRESENTED)
                                                 | (1u << GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE))))))
        && (!selfDestroy || data.selfDestroyed))
      return true;

    pump(0.003);
  } while (deadline.timeIntervalSinceNow > 0.0);

  fprintf(stderr, "frame feedback: timeout id=%llu events=%u mask=%u native=%u complete=%u\n",
          (unsigned long long)observation->frameId, data.eventsCount, data.eventMask,
          data.presentations, data.completions);
  return false;
}

static GPUCommandBuffer*
encode_clear(GPUQueue *queue, GPUFrame *frame) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUCommandBuffer           *command = NULL;
  GPURenderPassEncoder        *pass;

  if (GPUAcquireCommandBuffer(queue, "feedback clear", &command) != GPU_OK)
    return NULL;

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[1] = 0.5f;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount  = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    return NULL;
  }

  GPUEndRenderPass(pass);
  return command;
}

static bool
submit_observed(Fixture *fixture, GPUFrame *frame, Observation *observation) {
  GPUCommandBuffer *command;

  if (!attach(frame, observation, true)) {
    GPUEndFrame(frame);
    return false;
  }

  if (!(command = encode_clear(fixture->queue, frame))) {
    GPUEndFrame(frame);
    return false;
  }

  GPUSetCommandBufferCompletionHandler(command, observation, completed);
  return GPUFinishFrame(fixture->queue, command, frame) == GPU_OK;
}

static PresentationStatus
check_presented(Observation *observation, bool selfDestroy) {
  ObservationData data;
  GPUFrameEvent  *event;
  uint32_t        i, mask;
  bool            ok;

  mask = selfDestroy ? 0u : 1u << GPU_FRAME_EVENT_GPU_COMPLETE;
  ok = wait_observation(observation, mask, true, selfDestroy);
  snapshot(observation, &data);
  ok = ok && data.eventsCount == (selfDestroy ? 1u : 2u)
       && isfinite(data.nativeTime) && data.nativeTime >= 0.0;
  for (i = 0u; i < data.eventsCount; i++) {
    event = &data.events[i];
    ok = ok && event->result == GPU_OK;
    if (event->kind == GPU_FRAME_EVENT_GPU_COMPLETE) {
      ok = ok && (event->flags & GPU_FRAME_EVENT_GPU_TIME_VALID)
           && isfinite(event->gpuStartTime) && isfinite(event->gpuEndTime)
           && event->gpuStartTime > 0.0 && event->gpuEndTime >= event->gpuStartTime;
    } else if (event->kind == GPU_FRAME_EVENT_PRESENTED) {
      ok = ok && data.nativeTime > 0.0 && event->presentedTime == data.nativeTime;
    } else if (event->kind == GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE) {
      ok = ok && data.nativeTime == 0.0 && event->presentedTime == 0.0;
    } else {
      ok = false;
    }
  }
  if (!ok) {
    fprintf(stderr, "feedback presentation: id=%llu native=%.9f events=%u self=%u\n",
            (unsigned long long)observation->frameId, data.nativeTime,
            data.eventsCount, selfDestroy);
    for (i = 0u; i < data.eventsCount; i++) {
      event = &data.events[i];
      fprintf(stderr, "  event=%u result=%d flags=%u presented=%.9f gpu=[%.9f,%.9f] target=%.9f\n",
              event->kind, event->result, event->flags, event->presentedTime,
              event->gpuStartTime, event->gpuEndTime, event->targetPresentTime);
    }
  }
  if (!ok)
    return PRESENTATION_FAILED;

  if (data.nativeTime == 0.0) {
    printf("feedback: id=%llu native time=0%s; no presentation observed\n",
           (unsigned long long)observation->frameId, selfDestroy ? "" : ", unavailable agrees");
    return PRESENTATION_UNAVAILABLE;
  }
  return PRESENTATION_OBSERVED;
}

static bool
check_manual(Fixture *fixture, bool selfDestroy) {
  Observation *observation;
  GPUFrame    *frame;
  uint32_t     attempt;
  PresentationStatus status;

  for (attempt = 0u; attempt < 3u; attempt++) {
    observation = [Observation new];
    if (!observation)
      return false;

    observation->frameId     = UINT64_C(0x100000001) + 16u * selfDestroy + attempt;
    observation->selfDestroy = selfDestroy;
    frame = GPUBeginFrame(fixture->swapchain);
    status = frame && submit_observed(fixture, frame, observation)
               ? check_presented(observation, selfDestroy) : PRESENTATION_FAILED;
    drain(fixture->queue);
    cancel(observation);
    [observation release];
    if (status != PRESENTATION_UNAVAILABLE) {
      printf("feedback: manual%s=%s attempt=%u\n", selfDestroy ? " self destroy" : " native timestamp",
             status == PRESENTATION_OBSERVED ? "pass" : "fail", attempt + 1u);
      return status == PRESENTATION_OBSERVED;
    }
    pump(0.02);
  }
  fixture->unavailable = true;
  printf("feedback: no actual manual presentation after 3 fresh frames; skip\n");
  return false;
}

static void
paced(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  PacedCase *test;

  test = user;
  test->ticks++;
  if (!frame)
    return;

  GPUSetFramePacerPaused(test->pacer, true);
  test->observation = [Observation new];
  if (!test->observation) {
    test->failed = true;
    GPUEndFrame(frame);
    return;
  }
  test->observation->frameId = UINT64_C(0x200000001) + test->attempt;
  test->observation->target  = timing->targetPresentTime;
  test->failed = !submit_observed(test->fixture, frame, test->observation);
}

static bool
check_paced(Fixture *fixture) {
  GPUFramePacerCreateInfo info = {0};
  PacedCase              test = {0};
  NSDate                *deadline;
  PresentationStatus     status;
  bool                   ok = false;

  test.fixture = fixture;
  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.swapchain        = fixture->swapchain;
  info.callback         = paced;
  info.user             = &test;
  if (GPUCreateFramePacer(fixture->device, &info, &test.pacer) != GPU_OK)
    goto cleanup;

  for (test.attempt = 0u; test.attempt < 3u; test.attempt++) {
    GPUSetFramePacerPaused(test.pacer, false);
    deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
    while (!test.observation && deadline.timeIntervalSinceNow > 0.0) {
      pump(0.003);
    }
    status = test.observation && !test.failed && test.observation->target > 0.0
               ? check_presented(test.observation, false) : PRESENTATION_FAILED;
    drain(fixture->queue);
    cancel(test.observation);
    [test.observation release];
    test.observation = nil;
    if (status != PRESENTATION_UNAVAILABLE) {
      ok = status == PRESENTATION_OBSERVED;
      goto cleanup;
    }
    pump(0.02);
  }
  fixture->unavailable = true;
  printf("feedback: no actual paced presentation after 3 fresh frames; skip\n");

cleanup:
  GPUDestroyFramePacer(test.pacer);
  drain(fixture->queue);
  cancel(test.observation);
  [test.observation release];
  printf("feedback: paced id/target/native timestamp=%s\n",
         ok ? "pass" : fixture->unavailable ? "unavailable" : "fail");
  return ok;
}

static bool
check_discard(Fixture *fixture, bool scheduled) {
  ObservationData   data;
  Observation      *observation;
  GPUFrame         *frame = NULL;
  GPUCommandBuffer *command = NULL;
  bool              ok = false;

  observation = [Observation new];
  if (!observation)
    return false;

  observation->frameId = UINT64_C(0x300000001) + scheduled;
  if (!(frame = GPUBeginFrame(fixture->swapchain)) || !attach(frame, observation, false))
    goto cleanup;

  if (scheduled) {
    if (!(command = encode_clear(fixture->queue, frame)))
      goto cleanup;
    GPUSchedulePresent(command, frame);
    if (GPUDiscardCommandBuffer(command) != GPU_OK)
      goto cleanup;
    command = NULL;
  }
  GPUEndFrame(frame);
  frame = NULL;
  ok = wait_observation(observation, 1u << GPU_FRAME_EVENT_DISCARDED, false, false);
  snapshot(observation, &data);
  ok = ok && data.eventsCount == 1u && data.events[0].result == GPU_OK
       && data.events[0].flags == 0u && data.events[0].gpuStartTime == 0.0
       && data.events[0].gpuEndTime == 0.0 && data.events[0].presentedTime == 0.0;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUEndFrame(frame);
  cancel(observation);
  [observation release];
  printf("feedback: %s=%s\n", scheduled ? "schedule then discard" : "unscheduled end", ok ? "pass" : "fail");
  return ok;
}

static bool
check_late_create(Fixture *fixture) {
  Observation      *observation;
  GPUFrame         *frame = NULL;
  GPUCommandBuffer *command = NULL;
  GPUFrameFeedback *token = (GPUFrameFeedback *)(uintptr_t)1u;
  bool              ok = false;

  observation = [Observation new];
  if (!observation)
    return false;

  observation->frameId = UINT64_C(0x400000001);
  if (!(frame = GPUBeginFrame(fixture->swapchain))
      || !(command = encode_clear(fixture->queue, frame)))
    goto cleanup;

  GPUSchedulePresent(command, frame);
  ok = create_feedback(frame, observation, &token) == GPU_ERROR_INVALID_ARGUMENT && !token;

cleanup:
  if (token && token != (GPUFrameFeedback *)(uintptr_t)1u)
    GPUDestroyFrameFeedback(token);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUEndFrame(frame);
  [observation release];
  printf("feedback: late create rejected=%s\n", ok ? "pass" : "fail");
  return ok;
}

static bool
check_cancel_pending(Fixture *fixture) {
  GPUQueueSubmitExInfo   submit = {0};
  GPUQueueSemaphoreWait wait   = {0};
  GPUCommandBuffer     *commands[1];
  Observation          *observation;
  GPUSwapchain         *swapchain = NULL;
  GPUFrame             *frame = NULL;
  GPUCommandBuffer     *command = NULL;
  GPUFence             *fence = NULL;
  GPUSemaphore         *gate = NULL;
  uint32_t              before;
  bool                  ok = false;

  observation = [Observation new];
  if (!observation)
    return false;

  observation->frameId = UINT64_C(0x500000001);
  swapchain = GPUCreateSwapchainDefault(fixture->device, fixture->surface, 96u, 96u);
  if (!swapchain || GPUCreateFence(fixture->device, NULL, &fence) != GPU_OK
      || GPUCreateSemaphore(fixture->device, NULL, &gate) != GPU_OK
      || !(frame = GPUBeginFrame(swapchain)) || !attach(frame, observation, false)
      || !(command = encode_clear(fixture->queue, frame)))
    goto cleanup;

  GPUSchedulePresent(command, frame);
  commands[0] = command;
  wait.semaphore  = gate;
  wait.value      = 1u;
  wait.waitStages = GPU_STAGE_TOP;
  submit.ppCommandBuffers   = commands;
  submit.commandBufferCount = 1u;
  submit.pWaits              = &wait;
  submit.waitCount           = 1u;
  submit.fence               = fence;
  if (GPUQueueSubmitEx(fixture->queue, &submit) != GPU_OK)
    goto cleanup;
  command = NULL;
  GPUEndFrame(frame);
  frame = NULL;
  if (GPUWaitFence(fence, 0u) != GPU_ERROR_TIMEOUT)
    goto cleanup;

  cancel(observation);
  [observation release];
  observation = nil;
  GPUDestroySwapchain(swapchain);
  swapchain = NULL;
  before = atomic_load_explicit(&kCallbacks, memory_order_relaxed);
  [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
  if (GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK)
    goto cleanup;
  drain(fixture->queue);
  pump(0.1);
  ok = atomic_load_explicit(&kCallbacks, memory_order_relaxed) == before;

cleanup:
  if (gate)
    [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
  drain(fixture->queue);
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUEndFrame(frame);
  cancel(observation);
  [observation release];
  GPUDestroySwapchain(swapchain);
  GPUDestroyFence(fence);
  GPUDestroySemaphore(gate);
  printf("feedback: pending cancel, parent/user destruction=%s\n", ok ? "pass" : "fail");
  return ok;
}

static int
run_cases(GPUInstance *instance, GPUAdapter *adapter, GPUDevice *device) {
  Fixture fixture = {0};
  NSDate *deadline;
  bool    ok, positive, controls;

  fixture.device = device;
  fixture.queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  fixture.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(60, 60, 96, 96)
                                              styleMask:NSWindowStyleMaskTitled
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
  [fixture.window setReleasedWhenClosed:NO];
  [fixture.window orderFrontRegardless];
  fixture.surface = GPUCreateSurfaceFromNative(instance, adapter, fixture.window.contentView,
                                                GPU_SURFACE_APPLE_NSVIEW, 1.0f);
  fixture.swapchain = GPUCreateSwapchainDefault(device, fixture.surface, 96u, 96u);
  [CATransaction flush];
  ok = fixture.queue && fixture.surface && fixture.swapchain;
  if (ok) {
    /* an ordered window may still be occluded; wait only a bounded interval. */
    deadline = [NSDate dateWithTimeIntervalSinceNow:0.5];
    while (!(fixture.window.occlusionState & NSWindowOcclusionStateVisible)
           && deadline.timeIntervalSinceNow > 0.0) {
      pump(0.005);
    }
    printf("feedback: window ordered=%u visible=%u occlusion=%llu\n", fixture.window.visible,
           !!(fixture.window.occlusionState & NSWindowOcclusionStateVisible),
           (unsigned long long)fixture.window.occlusionState);
    positive = check_manual(&fixture, false)
               && check_manual(&fixture, true)
               && check_paced(&fixture);

    controls = check_discard(&fixture, false);
    if (!check_discard(&fixture, true))
      controls = false;
    if (!check_late_create(&fixture))
      controls = false;
    if (!check_cancel_pending(&fixture))
      controls = false;
    ok = controls && (positive || fixture.unavailable);
  }
  drain(fixture.queue);
  GPUDestroySwapchain(fixture.swapchain);
  GPUDestroySurface(fixture.surface);
  [fixture.window close];
  [fixture.window release];
  return !ok ? 1 : fixture.unavailable ? 77 : 0;
}

static Observation*
create_orphan(GPUInstance *instance, GPUAdapter *adapter, GPUDevice *device) {
  ObservationData   data;
  Observation      *observation;
  GPUFrame         *frame = NULL;
  GPUCommandBuffer *command = NULL;
  GPUSurface       *surface = NULL;
  GPUSwapchain     *swapchain = NULL;
  GPUQueue         *queue;
  NSWindow         *window;
  bool              ok = false;

  observation = [Observation new];
  if (!observation)
    return nil;

  observation->frameId = UINT64_C(0x600000001);
  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 64, 64)
                                      styleMask:NSWindowStyleMaskTitled
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  [window setReleasedWhenClosed:NO];
  [window orderFront:nil];
  surface = GPUCreateSurfaceFromNative(instance, adapter, window.contentView,
                                        GPU_SURFACE_APPLE_NSVIEW, 1.0f);
  swapchain = GPUCreateSwapchainDefault(device, surface, 64u, 64u);
  if (!queue || !surface || !swapchain || !(frame = GPUBeginFrame(swapchain))
      || !attach(frame, observation, false) || !(command = encode_clear(queue, frame)))
    goto cleanup;

  GPUSchedulePresent(command, frame);
  GPUEndFrame(frame);
  frame = NULL;
  GPUDestroySwapchain(swapchain);
  swapchain = NULL;
  snapshot(observation, &data);
  ok = data.eventsCount == 0u && !data.failed;

cleanup:
  if (!ok && command)
    GPUDiscardCommandBuffer(command);
  GPUEndFrame(frame);
  GPUDestroySwapchain(swapchain);
  GPUDestroySurface(surface);
  [window close];
  [window release];
  if (!ok) {
    cancel(observation);
    [observation release];
    return nil;
  }

  /* the last device teardown owns this intentionally unsubmitted command. */
  return observation;
}

static bool
check_orphan(Observation *observation) {
  ObservationData data;
  bool            ok;

  ok = wait_observation(observation, 1u << GPU_FRAME_EVENT_DISCARDED, false, false);
  snapshot(observation, &data);
  ok = ok && data.eventsCount == 1u && data.events[0].result == GPU_OK
       && data.events[0].flags == 0u && data.events[0].gpuStartTime == 0.0
       && data.events[0].gpuEndTime == 0.0 && data.events[0].presentedTime == 0.0;
  cancel(observation);
  [observation release];
  printf("feedback: orphaned command device teardown=%s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUDeviceCreateInfo  info = {0};
    GPURuntimeConfig     runtime = {0};
    GPUAdapterProperties properties = {0};
    const GPUFeature     features[] = {GPU_FEATURE_FRAME_PACER, GPU_FEATURE_FRAME_FEEDBACK};
    GPUInstance         *instance = NULL;
    GPUAdapter          *adapter;
    GPUDevice           *device = NULL;
    MTCommandQueue      *native;
    Observation         *orphan = nil;
    const char          *requested;
    GPUResult            result;
    int                  status = 1;

    atomic_init(&kCallbacks, 0u);
    requested = getenv("GPU_METAL_MODE");
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    if (GPUCreateInstance(NULL, &instance) != GPU_OK)
      goto cleanup;

    if (!(adapter = GPUGetAutoSelectedAdapter(instance))
        || GPUGetAdapterProperties(adapter, &properties) != GPU_OK
        || properties.backend != GPU_BACKEND_METAL)
      goto cleanup;

    if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_FEEDBACK)
        || !GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_PACER)) {
      status = 77;
      goto cleanup;
    }

    info.required.pFeatures    = features;
    info.required.featureCount = 2u;
    result = GPUCreateDevice(adapter, &info, &device);
    if (result != GPU_OK) {
      if (result == GPU_ERROR_UNSUPPORTED)
        status = 77;
      goto cleanup;
    }

    runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
    runtime.chain.structSize = sizeof(runtime);
    runtime.validationMode  = GPU_VALIDATION_FULL;
    runtime.enableStats     = false;
    if (GPUConfigureRuntime(device, &runtime) != GPU_OK)
      goto cleanup;

    native = mt_commandQueue(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u));
    if (!native)
      goto cleanup;
    printf("feedback: requested=%s native=%s stats=off\n", requested ? requested : "auto",
           native->mode == MTCommandMode4 ? "metal4" : "classic");
    if (requested && !strcmp(requested, "metal4") && native->mode != MTCommandMode4) {
      status = 77;
      goto cleanup;
    }
    if (requested && !strcmp(requested, "classic") && native->mode != MTCommandModeClassic)
      goto cleanup;

    status = run_cases(instance, adapter, device);
    if ((status == 0 || status == 77) && !(orphan = create_orphan(instance, adapter, device)))
      status = 1;

cleanup:
    GPUDestroyDevice(device);
    if (orphan && !check_orphan(orphan))
      status = 1;
    GPUDestroyInstance(instance);
    return status;
  }
}
