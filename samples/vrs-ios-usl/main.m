#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#import "../../include/gpu/gpu.h"

enum {
  VRS_FRAMES_IN_FLIGHT = 3u,
  VRS_WARP_DIVISIONS   = 48u
};

typedef struct VRSVertex {
  float position[4];
  float uv[2];
} VRSVertex;

static const VRSVertex kFullscreenVertices[] = {
  { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
  { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }
};

static GPUAdapter *
SelectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    count;
  GPUResult   result;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

@interface VRSViewController : UIViewController {
@private
  CADisplayLink                 *_displayLink;
  GPUInstance                   *_instance;
  GPUAdapter                    *_adapter;
  GPUDevice                     *_device;
  GPUQueue                      *_queue;
  GPUSurface                    *_surface;
  GPUSwapchain                  *_swapchain;
  GPUShaderLibrary              *_library;
  GPUShaderLayout               *_shaderLayout;
  GPURenderPipeline             *_gridPipeline;
  GPURenderPipeline             *_presentPipeline;
  GPURasterizationRateMapEXT    *_rateMap;
  GPUTexture                    *_rateTarget;
  GPUTextureView                *_rateTargetView;
  GPUBuffer                     *_fullscreenBuffer;
  GPUBuffer                     *_warpBuffer;
  GPUSampler                    *_sampler;
  GPUBindGroup                  *_textureGroup;
  GPUBindGroup                  *_samplerGroup;
  GPUFence                      *_frameFences[VRS_FRAMES_IN_FLIGHT];
  uint32_t                       _warpVertexCount;
  uint32_t                       _frameIndex;
  uint32_t                       _viewWidth;
  uint32_t                       _viewHeight;
  uint32_t                       _drawableWidth;
  uint32_t                       _drawableHeight;
  bool                           _fenceSubmitted[VRS_FRAMES_IN_FLIGHT];
}
- (void)setRenderingPaused:(BOOL)paused;
@end

@implementation VRSViewController

- (void)waitForGPU {
  for (uint32_t i = 0u; i < VRS_FRAMES_IN_FLIGHT; i++) {
    if (_fenceSubmitted[i]) {
      (void)GPUWaitFence(_frameFences[i], UINT64_MAX);
      _fenceSubmitted[i] = false;
    }
  }
}

- (void)destroyRateResources {
  GPUDestroyBindGroup(_samplerGroup);
  GPUDestroyBindGroup(_textureGroup);
  GPUDestroyBuffer(_warpBuffer);
  GPUDestroyTextureView(_rateTargetView);
  GPUDestroyTexture(_rateTarget);
  GPUDestroyRasterizationRateMapEXT(_rateMap);
  _samplerGroup    = NULL;
  _textureGroup    = NULL;
  _warpBuffer      = NULL;
  _rateTargetView  = NULL;
  _rateTarget      = NULL;
  _rateMap         = NULL;
  _warpVertexCount = 0u;
}

- (BOOL)createPipelines {
  GPUVertexAttribute attributes[] = {
    {
      .shaderLocation = 0u,
      .format         = GPU_VERTEX_FORMAT_FLOAT32X4,
      .offset         = offsetof(VRSVertex, position)
    },
    {
      .shaderLocation = 1u,
      .format         = GPU_VERTEX_FORMAT_FLOAT32X2,
      .offset         = offsetof(VRSVertex, uv)
    }
  };
  GPUVertexBufferLayout vertexLayouts[] = {
    {
      .strideBytes    = sizeof(VRSVertex),
      .stepMode       = GPU_VERTEX_STEP_MODE_VERTEX,
      .attributeCount = (uint32_t)GPU_ARRAY_LEN(attributes),
      .pAttributes    = attributes
    }
  };
  GPUColorTargetState colorTargets[] = {
    {
      .format = GPUGetSwapchainFormat(_swapchain),
      .blend  = {
        .enabled   = false,
        .writeMask = GPU_COLOR_WRITE_ALL
      }
    }
  };
  GPURenderPipelineCreateInfo info = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
      .structSize = sizeof(GPURenderPipelineCreateInfo)
    },
    .layout             = _shaderLayout->pipelineLayout,
    .library            = _library,
    .vertexEntry        = "vrs_vs",
    .vertex             = {
      .bufferLayoutCount = 1u,
      .pBufferLayouts    = vertexLayouts
    },
    .colorTargetCount   = 1u,
    .pColorTargets      = colorTargets,
    .depthStencilFormat = GPU_FORMAT_UNDEFINED,
    .primitiveTopology  = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode           = GPU_CULL_MODE_NONE,
    .frontFace          = GPU_FRONT_FACE_CCW,
    .multisample        = {
      .sampleCount           = 1u,
      .sampleMask            = 0xffffffffu,
      .alphaToCoverageEnable = false
    }
  };

  info.label         = "vrs-ios-grid-pipeline";
  info.fragmentEntry = "grid_fs";
  if (GPUCreateRenderPipeline(_device, &info, &_gridPipeline) != GPU_OK) {
    return NO;
  }
  info.label         = "vrs-ios-present-pipeline";
  info.fragmentEntry = "present_fs";
  return GPUCreateRenderPipeline(_device,
                                 &info,
                                 &_presentPipeline) == GPU_OK;
}

