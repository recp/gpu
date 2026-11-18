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

/* the frame pacer on every backend: ticks near the display's refresh on the
 * creating thread, pause stops them, frames drawn in ticks and by hand,
 * destruction in and out of a tick with no tick after it, a lower rate, and
 * how long a tick's frame takes to reach the display */

#include "frame_pacer_host.h"
#include "test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PACER_TEST_VULKAN)
#  include "../../src/backend/vk/common.h"
#endif

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

enum { SAMPLE_CAPACITY = 256u, INTERVAL_CAPACITY = 512u };

typedef struct PacerSample {
  double tick;       /* the callback's start */
  double deadline;
  double target;
  double acquired;   /* the target in hand */
  double submitted;  /* GPUFinishFrame returned */
  double presented;  /* reached the display (or the consumer); zero unknown */
} PacerSample;

typedef struct PacerTest {
  GPUDevice     *device;
  GPUQueue      *queue;
  GPUSurface    *surface;
  GPUSwapchain  *swapchain;
  GPUFramePacer *pacer;
  GPUFrame      *manual;
  uintptr_t      thread;
  uint32_t       frames;
  uint32_t       ticks;
  uint32_t       phase;
  uint32_t       width;
  uint32_t       height;
  double         lastTick;
  double         intervals[INTERVAL_CAPACITY];
  uint32_t       intervalCount;
  double         refreshes[INTERVAL_CAPACITY]; /* the ticks' refreshInterval */
  uint32_t       refreshCount;
  double         refresh;     /* the display's period: the idle ticks' median refreshInterval */
  PacerSample    samples[SAMPLE_CAPACITY];
  GPUFrameFeedback *feedbacks[SAMPLE_CAPACITY];
  uint32_t       sampleCount;
  uint32_t       presents;    /* presents of the measured swapchain */
  bool           feedback;
  bool           failed;
  bool           draw;
  bool           record;
} PacerTest;

enum {
  PHASE_DRAW = 0,         /* draw each tick, pause at the eighth frame */
  PHASE_DESTROY_PACER,    /* destroy the pacer in its tick */
  PHASE_DESTROY_SWAPCHAIN,
  PHASE_PAUSE_UNDRAWN,    /* pause in a tick without drawing */
  PHASE_PAUSE_DRAWN,      /* draw, then pause */
  PHASE_REPLACE,          /* draw, destroy, make another, begin a manual frame */
  PHASE_IDLE,             /* ticks that draw nothing */
  PHASE_MEASURE           /* draw and record each tick */
};

static uintptr_t
thread_self(void) {
#if defined(_WIN32)
  return (uintptr_t)GetCurrentThreadId();
#else
  return (uintptr_t)pthread_self();
#endif
}

uintptr_t
pacerHostThread(void) {
  return thread_self();
}

static bool
begin_rejected(GPUSwapchain *swapchain) {
  GPUFrame *frame;

  if (!(frame = GPUBeginFrame(swapchain)))
    return true;

  GPUEndFrame(frame);
  return false;
}

#if defined(_WIN32)
static DWORD WINAPI
wrong_thread(void *user) {
  *(bool *)((void **)user)[1] = begin_rejected(((void **)user)[0]);
  return 0;
}
#else
static void*
wrong_thread(void *user) {
  *(bool *)((void **)user)[1] = begin_rejected(((void **)user)[0]);
  return NULL;
}
#endif

static bool
check_other_thread(GPUSwapchain *swapchain) {
  void *args[2];
  bool  rejected = false;

  args[0] = swapchain;
  args[1] = &rejected;
#if defined(_WIN32)
  {
    HANDLE thread = CreateThread(NULL, 0, wrong_thread, args, 0, NULL);
    if (!thread)
      return false;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
  }
#else
  {
    pthread_t thread;
    if (pthread_create(&thread, NULL, wrong_thread, args) != 0)
      return false;
    pthread_join(thread, NULL);
  }
#endif
  return rejected;
}

static void
on_feedback(const GPUFrameEvent *event, void *user) {
  PacerTest *state;
  uint64_t   i;

  state = user;
  i     = event->frameId;
  if (event->kind == GPU_FRAME_EVENT_PRESENTED && i < SAMPLE_CAPACITY)
    state->samples[i].presented = event->presentedTime;
}

