#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#import <QuartzCore/QuartzCore.h>

#include <stdlib.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"
#import "CubeData.h"

#ifndef GPU_SAMPLE_BACKEND
#  define GPU_SAMPLE_BACKEND GPU_BACKEND_METAL
#endif

static NSString *
CubeWindowTitle(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? @"GPU Vulkan USL Rotating Cube"
           : @"GPU Metal USL Rotating Cube";
}

static const char *
CubeStatsLabel(void) {
  return GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN
           ? "GPU Vulkan textured cube"
           : "GPU Metal textured cube";
}

@interface TexturedCubeApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow          *_window;
  NSView            *_view;
  GPUInstance       *_instance;
  GPUAdapter        *_adapter;
  GPUDevice         *_device;
  GPUQueue          *_queue;
  GPUSurface        *_surface;
  GPUSwapchain      *_swapchain;
  GPUShaderLibrary  *_library;
  GPUShaderLayout   *_shaderLayout;
  GPURenderPipeline *_pipeline;
  GPUBuffer         *_vertexBuffer;
  GPUBuffer         *_indexBuffer;
  GPUBuffer         *_uniformBuffer;
  GPUTexture        *_texture;
  GPUTextureView    *_textureView;
  GPUTexture        *_depthTexture;
  GPUTextureView    *_depthView;
  GPUSampler        *_sampler;
  GPUBindGroup      *_materialGroup;
  GPUBindGroup      *_samplerGroup;
  NSTimer           *_timer;
  NSInteger          _exitAfterFrames;
  NSInteger          _submittedFrames;
  NSInteger          _completedFrames;
  NSTimeInterval      _animationStart;
  uint32_t            _drawableWidth;
  uint32_t            _drawableHeight;
  BOOL                _assertZeroAlloc;
  BOOL                _statsFailed;
  BOOL                _terminating;
  mat4                _viewProjection;
}
- (void)frameCompleted;
- (BOOL)statsFailed;
@end

static void
TexturedCubeFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  TexturedCubeApp *app;

  (void)cmdb;
  app = (__bridge TexturedCubeApp *)sender;
  [app frameCompleted];
}

@implementation TexturedCubeApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(CubeWindowTitle(),
                               self,
                               &_window,
                               &_view);
}

- (void)updateDrawableSize {
  CGFloat scale;

  scale           = _window.backingScaleFactor ?: 1.0f;
  _drawableWidth  = (uint32_t)(_view.bounds.size.width * scale);
  _drawableHeight = (uint32_t)(_view.bounds.size.height * scale);
  CubeBuildViewProjection(_drawableWidth,
                          _drawableHeight,
                          _viewProjection);
}

- (BOOL)createDepthTarget {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  [self updateDrawableSize];
  if (_drawableWidth == 0u || _drawableHeight == 0u) {
    return NO;
  }

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = _drawableWidth;
  textureInfo.height           = _drawableHeight;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  if (GPUCreateTexture(_device, &textureInfo, &texture) != GPU_OK) {
    return NO;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    return NO;
  }

  GPUDestroyTextureView(_depthView);
  GPUDestroyTexture(_depthTexture);
  _depthTexture = texture;
  _depthView    = view;
  return YES;
}

- (BOOL)createPipeline {
  GPUVertexAttribute          attributes[3] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPUDepthStencilState        depth         = {0};
  GPURenderPipelineCreateInfo info          = {0};

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[0].offset          = offsetof(CubeVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[1].offset          = offsetof(CubeVertex, normal);
  attributes[1].shaderLocation = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[2].offset          = offsetof(CubeVertex, uv);
  attributes[2].shaderLocation = 2u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(CubeVertex);
  vertexLayout.attributeCount   = 3u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(_swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "textured-cube-usl-pipeline";
  info.layout                   = _shaderLayout->pipelineLayout;
  info.library                  = _library;
  info.vertexEntry              = "cube_vs";
  info.fragmentEntry            = "cube_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_BACK;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  return GPUCreateRenderPipeline(_device, &info, &_pipeline) == GPU_OK;
}

- (BOOL)createGeometry {
  CubeUniforms        uniforms;
  GPUBufferCreateInfo info = {0};

  CubeBuildUniforms(0.0f, _viewProjection, &uniforms);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-cube-vertices";
  info.sizeBytes        = sizeof(kCubeVertices);
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device, &info, &_vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _vertexBuffer,
                          0u,
                          kCubeVertices,
                          sizeof(kCubeVertices)) != GPU_OK) {
    return NO;
  }

  info.label     = "textured-cube-indices";
  info.sizeBytes = sizeof(kCubeIndices);
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device, &info, &_indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _indexBuffer,
                          0u,
                          kCubeIndices,
                          sizeof(kCubeIndices)) != GPU_OK) {
    return NO;
  }

  info.label     = "textured-cube-uniforms";
  info.sizeBytes = sizeof(uniforms);
  info.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device, &info, &_uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return NO;
  }
  return YES;
}

