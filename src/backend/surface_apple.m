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

#include "surface_apple.h"

#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>
#include <stdatomic.h>

#if TARGET_OS_IOS
#  import <UIKit/UIKit.h>
#else
#  import <AppKit/AppKit.h>
#endif

GPU_HIDE
void*
createMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale) {
  CGRect        bounds;
  CAMetalLayer *layer;
  CALayer      *rootLayer;
#if TARGET_OS_IOS
  UIView       *view;
#else
  NSView       *view;
#endif

  if (!nativeHandle || scale <= 0.0f) {
    return NULL;
  }

#if TARGET_OS_IOS
  if (type != GPU_SURFACE_APPLE_UIVIEW) {
    return NULL;
  }

  view      = (UIView *)nativeHandle;
  rootLayer = view.layer;
  bounds    = view.bounds;
#else
  if (type != GPU_SURFACE_APPLE_NSVIEW) {
    return NULL;
  }

  view            = (NSView *)nativeHandle;
  view.wantsLayer = YES;
  rootLayer       = view.layer;
  bounds          = view.bounds;
#endif

  if (!rootLayer) {
    return NULL;
  }

  layer               = [[CAMetalLayer alloc] init];
  layer.frame         = bounds;
  layer.contentsScale = scale;
  layer.drawableSize  = CGSizeMake(bounds.size.width * scale,
                                   bounds.size.height * scale);
  layer.opaque        = YES;
#if !TARGET_OS_IOS
  layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
#endif

  [rootLayer addSublayer:layer];

  return layer;
}

GPU_HIDE
void
resizeMetalLayer(void    *metalLayer,
                 uint32_t width,
                 uint32_t height,
                 float    scale) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;

  if (!layer || width == 0u || height == 0u || scale <= 0.0f) {
    return;
  }

  layer.frame         = CGRectMake(0.0, 0.0, width, height);
  layer.contentsScale = scale;
  layer.drawableSize  = CGSizeMake((CGFloat)width * scale,
                                   (CGFloat)height * scale);
}

GPU_HIDE
void
destroyMetalLayer(void *metalLayer) {
  CAMetalLayer *layer;

  layer = (CAMetalLayer *)metalLayer;

  if (!layer) {
    return;
  }

  [layer removeFromSuperlayer];
  [layer release];
}

/* a view's watch: its flag is the observer's own, and the notification
 * center holds the observer weakly and for a delivery strongly, so a word
 * delivered as the watch goes writes into memory still its own */
@interface GPUAppleViewWatch: NSObject {
@public
  _Atomic(bool)  hidden;
#if TARGET_OS_IOS
  UIView        *view;
#else
  NSView        *view;
#endif
}
@end

@implementation GPUAppleViewWatch
#if TARGET_OS_IOS
/* the view's scene's word, or the app's for a view in no scene */
- (bool)gpuConcerns:(NSNotification *)note {
  UIScene *scene;

  scene = view.window.windowScene;
  if ([note.object isKindOfClass:[UIScene class]])
    return note.object == scene;

  return !scene;
}

- (void)gpuEnteredBackground:(NSNotification *)note {
  if ([self gpuConcerns:note])
    atomic_store_explicit(&hidden, true, memory_order_relaxed);
}

- (void)gpuEnteringForeground:(NSNotification *)note {
  if ([self gpuConcerns:note])
    atomic_store_explicit(&hidden, false, memory_order_relaxed);
}

/* a scene already in the background when watching begins */
- (void)gpuReadState {
  UIApplication *app;
  UIScene       *scene;

  if ((scene = view.window.windowScene)) {
    atomic_store_explicit(&hidden, scene.activationState == UISceneActivationStateBackground,
                          memory_order_relaxed);
  } else if ((app = [UIApplication sharedApplication])) {
    atomic_store_explicit(&hidden, app.applicationState == UIApplicationStateBackground,
                          memory_order_relaxed);
  }
}
#else
/* a window the system doesn't show (covered, minimized, on another Space, the
 * screen locked) gives its layer no drawable for up to a second: frames are
 * refused there, as DXGI's occluded present. Only AppKit's word on a change
 * counts, on the main thread: a window just ordered reads hidden until AppKit
 * hears from the window server, and one whose events nobody dispatches would
 * read so for good. A view in no window is not hidden */
- (void)gpuOcclusionChanged:(NSNotification *)note {
  NSWindow *window;

  window = view.window;
  if (note.object != window)
    return;

  atomic_store_explicit(&hidden, window && !(window.occlusionState & NSWindowOcclusionStateVisible),
                        memory_order_relaxed);
}
#endif

