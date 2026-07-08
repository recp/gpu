#import <AppKit/AppKit.h>
#include <math.h>
#import <mach-o/dyld.h>
#import <QuartzCore/QuartzCore.h>

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

@interface TriangleApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow *_window;
  NSView *_view;

  GPUPhysicalDevice *_physicalDevice;
  GPUDevice *_device;
  GPUCommandQueue *_queue;
  GPUSurface *_surface;
  GPUSwapChain *_swapchain;
  GPULibrary *_library;
  GPURenderPipeline *_pipeline;
  GPUBuffer *_vertexBuffer;
  GPUBuffer *_fragmentUniformBuffer;
  GPUBindGroupLayout *_fragmentLayout;
  GPUPipelineLayout *_pipelineLayout;
  GPUBindGroup *_fragmentGroup;
  NSTimer *_timer;
  NSTimeInterval _animationStart;
}
@end

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

  GPUShaderLibraryCreateInfo shaderInfo;
  GPUExtent2D size;

  _physicalDevice = GPUGetAutoSelectedPhysicalDevice(NULL);
  if (!_physicalDevice) {
    NSLog(@"GPU: failed to get physical device");
    return NO;
  }

  _device = GPUCreateDeviceWithDefaultQueues(_physicalDevice);
  if (!_device) {
    NSLog(@"GPU: failed to create device");
    return NO;
  }

  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0);
  if (!_queue) {
    NSLog(@"GPU: failed to get command queue");
    return NO;
  }

  _surface = GPUCreateSurface(NULL,
                              _physicalDevice,
                              (__bridge void *)_view,
                              GPU_SURFACE_APPLE_NSVIEW,
                              _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    NSLog(@"GPU: failed to create surface");
    return NO;
  }

  size.width = (uint32_t)_view.bounds.size.width;
  size.height = (uint32_t)_view.bounds.size.height;
  _swapchain = GPUCreateSwapChain(_device, _queue, _surface, size, true);
  if (!_swapchain) {
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
  shaderInfo.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  shaderInfo.sourceData = shaderText.UTF8String;
  shaderInfo.sourceSize = (uint64_t)[shaderText lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
  shaderInfo.sourcePathHint = shaderPath.UTF8String;
  if (GPUCreateShaderLibrary(_device, &shaderInfo, (GPUShaderLibrary **)&_library) != 0) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  GPUBindGroupLayoutEntry set0Entries[] = {
    {
      .binding = 0,
      .bindingType = GPU_BINDING_UNIFORM_BUFFER,
      .visibility = GPU_SHADER_STAGE_FRAGMENT_BIT,
      .arrayCount = 1,
      .hasDynamicOffset = false
    }
  };
  GPUBindGroupLayoutCreateInfo set0LayoutInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupLayoutCreateInfo) },
    .label = "triangle-manual-set0-layout",
    .entryCount = 1,
    .pEntries = set0Entries
  };
  if (GPUCreateBindGroupLayout(_device, &set0LayoutInfo, &_fragmentLayout) != GPU_OK) {
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
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT2, .offset = offsetof(TriangleVertex, position) },
    { .shaderLocation = 1, .format = GPU_VERTEX_FORMAT_FLOAT4, .offset = offsetof(TriangleVertex, color) }
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
      .format = GPU_FORMAT_BGRA8_UNORM_SRGB,
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
    .library = (GPUShaderLibrary *)_library,
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

  _vertexBuffer = GPUNewBuffer(_device,
                               sizeof(kTriangleVertices),
                               GPUResourceStorageModeShared);
  if (!_vertexBuffer) {
    NSLog(@"GPU: failed to create vertex buffer");
    return NO;
  }

  memcpy((void *)gpuBufferContents(_vertexBuffer),
         kTriangleVertices,
         sizeof(kTriangleVertices));

  _fragmentUniformBuffer = GPUNewBuffer(_device,
                                        sizeof(FragmentUniforms),
                                        GPUResourceStorageModeShared);
  if (!_fragmentUniformBuffer) {
    NSLog(@"GPU: failed to create fragment uniform buffer");
    return NO;
  }

  GPUBindGroupEntry set0Bindings[] = {
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
  GPUBindGroupCreateInfo set0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "triangle-manual-set0",
    .layout = _fragmentLayout,
    .entryCount = 1,
    .pEntries = set0Bindings
  };
  if (GPUCreateBindGroup(_device, &set0Info, &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind group");
    return NO;
  }

  return YES;
}

- (void)updateFragmentUniforms {
  FragmentUniforms *uniforms;
  float time;

  uniforms = (FragmentUniforms *)gpuBufferContents(_fragmentUniformBuffer);
  if (!uniforms) {
    return;
  }

  time = (float)(CACurrentMediaTime() - _animationStart);
  uniforms->tint[0] = 0.6f + 0.4f * sinf(time * 1.1f);
  uniforms->tint[1] = 0.6f + 0.4f * sinf(time * 1.7f + 2.1f);
  uniforms->tint[2] = 0.6f + 0.4f * sinf(time * 1.3f + 4.2f);
  uniforms->tint[3] = 1.0f;
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

  GPUSetFrontFace(encoder, GPUWindingCounterClockwise);
  GPUSetCullMode(encoder, GPUCullModeNone);
  GPUBindRenderPipeline(encoder, _pipeline);
  GPUBindVertexBuffers(encoder, 0, 1, &vertexBuffer);
  GPUBindRenderGroup(encoder, 0, _fragmentGroup, 0, NULL);
  GPUDraw(encoder, 3, 1, 0, 0);
  GPUEndRenderPass(encoder);
  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  }

cleanup:
  GPUEndFrame(frame);
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
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

  _animationStart = CACurrentMediaTime();
  _timer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
                                   target:self
                                 selector:@selector(tick:)
                                 userInfo:nil
                                  repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];

  [self renderFrame];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  (void)sender;
  return YES;
}

- (void)windowDidResize:(NSNotification *)notification {
  (void)notification;
  [self renderFrame];
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [_timer invalidate];
  _timer = nil;
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