static bool
finish_clear(PacerTest *state, GPUFrame *frame, float green, int64_t sample) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUFrameFeedbackCreateInfo   fbInfo = {0};
  GPUFrameFeedback            *feedback = NULL;
  GPUCommandBuffer            *command;
  GPURenderPassEncoder        *pass;
  GPUResult                    result;

  if (!frame)
    return false;

  if (!(color.view = GPUFrameGetTargetView(frame))) {
    GPUEndFrame(frame);
    return false;
  }

  if (sample >= 0)
    state->samples[sample].acquired = pacerHostNow();

  if (GPUAcquireCommandBuffer(state->queue, "pacer clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return false;
  }

  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[1] = green;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount   = 1u;
  if (!(pass = GPUBeginRenderPass(command, &info))) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return false;
  }

  GPUEndRenderPass(pass);
  if (sample >= 0 && state->feedback) {
    fbInfo.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_FEEDBACK_CREATE_INFO;
    fbInfo.chain.structSize = sizeof(fbInfo);
    fbInfo.callback         = on_feedback;
    fbInfo.user             = state;
    fbInfo.frameId          = (uint64_t)sample;
    if (GPUCreateFrameFeedback(frame, &fbInfo, &feedback) != GPU_OK)
      feedback = NULL;
  }

  result = GPUFinishFrame(state->queue, command, frame);
  if (feedback)
    state->feedbacks[sample] = feedback; /* kept until the frame is shown */

  return result == GPU_OK;
}

static bool
check_paused_clear(PacerTest *state, uint32_t width, uint32_t height) {
  GPUTextureInfo texture = {0};
  GPUFrame      *frame;
  GPUTexture    *target;
  bool           extent;

  if (!(frame = GPUBeginFrame(state->swapchain)))
    return false;

  target = GPUFrameGetTarget(frame);
  extent = target && GPUGetTextureInfo(target, &texture) == GPU_OK
           && texture.width == width && texture.height == height;
  if (!begin_rejected(state->swapchain)) {
    GPUEndFrame(frame);
    return false;
  }

  return finish_clear(state, frame, 0.5f, -1) && extent;
}

static bool
make_pacer(PacerTest *state, uint32_t latency, float rate);

static void
tick(GPUFrame *frame, const GPUFrameTiming *timing, void *user) {
  PacerTest *state;
  double     now;

  state = user;
  now   = pacerHostNow();
  state->ticks++;
  if (thread_self() != state->thread || !timing || !isfinite(timing->deadline)
      || !isfinite(timing->targetPresentTime) || timing->targetPresentTime <= 0.0
      || timing->refreshInterval < 0.0 || timing->deadline > timing->targetPresentTime) {
    fprintf(stderr, "pacer: bad tick (thread %d, deadline %.6f, target %.6f, interval %.6f)\n",
            thread_self() == state->thread, timing ? timing->deadline : 0.0,
            timing ? timing->targetPresentTime : 0.0, timing ? timing->refreshInterval : 0.0);
    state->failed = true;
  }

  if (state->lastTick > 0.0 && state->intervalCount < INTERVAL_CAPACITY)
    state->intervals[state->intervalCount++] = now - state->lastTick;
  state->lastTick = now;
  if (timing->refreshInterval > 0.0 && state->refreshCount < INTERVAL_CAPACITY)
    state->refreshes[state->refreshCount++] = timing->refreshInterval;

  if (!frame)
    return;

  state->frames++;
  if (!begin_rejected(state->swapchain)) {
    state->failed = true;
    return;
  }

  switch (state->phase) {
    case PHASE_IDLE:
      return; /* unconsumed: released, nothing presented */

    case PHASE_DESTROY_PACER:
      GPUDestroyFramePacer(state->pacer);
      state->pacer = NULL;
      return;

    case PHASE_DESTROY_SWAPCHAIN:
      GPUDestroySwapchain(state->swapchain);
      state->swapchain = NULL;
      state->pacer     = NULL;
      return;

    case PHASE_PAUSE_UNDRAWN:
      GPUSetFramePacerPaused(state->pacer, true);
      if (!begin_rejected(state->swapchain))
        state->failed = true;
      return;

    case PHASE_MEASURE:
      if (state->sampleCount < SAMPLE_CAPACITY) {
        PacerSample *s = &state->samples[state->sampleCount];

        s->tick     = now;
        s->deadline = timing->deadline;
        s->target   = timing->targetPresentTime;
        if (!finish_clear(state, frame, (float)(state->sampleCount & 7u) / 8.0f, (int64_t)state->sampleCount))
          state->failed = true;
        s->submitted = pacerHostNow();
        state->sampleCount++;
        state->presents++;
      } else {
        GPUSetFramePacerPaused(state->pacer, true);
      }
      return;

    default:
      break;
  }

  if (!finish_clear(state, frame, (float)(state->frames & 7u) / 8.0f, -1))
    state->failed = true;

  if (state->phase == PHASE_REPLACE) {
    GPUDestroyFramePacer(state->pacer);
    state->pacer = NULL;
    if (!make_pacer(state, 1u, 0.0f) || !(state->manual = GPUBeginFrame(state->swapchain)))
      state->failed = true;
    return;
  }

  if (state->frames == 8u || state->phase == PHASE_PAUSE_DRAWN) {
    GPUSetFramePacerPaused(state->pacer, true);
    if (!begin_rejected(state->swapchain))
      state->failed = true;
  }
}

