#import <AppKit/AppKit.h>
#import <mach-o/dyld.h>

#import "../../include/gpu/gpu.h"

typedef struct TriangleVertex {
  float position[2];
  float color[4];
} TriangleVertex;

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
  GPUFunction *_vertFunc;
  GPUFunction *_fragFunc;
  GPUVertexDescriptor *_vertexDesc;
  GPURenderPipeline *_pipeline;
  GPURenderPipelineState *_renderState;
  GPUBuffer *_vertexBuffer;
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

  _window.title = @"GPU + USL Hello Triangle";
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

  GPUSwitchGPUApi(GPU_BACKEND_METAL);

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

  _queue = GPUGetCommandQueue(_device, GPU_QUEUE_GRAPHICS_BIT);
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

  _vertFunc = GPUShaderFunction(_library, "tri_vs");
  _fragFunc = GPUShaderFunction(_library, "tri_fs");
  if (!_vertFunc || !_fragFunc) {
    NSLog(@"GPU: failed to lookup shader entry points");
    return NO;
  }

  _vertexDesc = GPUNewVertexDesc();
  GPUAttrib(_vertexDesc, 0, GPUFloat2, offsetof(TriangleVertex, position), 0);
  GPUAttrib(_vertexDesc, 1, GPUFloat4, offsetof(TriangleVertex, color), 0);
  GPULayout(_vertexDesc, 0, sizeof(TriangleVertex), 1, GPUPerVertex);

  _pipeline = GPUNewRenderPipeline(GPUPixelFormatBGRA8Unorm_sRGB);
  GPUSetFunction(_pipeline, _vertFunc, GPU_FUNCTION_VERT);
  GPUSetFunction(_pipeline, _fragFunc, GPU_FUNCTION_FRAG);
  GPUSetVertexDesc(_pipeline, _vertexDesc);
  GPUColorFormat(_pipeline, 0, GPUPixelFormatBGRA8Unorm_sRGB);

  _renderState = GPUNewRenderState(_device, _pipeline);
  if (!_renderState) {
    NSLog(@"GPU: failed to create render state");
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

  return YES;
}

- (void)renderFrame {
  GPUFrame *frame;
  GPUCommandBuffer *cmdb;
  GPURenderPassDesc *pass;
  GPURenderCommandEncoder *encoder;

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  cmdb = GPUNewCommandBuffer(_queue, NULL, NULL);
  if (!cmdb) {
    GPUEndFrame(frame);
    return;
  }

  pass = GPUBeginRenderPass(frame->target);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  encoder = GPUNewRenderCommandEncoder(cmdb, pass);
  if (!encoder) {
    GPUEndFrame(frame);
    return;
  }

  GPUSetFrontFace(encoder, GPUWindingCounterClockwise);
  GPUSetCullMode(encoder, GPUCullModeNone);
  GPUSetRenderState(encoder, _renderState);
  GPUSetVertexBuffer(encoder, _vertexBuffer, 0, 0);
  gpuDrawPrimitives(encoder, GPUPrimitiveTypeTriangle, 0, 3);
  GPUEndEncoding(encoder);
  GPUFinishFrame(cmdb, frame);
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
