#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#include <stdlib.h>

#import "../../include/gpu/gpu.h"
#import "../common/ShadowCompare.h"
#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"

#ifndef GPU_SAMPLE_BACKEND
#  define GPU_SAMPLE_BACKEND GPU_BACKEND_METAL
#endif

static NSString *
ShadowCompareWindowTitle(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? @"GPU Vulkan USL Shadow Compare"
           : @"GPU Metal USL Shadow Compare";
}

static const char *
ShadowCompareStatsLabel(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? "GPU Vulkan shadow compare"
           : "GPU Metal shadow compare";
}

@interface ShadowCompareApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow          *_window;
  NSView            *_view;
  GPUInstance       *_instance;
  GPUAdapter        *_adapter;
  GPUDevice         *_device;
  GPUQueue          *_queue;
  GPUSurface        *_surface;
  GPUSwapchain           *_swapchain;
  NSTimer                *_timer;
  GPUSampleShadowCompare  _renderer;
  NSInteger               _exitAfterFrames;
  NSInteger               _submittedFrames;
  NSInteger               _completedFrames;
  BOOL                    _assertZeroAlloc;
  BOOL                    _statsFailed;
  BOOL                    _terminating;
}
- (void)frameCompleted;
- (BOOL)statsFailed;
@end

static void
ShadowCompareFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  ShadowCompareApp *app;

  (void)cmdb;
  app = (__bridge ShadowCompareApp *)sender;
  [app frameCompleted];
}

static void
ShadowCompareDeviceError(GPUDevice                *device,
                         const GPUDeviceErrorInfo *error,
                         void                     *userData) {
  ShadowCompareApp *app;

  (void)device;
  app = (__bridge ShadowCompareApp *)userData;
  NSLog(@"GPU shadow compare error: %s",
        error && error->message ? error->message : "unknown error");
  if (app) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp terminate:nil];
    });
  }
}

@implementation ShadowCompareApp

- (BOOL)drawableSizeWidth:(uint32_t *)outWidth height:(uint32_t *)outHeight {
  CGFloat scale;

  if (!outWidth || !outHeight) {
    return NO;
  }
  scale      = _window.backingScaleFactor ?: 1.0f;
  *outWidth  = (uint32_t)(_view.bounds.size.width * scale);
  *outHeight = (uint32_t)(_view.bounds.size.height * scale);
  return *outWidth > 0u && *outHeight > 0u;
}

- (BOOL)setupGPU {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUShaderLibrary     *library;
  GPUShaderLayout      *shaderLayout;
  uint32_t              width;
  uint32_t              height;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "shadow-compare-native-usl";
  instanceInfo.preferredBackend = GPU_SAMPLE_BACKEND;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &_instance) != GPU_OK || !_instance) {
    return NO;
  }

  _adapter = GPUSampleSelectAdapter(_instance);
  if (!_adapter) {
    return NO;
  }
  _device = GPUCreateDeviceWithDefaultQueues(_adapter);
  if (!_device) {
    return NO;
  }
  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_queue) {
    return NO;
  }
  if (GPUSetDeviceErrorCallback(_device,
                                ShadowCompareDeviceError,
                                (__bridge void *)self) != GPU_OK) {
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)_view,
                                        GPU_SURFACE_APPLE_NSVIEW,
                                        _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    return NO;
  }
  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain || ![self drawableSizeWidth:&width height:&height]) {
    return NO;
  }

  library      = NULL;
  shaderLayout = NULL;
  if (!GPUSampleLoadUSL(_device,
                        @"shadow_compare.us",
                        1u,
                        &library,
                        &shaderLayout)) {
    return NO;
  }
  return GPUSampleShadowCompareInit(&_renderer,
                                    _device,
                                    _queue,
                                    _swapchain,
                                    library,
                                    shaderLayout,
                                    width,
                                    height) == GPU_OK;
}

- (void)renderFrame {
  GPUCommandBufferCompletionFn completion;
  GPUResult                    result;
  uint32_t                     width;
  uint32_t                     height;

  if (_terminating ||
      (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) ||
      !GPUSampleRecoverSwapchain(_swapchain, _view) ||
      ![self drawableSizeWidth:&width height:&height]) {
    return;
  }
  if (GPUSampleShadowCompareResize(&_renderer, width, height) != GPU_OK) {
    return;
  }

  completion = _exitAfterFrames > 0 ? ShadowCompareFrameComplete : NULL;
  result = GPUSampleShadowCompareRender(&_renderer,
                                        (__bridge void *)self,
                                        completion);
  if (result != GPU_OK) {
    NSLog(@"GPU shadow compare frame failed: %d", result);
    return;
  }

  _submittedFrames++;
  if (!GPUSampleCheckZeroAlloc(_device,
                               (uint32_t)_submittedFrames,
                               _assertZeroAlloc,
                               ShadowCompareStatsLabel())) {
    _statsFailed = YES;
    _terminating = YES;
    [NSApp terminate:nil];
  } else if (_exitAfterFrames > 0 &&
             _submittedFrames >= _exitAfterFrames) {
    [_timer invalidate];
    _timer = nil;
  }
}

- (void)frameCompleted {
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_completedFrames++;
    if (self->_exitAfterFrames > 0 &&
        self->_completedFrames >= self->_exitAfterFrames &&
        !self->_terminating) {
      self->_terminating = YES;
      [NSApp terminate:nil];
    }
  });
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
}

- (void)cleanupGPU {
  GPUSampleShadowCompareDestroy(&_renderer);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);
  _swapchain = NULL;
  _surface   = NULL;
  _queue     = NULL;
  _device    = NULL;
  _adapter   = NULL;
  _instance  = NULL;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  const char *exitAfterFrames;

  (void)notification;
  if (!GPUSampleCreateWindow(ShadowCompareWindowTitle(), self, &_window, &_view) ||
      ![self setupGPU]) {
    [NSApp terminate:nil];
    return;
  }

  exitAfterFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitAfterFrames) {
    _exitAfterFrames = strtol(exitAfterFrames, NULL, 10);
  }
  _assertZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  _timer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                   target:self
                                 selector:@selector(tick:)
                                 userInfo:nil
                                  repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
  [self renderFrame];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [_timer invalidate];
  _timer = nil;
  [self cleanupGPU];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

- (BOOL)statsFailed {
  return _statsFailed;
}

@end

int
main(int argc, const char *argv[]) {
  int result;

  result = 0;
  @autoreleasepool {
    ShadowCompareApp *delegate;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    delegate = [ShadowCompareApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
    result = [delegate statsFailed] ? 1 : 0;
  }
  return result;
}
