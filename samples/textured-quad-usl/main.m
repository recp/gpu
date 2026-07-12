#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#include <math.h>
#include <stdlib.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleUSL.h"

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

  GPUInstance *_instance;
  GPUAdapter *_adapter;
  GPUDevice *_device;
  GPUCommandQueue *_queue;
  GPUSurface *_surface;
  GPUSwapchain *_swapchain;
  GPULibrary *_library;
  GPUShaderLayout *_shaderLayout;
  GPURenderPipeline *_pipeline;
  GPUBuffer *_vertexBuffer;
  GPUBuffer *_fragmentUniformBuffer;
  GPUTexture *_texture;
  GPUTextureView *_textureView;
  GPUSampler *_sampler;
  GPUBindGroup *_fragmentGroup;
  GPUBindGroup *_samplerGroup;
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
TexturedQuadFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  TexturedQuadApp *app = (__bridge TexturedQuadApp *)sender;
  [app frameCompleted];
}

@implementation TexturedQuadApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU USL Textured Quad",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupTexture {
  GPUTextureCreateInfo textureInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO,
               .structSize = sizeof(GPUTextureCreateInfo) },
    .label = "checker-texture",
    .dimension = GPU_TEXTURE_DIMENSION_2D,
    .format = GPU_FORMAT_RGBA8_UNORM,
    .width = 2,
    .height = 2,
    .depthOrLayers = 1,
    .mipLevelCount = 1,
    .sampleCount = 1,
    .usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST
  };
  GPUTextureWriteRegion writeRegion = {
    .width = 2,
    .height = 2,
    .depth = 1,
    .mipLevel = 0,
    .baseArrayLayer = 0,
    .layerCount = 1,
    .bytesPerRow = 8,
    .rowsPerImage = 2
  };
  GPUTextureViewCreateInfo viewInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO,
               .structSize = sizeof(GPUTextureViewCreateInfo) },
    .label = "checker-texture-view",
    .viewType = GPU_TEXTURE_VIEW_2D,
    .format = GPU_FORMAT_RGBA8_UNORM,
    .baseMipLevel = 0,
    .mipLevelCount = 1,
    .baseArrayLayer = 0,
    .arrayLayerCount = 1
  };
  GPUSamplerCreateInfo samplerInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
               .structSize = sizeof(GPUSamplerCreateInfo) },
    .label = "checker-sampler",
    .desc = {
      .minFilter = GPU_FILTER_LINEAR,
      .magFilter = GPU_FILTER_LINEAR,
      .mipFilter = GPU_MIP_FILTER_LINEAR,
      .addressU = GPU_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressV = GPU_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressW = GPU_ADDRESS_MODE_CLAMP_TO_EDGE
    }
  };

  if (GPUCreateTexture(_device, &textureInfo, &_texture) != GPU_OK) {
    NSLog(@"GPU: failed to create texture");
    return NO;
  }

  if (GPUQueueWriteTexture(_queue,
                           _texture,
                           &writeRegion,
                           kCheckerPixels,
                           sizeof(kCheckerPixels)) != GPU_OK) {
    NSLog(@"GPU: failed to upload texture");
    return NO;
  }

  if (GPUCreateTextureView(_texture, &viewInfo, &_textureView) != GPU_OK) {
    NSLog(@"GPU: failed to create texture view");
    return NO;
  }

  if (GPUCreateSampler(_device, &samplerInfo, false, &_sampler) != GPU_OK) {
    NSLog(@"GPU: failed to create sampler");
    return NO;
  }

  return YES;
}

