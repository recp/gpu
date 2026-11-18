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

#include "../common.h"
#include "pacer_internal.h"
#include "surface_internal.h"
#include "swapchain_internal.h"

#if defined(__ANDROID__)
#include <android/looper.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* AChoreographer on the creating thread's ALooper: each refresh's callback
 * reposts itself, then ticks. API 33's vsync callback brings the frame
 * timelines (deadline and expected present per latency); before it the frame
 * time and the refresh period (API 30's refresh-rate callback, else measured)
 * give estimates. Every entry point is looked up at run time, so one build runs
 * from API 24 (no 64-bit frame callback before 29: unsupported there). */

typedef struct AChoreographer                  AChoreographer;
typedef struct AChoreographerFrameCallbackData AChoreographerFrameCallbackData;

typedef void (*PacerFrame64Fn)(int64_t frameTimeNanos, void *data);
typedef void (*PacerVsyncFn)(const AChoreographerFrameCallbackData *data, void *user);
typedef void (*PacerRefreshFn)(int64_t vsyncPeriodNanos, void *data);

static struct {
  AChoreographer *(*getInstance)(void);
  void    (*postFrame64)(AChoreographer *, PacerFrame64Fn, void *);
  void    (*postVsync)(AChoreographer *, PacerVsyncFn, void *);
  int64_t (*frameTime)(const AChoreographerFrameCallbackData *);
  size_t  (*timelineCount)(const AChoreographerFrameCallbackData *);
  size_t  (*preferredTimeline)(const AChoreographerFrameCallbackData *);
  int64_t (*expectedPresent)(const AChoreographerFrameCallbackData *, size_t);
  int64_t (*deadline)(const AChoreographerFrameCallbackData *, size_t);
  void    (*registerRefresh)(AChoreographer *, PacerRefreshFn, void *);
  void    (*unregisterRefresh)(AChoreographer *, PacerRefreshFn, void *);
  int32_t (*setFrameRate)(ANativeWindow *, float, int8_t);
  bool     loaded;
} pacer__android;

static pthread_once_t pacer__androidOnce = PTHREAD_ONCE_INIT;

static void
pacer__androidLoad(void) {
  void *lib;

  if (!(lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL)))
    return;

#define PACER_SYM(field, name) *(void **)&pacer__android.field = dlsym(lib, name)
  PACER_SYM(getInstance,       "AChoreographer_getInstance");
  PACER_SYM(postFrame64,       "AChoreographer_postFrameCallback64");
  PACER_SYM(postVsync,         "AChoreographer_postVsyncCallback");
  PACER_SYM(frameTime,         "AChoreographerFrameCallbackData_getFrameTimeNanos");
  PACER_SYM(timelineCount,     "AChoreographerFrameCallbackData_getFrameTimelinesLength");
  PACER_SYM(preferredTimeline, "AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex");
  PACER_SYM(expectedPresent,   "AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos");
  PACER_SYM(deadline,          "AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos");
  PACER_SYM(registerRefresh,   "AChoreographer_registerRefreshRateCallback");
  PACER_SYM(unregisterRefresh, "AChoreographer_unregisterRefreshRateCallback");
  PACER_SYM(setFrameRate,      "ANativeWindow_setFrameRate");
#undef PACER_SYM

  if (!pacer__android.frameTime || !pacer__android.timelineCount || !pacer__android.preferredTimeline
      || !pacer__android.expectedPresent || !pacer__android.deadline)
    pacer__android.postVsync = NULL;

  if (!pacer__android.unregisterRefresh)
    pacer__android.registerRefresh = NULL;

  pacer__android.loaded = pacer__android.getInstance
                          && (pacer__android.postFrame64 || pacer__android.postVsync);
}

struct GPUPacerClock {
  GPUPacer       *pacer;
  AChoreographer *choreographer;
  ANativeWindow  *window;      /* held for its frame rate */
  int64_t         periodNs;    /* the refresh-rate callback's */
  int64_t         deltas[8];   /* recent frame-time steps, without it */
  int64_t         lastFrameNs;
  uint32_t        deltaCount;
  atomic_uint     refs;        /* the pacer's and the posted callback's */
  bool            run;
  bool            posted;
  bool            dead;
  bool            rateSet;
  bool            refreshRegistered;
};

static void pacer__onFrame(int64_t frameTimeNanos, void *user);
static void pacer__onVsync(const AChoreographerFrameCallbackData *data, void *user);

static void
pacer__release(GPUPacerClock *clock) {
  if (atomic_fetch_sub(&clock->refs, 1u) == 1u)
    free(clock);
}

static void
pacer__post(GPUPacerClock *clock) {
  if (clock->posted)
    return;

  clock->posted = true;
  atomic_fetch_add(&clock->refs, 1u);
  if (pacer__android.postVsync)
    pacer__android.postVsync(clock->choreographer, pacer__onVsync, clock);
  else
    pacer__android.postFrame64(clock->choreographer, pacer__onFrame, clock);
}

/* a posted callback ran: false when there's nothing to tick. The next refresh
 * is asked for before this one ticks, so a long tick misses none */
static bool
pacer__fired(GPUPacerClock *clock) {
  bool live;

  clock->posted = false;
  live          = !clock->dead && clock->run;
  if (live)
    pacer__post(clock);

  pacer__release(clock);
  return live;
}

/* the refresh period: the refresh-rate callback's (API 30), else the median
 * of the last steps between frame times, which jitter by a few hundred
 * microseconds; a missed refresh's double step sits at the top, outside it */
