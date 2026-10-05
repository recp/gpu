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

#include "../../common/apple.h"
#include "../../common/sample_orbit.h"

#import <AppKit/AppKit.h>

#ifndef GPU_APPLE_SAMPLE_NAME
#  define GPU_APPLE_SAMPLE_NAME "GPU + USL Sample"
#endif

@interface GPUSampleView : NSView
@end

@interface GPUSampleHost : NSObject <NSApplicationDelegate, NSWindowDelegate> {
  NSWindow            *_window;
  NSView              *_view;
  NSProgressIndicator *_progress;
  NSTimer             *_timer;
  GPUAppleSample      *_sample;
}
@end

extern int
gpu_apple_sample_start(void);

@implementation GPUSampleView

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)mouseDown:(NSEvent *)event {
  NSPoint point;

  point = [self convertPoint:event.locationInWindow fromView:nil];
  sample_orbit_pointer_begin((float)point.x, (float)point.y);
}

- (void)mouseDragged:(NSEvent *)event {
  NSPoint point;

  point = [self convertPoint:event.locationInWindow fromView:nil];
  sample_orbit_pointer_move((float)point.x, (float)point.y);
}

- (void)mouseUp:(NSEvent *)event {
  (void)event;
  sample_orbit_pointer_end();
}

- (void)scrollWheel:(NSEvent *)event {
  sample_orbit_zoom((float)event.scrollingDeltaY * 0.08f);
}

- (void)magnifyWithEvent:(NSEvent *)event {
  sample_orbit_zoom((float)event.magnification * 4.0f);
}

@end

@implementation GPUSampleHost

- (void)installMainMenu {
  NSMenu     *applicationMenu;
  NSMenu     *mainMenu;
  NSMenuItem *applicationItem;
  NSMenuItem *quitItem;
  NSString   *quitTitle;

  mainMenu        = [NSMenu new];
  applicationItem = [NSMenuItem new];
  applicationMenu = [NSMenu new];
  quitTitle       = [NSString stringWithFormat:@"Quit %s",
                                               GPU_APPLE_SAMPLE_NAME];
  quitItem        = [[NSMenuItem alloc] initWithTitle:quitTitle
                                               action:@selector(terminate:)
                                        keyEquivalent:@"q"];
  quitItem.target = NSApp;

  quitItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;

  [applicationMenu addItem:quitItem];
  applicationItem.submenu = applicationMenu;
  [mainMenu addItem:applicationItem];
  NSApp.mainMenu = mainMenu;
}

- (void)startTimer {
  if (_timer || !_sample || GPUSampleAppleFailed(_sample)) {
    return;
  }

  _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 120.0
                                            target:self
                                          selector:@selector(render:)
                                          userInfo:nil
                                           repeats:YES];
}

- (void)stopTimer {
  [_timer invalidate];
  _timer = nil;
}

- (void)stopSample {
  [self stopTimer];

  if (_sample) {
    GPUSampleAppleStop(_sample);
    _sample = NULL;
  }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  NSRect frame;
  float  scale;

  (void)notification;
  [self installMainMenu];

  frame   = NSMakeRect(0.0, 0.0, 1120.0, 720.0);
  _window = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled
                                                  | NSWindowStyleMaskClosable
                                                  | NSWindowStyleMaskMiniaturizable
                                                  | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];

  _window.title           = @GPU_APPLE_SAMPLE_NAME;
  _window.delegate        = self;
  _window.backgroundColor = NSColor.blackColor;

  _view                       = [[GPUSampleView alloc] initWithFrame:frame];
  _view.wantsLayer            = YES;
  _view.layer.backgroundColor = NSColor.blackColor.CGColor;
  _view.autoresizingMask      = NSViewWidthSizable | NSViewHeightSizable;
  _window.contentView         = _view;

  _progress             = [NSProgressIndicator new];
  _progress.style       = NSProgressIndicatorStyleSpinning;
  _progress.controlSize = NSControlSizeRegular;

  _progress.translatesAutoresizingMaskIntoConstraints = NO;

  [_view addSubview:_progress];
  [NSLayoutConstraint activateConstraints:@[
    [_progress.centerXAnchor constraintEqualToAnchor:_view.centerXAnchor],
    [_progress.centerYAnchor constraintEqualToAnchor:_view.centerYAnchor]
  ]];
  [_progress startAnimation:nil];

  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [_window makeFirstResponder:_view];
  [NSApp activateIgnoringOtherApps:YES];

  scale = (float)(_window.backingScaleFactor ?: 1.0);

  if (!(_sample = GPUSampleAppleCreate((__bridge void *)_view,
                                       GPU_APPLE_SAMPLE_NAME,
                                       scale,
                                       gpu_apple_sample_start))
      || GPUSampleAppleFailed(_sample)) {
    NSLog(@"%s", GPUSampleAppleStatus(_sample));
    [NSApp terminate:nil];
    return;
  }

  [self startTimer];
}

- (void)render:(NSTimer *)timer {
  (void)timer;

  if (!GPUSampleAppleRender(_sample)) {
    [self stopTimer];
    return;
  }

  if (!_progress.hidden && GPUSampleAppleHasRenderedFrame(_sample)) {
    [_progress stopAnimation:nil];
    _progress.hidden = YES;
  }
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
  (void)notification;
  [self startTimer];
}

- (void)applicationDidResignActive:(NSNotification *)notification {
  (void)notification;
  [self stopTimer];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [self stopSample];
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [self stopSample];
  [NSApp terminate:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

@end

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    GPUSampleHost *host;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    host = [GPUSampleHost new];
    [NSApp setDelegate:host];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
  }

  return 0;
}