static bool
make_pacer(PacerTest *state, uint32_t latency, float rate) {
  GPUFramePacerCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "pacer test";
  info.swapchain        = state->swapchain;
  info.callback         = tick;
  info.user             = state;
  info.latencyFrames    = latency;
  info.maxRate          = rate;
  info.preferredRate    = rate;
  return GPUCreateFramePacer(state->device, &info, &state->pacer) == GPU_OK;
}

static void
reset_counts(PacerTest *state, uint32_t phase) {
  state->phase         = phase;
  state->frames        = 0u;
  state->ticks         = 0u;
  state->lastTick      = 0.0;
  state->intervalCount = 0u;
  state->refreshCount  = 0u;
}

static int
compare_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}

static double
percentile(double *values, uint32_t count, double p) {
  if (!count)
    return 0.0;

  qsort(values, count, sizeof(*values), compare_double);
  return values[(uint32_t)((count - 1u) * p + 0.5)];
}

#if defined(PACER_TEST_VULKAN)
/* the display's own times (VK_GOOGLE_display_timing), in present order: what
 * came since the last call (drained as they come: implementations keep few) */
static uint32_t
display_times(PacerTest *state, double *times, uint32_t capacity) {
  PFN_vkGetPastPresentationTimingGOOGLE past;
  VkPastPresentationTimingGOOGLE        timings[SAMPLE_CAPACITY];
  GPUSwapchainVk                       *native;
  GPUDeviceVk                          *device;
  uint32_t                              count, i;

  native = state->swapchain ? state->swapchain->_priv : NULL;
  device = state->device ? state->device->_priv : NULL;
  if (!native || !device || !state->device->adapter->supportsDisplayTiming)
    return 0u;

  past = (PFN_vkGetPastPresentationTimingGOOGLE)vkGetDeviceProcAddr(device->device,
                                                                    "vkGetPastPresentationTimingGOOGLE");
  count = SAMPLE_CAPACITY;
  if (!past || past(device->device, native->swapchain, &count, timings) != VK_SUCCESS)
    return 0u;

  for (i = 0u; i < count && i < capacity; i++)
    times[i] = timings[i].actualPresentTime * 1e-9;

  return i;
}
#endif

