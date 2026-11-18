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
#include "device_internal.h"
#include "frame_internal.h"
#include "pacer_internal.h"
#include "swapchain_internal.h"
#include <math.h>

/* the refresh period assumed until the clock knows the display's */
#define GPU_PACER_DEFAULT_PERIOD (1.0 / 60.0)

static bool
pacerOnOwner(const GPUPacer *p) {
  return p->anyThread || p->owner == pacerThreadSelf();
}

static void
pacerRelease(GPUPacer *p) {
  if (--p->refs == 0u)
    free(p);
}

/* a rate under the display's ticks every n-th refresh: the n whose rate is
 * nearest the preferred (else the maximum) and stays within min..max, give or
 * take a measured period's few percent */
static double
pacerTickInterval(const GPUPacer *p, double period) {
  double   rate, target;
  uint32_t n;

  if (p->maxRate <= 0.0f || period <= 0.0)
    return 0.0;

  rate   = 1.0 / period;
  target = p->preferredRate > 0.0f ? p->preferredRate : p->maxRate;
  n      = (uint32_t)floor(rate / target + 0.5);
  if (n < 1u)
    n = 1u;

  while (n > 1u && rate / n < p->minRate * 0.95)
    n--;

  while (n < 8u && rate / n > p->maxRate * 1.05)
    n++;

  return n > 1u ? n * period : 0.0;
}

GPU_HIDE
void
framePacerVsync(GPUPacer *p, const GPUPacerVsync *v) {
  GPUFrameTiming  timing;
  GPUFramePacerFn callback;
  GPUFrame       *frame;
  void           *user, *drawable;
  double          period, interval;

  if (!p->pacer || p->paused || p->inCallback)
    return;

  period   = v->period > 0.0 ? v->period : GPU_PACER_DEFAULT_PERIOD;
  interval = pacerTickInterval(p, v->period);
  if (interval > 0.0) {
    if (p->nextTick > 0.0 && v->vsync < p->nextTick - 0.5 * period)
      return; /* a refresh between the rate's ticks */

    p->nextTick = v->vsync + interval;
  }

  /* estimates follow the usual compositor pipeline: a frame submitted before
   * the next refresh is composed then and shown one refresh later */
  timing.deadline          = v->deadline > 0.0 ? v->deadline : v->vsync + period * p->latencyFrames;
  timing.targetPresentTime = v->present > 0.0 ? v->present : timing.deadline + period;
  timing.refreshInterval   = p->previousPresent > 0.0
                               ? timing.targetPresentTime - p->previousPresent : 0.0;
  p->previousPresent       = timing.targetPresentTime;

  p->inCallback = true;
  p->refs++;
  callback = p->pacer->callback;
  user     = p->pacer->user;
  frame    = frameBeginDrawable(p->pacer->swapchain, p);
  drawable = frame ? frame->drawable : NULL;
  if (frame) {
    frame->targetPresentTime = timing.targetPresentTime;
    frame->deadline          = timing.deadline;
  }

  p->callbackFrame = frame;
  callback(frame, &timing, user);

  /* unconsumed: released; destruction in the callback ended it already */
  if (p->pacer && frame && p->callbackFrame == frame && drawable && frame->drawable == drawable)
    GPUEndFrame(frame);

  p->callbackFrame = NULL;
  p->inCallback    = false;
  pacerRelease(p);
}

static GPUResult
pacerCreate(GPUFramePacer *fp, const GPUFramePacerCreateInfo *info) {
  GPUPacer *p;
  GPUResult result;

  if (fp->swapchain->activeFrame)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!(p = calloc(1u, sizeof(*p))))
    return GPU_ERROR_OUT_OF_MEMORY;

  p->pacer         = fp;
  p->owner         = pacerThreadSelf();
  p->paused        = true;
  p->refs          = 1u;
  p->latencyFrames = info->latencyFrames > 1u ? 2u : 1u;
  p->minRate       = info->minRate;
  p->maxRate       = info->maxRate;
  p->preferredRate = info->preferredRate;

  if ((result = pacerClockCreate(p, fp->swapchain, &p->clock)) != GPU_OK) {
    free(p);
    return result;
  }

  fp->_priv = p;
  return GPU_OK;
}

static void
pacerPause(GPUFramePacer *fp, bool paused) {
  GPUPacerClock *clock;
  GPUFrame      *active;
  GPUPacer      *p;

  if (!(p = fp->_priv) || !pacerOnOwner(p))
    return;

  clock = p->clock;
  if (p->anyThread)
    pacerClockLock(clock);

  active = fp->swapchain->activeFrame;

  /* a manual frame is finished before ticks resume */
  if (paused || !active || active == p->callbackFrame) {
    p->paused          = paused;
    p->previousPresent = 0.0;
    p->nextTick        = 0.0;
    pacerClockRun(clock, !paused);
  }

  if (p->anyThread)
    pacerClockUnlock(clock);
}

static void
pacerDestroy(GPUFramePacer *fp) {
  GPUPacerClock *clock;
  GPUFrame      *frame;
  GPUPacer      *p;
  bool           locked;

  if (!(p = fp->_priv))
    return;

  clock  = p->clock;
  locked = p->anyThread;
  if (locked)
    pacerClockLock(clock);

  pacerClockRun(clock, false); /* a clock on its own thread ticks no more */
  frame            = p->callbackFrame;
  p->pacer         = NULL;
  p->callbackFrame = NULL;
  p->clock         = NULL;
  fp->_priv        = NULL;

  /* the callback's frame goes with the pacer; a manual frame stays */
  if (frame && frame->drawable)
    GPUEndFrame(frame);

  pacerRelease(p);
  if (locked)
    pacerClockUnlock(clock);

  pacerClockDestroy(clock);
}

static bool
pacerPrepare(GPUFramePacer *fp) {
  GPUPacer *p;
  bool      ready;

  /* a paused pacer owns no image: a manual frame acquires as without one */
  if (!(p = fp->_priv) || !pacerOnOwner(p))
    return false;

  if (p->anyThread)
    pacerClockLock(p->clock);

  ready = p->paused && !p->inCallback && !fp->swapchain->activeFrame;
  if (p->anyThread)
    pacerClockUnlock(p->clock);

  return ready;
}

#if defined(GPU_PACER_NO_CLOCK)
/* Metal paces with its own display link: no clock built */
GPU_HIDE bool      pacerClockSupported(void) { return false; }
GPU_HIDE uintptr_t pacerThreadSelf(void) { return 0u; }
GPU_HIDE double    pacerHostSeconds(void) { return 0.0; }
GPU_HIDE void      pacerClockRun(GPUPacerClock *clock, bool run) { (void)clock; (void)run; }
GPU_HIDE void      pacerClockDestroy(GPUPacerClock *clock) { (void)clock; }
GPU_HIDE void      pacerClockLock(GPUPacerClock *clock) { (void)clock; }
GPU_HIDE void      pacerClockUnlock(GPUPacerClock *clock) { (void)clock; }

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  (void)pacer;
  (void)swapchain;
  *outClock = NULL;
  return GPU_ERROR_UNSUPPORTED;
}
#endif

GPU_HIDE
void
framePacerUseClock(GPUFrameApi *api) {
  api->createPacer      = pacerCreate;
  api->pausePacer       = pacerPause;
  api->destroyPacer     = pacerDestroy;
  api->prepareWithPacer = pacerPrepare;
}

GPU_HIDE
bool
framePacerUsesClock(const GPUFrameApi *api) {
  return api && api->createPacer == pacerCreate;
}
