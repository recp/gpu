#import <AppKit/AppKit.h>
#include <math.h>
#include <string.h>
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
  { { -0.8f,  0.8f, 0.0f, 1.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f } },
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
  GPUShaderLayout *_shaderLayout;
  GPURenderPipeline *_pipeline;
  GPUBuffer *_vertexBuffer;
  GPUBuffer *_fragmentUniformBuffer;
  GPUTexture *_texture;
  GPUSampler *_sampler;
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
  GPUShaderLibraryUSLInfo uslInfo;
  GPUBindGroupEntry groupEntries[3] = {0};
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

  if (GPUCreateShaderLibraryFromUSL(_device,
                                    bytecodeData.bytes,
                                    (uint64_t)bytecodeData.length,
                                    (GPUShaderLibrary **)&_library) != GPU_OK) {
    NSLog(@"GPU: failed to create shader library");
    return NO;
  }

  if (GPUGetShaderLibraryUSLInfo(_library, &uslInfo) != 0 ||
      uslInfo.abiVersion != GPU_SHADER_LIBRARY_USL_INFO_VERSION ||
      uslInfo.bytecodeSize != (uint64_t)bytecodeData.length ||
      uslInfo.bytecodeContentHash == 0 ||
      uslInfo.targetAtomCount == 0 ||
      uslInfo.targetAtomHash == 0 ||
      uslInfo.backendContentHash == 0 ||
      uslInfo.selectedEntryCount != 0u ||
      uslInfo.targetSupported != 1 ||
      uslInfo.targetSupportStatus != 1) {
    NSLog(@"GPU: failed to read USL shader-library cache identity");
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

  if (GPUGetShaderLibraryUSLInfo(_library, &uslInfo) != 0 ||
      uslInfo.bytecodeSize != (uint64_t)bytecodeData.length) {
    NSLog(@"GPU: unexpected USL shader-library info");
    return NO;
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT4, .offset = offsetof(QuadVertex, position) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(QuadVertex),
      .stepMode = GPU_VERTEX_STEP_MODE_VERTEX,
      .attributeCount = 1,
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
    .label = "textured-quad-usl-pipeline",
    .layout = _shaderLayout->pipelineLayout,
    .library = (GPUShaderLibrary *)_library,
    .vertexEntry = "quad_vs",
    .fragmentEntry = "quad_fs",
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

  groupEntries[0].binding = 0;
  groupEntries[0].textureView = (GPUTextureView *)_texture;
  groupEntries[1].binding = 0;
  groupEntries[1].sampler = _sampler;
  groupEntries[2].binding = 0;
  groupEntries[2].buffer.buffer = _fragmentUniformBuffer;
  groupEntries[2].buffer.offset = 0;
  groupEntries[2].buffer.size = sizeof(FragmentUniforms);

  GPUBindGroupCreateInfo set0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "textured-quad-usl-set0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 3,
    .pEntries = groupEntries
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
  uniforms->tint[0] = 0.75f + 0.25f * sinf(time * 1.1f);
  uniforms->tint[1] = 0.75f + 0.25f * sinf(time * 1.5f + 1.0f);
  uniforms->tint[2] = 0.75f + 0.25f * sinf(time * 1.9f + 2.0f);
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

  rp.label = "textured-quad-usl-pass";
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
  GPUDraw(encoder, 6, 1, 0, 0);
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
