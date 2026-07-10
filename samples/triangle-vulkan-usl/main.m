#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>

#import "../common/SampleApp.h"
#import "../common/SampleUSL.h"

@interface TriangleVulkanApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow           *_window;
  NSView             *_view;
  GPUInstance        *_instance;
  GPUAdapter         *_adapter;
  GPUDevice          *_device;
  GPUCommandQueue    *_queue;
  GPUSurface         *_surface;
  GPUSwapchain       *_swapchain;
  GPUShaderLibrary   *_library;
  GPUShaderLayout    *_shaderLayout;
  GPURenderPipeline  *_pipeline;
  NSTimer            *_timer;
  NSInteger           _exitAfterFrames;
  NSInteger           _submittedFrames;
  NSInteger           _completedFrames;
  BOOL                _terminating;
}
- (void)frameCompleted;
@end

static void
TriangleVulkanFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  TriangleVulkanApp *app;

  (void)cmdb;
  app = (__bridge TriangleVulkanApp *)sender;
  [app frameCompleted];
}

@implementation TriangleVulkanApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU Vulkan USL Triangle",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupGPU {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUPipelineLayout     *layout;
  GPUColorTargetState    colorTarget = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  uint32_t adapterCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create Vulkan instance");
    return NO;
  }

  adapterCount = 1u;
  if (GPUEnumerateAdapters(_instance, &adapterCount, &_adapter) != GPU_OK ||
      !_adapter) {
    NSLog(@"GPU: failed to get Vulkan adapter");
    return NO;
  }

  _device = GPUCreateDeviceWithDefaultQueues(_adapter);
  _queue  = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_device || !_queue) {
    NSLog(@"GPU: failed to create Vulkan device or queue");
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)_view,
                                        GPU_SURFACE_APPLE_NSVIEW,
                                        _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    NSLog(@"GPU: failed to create Vulkan surface");
    return NO;
  }

  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create Vulkan swapchain");
    return NO;
  }

  if (!GPUSampleLoadUSL(_device,
                        @"triangle.us",
                        0u,
                        &_library,
                        &_shaderLayout)) {
    return NO;
  }

  layout = _shaderLayout->pipelineLayout;
  colorTarget.format          = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;

  pipelineInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize   = sizeof(pipelineInfo);
  pipelineInfo.label              = "triangle-vulkan-usl-pipeline";
  pipelineInfo.layout             = layout;
  pipelineInfo.library            = _library;
  pipelineInfo.vertexEntry        = "tri_vs";
  pipelineInfo.fragmentEntry      = "tri_fs";
  pipelineInfo.colorTargetCount   = 1u;
  pipelineInfo.pColorTargets      = &colorTarget;
  pipelineInfo.primitiveTopology  = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode           = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace          = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = 0xffffffffu;
  if (GPUCreateRenderPipeline(_device, &pipelineInfo, &_pipeline) != GPU_OK ||
      !_pipeline) {
    NSLog(@"GPU: failed to create Vulkan render pipeline");
    return NO;
  }

  return YES;
}

- (void)renderFrame {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUResult                     result;

  if (_terminating ||
      (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames)) {
    return;
  }

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(_queue,
                              "triangle-vulkan-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         TriangleVulkanFrameComplete);
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.02f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.035f;
  color.clearColor.float32[3] = 1.0f;

  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label              = "triangle-vulkan-usl-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments  = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, _pipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(_queue, cmdb, frame);
  if (result != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", result);
    return;
  }
  _submittedFrames++;
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
  [self renderFrame];
}

- (void)cleanupGPU {
  if (_pipeline) {
    GPUDestroyRenderPipeline(_pipeline);
    _pipeline = NULL;
  }
  if (_shaderLayout) {
    GPUDestroyShaderLayout(_shaderLayout);
    _shaderLayout = NULL;
  }
  if (_library) {
    GPUDestroyShaderLibrary(_library);
    _library = NULL;
  }
  if (_swapchain) {
    GPUDestroySwapchain(_swapchain);
    _swapchain = NULL;
  }
  if (_surface) {
    GPUDestroySurface(_surface);
    _surface = NULL;
  }
  if (_device) {
    GPUDestroyDevice(_device);
    _device = NULL;
    _queue  = NULL;
  }
  if (_instance) {
    GPUDestroyInstance(_instance);
    _instance = NULL;
  }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  const char *exitAfterFrames;

  (void)notification;
  if (![self setupWindow] || ![self setupGPU]) {
    [NSApp terminate:nil];
    return;
  }

  exitAfterFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitAfterFrames && exitAfterFrames[0] != '\0') {
    _exitAfterFrames = strtol(exitAfterFrames, NULL, 10);
    if (_exitAfterFrames < 1) {
      _exitAfterFrames = 1;
    }
  }

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

- (void)windowDidResize:(NSNotification *)notification {
  uint32_t width;
  uint32_t height;

  (void)notification;
  if (!_swapchain || _terminating) {
    return;
  }

  width  = (uint32_t)_view.bounds.size.width;
  height = (uint32_t)_view.bounds.size.height;
  if (width > 0u && height > 0u &&
      GPUResizeSwapchain(_swapchain, width, height) == GPU_OK) {
    [self renderFrame];
  }
}

@end

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    TriangleVulkanApp *delegate;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    delegate = [TriangleVulkanApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
  }
  return 0;
}
