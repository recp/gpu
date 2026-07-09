#import <AppKit/AppKit.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#import <dispatch/dispatch.h>
#import <QuartzCore/QuartzCore.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleUSL.h"

typedef struct QuadVertex {
  float position[4];
  float uv[2];
} QuadVertex;

typedef struct ComputeParams {
  float tint[4];
} ComputeParams;

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

  GPUAdapter *_adapter;
  GPUDevice *_device;
  GPUCommandQueue *_queue;
  GPUSurface *_surface;
  GPUSwapchain *_swapchain;
  GPULibrary *_library;
  GPUShaderLayout *_shaderLayout;
  GPUComputePipeline *_computePipeline;
  GPURenderPipeline *_renderPipeline;
  GPUBuffer *_vertexBuffer;
  GPUBuffer *_computeParamsBuffer;
  GPUBuffer *_readbackBuffer;
  GPUTexture *_texture;
  GPUTextureView *_textureView;
  GPUSampler *_sampler;
  GPUBindGroup *_bindGroup;
  GPUBindGroup *_samplerBindGroup;
  uint32_t _computeParamsOffset;
  NSTimer *_timer;
  NSTimeInterval _animationStart;
  NSInteger _exitAfterFrames;
  NSInteger _submittedFrames;
  NSInteger _completedFrames;
  BOOL _validationFailed;
  BOOL _terminating;
}
- (void)frameCompleted;
- (BOOL)validationFailed;
@end

static volatile int gComputeUSLValidationFailed = 0;

static void
ComputeUSLFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  ComputeUSLApp *app = (__bridge ComputeUSLApp *)sender;
  [app frameCompleted];
}

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
    .usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_STORAGE | GPU_TEXTURE_USAGE_COPY_SRC
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
  GPUBindGroupEntry groupEntries[3] = {0};

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
                        @"compute_visible.us",
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
    NSLog(@"GPU: unexpected compute shader layout");
    return NO;
  }
  {
    const GPUBindGroupLayoutEntry *entries;
    uint32_t entryCount = 0u;
    BOOL sawDynamicUniform = NO;

    entries = GPUGetBindGroupLayoutEntries(_shaderLayout->bindGroupLayouts[0],
                                           &entryCount);
    for (uint32_t i = 0; entries && i < entryCount; i++) {
      if (entries[i].stage == GPUBindStageCompute &&
          entries[i].binding == 1u &&
          entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
          entries[i].hasDynamicOffset) {
        sawDynamicUniform = YES;
        break;
      }
    }
    if (!sawDynamicUniform) {
      NSLog(@"GPU: compute uniform reflection is not dynamic-offset capable");
      return NO;
    }
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

  if (_exitAfterFrames > 0) {
    GPUBufferCreateInfo readbackInfo = {
      .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                 .structSize = sizeof(GPUBufferCreateInfo) },
      .label = "compute-usl-readback",
      .sizeBytes = 4u,
      .usage = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC
    };
    if (GPUCreateBuffer(_device, &readbackInfo, &_readbackBuffer) != GPU_OK) {
      NSLog(@"GPU: failed to create compute readback buffer");
      return NO;
    }
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
    NSLog(@"GPU: failed to configure compute transient uniforms");
    return NO;
  }

  GPUTransientBufferSlice initialParamsSlice = {0};
  if (GPUAllocateTransientBuffer(_device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 sizeof(ComputeParams),
                                 256,
                                 &initialParamsSlice) != GPU_OK ||
      !initialParamsSlice.buffer) {
    NSLog(@"GPU: failed to allocate compute transient uniform slice");
    return NO;
  }
  _computeParamsBuffer = initialParamsSlice.buffer;

  groupEntries[0].binding = 0;
  groupEntries[0].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  groupEntries[0].textureView = _textureView;
  groupEntries[1].binding = 2;
  groupEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[1].textureView = _textureView;
  groupEntries[2].binding = 1;
  groupEntries[2].bindingType = GPU_BINDING_UNIFORM_BUFFER;
  groupEntries[2].buffer.buffer = _computeParamsBuffer;
  groupEntries[2].buffer.offset = 0;
  groupEntries[2].buffer.size = sizeof(ComputeParams);

  GPUBindGroupCreateInfo group0Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "compute-usl-group0",
    .layout = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 3,
    .pEntries = groupEntries
  };
  if (GPUCreateBindGroup(_device, &group0Info, &_bindGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create bind group");
    return NO;
  }

  GPUBindGroupEntry samplerEntry = {0};
  samplerEntry.binding = 0;
  samplerEntry.sampler = _sampler;

  GPUBindGroupCreateInfo group1Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "compute-usl-group1",
    .layout = _shaderLayout->bindGroupLayouts[1],
    .entryCount = 1,
    .pEntries = &samplerEntry
  };
  if (GPUCreateBindGroup(_device, &group1Info, &_samplerBindGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create sampler bind group");
    return NO;
  }

  return YES;
}