- (BOOL)createMaterial {
  uint8_t                       pixels[CUBE_CHECKER_SIZE *
                                       CUBE_CHECKER_SIZE * 4u];
  GPUTextureCreateInfo          textureInfo       = {0};
  GPUTextureWriteRegion         writeRegion       = {0};
  GPUTextureViewCreateInfo      viewInfo          = {0};
  GPUSamplerCreateInfo          samplerInfo       = {0};
  GPUBindGroupEntry             materialEntries[2] = {0};
  GPUBindGroupEntry             samplerEntry      = {0};
  GPUBindGroupCreateInfo        materialInfo      = {0};
  GPUBindGroupCreateInfo        samplerGroupInfo  = {0};

  CubeFillChecker(pixels);
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-checker";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = CUBE_CHECKER_SIZE;
  textureInfo.height           = CUBE_CHECKER_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(_device, &textureInfo, &_texture) != GPU_OK) {
    return NO;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = CUBE_CHECKER_SIZE;
  writeRegion.height       = CUBE_CHECKER_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = CUBE_CHECKER_SIZE * 4u;
  writeRegion.rowsPerImage = CUBE_CHECKER_SIZE;
  if (GPUQueueWriteTexture(_queue,
                           _texture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    return NO;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-checker-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(_texture, &viewInfo, &_textureView) != GPU_OK) {
    return NO;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "textured-cube-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_REPEAT;
  if (GPUCreateSampler(_device,
                       &samplerInfo,
                       false,
                       &_sampler) != GPU_OK) {
    return NO;
  }

  materialEntries[0].buffer.buffer = _uniformBuffer;
  materialEntries[0].buffer.size   = sizeof(CubeUniforms);
  materialEntries[0].binding       = 0u;
  materialEntries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  materialEntries[1].textureView   = _textureView;
  materialEntries[1].binding       = 1u;
  materialEntries[1].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  materialInfo.chain.sType         = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  materialInfo.chain.structSize    = sizeof(materialInfo);
  materialInfo.label               = "textured-cube-group0";
  materialInfo.layout              = _shaderLayout->bindGroupLayouts[0];
  materialInfo.pEntries            = materialEntries;
  materialInfo.entryCount          = 2u;
  if (GPUCreateBindGroup(_device,
                         &materialInfo,
                         &_materialGroup) != GPU_OK) {
    return NO;
  }

  samplerEntry.sampler              = _sampler;
  samplerEntry.binding              = 0u;
  samplerEntry.bindingType          = GPU_BINDING_SAMPLER;
  samplerGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  samplerGroupInfo.chain.structSize = sizeof(samplerGroupInfo);
  samplerGroupInfo.label            = "textured-cube-group1";
  samplerGroupInfo.layout           = _shaderLayout->bindGroupLayouts[1];
  samplerGroupInfo.pEntries         = &samplerEntry;
  samplerGroupInfo.entryCount       = 1u;
  return GPUCreateBindGroup(_device,
                            &samplerGroupInfo,
                            &_samplerGroup) == GPU_OK;
}

- (BOOL)setupGPU {
  GPUInstanceCreateInfo instanceInfo = {0};

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_SAMPLE_BACKEND;
  instanceInfo.enableValidation = GPU_SAMPLE_BACKEND == GPU_BACKEND_VULKAN;
  if (GPUCreateInstance(&instanceInfo, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create cube instance");
    return NO;
  }

  _adapter = GPUSampleSelectAdapter(_instance);
  if (!_adapter) {
    NSLog(@"GPU: failed to get cube adapter");
    return NO;
  }

  _device = GPUCreateDeviceWithDefaultQueues(_adapter);
  if (!_device) {
    NSLog(@"GPU: failed to create cube device");
    return NO;
  }

  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_queue) {
    NSLog(@"GPU: failed to get cube queue");
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)_view,
                                        GPU_SURFACE_APPLE_NSVIEW,
                                        _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    NSLog(@"GPU: failed to create cube surface");
    return NO;
  }

  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create cube swapchain");
    return NO;
  }
  if (!GPUSampleLoadUSL(_device,
                        @"textured_cube.us",
                        2u,
                        &_library,
                        &_shaderLayout) ||
      !_shaderLayout->bindGroupLayouts[0] ||
      !_shaderLayout->bindGroupLayouts[1]) {
    return NO;
  }
  if (![self createDepthTarget] ||
      ![self createPipeline] ||
      ![self createGeometry] ||
      ![self createMaterial]) {
    NSLog(@"GPU: failed to initialize textured cube resources");
    return NO;
  }
  return YES;
}