- (BOOL)setupGPU {
  GPUBindGroupEntry groupEntries[3] = {0};

  if (!GPUSampleCreateDefaultSurfaceGPU(_window,
                                        _view,
                                        &_instance,
                                        &_adapter,
                                        &_device,
                                        &_queue,
                                        &_surface,
                                        &_swapchain)) {
    return NO;
  }

  if (!GPUSampleLoadUSL(_device,
                        @"textured_quad.us",
                        2u,
                        (GPUShaderLibrary **)&_library,
                        &_shaderLayout)) {
    return NO;
  }
  if (!_shaderLayout ||
      _shaderLayout->bindGroupLayoutCount != 2u ||
      !_shaderLayout->bindGroupLayouts ||
      !_shaderLayout->bindGroupLayouts[0] ||
      !_shaderLayout->bindGroupLayouts[1]) {
    NSLog(@"GPU: unexpected textured quad shader layout");
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

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "textured-quad-usl-vertices",
    .sizeBytes = sizeof(kQuadVertices),
    .usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &vertexBufferInfo, &_vertexBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create vertex buffer");
    return NO;
  }

  if (GPUQueueWriteBuffer(_queue,
                          _vertexBuffer,
                          0,
                          kQuadVertices,
                          sizeof(kQuadVertices)) != GPU_OK) {
    NSLog(@"GPU: failed to upload vertex buffer");
    return NO;
  }

  GPUBufferCreateInfo uniformBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "textured-quad-usl-fragment-uniforms",
    .sizeBytes = sizeof(FragmentUniforms),
    .usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &uniformBufferInfo, &_fragmentUniformBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment uniform buffer");
    return NO;
  }

  if (![self setupTexture]) {
    return NO;
  }

  groupEntries[0].binding = 0;
  groupEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[0].textureView = _textureView;
  groupEntries[1].binding = 1;
  groupEntries[1].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  groupEntries[1].buffer.buffer = _fragmentUniformBuffer;
  groupEntries[1].buffer.offset = 0;
  groupEntries[1].buffer.size = sizeof(FragmentUniforms);

  GPUBindGroupCreateInfo group0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "textured-quad-usl-group0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 2,
    .pEntries = groupEntries
  };
  if (GPUCreateBindGroup(_device, &group0Info, &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind group");
    return NO;
  }

  GPUBindGroupEntry samplerEntry = {0};
  samplerEntry.binding = 0;
  samplerEntry.sampler = _sampler;

  GPUBindGroupCreateInfo group1Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "textured-quad-usl-group1",
    .layout = _shaderLayout->bindGroupLayouts[1],
    .entryCount = 1,
    .pEntries = &samplerEntry
  };
  if (GPUCreateBindGroup(_device, &group1Info, &_samplerGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create sampler bind group");
    return NO;
  }

  return YES;
}

- (void)updateFragmentUniforms {
  FragmentUniforms uniforms;
  float time;

  time = (float)(CACurrentMediaTime() - _animationStart);
  uniforms.tint[0] = 0.75f + 0.25f * sinf(time * 1.1f);
  uniforms.tint[1] = 0.75f + 0.25f * sinf(time * 1.5f + 1.0f);
  uniforms.tint[2] = 0.75f + 0.25f * sinf(time * 1.9f + 2.0f);
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

  if (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) {
    return;
  }

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  if (GPUAcquireCommandBuffer(_queue, "main-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         TexturedQuadFrameComplete);
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

  GPUBindRenderPipeline(encoder, _pipeline);
  GPUBindVertexBuffers(encoder, 0, 1, &vertexBuffer);
  GPUBindRenderGroup(encoder, 0, _fragmentGroup, 0, NULL);
  GPUBindRenderGroup(encoder, 1, _samplerGroup, 0, NULL);
  GPUDraw(encoder, 6, 1, 0, 0);
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
  if (_samplerGroup) {
    GPUDestroyBindGroup(_samplerGroup);
    _samplerGroup = NULL;
  }
  if (_fragmentGroup) {
    GPUDestroyBindGroup(_fragmentGroup);
    _fragmentGroup = NULL;
  }
  if (_pipeline) {
    GPUDestroyRenderPipeline(_pipeline);
    _pipeline = NULL;
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
  if (_fragmentUniformBuffer) {
    GPUDestroyBuffer(_fragmentUniformBuffer);
    _fragmentUniformBuffer = NULL;
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
    GPUDestroyShaderLibrary((GPUShaderLibrary *)_library);
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
  (void)notification;
  if (_terminating) {
    return;
  }
  [self renderFrame];
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
