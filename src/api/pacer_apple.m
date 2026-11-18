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

#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>
#include <pthread.h>

#if TARGET_OS_IOS || TARGET_OS_TV
#  import <UIKit/UIKit.h>
#else
#  import <AppKit/AppKit.h>
#endif

/* Vulkan (MoltenVK) and WebGPU on Apple: CADisplayLink on the creating
 * thread's run loop. Not CAMetalDisplayLink: that one hands out the layer's
 * drawables, which MoltenVK and Dawn take from the layer themselves. The link
 * applies the rate range itself and fires at it, so the refresh period given
 * to the pacer is the link's own interval. */

API_AVAILABLE(macos(14.0), ios(15.0), tvos(15.0))
@interface GPUPacerLinkTarget : NSObject {
@public
  GPUPacer      *pacer;
  CADisplayLink *link;
}
- (void)step:(CADisplayLink *)sender;
@end

struct GPUPacerClock {
  GPUPacerLinkTarget *target;
};

@implementation GPUPacerLinkTarget
- (void)step:(CADisplayLink *)sender {
  GPUPacerVsync vsync = {0};
  double        interval;

  if (!pacer || sender != link)
    return;

  /* the link's interval at its rate: what was asked of it is applied already */
  interval = sender.targetTimestamp - sender.timestamp;
  if (!(interval > 0.0))
    interval = sender.duration;

  vsync.vsync    = sender.timestamp;
  vsync.period   = interval > 0.0 ? interval : 0.0;
  vsync.deadline = sender.targetTimestamp + (pacer->latencyFrames - 1u) * vsync.period;
  [self retain];
  framePacerVsync(pacer, &vsync);
  [self release];
}
@end

GPU_HIDE
bool
pacerClockSupported(void) {
  if (@available(macOS 14.0, iOS 15.0, tvOS 15.0, *))
    return true;

  return false;
}

GPU_HIDE
uintptr_t
pacerThreadSelf(void) {
  return (uintptr_t)pthread_self();
}

GPU_HIDE
double
pacerHostSeconds(void) {
  return CACurrentMediaTime();
}

GPU_HIDE
GPUResult
pacerClockCreate(GPUPacer *pacer, GPUSwapchain *swapchain, GPUPacerClock **outClock) {
  GPUPacerLinkTarget *target;
  GPUPacerClock      *clock;
  CADisplayLink      *link;
  void               *view;
  float               preferred;

  *outClock = NULL;
  if (@available(macOS 14.0, iOS 15.0, tvOS 15.0, *)) {
    view = swapchain->surface ? swapchain->surface->nativeHandle : NULL;
    if (!view)
      return GPU_ERROR_INVALID_ARGUMENT;

    if (!(clock = calloc(1u, sizeof(*clock))))
      return GPU_ERROR_OUT_OF_MEMORY;

    if (!(target = [GPUPacerLinkTarget new])) {
      free(clock);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

#if TARGET_OS_IOS || TARGET_OS_TV
    link = swapchain->surface->type == GPU_SURFACE_APPLE_UIVIEW
             ? [CADisplayLink displayLinkWithTarget:target selector:@selector(step:)] : nil;
#else
    link = swapchain->surface->type == GPU_SURFACE_APPLE_NSVIEW
             ? [(NSView *)view displayLinkWithTarget:target selector:@selector(step:)] : nil;
#endif
    if (!link) {
      [target release];
      free(clock);
      return GPU_ERROR_UNSUPPORTED;
    }

    link.paused = YES;
    if (pacer->maxRate > 0.0f) {
      /* Core Animation takes no zero minimum with a maximum: none asked is the rate asked */
      preferred = pacer->preferredRate > 0.0f ? pacer->preferredRate : pacer->maxRate;
      link.preferredFrameRateRange = CAFrameRateRangeMake(pacer->minRate > 0.0f ? pacer->minRate : preferred,
                                                          pacer->maxRate, preferred);
    }

    target->pacer = pacer;
    target->link  = [link retain];
    [link addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
    clock->target = target;
    *outClock     = clock;
    return GPU_OK;
  }

  return GPU_ERROR_UNSUPPORTED;
}

GPU_HIDE
void
pacerClockRun(GPUPacerClock *clock, bool run) {
  if (clock && clock->target)
    clock->target->link.paused = run ? NO : YES;
}

GPU_HIDE
void
pacerClockDestroy(GPUPacerClock *clock) {
  GPUPacerLinkTarget *target;

  if (!clock)
    return;

  /* the link holds its target until invalidated; a step in progress holds it too */
  if ((target = clock->target)) {
    target->pacer = NULL;
    [target->link invalidate];
    [target->link release];
    target->link = nil;
    [target release];
  }

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
