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

#ifndef gpu_pacer_internal_h
#define gpu_pacer_internal_h

#include "../common.h"
#include "frame_internal.h"

/* frame pacing for backends without a display link of their own (Metal has
 * CAMetalDisplayLink): the platform's clock reports each display refresh and
 * the pacer turns it into the app's tick, the GPUFrameTiming and a frame whose
 * image is acquired at its first use, so a tick that draws nothing presents
 * nothing. One clock per platform, shared by Vulkan, DX12 and WebGPU:
 *   Android    AChoreographer on the creating thread's ALooper
 *   Apple      CADisplayLink on the creating thread's run loop
 *   Windows    the compositor clock or the output's vblank, waited on a thread
 *              of its own and delivered through the creating thread's messages
 *   browsers   requestAnimationFrame
 *   elsewhere  a thread of its own, ticks delivered there (no per-thread event
 *              loop to post to)
 */

typedef struct GPUPacerClock GPUPacerClock;
typedef struct GPUPacer      GPUPacer;

/* one display refresh as a clock reports it, host-clock seconds (the clock of
 * GPUFrameTiming: mach absolute time on Apple, CLOCK_MONOTONIC on Android and
 * Linux, the performance counter on Windows, performance.now() in browsers) */
typedef struct GPUPacerVsync {
  double vsync;    /* the refresh this tick starts at */
  double period;   /* the display's refresh period; zero when not known yet */
  double present;  /* when a frame begun now reaches the display; zero: estimated */
  double deadline; /* when its work must be submitted to make that; zero: estimated */
} GPUPacerVsync;

struct GPUPacer {
  GPUFramePacer *pacer;         /* NULL once destroyed: a tick in progress ends alone */
  GPUPacerClock *clock;
  GPUFrame      *callbackFrame;
  uintptr_t      owner;         /* the thread pacer calls come from (the creating thread) */
  double         previousPresent;
  double         previousPeriod; /* the last tick's: a change starts the intervals again */
  double         nextTick;      /* a rate below the display's: the next refresh that ticks */
  float          minRate;
  float          maxRate;
  float          preferredRate;
  uint32_t       latencyFrames; /* 1 or 2 */
  uint32_t       refs;          /* the pacer's and a tick's in progress */
  bool           paused;
  bool           inCallback;
  bool           anyThread;     /* the clock ticks on a thread of its own: calls come from any thread */
};

/* platform clocks (pacer_<platform>.c) */

GPU_HIDE
bool
pacerClockSupported(void);

/* paused; run starts reporting refreshes to framePacerVsync on the thread
 * pacer->owner names (a clock ticking on its own thread sets anyThread and
 * owner) */
GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock);

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run);

/* also from inside a tick: no refresh is reported after it returns */
GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock);

/* the pacer's rates changed: a clock that asks the display for them asks
 * again (the others' refreshes are thinned by the pacer itself) */
GPU_HIDE
void
pacerClockRate(GPUPacerClock *clock, const GPUPacer *pacer);

/* a clock that ticks on a thread of its own serializes the pacer's calls */
GPU_HIDE
void
pacerClockLock(GPUPacerClock *clock);

GPU_HIDE
void
pacerClockUnlock(GPUPacerClock *clock);

GPU_HIDE
uintptr_t
pacerThreadSelf(void);

/* now, on the clock GPUPacerVsync's times are in */
GPU_HIDE
double
pacerHostSeconds(void);

/* from a clock: one refresh */
GPU_HIDE
void
framePacerVsync(GPUPacer *pacer, const GPUPacerVsync *vsync);

/* createPacer, pausePacer, ratePacer, destroyPacer and prepareWithPacer on the platform's
 * clock; the backend brings beginDrawable (no wait) and acquireDrawable */
GPU_HIDE
void
framePacerUseClock(GPUFrameApi *api);

GPU_HIDE
bool
framePacerUsesClock(const GPUFrameApi *api);

#endif /* gpu_pacer_internal_h */
