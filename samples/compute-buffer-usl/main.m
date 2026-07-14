#import <AppKit/AppKit.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#import <dispatch/dispatch.h>

#import "../../include/gpu/gpu.h"
#import "../common/SampleApp.h"
#import "../common/SampleStats.h"
#import "../common/SampleUSL.h"

#ifndef GPU_SAMPLE_BACKEND
#  define GPU_SAMPLE_BACKEND GPU_BACKEND_METAL
#endif

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

@interface ComputeBufferUSLApp : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@private
  NSWindow           *_window;
  NSView             *_view;
  GPUInstance        *_instance;
  GPUAdapter         *_adapter;
  GPUDevice          *_device;
  GPUQueue           *_queue;
  GPUSurface         *_surface;
  GPUSwapchain       *_swapchain;
  GPUShaderLibrary   *_library;
  GPUShaderLayout    *_shaderLayout;
  GPUComputePipeline *_computePipeline;
  GPURenderPipeline  *_renderPipeline;
  GPUBuffer          *_vertexBuffer;
  GPUBuffer          *_indexBuffer;
  GPUBuffer          *_indirectBuffer;
  GPUBuffer          *_dispatchBuffer;
  GPUBindGroup       *_computeBindGroup;
  NSTimer            *_timer;
  NSInteger           _exitAfterFrames;
  NSInteger           _submittedFrames;
  NSInteger           _completedFrames;
  BOOL                _assertZeroAlloc;
  BOOL                _validationFailed;
  BOOL                _terminating;
  BOOL                _skipComputeBind;
}
- (void)frameCompleted;
- (BOOL)validationFailed;
@end

static const GeneratedVertex kExpectedVertices[] = {
  { { -0.6f, -0.6f, 0.0f, 1.0f }, { 1.0f, 0.2f, 0.1f, 1.0f } },
  { {  0.6f, -0.6f, 0.0f, 1.0f }, { 0.1f, 1.0f, 0.3f, 1.0f } },
  { {  0.0f,  0.6f, 0.0f, 1.0f }, { 0.2f, 0.4f, 1.0f, 1.0f } },
};

static const uint16_t kIndices[] = {0u, 1u, 2u};
static const uint32_t kDispatchArgs[] = {3u, 1u, 1u};
static const uint32_t kExpectedDrawArgs[] = {3u, 1u, 0u, 0u, 0u};

static volatile int gComputeBufferValidationFailed = 0;

static void
ComputeBufferFrameComplete(void *sender, GPUCommandBuffer *cmdb) {
  (void)cmdb;
  ComputeBufferUSLApp *app = (__bridge ComputeBufferUSLApp *)sender;
  [app frameCompleted];
}

@implementation ComputeBufferUSLApp

- (BOOL)setupWindow {
  return GPUSampleCreateWindow(@"GPU USL Compute Buffer",
                               self,
                               &_window,
                               &_view);
}

