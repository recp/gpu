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

#if !defined(_WIN32) && !defined(WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) \
    && !defined(__EMSCRIPTEN__)
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Linux and other Unix: no per-thread event loop to post a refresh to, so the
 * pacer ticks on a thread of its own and its calls may come from any thread
 * (serialized here; a tick holds the lock, so pausing or destroying from
 * another thread waits for a tick in progress). The refresh is the display
 * controller's vblank (DRM_IOCTL_WAIT_VBLANK on the primary node, open to any
 * client that can open it; timestamps on CLOCK_MONOTONIC), else a timer at
 * the last known period. */

struct pacer__vblankRequest {
  unsigned int  type;
  unsigned int  sequence;
  unsigned long signal;
};

struct pacer__vblankReply {
  unsigned int type;
  unsigned int sequence;
  long         tvalSec;
  long         tvalUsec;
};

union pacer__vblank {
  struct pacer__vblankRequest request;
  struct pacer__vblankReply   reply;
};

#define PACER_DRM_WAIT_VBLANK _IOWR('d', 0x3a, union pacer__vblank)
#define PACER_DRM_VBLANK_RELATIVE 0x1u

struct GPUPacerClock {
  GPUPacer       *pacer;
  pthread_t       thread;
  pthread_mutex_t lock;     /* recursive: a tick may pause or destroy */
  pthread_cond_t  changed;
  double          period;
  double          last;
  int             drm;
  bool            running;
  bool            dead;
  bool            detached; /* destroyed from its own tick: the thread frees it */
};

static double
pacer__now(void) {
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

/* the next refresh's time, waited for (last and period read under the lock) */
static double
pacer__wait(GPUPacerClock *clock, double last, double period) {
  union pacer__vblank vblank;
  struct timespec     until;
  double              next, now, at;

  if (clock->drm >= 0) {
    memset(&vblank, 0, sizeof(vblank));
    vblank.request.type     = PACER_DRM_VBLANK_RELATIVE;
    vblank.request.sequence = 1u;
    if (ioctl(clock->drm, PACER_DRM_WAIT_VBLANK, &vblank) == 0) {
      at = (double)vblank.reply.tvalSec + vblank.reply.tvalUsec * 1e-6;
      return at > 0.0 ? at : pacer__now();
    }

    if (errno != EINTR) {
      close(clock->drm);
      clock->drm = -1;
    }
  }

  now  = pacer__now();
  next = last > 0.0 ? last + period : now;
  while (next < now)
    next += period;

  until.tv_sec  = (time_t)next;
  until.tv_nsec = (long)((next - (double)until.tv_sec) * 1e9);
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, NULL) == EINTR) {
  }

  return next;
}

static void*
pacer__run(void *user) {
  GPUPacerClock *clock;
  GPUPacerVsync  vsync = {0};
  double         at, last, period;
  bool           detached;

  clock = user;
  for (;;) {
    pthread_mutex_lock(&clock->lock);
    while (!clock->running && !clock->dead)
      pthread_cond_wait(&clock->changed, &clock->lock);

    if (clock->dead) {
      pthread_mutex_unlock(&clock->lock);
      break;
    }

    last   = clock->last;
    period = clock->period;
    pthread_mutex_unlock(&clock->lock);
    at = pacer__wait(clock, last, period);

    pthread_mutex_lock(&clock->lock);
    if (clock->last > 0.0 && at - clock->last > 0.002 && at - clock->last < 0.1
        && at - clock->last < clock->period * 1.5)
      clock->period = (clock->period * 3.0 + (at - clock->last)) * 0.25;

    clock->last = at;
    if (clock->running && !clock->dead) {
      vsync.vsync  = at;
      vsync.period = clock->period;
      framePacerVsync(clock->pacer, &vsync);
    }

    pthread_mutex_unlock(&clock->lock);
  }

  pthread_mutex_lock(&clock->lock);
  detached = clock->detached;
  pthread_mutex_unlock(&clock->lock);
  if (detached) {
    if (clock->drm >= 0)
      close(clock->drm);

    pthread_cond_destroy(&clock->changed);
    pthread_mutex_destroy(&clock->lock);
    free(clock);
  }

  return NULL;
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
  return pacer__now();
}

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  pthread_mutexattr_t attr;
  GPUPacerClock      *clock;

  (void)swapchain;
  *outClock = NULL;
  if (!(clock = calloc(1u, sizeof(*clock))))
    return GPU_ERROR_OUT_OF_MEMORY;

  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&clock->lock, &attr);
  pthread_mutexattr_destroy(&attr);
  pthread_cond_init(&clock->changed, NULL);
  clock->pacer  = pacer;
  clock->period = 1.0 / 60.0;
  clock->drm    = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

  pacer->anyThread = true;
  if (pthread_create(&clock->thread, NULL, pacer__run, clock) != 0) {
    if (clock->drm >= 0)
      close(clock->drm);

    pthread_cond_destroy(&clock->changed);
    pthread_mutex_destroy(&clock->lock);
    free(clock);
    pacer->anyThread = false;
    return GPU_ERROR_UNSUPPORTED;
  }

  pacer->owner = (uintptr_t)clock->thread;
  *outClock    = clock;
  return GPU_OK;
}

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run) {
  if (!clock)
    return;

  pthread_mutex_lock(&clock->lock);
  clock->running = run;
  if (!run)
    clock->last = 0.0;

  pthread_cond_broadcast(&clock->changed);
  pthread_mutex_unlock(&clock->lock);
}

GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock) {
  bool own;

  if (!clock)
    return;

  own = pthread_equal(pthread_self(), clock->thread);
  pthread_mutex_lock(&clock->lock);
  clock->dead     = true;
  clock->running  = false;
  clock->detached = own;
  pthread_cond_broadcast(&clock->changed);
  pthread_mutex_unlock(&clock->lock);

  /* from its own tick: the thread ends after it and frees the clock */
  if (own) {
    pthread_detach(clock->thread);
    return;
  }

  pthread_join(clock->thread, NULL);
  if (clock->drm >= 0)
    close(clock->drm);

  pthread_cond_destroy(&clock->changed);
  pthread_mutex_destroy(&clock->lock);
  free(clock);
}

GPU_HIDE
void
pacerClockLock(GPUPacerClock *clock) {
  if (clock)
    pthread_mutex_lock(&clock->lock);
}

GPU_HIDE
void
pacerClockUnlock(GPUPacerClock *clock) {
  if (clock)
    pthread_mutex_unlock(&clock->lock);
}

#endif
