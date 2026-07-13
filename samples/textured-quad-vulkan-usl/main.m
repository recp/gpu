#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>

#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"

typedef struct QuadVertex {
  float position[4];
} QuadVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

static const QuadVertex kQuadVertices[] = {
  {{-0.8f, -0.8f, 0.0f, 1.0f}},
  {{ 0.8f, -0.8f, 0.0f, 1.0f}},
  {{-0.8f,  0.8f, 0.0f, 1.0f}},
  {{-0.8f,  0.8f, 0.0f, 1.0f}},
  {{ 0.8f, -0.8f, 0.0f, 1.0f}},
  {{ 0.8f,  0.8f, 0.0f, 1.0f}}
};

static const uint8_t kCheckerPixels[] = {
  255,   0,   0, 255,    0, 255,   0, 255,
    0,   0, 255, 255,  255, 255, 255, 255
};

static const GPUBindGroupLayoutEntry *
FindLayoutEntry(GPUBindGroupLayout *layout,
                uint32_t binding,
                GPUBindingType type) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t count;

  entries = GPUGetBindGroupLayoutEntries(layout, &count);
  for (uint32_t i = 0u; entries && i < count; i++) {
    if (entries[i].binding == binding && entries[i].bindingType == type) {
      return &entries[i];
    }
  }
  return NULL;
}

@interface TexturedQuadVulkanApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow          *_window;
  NSView            *_view;
  GPUInstance       *_instance;
  GPUAdapter        *_adapter;
  GPUDevice         *_device;
  GPUCommandQueue   *_queue;
  GPUSurface        *_surface;
  GPUSwapchain      *_swapchain;
  GPUShaderLibrary  *_library;
  GPUShaderLayout   *_shaderLayout;
  GPURenderPipeline *_pipeline;
  GPUBuffer         *_vertexBuffer;
  GPUBuffer         *_uniformBuffer;
  GPUTexture        *_texture;
  GPUTextureView    *_textureView;
  GPUSampler        *_sampler;
  GPUBindGroup      *_fragmentGroup;
  GPUBindGroup      *_samplerGroup;
  NSTimer           *_timer;
  NSInteger          _exitAfterFrames;
  NSInteger          _submittedFrames;
  NSInteger          _completedFrames;
  BOOL               _assertZeroAlloc;
  BOOL               _statsFailed;
  BOOL               _terminating;
}
- (void)frameCompleted;
- (BOOL)statsFailed;
@end

static void
TexturedQuadVulkanFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  TexturedQuadVulkanApp *app;

  (void)cmdb;
  app = (__bridge TexturedQuadVulkanApp *)sender;
  [app frameCompleted];
}