static bool
check_cadence(PacerTest *state, const char *what, double seconds, double expectRate) {
  double   intervals[INTERVAL_CAPACITY], median, rate, refresh;
  uint32_t n;

  n = state->refreshCount;
  memcpy(intervals, state->refreshes, n * sizeof(*intervals));
  refresh = percentile(intervals, n, 0.5);
  n = state->intervalCount;
  memcpy(intervals, state->intervals, n * sizeof(*intervals));
  median = percentile(intervals, n, 0.5);
  rate   = state->ticks / seconds;
  printf("pacer: %s: %u ticks in %.2f s (%.1f/s), interval median %.2f ms p10 %.2f p90 %.2f, refresh interval %.2f ms\n",
         what, state->ticks, seconds, rate, median * 1e3, percentile(intervals, n, 0.1) * 1e3,
         percentile(intervals, n, 0.9) * 1e3, refresh * 1e3);
  /* ticks near the expected rate; a loaded machine may miss a few */
  return n > 4u && rate > expectRate * 0.6 && rate < expectRate * 1.15
         && fabs(median * expectRate - 1.0) < 0.2;
}

static bool
measure(PacerTest *state, GPUAdapter *adapter, uint32_t latency) {
  double      lat[SAMPLE_CAPACITY], err[SAMPLE_CAPACITY], cpu[SAMPLE_CAPACITY], budget[SAMPLE_CAPACITY];
  double      take[SAMPLE_CAPACITY];
  double      times[SAMPLE_CAPACITY], period;
  uint32_t    i, n, got, missed, wait, shown;
  const char *kind;

  (void)adapter;
  /* a fresh swapchain: the display's times then line up with these presents */
  GPUDestroySwapchain(state->swapchain);
  state->swapchain = GPUCreateSwapchainDefault(state->device, state->surface, state->width, state->height);
  state->pacer     = NULL;
  memset(state->samples, 0, sizeof(state->samples));
  state->sampleCount = 0u;
  state->presents    = 0u;
  if (!state->swapchain || !make_pacer(state, latency, 0.0f))
    return false;

  (void)pacerHostPresented(times, SAMPLE_CAPACITY); /* drop what came before */
  reset_counts(state, PHASE_MEASURE);
  GPUSetFramePacerPaused(state->pacer, false);
  shown = 0u;
  (void)shown;
  for (wait = 0u; wait < 400u && state->sampleCount < 120u; wait++) {
    pacerHostPump(0.01);
#if defined(PACER_TEST_VULKAN)
    shown += display_times(state, times + shown, SAMPLE_CAPACITY - shown);
#endif
  }

  GPUSetFramePacerPaused(state->pacer, true);
  pacerHostPump(0.15);
  GPUQueueWaitIdle(state->queue);
  pacerHostPump(0.1);

  kind = "display";
  got  = 0u;
  if (!state->feedback) {
    got  = pacerHostPresented(times, SAMPLE_CAPACITY);
    kind = pacerHostPresentKind();
#if defined(PACER_TEST_VULKAN)
    if (!got) {
      got  = shown + display_times(state, times + shown, SAMPLE_CAPACITY - shown);
      kind = "display (VK_GOOGLE_display_timing)";
    }
#endif
    if (got && got != state->presents) {
      printf("pacer: %u presents, %u times reported: not matched\n", state->presents, got);
      got = 0u;
    }

    for (i = 0u; i < got && i < state->sampleCount; i++)
      state->samples[i].presented = times[i] > 0.0 ? times[i] : 0.0;
  } else {
    kind = "display (frame feedback)";
    for (i = 0u; i < SAMPLE_CAPACITY; i++) {
      if (state->feedbacks[i])
        GPUDestroyFrameFeedback(state->feedbacks[i]);
      state->feedbacks[i] = NULL;
    }
  }

  n = missed = 0u;
  period = state->refresh > 0.0 ? state->refresh : 1.0 / 60.0;
  for (i = 0u; i < state->sampleCount; i++) {
    PacerSample *s = &state->samples[i];

    cpu[i]    = s->submitted - s->tick;
    take[i]   = s->acquired - s->tick;
    budget[i] = s->deadline - s->tick;
    if (s->presented > 0.0) {
      lat[n] = s->presented - s->tick;
      err[n] = s->presented - s->target;
      n++;
    }
    if (i > 0u && s->presented > 0.0 && state->samples[i - 1u].presented > 0.0
        && s->presented - state->samples[i - 1u].presented > period * 1.5)
      missed++;
  }

  printf("pacer: latency %u: %u frames, tick to deadline median %.2f ms, to target median %.2f ms p90 %.2f, "
         "to submitted median %.2f ms p90 %.2f\n",
         latency, state->sampleCount, percentile(budget, state->sampleCount, 0.5) * 1e3,
         percentile(take, state->sampleCount, 0.5) * 1e3, percentile(take, state->sampleCount, 0.9) * 1e3,
         percentile(cpu, state->sampleCount, 0.5) * 1e3, percentile(cpu, state->sampleCount, 0.9) * 1e3);
  if (n) {
    printf("pacer: latency %u: tick to %s median %.2f ms p10 %.2f p90 %.2f (%.2f refreshes); "
           "minus target median %.2f ms p10 %.2f p90 %.2f; %u gaps over 1.5 refreshes in %u\n",
           latency, kind, percentile(lat, n, 0.5) * 1e3, percentile(lat, n, 0.1) * 1e3,
           percentile(lat, n, 0.9) * 1e3, percentile(lat, n, 0.5) / period,
           percentile(err, n, 0.5) * 1e3, percentile(err, n, 0.1) * 1e3, percentile(err, n, 0.9) * 1e3,
           missed, n);
  } else {
    printf("pacer: latency %u: no presentation times on this platform\n", latency);
  }

  GPUDestroyFramePacer(state->pacer);
  state->pacer = NULL;
  return state->sampleCount >= 60u && !state->failed;
}

