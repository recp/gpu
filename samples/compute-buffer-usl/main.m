#import <AppKit/AppKit.h>
#include <stddef.h>
#include <string.h>
#import <mach-o/dyld.h>

#import "../../include/gpu/gpu.h"

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

@interface ComputeBufferUSLApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow *_window;
  NSView *_view;

  GPUPhysicalDevice *_physicalDevice;
  GPUDevice *_device;
  GPUCommandQueue *_queue;
  GPUSurface *_surface;
  GPUSwapchain *_swapchain;
  GPUShaderLibrary *_library;
  GPUShaderLayout *_shaderLayout;
  GPUComputePipeline *_computePipeline;
  GPURenderPipeline *_renderPipeline;
  GPUBuffer *_vertexBuffer;
  GPUBindGroup *_bindGroup;
  NSTimer *_timer;
}
@end

@implementation ComputeBufferUSLApp

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

  _window.title = @"GPU USL Compute Buffer";
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

- (NSString *)sampleDir {
  uint32_t sizeBytes = 0;
  _NSGetExecutablePath(NULL, &sizeBytes);

  char *buffer = malloc(sizeBytes);
  if (!buffer) {
    return nil;
  }

  if (_NSGetExecutablePath(buffer, &sizeBytes) != 0) {
    free(buffer);
    return nil;
  }

  NSString *executablePath = [[NSFileManager defaultManager]
                              stringWithFileSystemRepresentation:buffer
                              length:strlen(buffer)];
  free(buffer);
  return [executablePath stringByDeletingLastPathComponent];
}

- (BOOL)setupGPU {
  NSString *sampleDir;
  NSString *bytecodePath;
  NSData *bytecodeData;

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
    NSLog(@"GPU: failed to get queue");
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

  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }

  sampleDir = [self sampleDir];
  if (!sampleDir) {
    return NO;
  }

  bytecodePath = [sampleDir stringByAppendingPathComponent:@"compute_buffer.us"];
  bytecodeData = [NSData dataWithContentsOfFile:bytecodePath];
  if (!bytecodeData) {
    NSLog(@"GPU: failed to load USL bytecode at %@", bytecodePath);
    return NO;
  }

  if (GPUCreateShaderLibraryFromUSL(_device,
                                    bytecodeData.bytes,
                                    (uint64_t)bytecodeData.length,
                                    &_library) != GPU_OK) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  if (GPUCreateShaderLayout(_device, _library, &_shaderLayout) != GPU_OK ||
      !_shaderLayout ||
      _shaderLayout->bindGroupLayoutCount != 1 ||
      !_shaderLayout->bindGroupLayouts[0] ||
      !_shaderLayout->pipelineLayout) {
    NSLog(@"GPU: failed to create shader layout");
    return NO;
  }

  GPUComputePipelineCreateInfo computeInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
               .structSize = sizeof(GPUComputePipelineCreateInfo) },
    .label = "compute-buffer-usl-fill-vertices",
    .layout = _shaderLayout->pipelineLayout,
    .library = _library,
    .entryPoint = "fill_vertices"
  };
  if (GPUCreateComputePipeline(_device, &computeInfo, &_computePipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create compute pipeline");
    return NO;
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT4, .offset = offsetof(GeneratedVertex, position) },
    { .shaderLocation = 1, .format = GPU_VERTEX_FORMAT_FLOAT4, .offset = offsetof(GeneratedVertex, color) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(GeneratedVertex),
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

  GPURenderPipelineCreateInfo renderInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
               .structSize = sizeof(GPURenderPipelineCreateInfo) },
    .label = "compute-buffer-usl-render-pipeline",
    .layout = _shaderLayout->pipelineLayout,
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
  if (GPUCreateRenderPipeline(_device, &renderInfo, &_renderPipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create render pipeline");
    return NO;
  }

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-buffer-usl-vertices",
    .sizeBytes = sizeof(GeneratedVertex) * 3u,
    .usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_STORAGE
  };
  if (GPUCreateBuffer(_device, &vertexBufferInfo, &_vertexBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create vertex/storage buffer");
    return NO;
  }

  GPUBindGroupEntry groupEntry = {0};
  groupEntry.binding = 0;
  groupEntry.bindingType = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = _vertexBuffer;
  groupEntry.buffer.offset = 0;
  groupEntry.buffer.size = vertexBufferInfo.sizeBytes;

  GPUBindGroupCreateInfo set0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "compute-buffer-usl-set0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1,
    .pEntries = &groupEntry
  };
  if (GPUCreateBindGroup(_device, &set0Info, &_bindGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create bind group");
    return NO;
  }

  return YES;
}

- (void)renderFrame {
  GPUFrame *frame = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUComputePassEncoder *compute = NULL;
  GPURenderPassEncoder *render = NULL;
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBuffer = {0};
  GPUResult submitResult = GPU_OK;

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  if (GPUAcquireCommandBuffer(_queue, "compute-buffer-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }

  compute = GPUBeginComputePass(cmdb, "compute-buffer-usl-fill");
  if (!compute) {
    goto cleanup;
  }
  GPUBindComputePipeline(compute, _computePipeline);
  GPUBindComputeGroup(compute, 0, _bindGroup, 0, NULL);
  GPUDispatch(compute, 1, 1, 1);
  GPUEndComputePass(compute);
  compute = NULL;

  color.view = GPUFrameGetTargetView(frame);
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.03f;
  color.clearColor.float32[1] = 0.03f;
  color.clearColor.float32[2] = 0.04f;
  color.clearColor.float32[3] = 1.0f;

  rp.label = "compute-buffer-usl-render-pass";
  rp.colorAttachmentCount = 1;
  rp.pColorAttachments = &color;

  render = GPUBeginRenderPass(cmdb, &rp);
  if (!render) {
    goto cleanup;
  }

  vertexBuffer.buffer = _vertexBuffer;
  vertexBuffer.offset = 0;

  GPUBindRenderPipeline(render, _renderPipeline);
  GPUBindVertexBuffers(render, 0, 1, &vertexBuffer);
  GPUDraw(render, 3, 1, 0, 0);
  GPUEndRenderPass(render);
  render = NULL;

  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  }

cleanup:
  if (compute) {
    GPUEndComputePass(compute);
  }
  if (render) {
    GPUEndRenderPass(render);
  }
  GPUEndFrame(frame);
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
}

- (void)cleanupGPU {
  if (_bindGroup) {
    GPUDestroyBindGroup(_bindGroup);
    _bindGroup = NULL;
  }
  if (_renderPipeline) {
    GPUDestroyRenderPipeline(_renderPipeline);
    _renderPipeline = NULL;
  }
  if (_computePipeline) {
    GPUDestroyComputePipeline(_computePipeline);
    _computePipeline = NULL;
  }
  if (_vertexBuffer) {
    GPUDestroyBuffer(_vertexBuffer);
    _vertexBuffer = NULL;
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
    _queue = NULL;
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
  (void)notification;
  [self renderFrame];
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
  @autoreleasepool {
    (void)argc;
    (void)argv;

    NSApplication *app = [NSApplication sharedApplication];
    ComputeBufferUSLApp *delegate = [[ComputeBufferUSLApp alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
