#include "../../common/apple.h"
#include "../../common/sample_orbit.h"

#import <AppKit/AppKit.h>

extern int
gpu_apple_sample_start(void);

#ifndef GPU_APPLE_SAMPLE_NAME
#  define GPU_APPLE_SAMPLE_NAME "GPU + USL Sample"
#endif

@interface GPUSampleView: NSView
@end

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

@end

@interface GPUSampleHost: NSObject <NSApplicationDelegate, NSWindowDelegate> {
  NSWindow            *_window;
  NSView              *_view;
  NSProgressIndicator *_progress;
  NSTimer             *_timer;
  GPUAppleSample      *_sample;
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
  frame = NSMakeRect(0.0, 0.0, 1120.0, 720.0);
  _window = [[NSWindow alloc]
    initWithContentRect:frame
              styleMask:NSWindowStyleMaskTitled |
                        NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable |
                        NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered
                  defer:NO];
  _window.title           = @GPU_APPLE_SAMPLE_NAME;
  _window.delegate        = self;
  _window.backgroundColor = NSColor.blackColor;
  _view = [[GPUSampleView alloc] initWithFrame:frame];
  _view.wantsLayer = YES;
  _view.layer.backgroundColor = NSColor.blackColor.CGColor;
  _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  _window.contentView    = _view;

  _progress = [NSProgressIndicator new];
  _progress.style = NSProgressIndicatorStyleSpinning;
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

  scale   = (float)(_window.backingScaleFactor ?: 1.0);
  _sample = GPUSampleAppleCreate((__bridge void *)_view,
                                 GPU_APPLE_SAMPLE_NAME,
                                 scale,
                                 gpu_apple_sample_start);
  if (!_sample || GPUSampleAppleFailed(_sample)) {
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

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
  (NSApplication *)sender {
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