- (BOOL)createStaticResources {
  GPUBufferCreateInfo vertexInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .structSize = sizeof(GPUBufferCreateInfo)
    },
    .label     = "vrs-ios-fullscreen-vertices",
    .sizeBytes = sizeof(kFullscreenVertices),
    .usage     = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST
  };
  GPUSamplerCreateInfo samplerInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .structSize = sizeof(GPUSamplerCreateInfo)
    },
    .label = "vrs-ios-linear-sampler",
    .desc  = {
      .minFilter = GPU_FILTER_LINEAR,
      .magFilter = GPU_FILTER_LINEAR,
      .mipFilter = GPU_MIP_FILTER_NEAREST,
      .addressU  = GPU_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressV  = GPU_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressW  = GPU_ADDRESS_MODE_CLAMP_TO_EDGE
    }
  };

  if (GPUCreateBuffer(_device,
                      &vertexInfo,
                      &_fullscreenBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _fullscreenBuffer,
                          0u,
                          kFullscreenVertices,
                          sizeof(kFullscreenVertices)) != GPU_OK ||
      GPUCreateSampler(_device, &samplerInfo, false, &_sampler) != GPU_OK) {
    return NO;
  }

  for (uint32_t i = 0u; i < VRS_FRAMES_IN_FLIGHT; i++) {
    GPUFenceCreateInfo fenceInfo = {
      .chain = {
        .sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .structSize = sizeof(GPUFenceCreateInfo)
      },
      .label    = "vrs-ios-frame-fence",
      .signaled = true
    };
    if (GPUCreateFence(_device, &fenceInfo, &_frameFences[i]) != GPU_OK) {
      return NO;
    }
  }
  return YES;
}

