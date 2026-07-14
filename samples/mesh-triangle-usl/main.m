#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#include <stdint.h>
#include <stdlib.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"

@interface MeshTriangleApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow            *_window;
  NSView              *_view;
  GPUInstance         *_instance;
  GPUAdapter          *_adapter;
  GPUDevice           *_device;
  GPUQueue            *_queue;
  GPUSurface          *_surface;
  GPUSwapchain        *_swapchain;
  GPUShaderLibrary    *_library;
  GPUShaderLayout     *_shaderLayout;
  GPURenderPipeline   *_pipeline;
  NSTimer             *_timer;
  NSInteger            _exitAfterFrames;
  NSInteger            _submittedFrames;
  NSInteger            _completedFrames;
  BOOL                 _assertZeroAlloc;
  BOOL                 _statsFailed;
  BOOL                 _terminating;
}
- (void)frameCompleted;
- (BOOL)statsFailed;
@end

static void
MeshTriangleFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  MeshTriangleApp *app = (__bridge MeshTriangleApp *)sender;
  [app frameCompleted];
}

@implementation MeshTriangleApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU USL Mesh Triangle",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupGPU {
  GPUFeature          requiredFeature;
  GPUDeviceCreateInfo deviceInfo;

  _instance = NULL;
  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  _adapter = GPUSampleSelectAdapter(_instance);
  if (!_adapter ||
      !GPUIsFeatureSupported(_adapter, GPU_FEATURE_MESH_SHADER)) {
    NSLog(@"GPU: mesh shaders are not supported");
    return NO;
  }

  requiredFeature = GPU_FEATURE_MESH_SHADER;
  deviceInfo = (GPUDeviceCreateInfo){
    .chain = {
      .sType = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .structSize = sizeof(GPUDeviceCreateInfo)
    },
    .label = "mesh-triangle-device",
    .required = {
      .featureCount = 1,
      .pFeatures = &requiredFeature
    }
  };
  if (GPUCreateDevice(_adapter, &deviceInfo, &_device) != GPU_OK || !_device) {
    NSLog(@"GPU: failed to create mesh device");
    return NO;
  }

  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0);
  if (!_queue) {
    NSLog(@"GPU: failed to get graphics queue");
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)_view,
                                        GPU_SURFACE_APPLE_NSVIEW,
                                        _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    NSLog(@"GPU: failed to create surface");
    return NO;
  }

  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }

  if (!GPUSampleLoadUSL(_device,
                        @"mesh_triangle.us",
                        0u,
                        &_library,
                        &_shaderLayout)) {
    return NO;
  }

  GPUColorTargetState colorTarget = {
    .format = GPUGetSwapchainFormat(_swapchain),
    .blend = {
      .enabled = false,
      .writeMask = GPU_COLOR_WRITE_ALL
    }
  };
  GPUMeshPipelineEXT meshInfo = {
    .chain = {
      .sType = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT,
      .structSize = sizeof(GPUMeshPipelineEXT)
    },
    .taskEntry = "task_main",
    .meshEntry = "mesh_main"
  };
  GPURenderPipelineCreateInfo pipelineInfo = {
    .chain = {
      .sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
      .structSize = sizeof(GPURenderPipelineCreateInfo),
      .pNext = &meshInfo.chain
    },
    .label = "mesh-triangle-pipeline",
    .layout = _shaderLayout->pipelineLayout,
    .library = _library,
    .fragmentEntry = "fragment_main",
    .colorTargetCount = 1,
    .pColorTargets = &colorTarget,
    .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode = GPU_CULL_MODE_NONE,
    .frontFace = GPU_FRONT_FACE_CCW,
    .multisample = {
      .sampleCount = 1,
      .sampleMask = UINT32_MAX
    }
  };
  if (GPUCreateRenderPipeline(_device, &pipelineInfo, &_pipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create mesh pipeline");
    return NO;
  }

  return YES;
}

