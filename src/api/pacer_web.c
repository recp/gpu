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
#include "swapchain_internal.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <pthread.h>

/* browsers: requestAnimationFrame on the creating thread (the page's, or a
 * worker's with an OffscreenCanvas). The callback's time is the frame's start
 * on performance.now()'s clock; the canvas's texture is presented when the
 * callback returns, which is why a paced frame takes it only when drawn. A
 * loop can't be cancelled from outside: a stopped or destroyed clock's next
 * callback ends it (and frees a destroyed one). */

struct GPUPacerClock {
  GPUPacer *pacer;
  double    last;
  double    period;
  bool      running;
  bool      looping;
  bool      dead;
};

static EM_BOOL
pacer__frame(double time, void *user) {
  GPUPacerClock *clock;
  GPUPacerVsync  vsync = {0};
  double         at, delta;

  clock = user;
  if (clock->dead || !clock->running) {
    clock->looping = false;
    if (clock->dead)
      free(clock);
    return EM_FALSE;
  }

  at    = time * 1e-3;
  delta = clock->last > 0.0 ? at - clock->last : 0.0;
  if (delta > 0.002 && delta < 0.1 && (clock->period <= 0.0 || delta < clock->period * 1.5))
    clock->period = clock->period > 0.0 ? (clock->period * 3.0 + delta) * 0.25 : delta;

  clock->last  = at;
  vsync.vsync  = at;
  vsync.period = clock->period;
  framePacerVsync(clock->pacer, &vsync);

  /* destroyed in the tick: the loop's next call frees it */
  return EM_TRUE;
}

GPU_HIDE
bool
pacerClockSupported(void) {
  return true;
}

GPU_HIDE
uintptr_t
pacerThreadSelf(void) {
  return (uintptr_t)pthread_self();
}

GPU_HIDE
double
pacerHostSeconds(void) {
  return emscripten_get_now() * 1e-3;
}

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  GPUPacerClock *clock;

  (void)swapchain;
  if (!(clock = calloc(1u, sizeof(*clock))))
    return GPU_ERROR_OUT_OF_MEMORY;

  clock->pacer = pacer;
  *outClock    = clock;
  return GPU_OK;
}

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run) {
  if (!clock || clock->dead)
    return;

  clock->running = run;
  if (!run)
    clock->last = 0.0;

  if (run && !clock->looping) {
    clock->looping = true;
    emscripten_request_animation_frame_loop(pacer__frame, clock);
  }
}

/* the display's rate isn't asked here: the pacer thins the refreshes */
GPU_HIDE
void
pacerClockRate(GPUPacerClock *clock, const GPUPacer *pacer) {
  (void)clock;
  (void)pacer;
}

GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock) {
  if (!clock)
    return;

  clock->dead    = true;
  clock->running = false;
  if (!clock->looping)
    free(clock);
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

#endif /* __EMSCRIPTEN__ */
