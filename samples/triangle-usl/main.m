#import <AppKit/AppKit.h>
#include <math.h>
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
  NSTimer *_timer;
  NSTimeInterval _animationStart;
}
@end

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
    .framesInFlight = 1,
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
      !initialUniformSlice.buffer ||
      initialUniformSlice.offset != 0) {
    NSLog(@"GPU: failed to allocate transient uniform slice");
    return NO;
  }
  _fragmentUniformBuffer = initialUniformSlice.buffer;

  GPUBindGroupEntry set0Bindings[] = {
    {
      .binding = 0,
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
    .label = "triangle-usl-set0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1,
    .pEntries = set0Bindings
  };
  if (GPUCreateBindGroup(_device, &set0Info, &_fragmentGroup) != GPU_OK) {
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
      slice.offset != 0 ||
      !slice.cpuPtr) {
    NSLog(@"GPU: failed to allocate frame uniform slice");
    return NO;
  }

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
  GPUBindRenderGroup(encoder, 0, _fragmentGroup, 0, NULL);
  GPUDraw(encoder, 3, 1, 0, 0);
  GPUEndRenderPass(encoder);
  encoder = NULL;
  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  }

cleanup:
  if (encoder) {
    GPUEndRenderPass(encoder);
  }
  GPUEndFrame(frame);
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
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
