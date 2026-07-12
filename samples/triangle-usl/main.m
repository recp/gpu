#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleUSL.h"

typedef struct TriangleVertex {
  float position[2];
} TriangleVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

static const TriangleVertex kTriangleVertices[] = {
  { {  0.0f,  0.65f } },
  { { -0.7f, -0.65f } },
  { {  0.7f, -0.65f } },
};

@interface TriangleApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
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
  GPUBindGroup *_fragmentGroup;
  uint32_t _fragmentUniformOffset;
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
TriangleUSLFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  TriangleApp *app = (__bridge TriangleApp *)sender;
  [app frameCompleted];
}

@implementation TriangleApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU USL Triangle Tint",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupGPU {
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
                        @"triangle.us",
                        1u,
                        (GPUShaderLibrary **)&_library,
                        &_shaderLayout)) {
    return NO;
  }
  if (!_shaderLayout ||
      _shaderLayout->bindGroupLayoutCount != 1u ||
      !_shaderLayout->bindGroupLayouts ||
      !_shaderLayout->bindGroupLayouts[0]) {
    NSLog(@"GPU: unexpected triangle shader layout");
    return NO;
  }
  {
    const GPUBindGroupLayoutEntry *entries;
    uint32_t entryCount = 0u;
    BOOL sawDynamicUniform = NO;

    entries = GPUGetBindGroupLayoutEntries(_shaderLayout->bindGroupLayouts[0],
                                           &entryCount);
    for (uint32_t i = 0; entries && i < entryCount; i++) {
      if (entries[i].binding == 0u &&
          entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
          entries[i].hasDynamicOffset) {
        sawDynamicUniform = YES;
        break;
      }
    }
    if (!sawDynamicUniform) {
      NSLog(@"GPU: triangle uniform reflection is not dynamic-offset capable");
      return NO;
    }
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT2, .offset = offsetof(TriangleVertex, position) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(TriangleVertex),
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
    .label = "triangle-usl-pipeline",
    .layout = _shaderLayout->pipelineLayout,
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

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "triangle-usl-vertices",
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

  GPUTransientAllocatorConfig transientInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG,
               .structSize = sizeof(GPUTransientAllocatorConfig) },
    .ringBytesPerFrame = 256,
    .framesInFlight = 3,
    .chunkBytes = 256,
    .allowChunkFallback = true
  };
  if (GPUConfigureTransientAllocator(_device, &transientInfo) != GPU_OK) {
    NSLog(@"GPU: failed to configure transient uniforms");
    return NO;
  }

  GPUTransientBufferSlice initialUniformSlice = {0};
  if (GPUAllocateTransientBuffer(_device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 sizeof(FragmentUniforms),
                                 256,
                                 &initialUniformSlice) != GPU_OK ||
      !initialUniformSlice.buffer) {
    NSLog(@"GPU: failed to allocate transient uniform slice");
    return NO;
  }
  _fragmentUniformBuffer = initialUniformSlice.buffer;

  GPUBindGroupEntry group0Bindings[] = {
    {
      .binding = 0,
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
    .label = "triangle-usl-group0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1,
    .pEntries = group0Bindings
  };
  if (GPUCreateBindGroup(_device, &group0Info, &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind group");
    return NO;
  }

  return YES;
}

- (BOOL)updateFragmentUniforms {
  FragmentUniforms uniforms;
  GPUTransientBufferSlice slice = {0};
  float time;

  time = (float)(CACurrentMediaTime() - _animationStart);
  uniforms.tint[0] = 0.6f + 0.4f * sinf(time * 1.1f);
  uniforms.tint[1] = 0.6f + 0.4f * sinf(time * 1.7f + 2.1f);
  uniforms.tint[2] = 0.6f + 0.4f * sinf(time * 1.3f + 4.2f);
  uniforms.tint[3] = 1.0f;
  if (GPUAllocateTransientBuffer(_device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 sizeof(uniforms),
                                 256,
                                 &slice) != GPU_OK ||
      slice.buffer != _fragmentUniformBuffer ||
      !slice.cpuPtr ||
      slice.offset > UINT32_MAX) {
    NSLog(@"GPU: failed to allocate frame uniform slice");
    return NO;
  }

  _fragmentUniformOffset = (uint32_t)slice.offset;
  memcpy(slice.cpuPtr, &uniforms, sizeof(uniforms));
  return YES;
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
                                         TriangleUSLFrameComplete);
  }

  color.view = GPUFrameGetTargetView(frame);
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.0f;
  color.clearColor.float32[1] = 0.0f;
  color.clearColor.float32[2] = 0.0f;
  color.clearColor.float32[3] = 1.0f;

  rp.label = "triangle-usl-pass";
  rp.colorAttachmentCount = 1;
  rp.pColorAttachments = &color;

  encoder = GPUBeginRenderPass(cmdb, &rp);
  if (!encoder) {
    goto cleanup;
  }

  if (![self updateFragmentUniforms]) {
    goto cleanup;
  }

  vertexBuffer.buffer = _vertexBuffer;
  vertexBuffer.offset = 0;

  GPUBindRenderPipeline(encoder, _pipeline);
  GPUBindVertexBuffers(encoder, 0, 1, &vertexBuffer);
  GPUBindRenderGroup(encoder, 0, _fragmentGroup, 1, &_fragmentUniformOffset);
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
  _fragmentUniformBuffer = NULL;
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