- (BOOL)updateUniforms {
  CubeUniforms uniforms;
  float        seconds;

  seconds = (float)(CACurrentMediaTime() - _animationStart);
  CubeBuildUniforms(seconds, _viewProjection, &uniforms);
  return GPUQueueWriteBuffer(_queue,
                             _uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

- (void)renderFrame {
  GPUFrame                           *frame;
  GPUCommandBuffer                   *cmdb;
  GPURenderPassEncoder               *pass;
  GPUBufferBinding                    vertexBuffer = {0};
  GPURenderPassColorAttachment        color        = {0};
  GPURenderPassDepthStencilAttachment depth        = {0};
  GPURenderPassCreateInfo             passInfo     = {0};
  GPUResult                           result;

  if (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) {
    return;
  }
  if (!GPUSampleRecoverSwapchain(_swapchain, _view) ||
      ![self updateUniforms]) {
    return;
  }

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  pass = NULL;
  if (GPUAcquireCommandBuffer(_queue,
                              "textured-cube-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         TexturedCubeFrameComplete);
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.048f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = _depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "textured-cube-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    goto cleanup;
  }

  vertexBuffer.buffer = _vertexBuffer;
  GPUBindRenderPipeline(pass, _pipeline);
  GPUBindRenderGroup(pass, 0u, _materialGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, _samplerGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, _indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, CUBE_INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);
  pass   = NULL;
  result = GPUFinishFrame(_queue, cmdb, frame);
  frame  = NULL;
  if (result != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", result);
    return;
  }

  _submittedFrames++;
  if (!GPUSampleCheckZeroAlloc(_device,
                               (uint32_t)_submittedFrames,
                               _assertZeroAlloc,
                               CubeStatsLabel())) {
    _statsFailed = YES;
    _terminating = YES;
    [_timer invalidate];
    _timer = nil;
    [NSApp terminate:nil];
  } else if (_exitAfterFrames > 0 &&
             _submittedFrames >= _exitAfterFrames) {
    [_timer invalidate];
    _timer = nil;
  }
  return;

cleanup:
  if (pass) {
    GPUEndRenderPass(pass);
  }
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
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
  if (!_terminating) {
    [self renderFrame];
  }
}

- (void)cleanupGPU {
  GPUDestroyBindGroup(_samplerGroup);
  GPUDestroyBindGroup(_materialGroup);
  GPUDestroyRenderPipeline(_pipeline);
  GPUDestroySampler(_sampler);
  GPUDestroyTextureView(_depthView);
  GPUDestroyTexture(_depthTexture);
  GPUDestroyTextureView(_textureView);
  GPUDestroyTexture(_texture);
  GPUDestroyBuffer(_uniformBuffer);
  GPUDestroyBuffer(_indexBuffer);
  GPUDestroyBuffer(_vertexBuffer);
  GPUDestroyShaderLayout(_shaderLayout);
  GPUDestroyShaderLibrary(_library);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);

  _samplerGroup  = NULL;
  _materialGroup = NULL;
  _pipeline      = NULL;
  _sampler       = NULL;
  _depthView     = NULL;
  _depthTexture  = NULL;
  _textureView   = NULL;
  _texture       = NULL;
  _uniformBuffer = NULL;
  _indexBuffer   = NULL;
  _vertexBuffer  = NULL;
  _shaderLayout  = NULL;
  _library       = NULL;
  _swapchain     = NULL;
  _surface       = NULL;
  _queue         = NULL;
  _device        = NULL;
  _adapter       = NULL;
  _instance      = NULL;
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
  _assertZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  _animationStart  = CACurrentMediaTime();
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

- (void)resizeDrawable {
  uint32_t width;
  uint32_t height;

  if (!_swapchain || _terminating) {
    return;
  }
  width  = (uint32_t)_view.bounds.size.width;
  height = (uint32_t)_view.bounds.size.height;
  if (width > 0u && height > 0u &&
      GPUResizeSwapchain(_swapchain, width, height) == GPU_OK &&
      [self createDepthTarget]) {
    [self renderFrame];
  }
}

- (void)windowDidResize:(NSNotification *)notification {
  (void)notification;
  [self resizeDrawable];
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
  (void)notification;
  [self resizeDrawable];
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
    TexturedCubeApp *delegate;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    delegate = [TexturedCubeApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
    result = [delegate statsFailed] ? 1 : 0;
  }
  return result;
}
