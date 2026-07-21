#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

#include <stdlib.h>

#import "../../include/gpu/gpu.h"
#import "../common/MeshTriangle.h"
#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"

#ifndef GPU_SAMPLE_BACKEND
#  define GPU_SAMPLE_BACKEND GPU_BACKEND_METAL
#endif

enum {
  kSkipReturnCode = 77
};

static NSString *
MeshTriangleWindowTitle(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? @"GPU Vulkan USL Mesh Triangle"
           : @"GPU Metal USL Mesh Triangle";
}

static const char *
MeshTriangleStatsLabel(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? "GPU Vulkan mesh triangle"
           : "GPU Metal mesh triangle";
}

@interface MeshTriangleApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow                 *_window;
  NSView                   *_view;
  GPUInstance              *_instance;
  GPUAdapter               *_adapter;
  GPUDevice                *_device;
  GPUQueue                 *_queue;
  GPUSurface               *_surface;
  GPUSwapchain             *_swapchain;
  NSTimer                  *_timer;
  GPUSampleMeshTriangle    _renderer;
  NSInteger                 _exitAfterFrames;
  NSInteger                 _submittedFrames;
  NSInteger                 _completedFrames;
  BOOL                      _assertZeroAlloc;
  BOOL                      _failed;
  BOOL                      _skipped;
  BOOL                      _terminating;
}
- (void)frameCompleted;
- (void)cleanupGPU;
- (void)failAndStop;
- (void)stopApplication;
- (int)exitCode;
@end

static void
MeshTriangleFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  MeshTriangleApp *app;

  (void)cmdb;
  app = (__bridge MeshTriangleApp *)sender;
  [app frameCompleted];
}

static void
MeshTriangleDeviceError(GPUDevice                *device,
                        const GPUDeviceErrorInfo *error,
                        void                     *userData) {
  MeshTriangleApp *app;
  const char      *message;

  (void)device;
  app     = (__bridge MeshTriangleApp *)userData;
  message = error && error->message ? error->message : "unknown error";
  NSLog(@"GPU mesh triangle error: %s", message);
  if (app) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [app failAndStop];
    });
  }
}

@implementation MeshTriangleApp

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
  GPUDeviceCreateInfo   deviceInfo   = {0};
  GPURuntimeConfig      runtime      = {0};
  GPUShaderLibrary     *library;
  GPUShaderLayout      *shaderLayout;
  GPUFeature            feature;
  uint32_t              width;
  uint32_t              height;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "mesh-triangle-native-usl";
  instanceInfo.preferredBackend = GPU_SAMPLE_BACKEND;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &_instance) != GPU_OK || !_instance) {
    return NO;
  }

  _adapter = GPUSampleSelectAdapter(_instance);
  if (!_adapter) {
    return NO;
  }
  if (!GPUIsFeatureSupported(_adapter, GPU_FEATURE_MESH_SHADER)) {
    _skipped = YES;
    return NO;
  }
  feature                           = GPU_FEATURE_MESH_SHADER;
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.label                 = "mesh-triangle-native-device";
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(_adapter, &deviceInfo, &_device) != GPU_OK ||
      !_device || !GPUIsFeatureEnabled(_device, GPU_FEATURE_MESH_SHADER)) {
    return NO;
  }
  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_queue) {
    return NO;
  }
  if (GPUSetDeviceErrorCallback(_device,
                                MeshTriangleDeviceError,
                                (__bridge void *)self) != GPU_OK) {
    return NO;
  }
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(_device, &runtime) != GPU_OK) {
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
                        @"mesh_triangle.us",
                        1u,
                        &library,
                        &shaderLayout)) {
    return NO;
  }
  return GPUSampleMeshTriangleInit(&_renderer,
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
  if (GPUSampleMeshTriangleResize(&_renderer, width, height) != GPU_OK) {
    [self failAndStop];
    return;
  }

  completion = _exitAfterFrames > 0 ? MeshTriangleFrameComplete : NULL;
  result = GPUSampleMeshTriangleRender(&_renderer,
                                       (__bridge void *)self,
                                       completion);
  if (result != GPU_OK) {
    NSLog(@"GPU mesh triangle frame failed: %d", result);
    [self failAndStop];
    return;
  }

  _submittedFrames++;
  if (!GPUSampleCheckZeroAlloc(_device,
                               (uint32_t)_submittedFrames,
                               _assertZeroAlloc,
                               MeshTriangleStatsLabel())) {
    [self failAndStop];
  } else if (_exitAfterFrames > 0 &&
             _submittedFrames >= _exitAfterFrames) {
    [_timer invalidate];
    _timer = nil;
  }
}

- (void)failAndStop {
  _failed      = YES;
  _terminating = YES;
  [self stopApplication];
}

- (void)stopApplication {
  NSEvent *event;

  [NSApp stop:nil];
  event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                             location:NSZeroPoint
                        modifierFlags:0
                            timestamp:0
                         windowNumber:0
                              context:nil
                              subtype:0
                                data1:0
                                data2:0];
  [NSApp postEvent:event atStart:YES];
}

- (void)frameCompleted {
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_completedFrames++;
    if (self->_exitAfterFrames > 0 &&
        self->_completedFrames >= self->_exitAfterFrames &&
        !self->_terminating) {
      self->_terminating = YES;
      [self stopApplication];
    }
  });
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
}

- (void)cleanupGPU {
  GPUSampleMeshTriangleDestroy(&_renderer);
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
  if (!GPUSampleCreateWindow(MeshTriangleWindowTitle(), self, &_window, &_view) ||
      ![self setupGPU]) {
    if (!_skipped) {
      _failed = YES;
    }
    [self stopApplication];
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

- (int)exitCode {
  return _skipped ? kSkipReturnCode : (_failed ? 1 : 0);
}

@end

int
main(int argc, const char *argv[]) {
  int result;

  result = 0;
  @autoreleasepool {
    MeshTriangleApp *delegate;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    delegate = [MeshTriangleApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
    result = [delegate exitCode];
    [delegate cleanupGPU];
  }
  return result;
}