static bool
check_pacer(GPUInstance *instance, GPUAdapter *adapter, GPUDevice *device) {
  PacerTest state;
  uint32_t  ticks, frames;
  bool      ok, unconsumed;
  double    rate, start;
#if defined(PACER_TEST_VULKAN)
  uint32_t  frameIndex;
#endif

  memset(&state, 0, sizeof(state));
  state.device   = device;
  state.thread   = thread_self();
  state.width    = 96u;
  state.height   = 96u;
  state.feedback = GPUIsFeatureEnabled(device, GPU_FEATURE_FRAME_FEEDBACK);
  if (!pacerHostOpen(instance, adapter, 96u, 96u, &state.surface)) {
    printf("pacer: no window here; skip\n");
    return true;
  }

  state.swapchain = GPUCreateSwapchainDefault(device, state.surface, 96u, 96u);
  state.queue     = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  ok = state.swapchain && state.queue && make_pacer(&state, 1u, 0.0f);
  if (!ok) {
    fprintf(stderr, "pacer: setup failed\n");
    goto cleanup;
  }

  /* a swapchain destroyed with a frame's image acquired, then others: nothing
   * of that acquisition outlives it */
  ok = (state.manual = GPUBeginFrame(state.swapchain)) != NULL;
  GPUDestroySwapchain(state.swapchain);
  state.manual    = NULL;
  state.pacer     = NULL;
  state.swapchain = GPUCreateSwapchainDefault(device, state.surface, state.width, state.height);
  GPUDestroySwapchain(state.swapchain);
  state.swapchain = GPUCreateSwapchainDefault(device, state.surface, state.width, state.height);
  for (int i = 0; ok && i < 8; i++)
    ok = state.swapchain && check_paused_clear(&state, state.width, state.height);
  ok = ok && GPUQueueWaitIdle(state.queue) == GPU_OK && make_pacer(&state, 1u, 0.0f);
  printf("pacer: swapchain destroyed with a frame acquired, then others=%s\n", ok ? "pass" : "fail");
  if (!ok)
    goto cleanup;

  if (getenv("PACER_TEST_MEASURE_ONLY")) {
    ok = measure(&state, adapter, 1u) && measure(&state, adapter, 2u);
    goto cleanup;
  }

  /* made paused: no tick; manual frames on this thread only */
  pacerHostPump(0.1);
  ok = state.ticks == 0u && check_other_thread(state.swapchain) && check_paused_clear(&state, 96u, 96u);
  if (!ok)
    goto cleanup;

  if (pacerHostResize(128u, 80u)) {
    state.width  = 128u;
    state.height = 80u;
  }
  ok = GPUResizeSwapchain(state.swapchain, 128u, 80u) == GPU_OK
       && check_paused_clear(&state, state.width, state.height);
  if (!ok)
    goto cleanup;

  /* ticks that draw nothing: at the display's rate, nothing presented */
  reset_counts(&state, PHASE_IDLE);
#if defined(PACER_TEST_VULKAN)
  frameIndex = ((GPUSwapchainVk *)state.swapchain->_priv)->frameIndex;
#endif
  GPUSetFramePacerPaused(state.pacer, false);
  ok = begin_rejected(state.swapchain);
  start = pacerHostNow();
  pacerHostPump(1.0);
  {
    double refreshes[INTERVAL_CAPACITY];

    memcpy(refreshes, state.refreshes, state.refreshCount * sizeof(*refreshes));
    state.refresh = percentile(refreshes, state.refreshCount, 0.5);
  }
  rate = state.refresh > 0.0 ? 1.0 / state.refresh : 60.0;
  ok = ok && check_cadence(&state, "idle ticks", pacerHostNow() - start, rate) && !state.failed;
  unconsumed = true;
#if defined(PACER_TEST_VULKAN)
  unconsumed = ((GPUSwapchainVk *)state.swapchain->_priv)->frameIndex == frameIndex;
#endif
  GPUSetFramePacerPaused(state.pacer, true);
  printf("pacer: ticks that draw nothing present nothing=%s\n", unconsumed ? "pass" : "fail");
  ok = ok && unconsumed;
  if (!ok)
    goto cleanup;

  /* paused: no tick */
  ticks = state.ticks;
  pacerHostPump(0.2);
  ok = state.ticks == ticks && check_paused_clear(&state, state.width, state.height);
  printf("pacer: pause stops ticks=%s\n", ok ? "pass" : "fail");
  if (!ok)
    goto cleanup;

  /* eight drawn, paused in the eighth */
  reset_counts(&state, PHASE_DRAW);
  GPUSetFramePacerPaused(state.pacer, false);
  for (int i = 0; i < 100 && state.frames < 8u; i++)
    pacerHostPump(0.02);
  frames = state.frames;
  pacerHostPump(0.1);
  ok = frames == 8u && state.frames == frames && !state.failed && check_paused_clear(&state, state.width, state.height);
  if (!ok)
    goto cleanup;

  reset_counts(&state, PHASE_PAUSE_UNDRAWN);
  GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.2);
  ok = state.frames == 1u && !state.failed && check_paused_clear(&state, state.width, state.height);
  if (!ok)
    goto cleanup;

  reset_counts(&state, PHASE_PAUSE_DRAWN);
  GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.2);
  ok = state.frames == 1u && !state.failed && check_paused_clear(&state, state.width, state.height);
  if (!ok)
    goto cleanup;

  /* a manual frame: resuming waits for it; the pacer's destruction keeps it */
  if (!(state.manual = GPUBeginFrame(state.swapchain))) {
    ok = false;
    goto cleanup;
  }

  ticks = state.ticks;
  GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.1);
  ok = state.ticks == ticks && !state.failed && begin_rejected(state.swapchain);
  if (!ok)
    goto cleanup;

  GPUDestroyFramePacer(state.pacer);
  state.pacer = NULL;
  ok = finish_clear(&state, state.manual, 0.75f, -1);
  state.manual = NULL;
  if (!ok)
    goto cleanup;

  /* replaced in a tick, a manual frame begun there */
  reset_counts(&state, PHASE_REPLACE);
  ok = make_pacer(&state, 1u, 0.0f);
  if (ok)
    GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.2);
  ok = ok && state.frames == 1u && state.pacer && state.manual && !state.failed;
  if (!ok)
    goto cleanup;

  ok = finish_clear(&state, state.manual, 0.25f, -1);
  state.manual = NULL;
  GPUDestroyFramePacer(state.pacer);
  state.pacer = NULL;
  if (!ok)
    goto cleanup;

  /* destroyed in its tick: no tick after */
  reset_counts(&state, PHASE_DESTROY_PACER);
  ok = make_pacer(&state, 1u, 0.0f);
  if (ok)
    GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.15);
  ticks = state.ticks;
  pacerHostPump(0.15);
  ok = ok && state.frames == 1u && state.ticks == ticks && !state.pacer && !state.failed;
  printf("pacer: destroyed in a tick, none after=%s\n", ok ? "pass" : "fail");
  if (!ok)
    goto cleanup;

  /* destroyed while ticking, outside a tick: none after */
  reset_counts(&state, PHASE_IDLE);
  ok = make_pacer(&state, 1u, 0.0f);
  if (ok)
    GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.1);
  GPUDestroyFramePacer(state.pacer);
  state.pacer = NULL;
  ticks = state.ticks;
  pacerHostPump(0.15);
  ok = ok && ticks > 0u && state.ticks == ticks && !state.failed;
  printf("pacer: destroyed while running, none after=%s\n", ok ? "pass" : "fail");
  if (!ok)
    goto cleanup;

  /* half the display's rate */
  if (rate > 50.0) {
    reset_counts(&state, PHASE_IDLE);
    ok = make_pacer(&state, 1u, (float)(rate * 0.5));
    if (ok)
      GPUSetFramePacerPaused(state.pacer, false);
    start = pacerHostNow();
    pacerHostPump(1.0);
    ok = ok && check_cadence(&state, "half rate", pacerHostNow() - start, rate * 0.5) && !state.failed;
    GPUDestroyFramePacer(state.pacer);
    state.pacer = NULL;
    if (!ok)
      goto cleanup;
  }

  /* the swapchain destroyed in a tick */
  reset_counts(&state, PHASE_DESTROY_SWAPCHAIN);
  ok = make_pacer(&state, 1u, 0.0f);
  if (ok)
    GPUSetFramePacerPaused(state.pacer, false);
  pacerHostPump(0.2);
  ok = ok && state.frames == 1u && !state.swapchain && !state.pacer && !state.failed;
  if (!ok)
    goto cleanup;

  /* a swapchain destroyed with a manual frame and a paused pacer */
  state.swapchain = GPUCreateSwapchainDefault(device, state.surface, state.width, state.height);
  ok = state.swapchain && make_pacer(&state, 1u, 0.0f)
       && (state.manual = GPUBeginFrame(state.swapchain));
  GPUDestroySwapchain(state.swapchain);
  state.swapchain = NULL;
  state.pacer     = NULL;
  state.manual    = NULL;
  if (!ok)
    goto cleanup;

  printf("pacer: paused draws/resize, owner thread, callback rejection, unconsumed cleanup, resume, "
         "manual ownership, destruction=pass\n");

  /* tick to display, latency one and two */
  state.swapchain = GPUCreateSwapchainDefault(device, state.surface, state.width, state.height);
  ok = state.swapchain && measure(&state, adapter, 1u) && measure(&state, adapter, 2u);