static double
pacer__period(GPUPacerClock *clock, int64_t frameNs) {
  int64_t  sorted[8], delta, value;
  uint32_t count, i, j;

  delta = clock->lastFrameNs ? frameNs - clock->lastFrameNs : 0;
  clock->lastFrameNs = frameNs;
  if (delta > 2000000 && delta < 100000000)
    clock->deltas[clock->deltaCount++ & 7u] = delta;

  if (clock->periodNs > 0)
    return clock->periodNs * 1e-9;

  count = clock->deltaCount < 8u ? clock->deltaCount : 8u;
  for (i = 0u; i < count; i++) {
    value = clock->deltas[i];
    for (j = i; j > 0u && sorted[j - 1u] > value; j--)
      sorted[j] = sorted[j - 1u];
    sorted[j] = value;
  }

  return count ? sorted[(count - 1u) / 2u] * 1e-9 : 0.0;
}

static void
pacer__onFrame(int64_t frameTimeNanos, void *user) {
  GPUPacerClock *clock;
  GPUPacerVsync  vsync = {0};

  clock = user;
  if (!pacer__fired(clock))
    return;

  vsync.vsync  = frameTimeNanos * 1e-9;
  vsync.period = pacer__period(clock, frameTimeNanos);
  framePacerVsync(clock->pacer, &vsync);
}

static void
pacer__onVsync(const AChoreographerFrameCallbackData *data, void *user) {
  GPUPacerClock *clock;
  GPUPacerVsync  vsync = {0};
  size_t         count, i;
  int64_t        frameNs;

  clock = user;
  if (!pacer__fired(clock))
    return;

  frameNs      = pacer__android.frameTime(data);
  vsync.vsync  = frameNs * 1e-9;
  vsync.period = pacer__period(clock, frameNs);
  count        = pacer__android.timelineCount(data);
  i            = pacer__android.preferredTimeline(data);

  /* latency two: the timeline after the preferred one, a refresh more to draw */
  if (count > 0u) {
    if (i >= count)
      i = count - 1u;

    if (clock->pacer->latencyFrames > 1u && i + 1u < count)
      i++;

    vsync.deadline = pacer__android.deadline(data, i) * 1e-9;
    vsync.present  = pacer__android.expectedPresent(data, i) * 1e-9;
    if (vsync.present <= vsync.deadline || vsync.deadline <= 0.0)
      vsync.present = vsync.deadline = 0.0;
  }

  framePacerVsync(clock->pacer, &vsync);
}

static void
pacer__onRefresh(int64_t vsyncPeriodNanos, void *user) {
  GPUPacerClock *clock;

  clock = user;
  if (!clock->dead && vsyncPeriodNanos > 0)
    clock->periodNs = vsyncPeriodNanos;
}

GPU_HIDE
bool
pacerClockSupported(void) {
  pthread_once(&pacer__androidOnce, pacer__androidLoad);
  return pacer__android.loaded;
}

GPU_HIDE
uintptr_t
pacerThreadSelf(void) {
  return (uintptr_t)pthread_self();
}

GPU_HIDE
double
pacerHostSeconds(void) {
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  GPUPacerClock  *clock;
  AChoreographer *choreographer;
  float           rate;

  *outClock = NULL;
  if (!pacerClockSupported())
    return GPU_ERROR_UNSUPPORTED;

  /* ticks come through this thread's looper, made here if it has none: the
   * thread polls it (the main thread's Java looper does) */
  if (!ALooper_forThread() && !ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS))
    return GPU_ERROR_UNSUPPORTED;

  if (!(choreographer = pacer__android.getInstance()))
    return GPU_ERROR_UNSUPPORTED;

  if (!(clock = calloc(1u, sizeof(*clock))))
    return GPU_ERROR_OUT_OF_MEMORY;

  clock->pacer         = pacer;
  clock->choreographer = choreographer;
  atomic_init(&clock->refs, 1u);

  if (pacer__android.registerRefresh) {
    pacer__android.registerRefresh(choreographer, pacer__onRefresh, clock);
    clock->refreshRegistered = true;
  }

  /* a rate asked of the display for this window (API 30); ticks keep to it too */
  if (pacer->maxRate > 0.0f && pacer__android.setFrameRate && swapchain->surface
      && swapchain->surface->type == GPU_SURFACE_ANDROID_NATIVE_WINDOW
      && (clock->window = swapchain->surface->nativeHandle)) {
    ANativeWindow_acquire(clock->window);
    rate = pacer->preferredRate > 0.0f ? pacer->preferredRate : pacer->maxRate;
    clock->rateSet = pacer__android.setFrameRate(clock->window, rate, 0) == 0;
  }

  *outClock = clock;
  return GPU_OK;
}

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run) {
  if (!clock || clock->dead)
    return;

  clock->run = run;
  if (run)
    pacer__post(clock);
}

GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock) {
  if (!clock)
    return;

  /* a posted callback can't be taken back: it finds the clock dead and frees it */
  clock->dead = true;
  clock->run  = false;
  if (clock->refreshRegistered)
    pacer__android.unregisterRefresh(clock->choreographer, pacer__onRefresh, clock);

  if (clock->window) {
    if (clock->rateSet)
      (void)pacer__android.setFrameRate(clock->window, 0.0f, 0);

    ANativeWindow_release(clock->window);
    clock->window = NULL;
  }

  pacer__release(clock);
}

GPU_HIDE
void
pacerClockLock(GPUPacerClock *clock) {
  (void)clock;
}

GPU_HIDE
void
pacerClockUnlock(GPUPacerClock *clock) {
  (void)clock;
}

#endif /* __ANDROID__ */
