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

#include "apple_window.h"
#import <QuartzCore/QuartzCore.h>
#import <TargetConditionals.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if TARGET_OS_IOS
#  import <UIKit/UIKit.h>
#else
#  import <AppKit/AppKit.h>
#endif

struct AppleWindow {
#if TARGET_OS_IOS
  UIWindow *window;
#else
  NSWindow *window;
  NSWindow *cover; /* over it while hidden */
#endif
};

GPUSurface*
appleWindowSurface(AppleWindow *window, GPUInstance *instance, GPUAdapter *adapter) {
  GPUSurface *surface;

  if (!window)
    return NULL;

  @autoreleasepool {
    surface = GPUCreateSurfaceFromNative(instance, adapter, appleWindowView(window), appleWindowSurfaceType(), 1.0f);
  }
  return surface;
}

bool
appleWindowWaitShown(AppleWindow *window, bool shown, double seconds) {
  double deadline;

  deadline = CACurrentMediaTime() + seconds;
  while (appleWindowShown(window) != shown && CACurrentMediaTime() < deadline)
    appleWindowPump(0.005);

  return appleWindowShown(window) == shown;
}

#if TARGET_OS_IOS

static UIBackgroundTaskIdentifier apple__task;
static bool                       apple__tasked; /* apple__task held */

/* the app's window scene, the active one first */
static UIWindowScene*
apple__scene(void) {
  UIWindowScene *found = nil;
  UIApplication *app;

  if (!(app = [UIApplication sharedApplication]))
    return nil;

  for (UIScene *scene in app.connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]])
      continue;

    if (!found || scene.activationState == UISceneActivationStateForegroundActive)
      found = (UIWindowScene *)scene;
  }

  return found;
}

static bool
apple__sceneIs(AppleWindow *window, UISceneActivationState state) {
  bool is;

  if (!window)
    return false;

  @autoreleasepool {
    is = window->window.windowScene && window->window.windowScene.activationState == state;
  }
  return is;
}

static bool
apple__waitScene(AppleWindow *window, UISceneActivationState state, double seconds) {
  double deadline;

  deadline = CACurrentMediaTime() + seconds;
  while (!apple__sceneIs(window, state) && CACurrentMediaTime() < deadline)
    appleWindowPump(0.005);

  return apple__sceneIs(window, state);
}

AppleWindow*
appleWindowOpen(double x, double y, uint32_t width, uint32_t height) {
  UIViewController *controller;
  UIWindowScene    *scene;
  AppleWindow      *window;

  if (!(window = calloc(1u, sizeof(*window))))
    return NULL;

  @autoreleasepool {
    if (!(scene = apple__scene())) {
      free(window);
      return NULL;
    }

    window->window = [[UIWindow alloc] initWithWindowScene:scene];
    controller     = [UIViewController new];
    controller.view.backgroundColor = [UIColor blackColor];
    window->window.rootViewController = controller;
    [controller release];

    /* above the app's own window, not made key: no focus taken */
    window->window.windowLevel = UIWindowLevelNormal + 1.0;
    window->window.frame       = CGRectMake(x, y, width, height);
    window->window.hidden      = NO;
    [window->window layoutIfNeeded];
  }
  return window;
}

void*
appleWindowView(AppleWindow *window) {
  return window ? window->window.rootViewController.view : NULL;
}

GPUSurfaceType
appleWindowSurfaceType(void) {
  return GPU_SURFACE_APPLE_UIVIEW;
}

bool
appleWindowShown(AppleWindow *window) {
  return window && !window->window.hidden
         && (apple__sceneIs(window, UISceneActivationStateForegroundActive)
             || apple__sceneIs(window, UISceneActivationStateForegroundInactive));
}

void
appleWindowResize(AppleWindow *window, uint32_t width, uint32_t height) {
  CGRect frame;

  @autoreleasepool {
    frame                = window->window.frame;
    window->window.frame = CGRectMake(frame.origin.x, frame.origin.y, width, height);
    [window->window layoutIfNeeded];
  }
}

double
appleWindowDisplayRate(AppleWindow *window) {
  NSProcessInfo *process;
  id             faster;
  double         rate;

  if (!window)
    return 0.0;

  rate = 0.0;
  @autoreleasepool {
    process = [NSProcessInfo processInfo];
    if (!process.lowPowerModeEnabled && process.thermalState < NSProcessInfoThermalStateSerious) {
      rate   = (double)window->window.windowScene.screen.maximumFramesPerSecond;
      faster = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CADisableMinimumFrameDurationOnPhone"];
      if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone
          && !([faster respondsToSelector:@selector(boolValue)] && [faster boolValue]))
        rate = fmin(rate, 60.0);
    }
  }
  return rate;
}

/* no public call sends an app to the background: UIApplication's suspend does
 * what the home gesture does. The background task keeps the app running there
 * (the system suspends it seconds after otherwise) */