- (BOOL)setupGPU {
  GPUInstanceCreateInfo instanceInfo = {0};
  uint32_t              adapterCount;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_SAMPLE_BACKEND;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  adapterCount = 1u;
  if (GPUEnumerateAdapters(_instance, &adapterCount, &_adapter) != GPU_OK ||
      !_adapter) {
    NSLog(@"GPU: failed to get adapter");
    return NO;
  }

  _device = GPUCreateDeviceWithDefaultQueues(_adapter);
  _queue  = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_device || !_queue) {
    NSLog(@"GPU: failed to create device or graphics queue");
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)_view,
                                        GPU_SURFACE_APPLE_NSVIEW,
                                        _window.backingScaleFactor ?: 1.0f);
  if (!_surface) {
    NSLog(@"GPU: failed to create surface");
    return NO;
  }

  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         (uint32_t)_view.bounds.size.width,
                                         (uint32_t)_view.bounds.size.height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }

  if (getenv("GPU_SAMPLE_VERBOSE_VALIDATION")) {
    GPURuntimeConfig runtimeConfig = {
      .chain = { .sType = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG,
                 .structSize = sizeof(GPURuntimeConfig) },
      .validationMode = GPU_VALIDATION_BASIC,
      .enableVerboseLogs = true
    };
    (void)GPUConfigureRuntime(_device, &runtimeConfig);
  }

  if (!GPUSampleLoadUSL(_device,
                        @"compute_buffer.us",
                        2u,
                        &_library,
                        &_shaderLayout)) {
    return NO;
  }

  GPUComputePipelineCreateInfo computeInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
               .structSize = sizeof(GPUComputePipelineCreateInfo) },
    .label = "compute-buffer-usl-fill-vertices",
    .layout = _shaderLayout->pipelineLayout,
    .library = _library,
    .entryPoint = "fill_vertices"
  };
  if (GPUCreateComputePipeline(_device, &computeInfo, &_computePipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create compute pipeline");
    return NO;
  }

  GPUVertexAttribute vertexAttrs[] = {
    { .shaderLocation = 0, .format = GPU_VERTEX_FORMAT_FLOAT32X4, .offset = offsetof(GeneratedVertex, position) },
    { .shaderLocation = 1, .format = GPU_VERTEX_FORMAT_FLOAT32X4, .offset = offsetof(GeneratedVertex, color) }
  };
  GPUVertexBufferLayout vertexBuffers[] = {
    {
      .strideBytes = sizeof(GeneratedVertex),
      .stepMode = GPU_VERTEX_STEP_MODE_VERTEX,
      .attributeCount = 2,
      .pAttributes = vertexAttrs
    }
  };
  GPUColorTargetState colorTargets[] = {
    {
      .format = GPUGetSwapchainFormat(_swapchain),
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
    .label = "compute-buffer-usl-render-pipeline",
    .layout = _shaderLayout->pipelineLayout,
    .library = _library,
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
  if (GPUCreateRenderPipeline(_device, &renderInfo, &_renderPipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create render pipeline");
    return NO;
  }

  GPUBufferCreateInfo vertexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-buffer-usl-vertices",
    .sizeBytes = sizeof(GeneratedVertex) * 3u,
    .usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC
  };
  if (GPUCreateBuffer(_device, &vertexBufferInfo, &_vertexBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create vertex/storage buffer");
    return NO;
  }

  GPUBufferCreateInfo indexBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-buffer-usl-indices",
    .sizeBytes = sizeof(kIndices),
    .usage = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &indexBufferInfo, &_indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _indexBuffer,
                          0u,
                          kIndices,
                          sizeof(kIndices)) != GPU_OK) {
    NSLog(@"GPU: failed to create index buffer");
    return NO;
  }

  GPUBufferCreateInfo indirectBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-buffer-usl-draw-args",
    .sizeBytes = sizeof(kExpectedDrawArgs),
    .usage = GPU_BUFFER_USAGE_STORAGE |
             GPU_BUFFER_USAGE_INDIRECT |
             GPU_BUFFER_USAGE_COPY_SRC
  };
  if (GPUCreateBuffer(_device,
                      &indirectBufferInfo,
                      &_indirectBuffer) != GPU_OK) {
    NSLog(@"GPU: failed to create indirect/storage buffer");
    return NO;
  }

  GPUBufferCreateInfo dispatchBufferInfo = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .structSize = sizeof(GPUBufferCreateInfo) },
    .label = "compute-buffer-usl-dispatch-args",
    .sizeBytes = sizeof(kDispatchArgs),
    .usage = GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device,
                      &dispatchBufferInfo,
                      &_dispatchBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _dispatchBuffer,
                          0u,
                          kDispatchArgs,
                          sizeof(kDispatchArgs)) != GPU_OK) {
    NSLog(@"GPU: failed to create dispatch argument buffer");
    return NO;
  }

  if (!_shaderLayout ||
      _shaderLayout->bindGroupLayoutCount < 2u ||
      !_shaderLayout->bindGroupLayouts[1]) {
    NSLog(@"GPU: expected compute buffer shader layout group 1");
    return NO;
  }

  uint32_t layoutEntryCount = 0u;
  const GPUBindGroupLayoutEntry *layoutEntries = GPUGetBindGroupLayoutEntries(
    _shaderLayout->bindGroupLayouts[1],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 2u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_STORAGE_BUFFER) {
    NSLog(@"GPU: unexpected compute buffer reflection layout");
    return NO;
  }

  GPUBindGroupEntry groupEntries[2] = {0};
  groupEntries[0].binding       = 0u;
  groupEntries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[0].buffer.buffer = _vertexBuffer;
  groupEntries[0].buffer.size   = vertexBufferInfo.sizeBytes;
  groupEntries[1].binding       = 1u;
  groupEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[1].buffer.buffer = _indirectBuffer;
  groupEntries[1].buffer.size   = indirectBufferInfo.sizeBytes;

  GPUBindGroupCreateInfo group1Info = {
    .chain = { .sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
               .structSize = sizeof(GPUBindGroupCreateInfo) },
    .label = "compute-buffer-usl-group1",
    .layout = _shaderLayout->bindGroupLayouts[1],
    .entryCount = 2,
    .pEntries = groupEntries
  };
  if (GPUCreateBindGroup(_device, &group1Info, &_computeBindGroup) != GPU_OK) {
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
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo rp = {0};
  GPUBufferBinding vertexBuffer = {0};
  GPUBufferBarrier barriers[2] = {0};
  GPUBarrierBatch barrierBatch = {0};
  GPUResult submitResult = GPU_OK;

  if (_exitAfterFrames > 0 && _submittedFrames >= _exitAfterFrames) {
    return;
  }

  frame = GPUBeginFrame(_swapchain);
  if (!frame) {
    return;
  }

  if (GPUAcquireCommandBuffer(_queue, "compute-buffer-frame", &cmdb) != GPU_OK || !cmdb) {
    goto cleanup;
  }
  if (_exitAfterFrames > 0) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         (__bridge void *)self,
                                         ComputeBufferFrameComplete);
  }

  compute = GPUBeginComputePass(cmdb, "compute-buffer-usl-fill");
  if (!compute) {
    goto cleanup;
  }
  GPUBindComputePipeline(compute, _computePipeline);
  if (!_skipComputeBind) {
    GPUBindComputeGroup(compute, 1, _computeBindGroup, 0, NULL);
  }
  GPUDispatchIndirect(compute, _dispatchBuffer, 0u);
  GPUEndComputePass(compute);
  compute = NULL;

  barriers[0].buffer    = _vertexBuffer;
  barriers[0].srcAccess = GPU_ACCESS_SHADER_WRITE;
  barriers[0].dstAccess = GPU_ACCESS_SHADER_READ;
  barriers[0].sizeBytes = sizeof(GeneratedVertex) * 3u;
  barriers[1].buffer    = _indirectBuffer;
  barriers[1].srcAccess = GPU_ACCESS_SHADER_WRITE;
  barriers[1].dstAccess = GPU_ACCESS_INDIRECT_READ;
  barriers[1].sizeBytes = sizeof(kExpectedDrawArgs);
  barrierBatch.srcStages          = GPU_STAGE_COMPUTE;
  barrierBatch.dstStages          = GPU_STAGE_VERTEX;
  barrierBatch.bufferBarrierCount = 2u;
  barrierBatch.pBufferBarriers     = barriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  color.view = GPUFrameGetTargetView(frame);
  color.loadOp = GPU_LOAD_OP_CLEAR;
  color.storeOp = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.03f;
  color.clearColor.float32[1] = 0.03f;
  color.clearColor.float32[2] = 0.04f;
  color.clearColor.float32[3] = 1.0f;

  rp.label = "compute-buffer-usl-render-pass";
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
  GPUBindIndexBuffer(render, _indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexedIndirect(render, _indirectBuffer, 0u);
  GPUEndRenderPass(render);
  render = NULL;

  submitResult = GPUFinishFrame(_queue, cmdb, frame);
  frame = NULL;
  if (submitResult != GPU_OK) {
    NSLog(@"GPUFinishFrame failed: %d", submitResult);
  } else {
    _submittedFrames++;
    if (!GPUSampleCheckZeroAlloc(_device,
                                 (uint32_t)_submittedFrames,
                                 _assertZeroAlloc,
                                 "GPU compute render")) {
      _validationFailed = YES;
      gComputeBufferValidationFailed = 1;
      _terminating = YES;
      [_timer invalidate];
      _timer = nil;
      [NSApp terminate:nil];
      return;
    }
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

- (BOOL)verifyReadback {
  GeneratedVertex vertices[3];
  uint32_t        drawArgs[5];
  GPUResult result;

  memset(vertices, 0, sizeof(vertices));
  memset(drawArgs, 0, sizeof(drawArgs));
  result = GPUQueueReadBuffer(_queue,
                              _vertexBuffer,
                              0,
                              vertices,
                              sizeof(vertices));
  if (result != GPU_OK) {
    NSLog(@"GPUQueueReadBuffer failed: %d", result);
    return NO;
  }
  result = GPUQueueReadBuffer(_queue,
                              _indirectBuffer,
                              0u,
                              drawArgs,
                              sizeof(drawArgs));
  if (result != GPU_OK) {
    NSLog(@"GPUQueueReadBuffer for draw args failed: %d", result);
    return NO;
  }

  for (NSUInteger i = 0; i < 3; i++) {
    for (NSUInteger j = 0; j < 4; j++) {
      if (fabsf(vertices[i].position[j] - kExpectedVertices[i].position[j]) > 0.0001f ||
          fabsf(vertices[i].color[j] - kExpectedVertices[i].color[j]) > 0.0001f) {
        if (!_skipComputeBind) {
          NSLog(@"GPU readback mismatch at vertex %lu component %lu",
                (unsigned long)i,
                (unsigned long)j);
        }
        return NO;
      }
    }
  }

  if (memcmp(drawArgs, kExpectedDrawArgs, sizeof(drawArgs)) != 0) {
    if (!_skipComputeBind) {
      NSLog(@"GPU indirect draw argument readback mismatch");
    }
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
    gComputeBufferValidationFailed = 1;
    if (_exitAfterFrames > 0) {
      exit(1);
    }
  }

  if (_completedFrames >= _exitAfterFrames) {
    _terminating = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp terminate:nil];
    });
  }
}