- (BOOL)createWarpVertices:(GPUExtent2D)physicalSize {
  const uint32_t pointWidth  = VRS_WARP_DIVISIONS + 1u;
  const uint32_t pointCount  = pointWidth * pointWidth;
  const uint32_t vertexCount = VRS_WARP_DIVISIONS *
                               VRS_WARP_DIVISIONS * 6u;
  VRSVertex          *points;
  VRSVertex          *vertices;
  GPUBufferCreateInfo bufferInfo;
  uint32_t            cursor;
  BOOL                ok;

  points   = calloc(pointCount, sizeof(*points));
  vertices = calloc(vertexCount, sizeof(*vertices));
  if (!points || !vertices) {
    free(vertices);
    free(points);
    return NO;
  }

  ok = YES;
  for (uint32_t y = 0u; y < pointWidth && ok; y++) {
    for (uint32_t x = 0u; x < pointWidth; x++) {
      const float fx = (float)x / (float)VRS_WARP_DIVISIONS;
      const float fy = (float)y / (float)VRS_WARP_DIVISIONS;
      GPUCoordinate2D screen;
      GPUCoordinate2D physical;
      VRSVertex       *vertex;

      screen.x = fx * (float)_drawableWidth;
      screen.y = fy * (float)_drawableHeight;
      if (GPUMapRasterizationRateScreenToPhysicalEXT(_rateMap,
                                                      0u,
                                                      screen,
                                                      &physical) != GPU_OK) {
        ok = NO;
        break;
      }
      vertex              = &points[y * pointWidth + x];
      vertex->position[0] = fx * 2.0f - 1.0f;
      vertex->position[1] = 1.0f - fy * 2.0f;
      vertex->position[2] = 0.0f;
      vertex->position[3] = 1.0f;
      vertex->uv[0]       = physical.x / (float)physicalSize.width;
      vertex->uv[1]       = physical.y / (float)physicalSize.height;
    }
  }

  cursor = 0u;
  for (uint32_t y = 0u; y < VRS_WARP_DIVISIONS && ok; y++) {
    for (uint32_t x = 0u; x < VRS_WARP_DIVISIONS; x++) {
      const VRSVertex p00 = points[y * pointWidth + x];
      const VRSVertex p10 = points[y * pointWidth + x + 1u];
      const VRSVertex p01 = points[(y + 1u) * pointWidth + x];
      const VRSVertex p11 = points[(y + 1u) * pointWidth + x + 1u];

      vertices[cursor++] = p00;
      vertices[cursor++] = p01;
      vertices[cursor++] = p10;
      vertices[cursor++] = p10;
      vertices[cursor++] = p01;
      vertices[cursor++] = p11;
    }
  }
  free(points);
  if (!ok) {
    free(vertices);
    return NO;
  }

  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vrs-ios-warp-vertices";
  bufferInfo.sizeBytes        = (uint64_t)vertexCount * sizeof(*vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  ok = GPUCreateBuffer(_device, &bufferInfo, &_warpBuffer) == GPU_OK &&
       GPUQueueWriteBuffer(_queue,
                           _warpBuffer,
                           0u,
                           vertices,
                           bufferInfo.sizeBytes) == GPU_OK;
  free(vertices);
  if (ok) {
    _warpVertexCount = vertexCount;
  }
  return ok;
}

- (BOOL)createRateBindGroups {
  GPUBindGroupEntry textureEntry = {
    .binding     = 0u,
    .bindingType = GPU_BINDING_SAMPLED_TEXTURE,
    .textureView = _rateTargetView
  };
  GPUBindGroupCreateInfo textureInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
      .structSize = sizeof(GPUBindGroupCreateInfo)
    },
    .label      = "vrs-ios-source-group",
    .layout     = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1u,
    .pEntries   = &textureEntry
  };
  GPUBindGroupEntry samplerEntry = {
    .binding     = 0u,
    .bindingType = GPU_BINDING_SAMPLER,
    .sampler     = _sampler
  };
  GPUBindGroupCreateInfo samplerInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
      .structSize = sizeof(GPUBindGroupCreateInfo)
    },
    .label      = "vrs-ios-sampler-group",
    .layout     = _shaderLayout->bindGroupLayouts[1],
    .entryCount = 1u,
    .pEntries   = &samplerEntry
  };

  return GPUCreateBindGroup(_device,
                            &textureInfo,
                            &_textureGroup) == GPU_OK &&
         GPUCreateBindGroup(_device,
                            &samplerInfo,
                            &_samplerGroup) == GPU_OK;
}