cleanup:
  if (!ok)
    fprintf(stderr, "pacer failed: phase=%u frames=%u ticks=%u failed=%d\n",
            state.phase, state.frames, state.ticks, state.failed);
  GPUEndFrame(state.manual);
  GPUDestroyFramePacer(state.pacer);
  GPUDestroySwapchain(state.swapchain);
  GPUDestroySurface(state.surface);
  pacerHostClose();
  printf("pacer=%s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  GPUDeviceCreateInfo info = {0};
  GPUFeature          features[2];
  GPUInstance        *instance = NULL;
  GPUAdapter         *adapter;
  GPUDevice          *device = NULL;
  uint32_t            count;
  int                 status;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK) {
    fprintf(stderr, "pacer: no instance\n");
    return 1;
  }

  if (gpu_test_request_adapter(instance, &adapter) != GPU_OK || !adapter) {
    fprintf(stderr, "pacer: no adapter\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_PACER)) {
    printf("pacer: unsupported here; skip\n");
    GPUDestroyInstance(instance);
    return 77;
  }

  count = 0u;
  features[count++] = GPU_FEATURE_FRAME_PACER;
  if (GPUIsFeatureSupported(adapter, GPU_FEATURE_FRAME_FEEDBACK))
    features[count++] = GPU_FEATURE_FRAME_FEEDBACK;

  info.required.pFeatures    = features;
  info.required.featureCount = count;
  if (gpu_test_create_device(adapter, &info, &device) != GPU_OK || !device) {
    fprintf(stderr, "pacer: no device\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  status = check_pacer(instance, adapter, device) ? 0 : 1;
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return status;
}