- (void)renderFrame {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment color;
  GPURenderPassCreateInfo      passInfo;
  GPUResult                    result;

  if (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) return;
  if (!GPUSampleRecoverSwapchain(_swapchain, _view)) return;

  frame = GPUBeginFrame(_swapchain);
  if (!frame) return;

  cmdb = NULL;
  pass = NULL;
  result = GPU_OK;
  if (GPUAcquireCommandBuffer(_queue, "mesh-frame", &cmdb) != GPU_OK || !cmdb)
    goto cleanup;
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         MeshTriangleFrameComplete);
  }

  color = (GPURenderPassColorAttachment){
    .view = GPUFrameGetTargetView(frame),
    .loadOp = GPU_LOAD_OP_CLEAR,
    .storeOp = GPU_STORE_OP_STORE,
    .clearColor.float32 = {0.02f, 0.02f, 0.025f, 1.0f}
  };
  passInfo = (GPURenderPassCreateInfo){
    .label = "mesh-triangle-pass",
    .colorAttachmentCount = 1,
    .pColorAttachments = &color
  };
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) goto cleanup;

  GPUBindRenderPipeline(pass, _pipeline);
  GPUDrawMeshEXT(pass, 1, 1, 1);
  GPUEndRenderPass(pass);
  pass = NULL;

  result = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (result != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", result);
    return;
  }

  _submittedFrames++;
  if (!GPUSampleCheckZeroAlloc(_device,
                               (uint32_t)_submittedFrames,
                               _assertZeroAlloc,
                               "GPU Metal mesh triangle")) {
    _statsFailed = YES;
    _terminating = YES;
    [NSApp terminate:nil];
    return;
  }
  if (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) {
    [_timer invalidate];
    _timer = nil;
  }
  return;

cleanup:
  if (pass) GPUEndRenderPass(pass);
  GPUEndFrame(frame);
}

- (void)frameCompleted {
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_completedFrames++;
    if (self->_exitAfterFrames > 0 &&
        self->_completedFrames >= self->_exitAfterFrames &&
        !self->_terminating) {
      self->_terminating = YES;
      [self->_timer invalidate];
      self->_timer = nil;
      [NSApp terminate:nil];
    }
  });
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  if (!_terminating) [self renderFrame];
}

- (void)cleanupGPU {
  if (_pipeline) GPUDestroyRenderPipeline(_pipeline);
  if (_shaderLayout) GPUDestroyShaderLayout(_shaderLayout);
  if (_library) GPUDestroyShaderLibrary(_library);
  if (_swapchain) GPUDestroySwapchain(_swapchain);
  if (_surface) GPUDestroySurface(_surface);
  if (_device) GPUDestroyDevice(_device);
  if (_instance) GPUDestroyInstance(_instance);

  _pipeline = NULL;
  _shaderLayout = NULL;
  _library = NULL;
  _swapchain = NULL;
  _surface = NULL;
  _device = NULL;
  _queue = NULL;
  _adapter = NULL;
  _instance = NULL;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  const char *exitAfterFrames;

  (void)notification;
  if (![self setupWindow] || ![self setupGPU]) {
    [NSApp terminate:nil];
    return;
  }

  exitAfterFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitAfterFrames && exitAfterFrames[0]) {
    _exitAfterFrames = MAX(1, strtol(exitAfterFrames, NULL, 10));
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

- (BOOL)statsFailed {
  return _statsFailed;
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

- (void)windowDidResize:(NSNotification *)notification {
  uint32_t width;
  uint32_t height;

  (void)notification;
  if (!_swapchain || _terminating) return;

  width  = (uint32_t)_view.bounds.size.width;
  height = (uint32_t)_view.bounds.size.height;
  if (width > 0u && height > 0u &&
      GPUResizeSwapchain(_swapchain, width, height) == GPU_OK) {
    [self renderFrame];
  }
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [_timer invalidate];
  _timer = nil;
  [self cleanupGPU];
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
    result = [delegate statsFailed] ? 1 : 0;
  }
  return result;
}