@implementation TexturedQuadVulkanApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU Vulkan USL Textured Quad",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupResources {
  GPUBufferCreateInfo      vertexInfo = {0};
  GPUBufferCreateInfo      uniformInfo = {0};
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        fragmentEntries[2] = {{0}};
  GPUBindGroupEntry        samplerEntry = {0};
  GPUBindGroupCreateInfo   groupInfo = {0};
  FragmentUniforms         uniforms = {{0.9f, 0.95f, 1.0f, 1.0f}};

  vertexInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertexInfo.chain.structSize = sizeof(vertexInfo);
  vertexInfo.label            = "textured-quad-vulkan-vertices";
  vertexInfo.sizeBytes        = sizeof(kQuadVertices);
  vertexInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device, &vertexInfo, &_vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _vertexBuffer,
                          0u,
                          kQuadVertices,
                          sizeof(kQuadVertices)) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan quad vertices");
    return NO;
  }

  uniformInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  uniformInfo.chain.structSize = sizeof(uniformInfo);
  uniformInfo.label            = "textured-quad-vulkan-uniforms";
  uniformInfo.sizeBytes        = sizeof(uniforms);
  uniformInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                 GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device, &uniformInfo, &_uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan quad uniforms");
    return NO;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-quad-vulkan-checker";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(_device, &textureInfo, &_texture) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan checker texture");
    return NO;
  }

  writeRegion.width          = 2u;
  writeRegion.height         = 2u;
  writeRegion.depth          = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = 8u;
  writeRegion.rowsPerImage   = 2u;
  if (GPUQueueWriteTexture(_queue,
                           _texture,
                           &writeRegion,
                           kCheckerPixels,
                           sizeof(kCheckerPixels)) != GPU_OK) {
    NSLog(@"GPU: failed to upload Vulkan checker texture");
    return NO;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-quad-vulkan-checker-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(_texture, &viewInfo, &_textureView) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan checker view");
    return NO;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "textured-quad-vulkan-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_LINEAR;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(_device,
                       &samplerInfo,
                       false,
                       &_sampler) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan checker sampler");
    return NO;
  }

  fragmentEntries[0].binding       = 0u;
  fragmentEntries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  fragmentEntries[0].textureView   = _textureView;
  fragmentEntries[1].binding       = 1u;
  fragmentEntries[1].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  fragmentEntries[1].buffer.buffer = _uniformBuffer;
  fragmentEntries[1].buffer.size   = sizeof(uniforms);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "textured-quad-vulkan-group0";
  groupInfo.layout           = _shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = fragmentEntries;
  if (GPUCreateBindGroup(_device,
                         &groupInfo,
                         &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan fragment group");
    return NO;
  }

  samplerEntry.binding     = 0u;
  samplerEntry.bindingType = GPU_BINDING_SAMPLER;
  samplerEntry.sampler     = _sampler;
  groupInfo.label          = "textured-quad-vulkan-group1";
  groupInfo.layout         = _shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount     = 1u;
  groupInfo.pEntries       = &samplerEntry;
  if (GPUCreateBindGroup(_device,
                         &groupInfo,
                         &_samplerGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create Vulkan sampler group");
    return NO;
  }

  return YES;
}

- (BOOL)setupGPU {
  GPUInstanceCreateInfo       instanceInfo = {0};
  GPUVertexAttribute          vertexAttribute = {0};
  GPUVertexBufferLayout       vertexLayout = {0};
  GPUColorTargetState         colorTarget = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  const GPUBindGroupLayoutEntry *textureEntry;
  const GPUBindGroupLayoutEntry *uniformEntry;
  const GPUBindGroupLayoutEntry *samplerEntry;
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
                        @"textured_quad.us",
                        2u,
                        &_library,
                        &_shaderLayout)) {
    return NO;
  }

  textureEntry = FindLayoutEntry(_shaderLayout->bindGroupLayouts[0],
                                 0u,
                                 GPU_BINDING_SAMPLED_TEXTURE);
  uniformEntry = FindLayoutEntry(_shaderLayout->bindGroupLayouts[0],
                                 1u,
                                 GPU_BINDING_UNIFORM_BUFFER);
  samplerEntry = FindLayoutEntry(_shaderLayout->bindGroupLayouts[1],
                                 0u,
                                 GPU_BINDING_SAMPLER);
  if (!textureEntry || !uniformEntry || !samplerEntry ||
      textureEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      uniformEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      samplerEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    NSLog(@"GPU: unexpected Vulkan textured quad reflection layout");
    return NO;
  }

  if (![self setupResources]) {
    return NO;
  }

  vertexAttribute.shaderLocation = 0u;
  vertexAttribute.format         = GPU_VERTEX_FORMAT_FLOAT4;
  vertexLayout.strideBytes       = sizeof(QuadVertex);
  vertexLayout.stepMode          = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount    = 1u;
  vertexLayout.pAttributes       = &vertexAttribute;
  colorTarget.format             = GPU_FORMAT_BGRA8_UNORM;
  colorTarget.blend.writeMask    = GPU_COLOR_WRITE_ALL;

  pipelineInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize   = sizeof(pipelineInfo);
  pipelineInfo.label              = "textured-quad-vulkan-pipeline";
  pipelineInfo.layout             = _shaderLayout->pipelineLayout;
  pipelineInfo.library            = _library;
  pipelineInfo.vertexEntry        = "quad_vs";
  pipelineInfo.fragmentEntry      = "quad_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts    = &vertexLayout;
  pipelineInfo.colorTargetCount   = 1u;
  pipelineInfo.pColorTargets      = &colorTarget;
  pipelineInfo.primitiveTopology  = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode           = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace          = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = 0xffffffffu;
  if (GPUCreateRenderPipeline(_device, &pipelineInfo, &_pipeline) != GPU_OK ||
      !_pipeline) {
    NSLog(@"GPU: failed to create Vulkan textured quad pipeline");
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
  GPUBufferBinding              vertexBinding = {0};
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
                              "textured-quad-vulkan-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(
      cmdb,
      (__bridge void *)self,
      TexturedQuadVulkanFrameComplete
    );
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.02f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.035f;
  color.clearColor.float32[3] = 1.0f;

  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "textured-quad-vulkan-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  vertexBinding.buffer = _vertexBuffer;
  GPUBindRenderPipeline(pass, _pipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(pass, 0u, _fragmentGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, _samplerGroup, 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(_queue, cmdb, frame);
  if (result != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", result);
    return;
  }
  _submittedFrames++;
  if (!GPUSampleCheckZeroAlloc(_device,
                               (uint32_t)_submittedFrames,
                               _assertZeroAlloc,
                               "GPU Vulkan textured quad")) {
    _statsFailed = YES;
    _terminating = YES;
    [_timer invalidate];
    _timer = nil;
    [NSApp terminate:nil];
  }
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
  if (_samplerGroup) {
    GPUDestroyBindGroup(_samplerGroup);
    _samplerGroup = NULL;
  }
  if (_fragmentGroup) {
    GPUDestroyBindGroup(_fragmentGroup);
    _fragmentGroup = NULL;
  }
  if (_sampler) {
    GPUDestroySampler(_sampler);
    _sampler = NULL;
  }
  if (_textureView) {
    GPUDestroyTextureView(_textureView);
    _textureView = NULL;
  }
  if (_texture) {
    GPUDestroyTexture(_texture);
    _texture = NULL;
  }
  if (_uniformBuffer) {
    GPUDestroyBuffer(_uniformBuffer);
    _uniformBuffer = NULL;
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
  int result;

  result = 0;
  @autoreleasepool {
    TexturedQuadVulkanApp *delegate;

    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    delegate = [TexturedQuadVulkanApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
    result = [delegate statsFailed] ? 1 : 0;
  }
  return result;
}