- (BOOL)updateComputeParams {
  ComputeParams params;
  GPUTransientBufferSlice slice = {0};
  float time;

  time = (float)(CACurrentMediaTime() - _animationStart);
  if (_exitAfterFrames > 0) {
    params.tint[0] = 1.0f;
    params.tint[1] = 1.0f;
    params.tint[2] = 1.0f;
  } else {
    params.tint[0] = 0.75f + 0.25f * sinf(time * 1.2f);
    params.tint[1] = 0.75f + 0.25f * sinf(time * 1.7f + 2.0f);
    params.tint[2] = 0.75f + 0.25f * sinf(time * 1.4f + 4.0f);
  }
  params.tint[3] = 1.0f;

  if (GPUAllocateTransientBuffer(_device,
                                 GPU_BUFFER_USAGE_UNIFORM,
                                 sizeof(params),
                                 256,
                                 &slice) != GPU_OK ||
      slice.buffer != _computeParamsBuffer ||
      !slice.cpuPtr ||
      slice.offset > UINT32_MAX) {
    NSLog(@"GPU: failed to allocate compute frame uniform slice");
    return NO;
  }

  _computeParamsOffset = (uint32_t)slice.offset;
  *(ComputeParams *)slice.cpuPtr = params;
  return YES;
}

- (void)renderFrame {
  GPUFrame *frame = NULL;
  GPUCommandBuffer *cmdb = NULL;
  GPUComputePassEncoder *compute = NULL;
  GPUCopyPassEncoder *copy = NULL;
  GPURenderPassEncoder *render = NULL;
  GPUResult submitResult = GPU_OK;
  GPUTextureBarrier textureBarrier = {0};
  GPUBarrierBatch barriers = {0};
  GPUBufferTextureCopyRegion readbackRegion = {0};
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

  if (GPUAcquireCommandBuffer(_queue, "compute-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         ComputeUSLFrameComplete);
  }

  compute = GPUBeginComputePass(cmdb, "compute-usl-fill");
  if (!compute) {
    goto cleanup;
  }
  if (![self updateComputeParams]) {
    goto cleanup;
  }
  GPUBindComputePipeline(compute, _computePipeline);
  GPUBindComputeGroup(compute, 0, _bindGroup, 1, &_computeParamsOffset);
  GPUDispatch(compute,
              (kTextureSize + kWorkgroupSize - 1u) / kWorkgroupSize,
              (kTextureSize + kWorkgroupSize - 1u) / kWorkgroupSize,
              1);
  GPUEndComputePass(compute);
  compute = NULL;

  if (_readbackBuffer) {
    textureBarrier.texture = _texture;
    textureBarrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
    textureBarrier.dstAccess = GPU_ACCESS_TRANSFER_READ;
    textureBarrier.baseMip = 0;
    textureBarrier.mipCount = 1;
    textureBarrier.baseLayer = 0;
    textureBarrier.layerCount = 1;

    barriers.srcStages = GPU_STAGE_COMPUTE;
    barriers.dstStages = GPU_STAGE_TRANSFER;
    barriers.textureBarrierCount = 1;
    barriers.pTextureBarriers = &textureBarrier;
    GPUEncodeBarriers(cmdb, &barriers);

    copy = GPUBeginCopyPass(cmdb, "compute-usl-readback");
    if (!copy) {
      goto cleanup;
    }

    readbackRegion.bufferOffset = 0u;
    readbackRegion.bytesPerRow = 4u;
    readbackRegion.rowsPerImage = 1u;
    readbackRegion.texture.width = 1u;
    readbackRegion.texture.height = 1u;
    readbackRegion.texture.depth = 1u;
    readbackRegion.texture.layerCount = 1u;
    GPUCopyTextureToBuffer(copy, _texture, _readbackBuffer, &readbackRegion);
    GPUEndCopyPass(copy);
    copy = NULL;

    memset(&textureBarrier, 0, sizeof(textureBarrier));
    memset(&barriers, 0, sizeof(barriers));
    textureBarrier.texture = _texture;
    textureBarrier.srcAccess = GPU_ACCESS_TRANSFER_READ;
    textureBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
    textureBarrier.baseMip = 0;
    textureBarrier.mipCount = 1;
    textureBarrier.baseLayer = 0;
    textureBarrier.layerCount = 1;
    barriers.srcStages = GPU_STAGE_TRANSFER;
  } else {
    textureBarrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
    textureBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
    barriers.srcStages = GPU_STAGE_COMPUTE;
  }

  textureBarrier.texture = _texture;
  textureBarrier.baseMip = 0;
  textureBarrier.mipCount = 1;
  textureBarrier.baseLayer = 0;
  textureBarrier.layerCount = 1;

  barriers.dstStages = GPU_STAGE_FRAGMENT;
  barriers.textureBarrierCount = 1;
  barriers.pTextureBarriers = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barriers);

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
  GPUBindRenderGroup(render, 0, _bindGroup, 1, &_computeParamsOffset);
  GPUBindRenderGroup(render, 1, _samplerBindGroup, 0, NULL);
  GPUDraw(render, 6, 1, 0, 0);
  GPUEndRenderPass(render);
  render = NULL;

  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  } else {
    _submittedFrames++;
  }