bool
appleWindowHide(AppleWindow *window, double seconds) {
  UIApplication *app;
  SEL            suspend;

  app     = [UIApplication sharedApplication];
  suspend = NSSelectorFromString(@"suspend");
  if (!window || !app || ![app respondsToSelector:suspend] || !appleWindowShown(window))
    return false;

  @autoreleasepool {
    if (!apple__tasked) {
      apple__tasked = true;
      apple__task   = [app beginBackgroundTaskWithName:@"gpu frame test" expirationHandler:^{
        if (apple__tasked)
          [[UIApplication sharedApplication] endBackgroundTask:apple__task];
        apple__tasked = false;
      }];
    }

    [app performSelector:suspend];
  }
  if (!apple__waitScene(window, UISceneActivationStateBackground, seconds))
    return false;

  /* the scene's notifications, posted with its state, reach their observers */
  appleWindowPump(0.05);
  return !appleWindowShown(window);
}

bool
appleWindowUnhide(AppleWindow *window, double seconds) {
  bool active;

  if (!window)
    return false;

  if (!apple__sceneIs(window, UISceneActivationStateForegroundActive)) {
    printf("apple window: waiting for the foreground\n");
    fflush(stdout);
  }

  active = apple__waitScene(window, UISceneActivationStateForegroundActive, seconds);
  if (active && apple__tasked) {
    apple__tasked = false;
    [[UIApplication sharedApplication] endBackgroundTask:apple__task];
  }

  appleWindowPump(0.05);
  return active && appleWindowShown(window);
}

void
appleWindowClose(AppleWindow *window) {
  if (!window)
    return;

  @autoreleasepool {
    window->window.hidden = YES;
    [window->window release];
  }
  free(window);
}

void
appleWindowPump(double seconds) {
  double deadline, left;

  deadline = CACurrentMediaTime() + seconds;
  do {
    @autoreleasepool {
      left = deadline - CACurrentMediaTime();
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, left > 0.002 ? 0.002 : left > 0.0 ? left : 0.0, false);
    }
  } while (CACurrentMediaTime() < deadline);
}

#else

AppleWindow*
appleWindowOpen(double x, double y, uint32_t width, uint32_t height) {
  AppleWindow *window;

  if (!(window = calloc(1u, sizeof(*window))))
    return NULL;

  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    window->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, y, width, height)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [window->window setReleasedWhenClosed:NO];
    /* ticks and presents are the system's to give: a covered window gets
     * none, or a few, so it goes above other apps' windows */
    [window->window setLevel:NSFloatingWindowLevel];
    [window->window orderFrontRegardless];
  }
  return window;
}

void*
appleWindowView(AppleWindow *window) {
  return window ? [window->window contentView] : NULL;
}

GPUSurfaceType
appleWindowSurfaceType(void) {
  return GPU_SURFACE_APPLE_NSVIEW;
}

bool
appleWindowShown(AppleWindow *window) {
  return window && (window->window.occlusionState & NSWindowOcclusionStateVisible) != 0;
}

void
appleWindowResize(AppleWindow *window, uint32_t width, uint32_t height) {
  @autoreleasepool {
    [window->window setContentSize:NSMakeSize(width, height)];
  }
}

double
appleWindowDisplayRate(AppleWindow *window) {
  NSProcessInfo *process;
  NSScreen      *screen;
  double         rate;

  if (!window)
    return 0.0;

  rate = 0.0;
  @autoreleasepool {
    process = [NSProcessInfo processInfo];
    screen  = window->window.screen ? window->window.screen : [NSScreen mainScreen];
    if (!process.lowPowerModeEnabled && process.thermalState < NSProcessInfoThermalStateSerious)
      rate = (double)screen.maximumFramesPerSecond;
  }
  return rate;
}

bool
appleWindowHide(AppleWindow *window, double seconds) {
  NSWindow *cover;

  if (!window || window->cover)
    return false;

  @autoreleasepool {
    cover = [[NSWindow alloc] initWithContentRect:NSInsetRect(window->window.frame, -8.0, -8.0)
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [cover setReleasedWhenClosed:NO];
    [cover setOpaque:YES];
    [cover setBackgroundColor:[NSColor blackColor]];
    [cover setLevel:NSStatusWindowLevel];
    [cover orderFrontRegardless];
    window->cover = cover;
  }
  return appleWindowWaitShown(window, false, seconds);
}

bool
appleWindowUnhide(AppleWindow *window, double seconds) {
  if (!window)
    return false;

  @autoreleasepool {
    if (window->cover) {
      [window->cover orderOut:nil];
      [window->cover close];
      [window->cover release];
      window->cover = nil;
    }
  }

  return appleWindowWaitShown(window, true, seconds);
}

void
appleWindowClose(AppleWindow *window) {
  if (!window)
    return;

  @autoreleasepool {
    if (window->cover) {
      [window->cover close];
      [window->cover release];
    }

    [window->window close];
    [window->window release];
  }
  free(window);
}

/* AppKit's events dispatched: a window's occlusion is known only through them */
void
appleWindowPump(double seconds) {
  NSEvent *event;
  double   deadline;

  deadline = CACurrentMediaTime() + seconds;
  do {
    @autoreleasepool {
      event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                 untilDate:[NSDate dateWithTimeIntervalSinceNow:0.002]
                                    inMode:NSDefaultRunLoopMode
                                   dequeue:YES];
      if (event)
        [NSApp sendEvent:event];
      [NSApp updateWindows];
    }
  } while (CACurrentMediaTime() < deadline);
}

#endif
