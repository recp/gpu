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

/* frame_pacer.c's window on macOS: a small titled window, the main run loop */

#include "frame_pacer_host.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

static NSWindow *pacer__window;

/* ticks are the system's to give: a covered window gets none, or a few, so the
 * window is shown above other apps' (not activated: no focus taken) and waited
 * for, bounded; one never shown is no window to pace */
bool
pacerHostOpen(GPUInstance *inst, GPUAdapter *adapter, uint32_t width, uint32_t height, GPUSurface **outSurface) {
  NSDate *deadline;

  *outSurface = NULL;
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    pacer__window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, width, height)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [pacer__window setReleasedWhenClosed:NO];
    [pacer__window setLevel:NSFloatingWindowLevel];
    [pacer__window orderFrontRegardless];
  }

  deadline = [NSDate dateWithTimeIntervalSinceNow:0.5];
  while (!(pacer__window.occlusionState & NSWindowOcclusionStateVisible) && deadline.timeIntervalSinceNow > 0.0)
    pacerHostPump(0.005);

  if (!(pacer__window.occlusionState & NSWindowOcclusionStateVisible)) {
    pacerHostClose();
    return false;
  }

  @autoreleasepool {
    *outSurface = GPUCreateSurfaceFromNative(inst, adapter, [pacer__window contentView],
                                             GPU_SURFACE_APPLE_NSVIEW, 1.0f);
  }
  return *outSurface != NULL;
}

bool
pacerHostResize(uint32_t width, uint32_t height) {
  [pacer__window setContentSize:NSMakeSize(width, height)];
  return true;
}

void
pacerHostPump(double seconds) {
  @autoreleasepool {
    NSDate  *deadline;
    NSEvent *event;

    deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (deadline.timeIntervalSinceNow > 0.0) {
      event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                 untilDate:[NSDate dateWithTimeIntervalSinceNow:0.002]
                                    inMode:NSDefaultRunLoopMode
                                   dequeue:YES];
      if (event)
        [NSApp sendEvent:event];
      [NSApp updateWindows];
    }
  }
}

double
pacerHostNow(void) {
  return CACurrentMediaTime();
}

uint32_t
pacerHostPresented(double *times, uint32_t capacity) {
  (void)times;
  (void)capacity;
  return 0u;
}

const char*
pacerHostPresentKind(void) {
  return "display";
}

void
pacerHostClose(void) {
  [pacer__window close];
  [pacer__window release];
  pacer__window = nil;
}