- (void)dealloc {
  [view release];
  [super dealloc];
}
@end

GPU_HIDE
void*
appleWatchView(void *nativeView) {
  NSNotificationCenter *center;
  GPUAppleViewWatch    *watch;

  if (!nativeView || !(watch = [GPUAppleViewWatch new]))
    return NULL;

  atomic_init(&watch->hidden, false);
  center = [NSNotificationCenter defaultCenter];
#if TARGET_OS_IOS
  watch->view = [(UIView *)nativeView retain];
  [center addObserver:watch selector:@selector(gpuEnteredBackground:)
                 name:UISceneDidEnterBackgroundNotification object:nil];
  [center addObserver:watch selector:@selector(gpuEnteringForeground:)
                 name:UISceneWillEnterForegroundNotification object:nil];
  [center addObserver:watch selector:@selector(gpuEnteredBackground:)
                 name:UIApplicationDidEnterBackgroundNotification object:nil];
  [center addObserver:watch selector:@selector(gpuEnteringForeground:)
                 name:UIApplicationWillEnterForegroundNotification object:nil];

  /* UIKit's state is the main thread's: read there, after the observers so
   * a word between the two is not lost */
  if ([NSThread isMainThread]) {
    [watch gpuReadState];
  } else {
    [watch retain];
    dispatch_async(dispatch_get_main_queue(), ^{
      [watch gpuReadState];
      [watch release];
    });
  }
#else
  watch->view = [(NSView *)nativeView retain];
  [center addObserver:watch selector:@selector(gpuOcclusionChanged:)
                 name:NSWindowDidChangeOcclusionStateNotification object:nil];
#endif

  return watch;
}

GPU_HIDE
bool
appleViewHidden(const void *watch) {
  return watch && atomic_load_explicit(&((GPUAppleViewWatch *)watch)->hidden, memory_order_relaxed);
}

GPU_HIDE
void
appleUnwatchView(void *watch) {
  if (!watch)
    return;

  [[NSNotificationCenter defaultCenter] removeObserver:(GPUAppleViewWatch *)watch];
  [(GPUAppleViewWatch *)watch release];
}

static _Atomic(bool) apple__gpuRefused;

#if TARGET_OS_IOS
/* the app's own word, not a scene's: one scene in the background leaves the
 * app's others drawing. Back at its will-enter-foreground (or its activation,
 * for a watch begun in between) */
@interface GPUAppleAppWatch: NSObject
@end

@implementation GPUAppleAppWatch
- (void)gpuEnteredBackground:(NSNotification *)note {
  (void)note;
  atomic_store_explicit(&apple__gpuRefused, true, memory_order_relaxed);
}

- (void)gpuEnteringForeground:(NSNotification *)note {
  (void)note;
  atomic_store_explicit(&apple__gpuRefused, false, memory_order_relaxed);
}

- (void)gpuReadState {
  UIApplication *app;

  if ((app = [UIApplication sharedApplication]))
    atomic_store_explicit(&apple__gpuRefused, app.applicationState == UIApplicationStateBackground,
                          memory_order_relaxed);
}
@end
#endif

GPU_HIDE
void
appleWatchGPUAccess(void) {
#if TARGET_OS_IOS
  static dispatch_once_t once;

  dispatch_once(&once, ^{
    NSNotificationCenter *center;
    GPUAppleAppWatch     *watch;

    /* the process's for good */
    watch  = [GPUAppleAppWatch new];
    center = [NSNotificationCenter defaultCenter];
    [center addObserver:watch selector:@selector(gpuEnteredBackground:)
                   name:UIApplicationDidEnterBackgroundNotification object:nil];
    [center addObserver:watch selector:@selector(gpuEnteringForeground:)
                   name:UIApplicationWillEnterForegroundNotification object:nil];
    [center addObserver:watch selector:@selector(gpuEnteringForeground:)
                   name:UIApplicationDidBecomeActiveNotification object:nil];

    /* an app launched into the background starts refused */
    if ([NSThread isMainThread]) {
      [watch gpuReadState];
    } else {
      dispatch_async(dispatch_get_main_queue(), ^{
        [watch gpuReadState];
      });
    }
  });
#endif
}

GPU_HIDE
bool
appleGPURefused(void) {
  return atomic_load_explicit(&apple__gpuRefused, memory_order_relaxed);
}

GPU_HIDE
void
appleSetGPURefused(bool refused) {
  atomic_store_explicit(&apple__gpuRefused, refused, memory_order_relaxed);
}
