#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <math.h>
#import <mach-o/dyld.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>

#import "../../include/gpu/gpu.h"

typedef struct TriangleVertex {
  float position[2];
  float color[4];
} TriangleVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

static const TriangleVertex kTriangleVertices[] = {
  { {  0.0f,  0.65f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
  { { -0.7f, -0.65f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
  { {  0.7f, -0.65f }, { 0.2f, 0.4f, 1.0f, 1.0f } },
};

static GPUAdapter *
SelectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter = NULL;
  uint32_t adapterCount = 1;
  GPUResult result;

  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

@interface TriangleApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow *_window;
  NSView *_view;

  GPUInstance *_instance;
  GPUAdapter *_adapter;
  GPUDevice *_device;
  GPUQueue        *_queue;
  GPUSurface *_surface;
  GPUSwapchain *_swapchain;
  GPUShaderLibrary *_library;
  GPURenderPipeline *_pipeline;
  GPUBuffer *_vertexBuffer;
  GPUBuffer *_fragmentUniformBuffer;
  GPUBindGroupLayout *_fragmentLayout;
  GPUPipelineLayout *_pipelineLayout;
  GPUBindGroup *_fragmentGroup;
  NSTimer *_timer;
  NSTimeInterval _animationStart;
  NSInteger _exitAfterFrames;
  NSInteger _submittedFrames;
  NSInteger _completedFrames;
  BOOL _terminating;
}
- (void)frameCompleted;
@end

static void
TriangleFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  TriangleApp *app = (__bridge TriangleApp *)sender;
  [app frameCompleted];
}

@implementation TriangleApp

- (BOOL)setupWindow {
  NSRect frame = NSMakeRect(0, 0, 960, 640);

  _window = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled |
                                                   NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  if (!_window) {
    return NO;
  }

  _window.title = @"GPU Manual Triangle Tint";
  _window.delegate = self;

  _view = [[NSView alloc] initWithFrame:frame];
  if (!_view) {
    return NO;
  }

  [_window setContentView:_view];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  return YES;
}

- (BOOL)setupGPU {
  NSString *shaderText;
  NSString *shaderPath;
  NSString *executablePath;
  NSString *sampleDir;

  GPUSwapchainCreateInfo swapchainInfo = {0};
  GPUShaderLibraryCreateInfo shaderInfo = {0};

  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  _adapter = SelectAdapter(_instance);
  if (!_adapter) {
    NSLog(@"GPU: failed to get adapter");
    return NO;
  }

  _device = GPUCreateDeviceWithDefaultQueues(_adapter);
  if (!_device) {
    NSLog(@"GPU: failed to create device");
    return NO;
  }

  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0);
  if (!_queue) {
    NSLog(@"GPU: failed to get command queue");
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

  swapchainInfo.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO;
  swapchainInfo.chain.structSize = sizeof(swapchainInfo);
  swapchainInfo.label            = "triangle-manual-swapchain";
  swapchainInfo.surface          = _surface;
  swapchainInfo.width            = (uint32_t)_view.bounds.size.width;
  swapchainInfo.height           = (uint32_t)_view.bounds.size.height;
  swapchainInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  swapchainInfo.imageCount       = 3;
  swapchainInfo.presentMode      = GPU_PRESENT_MODE_FIFO;

  if (GPUCreateSwapchain(_device, &swapchainInfo, &_swapchain) != GPU_OK) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }

  {
    uint32_t sizeBytes = 0;
    _NSGetExecutablePath(NULL, &sizeBytes);
    char *buffer = malloc(sizeBytes);
    if (!buffer) {
      return NO;
    }
    if (_NSGetExecutablePath(buffer, &sizeBytes) != 0) {
      free(buffer);
      return NO;
    }
    executablePath = [[NSFileManager defaultManager]
                      stringWithFileSystemRepresentation:buffer
                      length:strlen(buffer)];
    free(buffer);
  }

  sampleDir = [executablePath stringByDeletingLastPathComponent];
  shaderPath = [sampleDir stringByAppendingPathComponent:@"triangle.usl.metal"];
  shaderText = [NSString stringWithContentsOfFile:shaderPath
                                         encoding:NSUTF8StringEncoding
                                            error:nil];
  if (!shaderText) {
    NSLog(@"GPU: failed to load shader source at %@", shaderPath);
    return NO;
  }

  shaderInfo.label = "triangle.usl.metal";
  shaderInfo.chain.sType = GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO;
  shaderInfo.chain.structSize = sizeof(shaderInfo);
  shaderInfo.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  shaderInfo.sourceData = shaderText.UTF8String;
  shaderInfo.sourceSize = (uint64_t)[shaderText lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
  shaderInfo.sourcePathHint = shaderPath.UTF8String;
  if (GPUCreateShaderLibrary(_device, &shaderInfo, &_library) != 0) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  GPUBindGroupLayoutEntry group0Entries[] = {
    {
      .binding = 0,
      .bindingType = GPU_BINDING_UNIFORM_BUFFER,
      .visibility = GPU_SHADER_STAGE_FRAGMENT_BIT,
      .arrayCount = 1,
      .hasDynamicOffset = false
    }
  };
  GPUBindGroupLayoutCreateInfo group0LayoutInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupLayoutCreateInfo) },
    .label = "triangle-manual-group0-layout",
    .entryCount = 1,
    .pEntries = group0Entries
  };
  if (GPUCreateBindGroupLayout(_device, &group0LayoutInfo, &_fragmentLayout) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind layout");
    return NO;
  }

  GPUBindGroupLayout *layouts[] = { _fragmentLayout };
  GPUPipelineLayoutCreateInfo pipelineLayoutInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
               .structSize = sizeof(GPUPipelineLayoutCreateInfo) },
    .label = "triangle-manual-pipeline-layout",
    .bindGroupLayoutCount = 1,
    .ppBindGroupLayouts = layouts,
    .pushConstantSizeBytes = 0,
    .pushConstantStages = 0
  };
  if (GPUCreatePipelineLayout(_device, &pipelineLayoutInfo, &_pipelineLayout) != GPU_OK) {
    NSLog(@"GPU: failed to create pipeline layout");
    return NO;
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT32X2, .offset = offsetof(TriangleVertex, position) },
    { .shaderLocation = 1, .format = GPU_VERTEX_FORMAT_FLOAT32X4, .offset = offsetof(TriangleVertex, color) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(TriangleVertex),
      .stepMode = GPU_VERTEX_STEP_MODE_VERTEX,
      .attributeCount = 2,
      .pAttributes = vertexAttrs
    }
  };
  GPUColorTargetState colorTargets[] = {
    {
      .format = GPU_FORMAT_BGRA8_UNORM,
      .blend = {
        .enabled = false,
        .writeMask = GPU_COLOR_WRITE_ALL
      }
    }
  };
  GPUMultisampleState multisample = {
    .sampleCount = 1,
    .sampleMask = 0xffffffffu,
    .alphaToCoverageEnable = false
  };

  GPURenderPipelineCreateInfo pipelineInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
               .structSize = sizeof(GPURenderPipelineCreateInfo) },
    .label = "triangle-manual-pipeline",
    .layout = _pipelineLayout,
    .library = _library,
    .vertexEntry = "tri_vs",
    .fragmentEntry = "tri_fs",
    .vertex = {
      .bufferLayoutCount = 1,
      .pBufferLayouts = vertexBuffers
    },
    .colorTargetCount = 1,
    .pColorTargets = colorTargets,
    .depthStencilFormat = GPU_FORMAT_UNDEFINED,
    .pDepthStencilState = NULL,
    .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode = GPU_CULL_MODE_NONE,
    .frontFace = GPU_FRONT_FACE_CCW,
    .multisample = multisample
  };
  if (GPUCreateRenderPipeline(_device, &pipelineInfo, &_pipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create render pipeline");
    return NO;
  }

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "triangle-manual-vertices",
    .sizeBytes = sizeof(kTriangleVertices),
    .usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &vertexBufferInfo, &_vertexBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create vertex buffer");
    return NO;
  }

  if (GPUQueueWriteBuffer(_queue,
                          _vertexBuffer,
                          0,
                          kTriangleVertices,
                          sizeof(kTriangleVertices)) != GPU_OK) {
    NSLog(@"GPU: failed to upload vertex buffer");
    return NO;
  }

  GPUBufferCreateInfo uniformBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "triangle-manual-fragment-uniforms",
    .sizeBytes = sizeof(FragmentUniforms),
    .usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &uniformBufferInfo, &_fragmentUniformBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment uniform buffer");
    return NO;
  }

  GPUBindGroupEntry group0Bindings[] = {
    {
      .binding = 0,
      .bindingType = GPU_BINDING_UNIFORM_BUFFER,
      .buffer = {
        .buffer = _fragmentUniformBuffer,
        .offset = 0,
        .size = sizeof(FragmentUniforms)
      }
    }
  };
  GPUBindGroupCreateInfo group0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "triangle-manual-group0",
    .layout = _fragmentLayout,
    .entryCount = 1,
    .pEntries = group0Bindings
  };
  if (GPUCreateBindGroup(_device, &group0Info, &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind group");
    return NO;
  }

  return YES;
}