- (BOOL)rebuildRateResourcesWithWidth:(uint32_t)width
                               height:(uint32_t)height {
  static const float horizontal[] = {0.30f, 0.55f, 1.0f, 0.55f, 0.30f};
  static const float vertical[]   = {0.30f, 0.55f, 1.0f, 0.55f, 0.30f};
  GPURasterizationRateLayerEXT layer = {
    .pHorizontal     = horizontal,
    .pVertical       = vertical,
    .horizontalCount = (uint32_t)GPU_ARRAY_LEN(horizontal),
    .verticalCount   = (uint32_t)GPU_ARRAY_LEN(vertical)
  };
  GPURasterizationRateMapCreateInfoEXT mapInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_CREATE_INFO_EXT,
      .structSize = sizeof(GPURasterizationRateMapCreateInfoEXT)
    },
    .label       = "vrs-ios-foveated-map",
    .pLayers     = &layer,
    .screenSize  = {width, height},
    .layerCount  = 1u
  };
  GPUTextureCreateInfo textureInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO,
      .structSize = sizeof(GPUTextureCreateInfo)
    },
    .label         = "vrs-ios-physical-target",
    .dimension     = GPU_TEXTURE_DIMENSION_2D,
    .format        = GPUGetSwapchainFormat(_swapchain),
    .depthOrLayers = 1u,
    .mipLevelCount = 1u,
    .sampleCount   = 1u,
    .usage         = GPU_TEXTURE_USAGE_COLOR_TARGET |
                     GPU_TEXTURE_USAGE_SAMPLED
  };
  GPUTextureViewCreateInfo viewInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO,
      .structSize = sizeof(GPUTextureViewCreateInfo)
    },
    .label           = "vrs-ios-physical-target-view",
    .viewType        = GPU_TEXTURE_VIEW_2D,
    .format          = GPUGetSwapchainFormat(_swapchain),
    .baseMipLevel    = 0u,
    .mipLevelCount   = 1u,
    .baseArrayLayer  = 0u,
    .arrayLayerCount = 1u
  };
  GPUExtent2D physicalSize;

  if (width == 0u || height == 0u) {
    return NO;
  }
  [self waitForGPU];
  [self destroyRateResources];

  if (GPUCreateRasterizationRateMapEXT(_device,
                                       &mapInfo,
                                       &_rateMap) != GPU_OK ||
      GPUGetRasterizationRateMapPhysicalSizeEXT(_rateMap,
                                                 0u,
                                                 &physicalSize) != GPU_OK ||
      physicalSize.width == 0u || physicalSize.height == 0u) {
    NSLog(@"GPU: failed to create the rasterization-rate map");
    return NO;
  }

  textureInfo.width  = physicalSize.width;
  textureInfo.height = physicalSize.height;
  _drawableWidth     = width;
  _drawableHeight    = height;
  if (GPUCreateTexture(_device, &textureInfo, &_rateTarget) != GPU_OK ||
      GPUCreateTextureView(_rateTarget,
                           &viewInfo,
                           &_rateTargetView) != GPU_OK ||
      ![self createWarpVertices:physicalSize] ||
      ![self createRateBindGroups]) {
    NSLog(@"GPU: failed to create VRS intermediate resources");
    [self destroyRateResources];
    return NO;
  }

  NSLog(@"GPU: VRS logical %ux%u -> physical %ux%u",
        width,
        height,
        physicalSize.width,
        physicalSize.height);
  return YES;
}

