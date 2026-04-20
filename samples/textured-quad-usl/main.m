#import <AppKit/AppKit.h>
#include <math.h>
#import <mach-o/dyld.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"

typedef struct QuadVertex {
  float position[4];
} QuadVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

static const QuadVertex kQuadVertices[] = {
  { { -0.8f, -0.8f, 0.0f, 1.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f } },
  { { -0.8f,  0.8f, 0.0f, 1.0f } },
  { {  0.8f,  0.8f, 0.0f, 1.0f } },
};

static const uint8_t kCheckerPixels[] = {
  255,   0,   0, 255,    0, 255,   0, 255,
    0,   0, 255, 255,  255, 255, 255, 255,
};

@interface TexturedQuadApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
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
  GPUBuffer *_fragmentUniformBuffer;
  GPUTexture *_texture;
  GPUSampler *_sampler;
  GPUBindGroupLayout *_fragmentLayout;
  GPUBindGroup *_fragmentGroup;
  NSTimer *_timer;
  NSTimeInterval _animationStart;
}
@end

@implementation TexturedQuadApp

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

  _window.title = @"GPU USL Textured Quad";
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
  NSString *executablePath;
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
  executablePath = [[NSFileManager defaultManager]
                    stringWithFileSystemRepresentation:buffer
                    length:strlen(buffer)];
  free(buffer);
  return [executablePath stringByDeletingLastPathComponent];
}

- (BOOL)setupTexture {
  GPUTextureDesc desc = {0};
  id<MTLTexture> texture;
  MTLRegion region;

  desc.width = 2;
  desc.height = 2;
  desc.depth = 1;
  desc.mipmapLevelCount = 1;
  desc.format = GPUPixelFormatRGBA8Unorm;
  desc.storageMode = GPUStorageModeManaged;
  desc.usage = GPUTextureUsageShaderRead;

  _texture = GPUNewTextureWith(_device, &desc);
  if (!_texture) {
    NSLog(@"GPU: failed to create texture");
    return NO;
  }

  texture = (__bridge id<MTLTexture>)_texture;
  region = MTLRegionMake2D(0, 0, 2, 2);
  [texture replaceRegion:region mipmapLevel:0 withBytes:kCheckerPixels bytesPerRow:8];

  _sampler = GPUCreateSampler(_device, false);
  if (!_sampler) {
    NSLog(@"GPU: failed to create sampler");
    return NO;
  }

  return YES;
}

- (BOOL)setupGPU {
  NSString *bytecodePath;
  NSString *sampleDir;
  NSData   *bytecodeData;
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUBindGroupEntry groupEntries[3] = {0};
  uint32_t groupCount = 0;
  uint32_t layoutCount;
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

  sampleDir = [self sampleDir];
  if (!sampleDir) {
    return NO;
  }

  bytecodePath = [sampleDir stringByAppendingPathComponent:@"textured_quad.us"];
  bytecodeData = [NSData dataWithContentsOfFile:bytecodePath];
  if (!bytecodeData) {
    NSLog(@"GPU: failed to load USL bytecode at %@", bytecodePath);
    return NO;
  }

  if (GPUCreateShaderLibraryFromUSLBytecode(_device,
                                            bytecodeData.bytes,
                                            (uint64_t)bytecodeData.length,
                                            (GPUShaderLibrary **)&_library) != 0) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  _vertFunc = GPUShaderFunction(_library, "quad_vs");
  _fragFunc = GPUShaderFunction(_library, "quad_fs");
  if (!_vertFunc || !_fragFunc) {
    NSLog(@"GPU: failed to lookup shader entry points");
    return NO;
  }

  _vertexDesc = GPUNewVertexDesc();
  GPUAttrib(_vertexDesc, 0, GPUFloat4, offsetof(QuadVertex, position), 0);
  GPULayout(_vertexDesc, 0, sizeof(QuadVertex), 1, GPUPerVertex);

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
                               sizeof(kQuadVertices),
                               GPUResourceStorageModeShared);
  if (!_vertexBuffer) {
    NSLog(@"GPU: failed to create vertex buffer");
    return NO;
  }

  memcpy((void *)gpuBufferContents(_vertexBuffer),
         kQuadVertices,
         sizeof(kQuadVertices));

  _fragmentUniformBuffer = GPUNewBuffer(_device,
                                        sizeof(FragmentUniforms),
                                        GPUResourceStorageModeShared);
  if (!_fragmentUniformBuffer) {
    NSLog(@"GPU: failed to create fragment uniform buffer");
    return NO;
  }

  if (![self setupTexture]) {
    return NO;
  }

  if (GPUCreateBindGroupLayoutFromUSLBytecode(bytecodeData.bytes,
                                              (uint64_t)bytecodeData.length,
                                              "quad_fs",
                                              &_fragmentLayout) != 0) {
    NSLog(@"GPU: failed to create fragment bind layout");
    return NO;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(_fragmentLayout, &layoutCount);
  if (!layoutEntries || layoutCount != 3) {
    NSLog(@"GPU: unexpected bind layout extracted from USL bytecode");
    return NO;
  }

  for (uint32_t i = 0; i < layoutCount; i++) {
    groupEntries[groupCount].binding = layoutEntries[i].binding;
    groupEntries[groupCount].kind = layoutEntries[i].kind;
    switch (layoutEntries[i].kind) {
      case GPUBindKindTexture:
        groupEntries[groupCount].texture = _texture;
        break;
      case GPUBindKindSampler:
        groupEntries[groupCount].sampler = _sampler;
        break;
      case GPUBindKindBuffer:
        groupEntries[groupCount].buffer = _fragmentUniformBuffer;
        break;
      default:
        break;
    }
    groupCount++;
  }

  if (GPUCreateBindGroup(_fragmentLayout, groupEntries, groupCount, &_fragmentGroup) != 0) {
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
  uniforms->tint[0] = 0.75f + 0.25f * sinf(time * 1.1f);
  uniforms->tint[1] = 0.75f + 0.25f * sinf(time * 1.5f + 1.0f);
  uniforms->tint[2] = 0.75f + 0.25f * sinf(time * 1.9f + 2.0f);
  uniforms->tint[3] = 1.0f;
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

  [self updateFragmentUniforms];

  GPUSetFrontFace(encoder, GPUWindingCounterClockwise);
  GPUSetCullMode(encoder, GPUCullModeNone);
  GPUSetRenderState(encoder, _renderState);
  GPUSetVertexBuffer(encoder, _vertexBuffer, 0, 0);
  GPUBindRenderGroup(encoder, _fragmentGroup);
  gpuDrawPrimitives(encoder, GPUPrimitiveTypeTriangleStrip, 0, 4);
  GPUEndEncoding(encoder);
  GPUFinishFrame(cmdb, frame);
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
    TexturedQuadApp *delegate;

    (void)argc;
    (void)argv;

    [NSApplication sharedApplication];
    delegate = [TexturedQuadApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
  }
  return 0;
}