- (void)updateFragmentUniforms {
  FragmentUniforms uniforms;
  float time;

  time = (float)(CACurrentMediaTime() - _animationStart);
  uniforms.tint[0] = 0.6f + 0.4f * sinf(time * 1.1f);
  uniforms.tint[1] = 0.6f + 0.4f * sinf(time * 1.7f + 2.1f);
  uniforms.tint[2] = 0.6f + 0.4f * sinf(time * 1.3f + 4.2f);
  uniforms.tint[3] = 1.0f;
  GPUQueueWriteBuffer(_queue,
                      _fragmentUniformBuffer,
                      0,
                      &uniforms,
                      sizeof(uniforms));
}

- (void)renderFrame {
  GPUFrame *frame = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUResult submitResult = GPU_OK;
  GPURenderPassEncoder *encoder = NULL;
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBuffer = {0};

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  if (GPUAcquireCommandBuffer(_queue, "main-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb, (__bridge void *)self, TriangleFrameComplete);
  }

  color.view = GPUFrameGetTargetView(frame);
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.0f;
  color.clearColor.float32[1] = 0.0f;
  color.clearColor.float32[2] = 0.0f;
  color.clearColor.float32[3] = 1.0f;

  rp.label = "triangle-manual-pass";
  rp.colorAttachmentCount = 1;
  rp.pColorAttachments = &color;

  encoder = GPUBeginRenderPass(cmdb, &rp);
  if (!encoder) {
    goto cleanup;
  }

  [self updateFragmentUniforms];

  vertexBuffer.buffer = _vertexBuffer;
  vertexBuffer.offset = 0;

  GPUBindRenderPipeline(encoder, _pipeline);
  GPUBindVertexBuffers(encoder, 0, 1, &vertexBuffer);
  GPUBindRenderGroup(encoder, 0, _fragmentGroup, 0, NULL);
  GPUDraw(encoder, 3, 1, 0, 0);
  GPUEndRenderPass(encoder);
  encoder = NULL;
  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  } else if (_exitAfterFrames > 0) {
    _submittedFrames++;
    if (_submittedFrames >= _exitAfterFrames) {
      [_timer invalidate];
      _timer = nil;
    }
  }

cleanup:
  if (encoder) {
    GPUEndRenderPass(encoder);
  }
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
  if (_terminating) {
    return;
  }
  [self renderFrame];
}