- (BOOL)createGPU {
  GPUDeviceCreateInfo deviceInfo;
  GPUVRSCapabilitiesEXT caps;
  GPUFeature feature;
  NSURL     *artifactURL;
  NSData    *artifact;
  CGFloat    scale;

  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    return NO;
  }
  _adapter = SelectAdapter(_instance);
  memset(&caps, 0, sizeof(caps));
  if (!_adapter ||
      GPUGetVRSCapabilitiesEXT(_adapter, &caps) != GPU_OK ||
      (caps.modes & GPU_VRS_RATE_MAP_BIT_EXT) == 0u) {
    NSLog(@"GPU: this device has no Metal rasterization-rate map support");
    return NO;
  }

  feature = GPU_FEATURE_VARIABLE_RATE_SHADING;
  memset(&deviceInfo, 0, sizeof(deviceInfo));
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.label                 = "vrs-ios-device";
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(_adapter, &deviceInfo, &_device) != GPU_OK ||
      !(_queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u))) {
    return NO;
  }

  scale    = UIScreen.mainScreen.scale;
  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)self.view,
                                        GPU_SURFACE_APPLE_UIVIEW,
                                        scale);
  _viewWidth  = (uint32_t)self.view.bounds.size.width;
  _viewHeight = (uint32_t)self.view.bounds.size.height;
  _swapchain  = GPUCreateSwapchainDefault(_device,
                                           _surface,
                                           _viewWidth,
                                           _viewHeight);
  if (!_surface || !_swapchain) {
    return NO;
  }

  artifactURL = [NSBundle.mainBundle URLForResource:@"vrs"
                                      withExtension:@"us"];
  artifact = artifactURL ? [NSData dataWithContentsOfURL:artifactURL] : nil;
  if (!artifact ||
      GPUCreateShaderLibraryFromUSL(_device,
                                    artifact.bytes,
                                    (uint64_t)artifact.length,
                                    &_library) != GPU_OK ||
      GPUCreateShaderLayout(_device,
                            _library,
                            &_shaderLayout) != GPU_OK ||
      !_shaderLayout || _shaderLayout->bindGroupLayoutCount != 2u ||
      !_shaderLayout->bindGroupLayouts[0] ||
      !_shaderLayout->bindGroupLayouts[1] ||
      ![self createPipelines] ||
      ![self createStaticResources]) {
    NSLog(@"GPU: failed to create VRS shader resources");
    return NO;
  }

  return [self rebuildRateResourcesWithWidth:
            (uint32_t)lround(self.view.bounds.size.width * scale)
                                         height:
            (uint32_t)lround(self.view.bounds.size.height * scale)];
}