cleanup:
  if (compute) {
    GPUEndComputePass(compute);
  }
  if (copy) {
    GPUEndCopyPass(copy);
  }
  if (render) {
    GPUEndRenderPass(render);
  }
  GPUEndFrame(frame);
}

- (BOOL)verifyReadback {
  uint8_t pixel[4] = {0, 0, 0, 0};
  GPUResult result;

  result = GPUQueueReadBuffer(_queue,
                              _readbackBuffer,
                              0,
                              pixel,
                              sizeof(pixel));
  if (result != GPU_OK) {
    NSLog(@"GPUQueueReadBuffer failed: %d", result);
    return NO;
  }

  if (pixel[0] > 2u ||
      pixel[1] > 2u ||
      pixel[2] < 180u ||
      pixel[2] > 200u ||
      pixel[3] < 250u) {
    NSLog(@"GPU compute texture readback mismatch: %u %u %u %u",
          pixel[0],
          pixel[1],
          pixel[2],
          pixel[3]);
    return NO;
  }

  return YES;
}

- (void)frameCompleted {
  _completedFrames++;
  if (_exitAfterFrames <= 0 || _terminating) {
    return;
  }

  if (![self verifyReadback]) {
    _validationFailed = YES;
    gComputeUSLValidationFailed = 1;
    exit(1);
  }

  if (_completedFrames >= _exitAfterFrames) {
    _terminating = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp terminate:nil];
    });
  }
}

- (BOOL)validationFailed {
  return _validationFailed || gComputeUSLValidationFailed != 0;
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
}

- (void)cleanupGPU {
  if (_samplerBindGroup) {
    GPUDestroyBindGroup(_samplerBindGroup);
    _samplerBindGroup = NULL;
  }
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
  _computeParamsBuffer = NULL;
  if (_readbackBuffer) {
    GPUDestroyBuffer(_readbackBuffer);
    _readbackBuffer = NULL;
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

  const char *exitAfterFrames = getenv("GPU_SAMPLE_EXIT_AFTER_FRAMES");
  if (exitAfterFrames && exitAfterFrames[0] != '\0') {
    _exitAfterFrames = strtol(exitAfterFrames, NULL, 10);
  }

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
    ComputeUSLApp *delegate;

    (void)argc;
    (void)argv;

    [NSApplication sharedApplication];
    delegate = [ComputeUSLApp new];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp run];
    return [delegate validationFailed] ? 1 : 0;
  }
}