- (void)cleanupGPU {
  if (_fragmentGroup) {
    GPUDestroyBindGroup(_fragmentGroup);
    _fragmentGroup = NULL;
  }
  if (_pipeline) {
    GPUDestroyRenderPipeline(_pipeline);
    _pipeline = NULL;
  }
  if (_fragmentUniformBuffer) {
    GPUDestroyBuffer(_fragmentUniformBuffer);
    _fragmentUniformBuffer = NULL;
  }
  if (_vertexBuffer) {
    GPUDestroyBuffer(_vertexBuffer);
    _vertexBuffer = NULL;
  }
  if (_pipelineLayout) {
    GPUDestroyPipelineLayout(_pipelineLayout);
    _pipelineLayout = NULL;
  }
  if (_fragmentLayout) {
    GPUDestroyBindGroupLayout(_fragmentLayout);
    _fragmentLayout = NULL;
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
    _queue = NULL;
  }
  if (_instance) {
    GPUDestroyInstance(_instance);
    _instance = NULL;
    _adapter = NULL;
  }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;

  if (![self setupWindow]) {
    [NSApp terminate:nil];
    return;
  }

  if (![self setupGPU]) {
    [NSApp terminate:nil];
    return;
  }

  const char *exitAfterFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitAfterFrames && exitAfterFrames[0] != '\0') {
    _exitAfterFrames = strtol(exitAfterFrames, NULL, 10);
    if (_exitAfterFrames < 1) {
      _exitAfterFrames = 1;
    }
  }

  _animationStart = CACurrentMediaTime();
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

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [_timer invalidate];
  _timer = nil;
  [self cleanupGPU];
}

@end

int main(int argc, const char * argv[]) {
  @autoreleasepool {
    TriangleApp *delegate;

    (void)argc;
    (void)argv;

    [NSApplication sharedApplication];
    delegate = [TriangleApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
  }
  return 0;
}