- (void)drawFrame {
  const uint32_t fenceIndex = _frameIndex % VRS_FRAMES_IN_FLIGHT;
  GPURasterizationRateMapRenderPassEXT rateExtension;
  GPURenderPassColorAttachment         color;
  GPURenderPassCreateInfo              passInfo;
  GPUTextureBarrier                    textureBarrier;
  GPUBarrierBatch                      barrierBatch;
  GPUBufferBinding                     vertexBinding;
  GPUViewport                          viewport;
  GPURenderPassEncoder                *pass;
  GPUCommandBuffer                    *cmdb;
  GPUFrame                            *frame;
  GPUQueueSubmitInfo                   submit;

  if (!_rateMap || !_rateTargetView || !_warpBuffer) {
    return;
  }
  if (_fenceSubmitted[fenceIndex]) {
    if (GPUWaitFence(_frameFences[fenceIndex], UINT64_MAX) != GPU_OK) {
      return;
    }
    _fenceSubmitted[fenceIndex] = false;
  }
  GPUResetFence(_frameFences[fenceIndex]);

  frame = GPUBeginFrame(_swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(_queue,
                              "vrs-ios-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  memset(&rateExtension, 0, sizeof(rateExtension));
  rateExtension.chain.sType =
    GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_RENDER_PASS_EXT;
  rateExtension.chain.structSize = sizeof(rateExtension);
  rateExtension.map              = _rateMap;
  memset(&color, 0, sizeof(color));
  color.view                  = _rateTargetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  memset(&passInfo, 0, sizeof(passInfo));
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.chain.pNext          = &rateExtension;
  passInfo.label                = "vrs-ios-rate-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  viewport.x          = 0.0f;
  viewport.y          = 0.0f;
  viewport.width      = (float)_drawableWidth;
  viewport.height     = (float)_drawableHeight;
  viewport.minDepth   = 0.0f;
  viewport.maxDepth   = 1.0f;
  vertexBinding.buffer = _fullscreenBuffer;
  vertexBinding.offset = 0u;
  GPUBindRenderPipeline(pass, _gridPipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  GPUSetViewport(pass, &viewport);
  GPUDraw(pass,
          (uint32_t)GPU_ARRAY_LEN(kFullscreenVertices),
          1u,
          0u,
          0u);
  GPUEndRenderPass(pass);

  memset(&textureBarrier, 0, sizeof(textureBarrier));
  textureBarrier.texture    = _rateTarget;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  memset(&barrierBatch, 0, sizeof(barrierBatch));
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  memset(&color, 0, sizeof(color));
  color.view                    = GPUFrameGetTargetView(frame);
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  color.clearColor.float32[0]   = 0.005f;
  color.clearColor.float32[1]   = 0.008f;
  color.clearColor.float32[2]   = 0.015f;
  color.clearColor.float32[3]   = 1.0f;
  memset(&passInfo, 0, sizeof(passInfo));
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "vrs-ios-present-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  vertexBinding.buffer = _warpBuffer;
  vertexBinding.offset = 0u;
  GPUBindRenderPipeline(pass, _presentPipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(pass, 0u, _textureGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, _samplerGroup, 0u, NULL);
  GPUDraw(pass, _warpVertexCount, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  GPUSchedulePresent(cmdb, frame);
  memset(&submit, 0, sizeof(submit));
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &cmdb;
  submit.fence              = _frameFences[fenceIndex];
  if (GPUQueueSubmit(_queue, &submit) == GPU_OK) {
    _fenceSubmitted[fenceIndex] = true;
    _frameIndex++;
  }
  GPUEndFrame(frame);
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.blackColor;

  if (![self createGPU]) {
    return;
  }
  _displayLink = [CADisplayLink displayLinkWithTarget:self
                                              selector:@selector(drawFrame)];
  _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30.0f,
                                                               120.0f,
                                                               60.0f);
  [_displayLink addToRunLoop:NSRunLoop.mainRunLoop
                     forMode:NSRunLoopCommonModes];
}

- (void)viewDidLayoutSubviews {
  uint32_t width;
  uint32_t height;
  uint32_t drawableWidth;
  uint32_t drawableHeight;
  CGFloat  scale;

  [super viewDidLayoutSubviews];
  if (!_swapchain) {
    return;
  }
  width          = (uint32_t)self.view.bounds.size.width;
  height         = (uint32_t)self.view.bounds.size.height;
  scale          = UIScreen.mainScreen.scale;
  drawableWidth  = (uint32_t)lround(self.view.bounds.size.width * scale);
  drawableHeight = (uint32_t)lround(self.view.bounds.size.height * scale);
  if (width == 0u || height == 0u || drawableWidth == 0u ||
      drawableHeight == 0u ||
      (width == _viewWidth && height == _viewHeight &&
       drawableWidth == _drawableWidth &&
       drawableHeight == _drawableHeight)) {
    return;
  }
  if (GPUResizeSwapchain(_swapchain, width, height) == GPU_OK &&
      [self rebuildRateResourcesWithWidth:drawableWidth
                                   height:drawableHeight]) {
    _viewWidth  = width;
    _viewHeight = height;
  }
}

- (void)setRenderingPaused:(BOOL)paused {
  _displayLink.paused = paused;
}

- (void)dealloc {
  [_displayLink invalidate];
  [self waitForGPU];
  [self destroyRateResources];
  for (uint32_t i = 0u; i < VRS_FRAMES_IN_FLIGHT; i++) {
    GPUDestroyFence(_frameFences[i]);
  }
  GPUDestroySampler(_sampler);
  GPUDestroyBuffer(_fullscreenBuffer);
  GPUDestroyRenderPipeline(_presentPipeline);
  GPUDestroyRenderPipeline(_gridPipeline);
  GPUDestroyShaderLayout(_shaderLayout);
  GPUDestroyShaderLibrary(_library);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);
}

@end

@interface VRSAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow            *window;
@property(nonatomic, strong) VRSViewController  *controller;
@end

@implementation VRSAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  (void)application;
  (void)launchOptions;

  self.controller = [VRSViewController new];
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = self.controller;
  [self.window makeKeyAndVisible];
  return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
  (void)application;
  [self.controller setRenderingPaused:YES];
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
  (void)application;
  [self.controller setRenderingPaused:NO];
}

@end

int
main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc,
                             argv,
                             nil,
                             NSStringFromClass(VRSAppDelegate.class));
  }
}
