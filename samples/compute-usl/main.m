#import <AppKit/AppKit.h>
#include <stddef.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleUSL.h"

typedef struct QuadVertex {
  float position[4];
  float uv[2];
} QuadVertex;

static const uint32_t kTextureSize = 256;
static const uint32_t kWorkgroupSize = 8;

static const QuadVertex kQuadVertices[] = {
  { { -0.8f, -0.8f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { { -0.8f,  0.8f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { { -0.8f,  0.8f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { {  0.8f,  0.8f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
};

@interface ComputeUSLApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow *_window;
  NSView *_view;

  GPUPhysicalDevice *_physicalDevice;
  GPUDevice *_device;
  GPUCommandQueue *_queue;
  GPUSurface *_surface;
  GPUSwapchain *_swapchain;
  GPULibrary *_library;
  GPUShaderLayout *_shaderLayout;
  GPUComputePipeline *_computePipeline;
  GPURenderPipeline *_renderPipeline;
  GPUBuffer *_vertexBuffer;
  GPUTexture *_texture;
  GPUTextureView *_textureView;
  GPUSampler *_sampler;
  GPUBindGroup *_bindGroup;
  NSTimer *_timer;
}
@end

@implementation ComputeUSLApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU USL Compute Texture",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupTexture {
  GPUTextureCreateInfo textureInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO,
               .structSize = sizeof(GPUTextureCreateInfo) },
    .label = "compute-output-texture",
    .dimension = GPU_TEXTURE_DIMENSION_2D,
    .format = GPU_FORMAT_RGBA8_UNORM,
    .width = kTextureSize,
    .height = kTextureSize,
    .depthOrLayers = 1,
    .mipLevelCount = 1,
    .sampleCount = 1,
    .usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_STORAGE
  };
  GPUTextureViewCreateInfo viewInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO,
               .structSize = sizeof(GPUTextureViewCreateInfo) },
    .label = "compute-output-view",
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
    .label = "compute-output-sampler",
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
    NSLog(@"GPU: failed to create compute texture");
    return NO;
  }

  if (GPUCreateTextureView(_texture, &viewInfo, &_textureView) != GPU_OK) {
    NSLog(@"GPU: failed to create compute texture view");
    return NO;
  }

  if (GPUCreateSampler(_device, &samplerInfo, false, &_sampler) != GPU_OK) {
    NSLog(@"GPU: failed to create sampler");
    return NO;
  }

  return YES;
}

- (BOOL)setupGPU {
  GPUBindGroupEntry groupEntries[2] = {0};

  if (!GPUSampleCreateDefaultSurfaceGPU(_window,
                                        _view,
                                        &_physicalDevice,
                                        &_device,
                                        &_queue,
                                        &_surface,
                                        &_swapchain)) {
    return NO;
  }

  if (!GPUSampleLoadUSL(_device,
                        @"compute_visible.us",
                        1u,
                        (GPUShaderLibrary **)&_library,
                        &_shaderLayout)) {
    return NO;
  }

  GPUComputePipelineCreateInfo computeInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
               .structSize = sizeof(GPUComputePipelineCreateInfo) },
    .label = "compute-usl-fill-image",
    .layout = _shaderLayout->pipelineLayout,
    .library = (GPUShaderLibrary *)_library,
    .entryPoint = "fill_image"
  };
  if (GPUCreateComputePipeline(_device, &computeInfo, &_computePipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create compute pipeline");
    return NO;
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT4, .offset = offsetof(QuadVertex, position) },
    { .shaderLocation = 1, .format = GPU_VERTEX_FORMAT_FLOAT2, .offset = offsetof(QuadVertex, uv) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(QuadVertex),
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
    .label = "compute-usl-render-pipeline",
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
  if (GPUCreateRenderPipeline(_device, &renderInfo, &_renderPipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create render pipeline");
    return NO;
  }

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-usl-vertices",
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

  if (![self setupTexture]) {
    return NO;
  }

  groupEntries[0].binding = 0;
  groupEntries[0].textureView = _textureView;
  groupEntries[1].binding = 0;
  groupEntries[1].sampler = _sampler;

  GPUBindGroupCreateInfo set0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "compute-usl-set0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 2,
    .pEntries = groupEntries
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
  GPUResult submitResult = GPU_OK;
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBuffer = {0};

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  if (GPUAcquireCommandBuffer(_queue, "compute-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }

  compute = GPUBeginComputePass(cmdb, "compute-usl-fill");
  if (!compute) {
    goto cleanup;
  }
  GPUBindComputePipeline(compute, _computePipeline);
  GPUBindComputeGroup(compute, 0, _bindGroup, 0, NULL);
  GPUDispatch(compute,
              (kTextureSize + kWorkgroupSize - 1u) / kWorkgroupSize,
              (kTextureSize + kWorkgroupSize - 1u) / kWorkgroupSize,
              1);
  GPUEndComputePass(compute);
  compute = NULL;

  color.view = GPUFrameGetTargetView(frame);
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.0f;
  color.clearColor.float32[1] = 0.0f;
  color.clearColor.float32[2] = 0.0f;
  color.clearColor.float32[3] = 1.0f;

  rp.label = "compute-usl-render-pass";
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
  GPUBindRenderGroup(render, 0, _bindGroup, 0, NULL);
  GPUDraw(render, 6, 1, 0, 0);
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
    ComputeUSLApp *delegate;

    (void)argc;
    (void)argv;

    [NSApplication sharedApplication];
    delegate = [ComputeUSLApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
  }
  return 0;
}