- (BOOL)validationFailed {
  return _validationFailed || gComputeBufferValidationFailed != 0;
}

- (void)tick:(NSTimer *)timer {
  (void)timer;
  [self renderFrame];
}

- (void)cleanupGPU {
  if (_computeBindGroup) {
    GPUDestroyBindGroup(_computeBindGroup);
    _computeBindGroup = NULL;
  }
  if (_renderPipeline) {
    GPUDestroyRenderPipeline(_renderPipeline);
    _renderPipeline = NULL;
  }
  if (_computePipeline) {
    GPUDestroyComputePipeline(_computePipeline);
    _computePipeline = NULL;
  }
  if (_vertexBuffer) {
    GPUDestroyBuffer(_vertexBuffer);
    _vertexBuffer = NULL;
  }
  if (_indexBuffer) {
    GPUDestroyBuffer(_indexBuffer);
    _indexBuffer = NULL;
  }
  if (_indirectBuffer) {
    GPUDestroyBuffer(_indirectBuffer);
    _indirectBuffer = NULL;
  }
  if (_dispatchBuffer) {
    GPUDestroyBuffer(_dispatchBuffer);
    _dispatchBuffer = NULL;
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
    _queue = NULL;
  }
  if (_instance) {
    GPUDestroyInstance(_instance);
    _instance = NULL;
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
  }
  _assertZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  _skipComputeBind = getenv("GPU_SAMPLE_SKIP_COMPUTE_BIND") != NULL;

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

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  [_timer invalidate];
  _timer = nil;
  [self cleanupGPU];
}

@end

int
main(int argc, const char *argv[]) {
  @autoreleasepool {
    (void)argc;
    (void)argv;

    NSApplication *app = [NSApplication sharedApplication];
    ComputeBufferUSLApp *delegate = [[ComputeBufferUSLApp alloc] init];
    app.delegate = delegate;
    [app run];
    return [delegate validationFailed] ? 1 : 0;
  }
}
