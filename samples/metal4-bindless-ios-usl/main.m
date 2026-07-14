#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#import "../../include/gpu/gpu.h"

enum {
  kResourceCount       = 2u,
  kSelectionBufferSize = 256u
};

static const uint8_t kTexturePixels[kResourceCount][16] = {
  {
    255u,  32u,  32u, 255u,   32u,  64u, 255u, 255u,
     32u,  64u, 255u, 255u,  255u,  32u,  32u, 255u
  },
  {
     32u, 255u,  96u, 255u,  255u, 224u,  32u, 255u,
    255u, 224u,  32u, 255u,   32u, 255u,  96u, 255u
  }
};

static const char *kTextureLabels[kResourceCount] = {
  "metal4-bindless-texture-0",
  "metal4-bindless-texture-1"
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

static BOOL
CreateTexture(GPUDevice       *device,
              GPUQueue        *queue,
              const char      *label,
              const uint8_t    pixels[16],
              GPUTexture     **outTexture,
              GPUTextureView **outView) {
  GPUTextureCreateInfo     textureInfo;
  GPUTextureViewCreateInfo viewInfo;
  GPUTextureWriteRegion    writeRegion;

  *outTexture = NULL;
  *outView    = NULL;

  memset(&textureInfo, 0, sizeof(textureInfo));
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK ||
      !*outTexture) {
    return NO;
  }

  memset(&writeRegion, 0, sizeof(writeRegion));
  writeRegion.width        = 2u;
  writeRegion.height       = 2u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 8u;
  writeRegion.rowsPerImage = 2u;
  if (GPUQueueWriteTexture(queue,
                           *outTexture,
                           &writeRegion,
                           pixels,
                           16u) != GPU_OK) {
    return NO;
  }

  memset(&viewInfo, 0, sizeof(viewInfo));
  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK &&
         *outView;
}

@interface Metal4BindlessViewController : UIViewController {
@private
  CADisplayLink       *_displayLink;
  GPUInstance         *_instance;
  GPUAdapter          *_adapter;
  GPUDevice           *_device;
  GPUQueue            *_queue;
  GPUSurface          *_surface;
  GPUSwapchain        *_swapchain;
  GPUShaderLibrary    *_library;
  GPUShaderLayout     *_shaderLayout;
  GPUBindGroupLayout  *_bindlessLayout;
  GPUPipelineLayout   *_pipelineLayout;
  GPURenderPipeline   *_pipeline;
  GPUTexture          *_textures[kResourceCount];
  GPUTextureView      *_textureViews[kResourceCount];
  GPUSampler          *_samplers[kResourceCount];
  GPUBuffer           *_selectionBuffer;
  GPUBindGroup        *_bindlessGroup;
  uint32_t             _width;
  uint32_t             _height;
}
- (void)setRenderingPaused:(BOOL)paused;
@end

@implementation Metal4BindlessViewController

- (BOOL)createDevice {
  GPUDeviceCreateInfo deviceInfo;
  GPUFeature          requiredFeature;

  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  _adapter = SelectAdapter(_instance);
  if (!_adapter ||
      !GPUIsFeatureSupported(_adapter, GPU_FEATURE_BINDLESS)) {
    NSLog(@"GPU: Metal adapter does not support bindless resources");
    return NO;
  }

  requiredFeature = GPU_FEATURE_BINDLESS;
  memset(&deviceInfo, 0, sizeof(deviceInfo));
  deviceInfo.chain.sType             = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize        = sizeof(deviceInfo);
  deviceInfo.label                   = "metal4-bindless-ios-device";
  deviceInfo.required.featureCount   = 1u;
  deviceInfo.required.pFeatures      = &requiredFeature;
  if (GPUCreateDevice(_adapter, &deviceInfo, &_device) != GPU_OK ||
      !_device || !GPUIsFeatureEnabled(_device, GPU_FEATURE_BINDLESS)) {
    NSLog(@"GPU: failed to create a Metal 4 bindless device");
    return NO;
  }

  _queue = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_queue) {
    NSLog(@"GPU: failed to get graphics queue");
    return NO;
  }
  return YES;
}

- (BOOL)createSurface {
  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)self.view,
                                        GPU_SURFACE_APPLE_UIVIEW,
                                        UIScreen.mainScreen.scale);
  if (!_surface) {
    NSLog(@"GPU: failed to create UIKit surface");
    return NO;
  }

  _width     = (uint32_t)self.view.bounds.size.width;
  _height    = (uint32_t)self.view.bounds.size.height;
  _swapchain = GPUCreateSwapchainDefault(_device,
                                         _surface,
                                         _width,
                                         _height);
  if (!_swapchain) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }
  return YES;
}

- (BOOL)createShaderLayout {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindlessLayoutEXT           bindlessInfo;
  GPUBindGroupLayoutCreateInfo   layoutInfo;
  GPUPipelineLayoutCreateInfo    pipelineLayoutInfo;
  NSURL                         *artifactURL;
  NSData                        *artifact;
  uint32_t                       entryCount;

  artifactURL = [NSBundle.mainBundle URLForResource:@"metal4_bindless"
                                      withExtension:@"us"];
  artifact = artifactURL ? [NSData dataWithContentsOfURL:artifactURL] : nil;
  if (!artifact ||
      GPUCreateShaderLibraryFromUSL(_device,
                                    artifact.bytes,
                                    (uint64_t)artifact.length,
                                    &_library) != GPU_OK ||
      GPUCreateShaderLayout(_device, _library, &_shaderLayout) != GPU_OK ||
      !_shaderLayout || _shaderLayout->bindGroupLayoutCount != 1u ||
      !_shaderLayout->bindGroupLayouts ||
      !_shaderLayout->bindGroupLayouts[0]) {
    NSLog(@"GPU: failed to load Metal 4 bindless USL reflection");
    return NO;
  }

  entries = GPUGetBindGroupLayoutEntries(_shaderLayout->bindGroupLayouts[0],
                                         &entryCount);
  if (!entries || entryCount != 3u ||
      entries[0].binding != 0u || entries[0].arrayCount != kResourceCount ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[1].binding != 2u || entries[1].arrayCount != kResourceCount ||
      entries[1].bindingType != GPU_BINDING_SAMPLER ||
      entries[2].binding != 4u || entries[2].arrayCount != 1u ||
      entries[2].bindingType != GPU_BINDING_UNIFORM_BUFFER) {
    NSLog(@"GPU: unexpected Metal 4 bindless USL layout");
    return NO;
  }

  memset(&bindlessInfo, 0, sizeof(bindlessInfo));
  bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
  bindlessInfo.chain.structSize = sizeof(bindlessInfo);
  bindlessInfo.sourceLayout     = _shaderLayout->bindGroupLayouts[0];

  memset(&layoutInfo, 0, sizeof(layoutInfo));
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.chain.pNext      = &bindlessInfo;
  layoutInfo.label            = "metal4-bindless-ios-layout";
  if (GPUCreateBindGroupLayout(_device,
                               &layoutInfo,
                               &_bindlessLayout) != GPU_OK ||
      !_bindlessLayout) {
    NSLog(@"GPU: failed to create bindless layout");
    return NO;
  }

  memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize     = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label                = "metal4-bindless-ios-pipeline-layout";
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  pipelineLayoutInfo.ppBindGroupLayouts    = &_bindlessLayout;
  if (GPUCreatePipelineLayout(_device,
                              &pipelineLayoutInfo,
                              &_pipelineLayout) != GPU_OK ||
      !_pipelineLayout) {
    NSLog(@"GPU: failed to create bindless pipeline layout");
    return NO;
  }
  return YES;
}

- (BOOL)createPipeline {
  GPUColorTargetState          colorTarget;
  GPURenderPipelineCreateInfo  pipelineInfo;

  memset(&colorTarget, 0, sizeof(colorTarget));
  colorTarget.format          = GPUGetSwapchainFormat(_swapchain);
  colorTarget.blend.enabled   = false;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;

  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  pipelineInfo.chain.sType            = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize       = sizeof(pipelineInfo);
  pipelineInfo.label                  = "metal4-bindless-ios-pipeline";
  pipelineInfo.layout                 = _pipelineLayout;
  pipelineInfo.library                = _library;
  pipelineInfo.vertexEntry            = "metal4_bindless_vs";
  pipelineInfo.fragmentEntry          = "metal4_bindless_fs";
  pipelineInfo.colorTargetCount       = 1u;
  pipelineInfo.pColorTargets          = &colorTarget;
  pipelineInfo.depthStencilFormat     = GPU_FORMAT_UNDEFINED;
  pipelineInfo.primitiveTopology      = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode               = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace              = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = 0xffffffffu;
  if (GPUCreateRenderPipeline(_device,
                              &pipelineInfo,
                              &_pipeline) != GPU_OK ||
      !_pipeline) {
    NSLog(@"GPU: failed to create Metal 4 bindless pipeline");
    return NO;
  }
  return YES;
}

- (BOOL)createResources {
  GPUSamplerCreateInfo samplerInfo;
  GPUBufferCreateInfo  bufferInfo;
  uint32_t             selection[64];

  for (uint32_t i = 0u; i < kResourceCount; i++) {
    if (!CreateTexture(_device,
                       _queue,
                       kTextureLabels[i],
                       kTexturePixels[i],
                       &_textures[i],
                       &_textureViews[i])) {
      NSLog(@"GPU: failed to create texture %u", i);
      return NO;
    }
  }

  memset(&samplerInfo, 0, sizeof(samplerInfo));
  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "metal4-bindless-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  for (uint32_t i = 0u; i < kResourceCount; i++) {
    if (GPUCreateSampler(_device,
                         &samplerInfo,
                         false,
                         &_samplers[i]) != GPU_OK ||
        !_samplers[i]) {
      NSLog(@"GPU: failed to create sampler %u", i);
      return NO;
    }
  }

  memset(selection, 0, sizeof(selection));
  selection[0] = 1u;
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "metal4-bindless-selection";
  bufferInfo.sizeBytes        = kSelectionBufferSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(_device,
                      &bufferInfo,
                      &_selectionBuffer) != GPU_OK ||
      !_selectionBuffer ||
      GPUQueueWriteBuffer(_queue,
                          _selectionBuffer,
                          0u,
                          selection,
                          sizeof(selection)) != GPU_OK) {
    NSLog(@"GPU: failed to create selection buffer");
    return NO;
  }
  return YES;
}

- (BOOL)createBindGroup {
  GPUBindGroupEntry      entries[5];
  GPUBindGroupCreateInfo groupInfo;

  memset(entries, 0, sizeof(entries));
  for (uint32_t i = 0u; i < kResourceCount; i++) {
    entries[i].binding     = 0u;
    entries[i].arrayIndex  = i;
    entries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[i].textureView = _textureViews[i];

    entries[kResourceCount + i].binding     = 2u;
    entries[kResourceCount + i].arrayIndex  = i;
    entries[kResourceCount + i].bindingType = GPU_BINDING_SAMPLER;
    entries[kResourceCount + i].sampler     = _samplers[i];
  }
  entries[4].binding       = 4u;
  entries[4].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[4].buffer.buffer = _selectionBuffer;
  entries[4].buffer.size   = kSelectionBufferSize;

  memset(&groupInfo, 0, sizeof(groupInfo));
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "metal4-bindless-ios-group";
  groupInfo.layout           = _bindlessLayout;
  if (GPUCreateBindGroup(_device,
                         &groupInfo,
                         &_bindlessGroup) != GPU_OK ||
      !_bindlessGroup ||
      GPUUpdateBindGroupEXT(_bindlessGroup,
                            5u,
                            entries) != GPU_OK) {
    NSLog(@"GPU: failed to create or update bindless group");
    return NO;
  }
  return YES;
}

- (BOOL)createGPU {
  if (![self createDevice] ||
      ![self createSurface] ||
      ![self createShaderLayout] ||
      ![self createPipeline] ||
      ![self createResources] ||
      ![self createBindGroup]) {
    return NO;
  }

  NSLog(@"GPU: Metal 4 bindless argument-table sample ready");
  return YES;
}

- (void)drawFrame {
  GPURenderPassColorAttachment color;
  GPURenderPassCreateInfo      passInfo;
  GPURenderPassEncoder        *pass;
  GPUCommandBuffer            *cmdb;
  GPUFrame                    *frame;

  frame = GPUBeginFrame(_swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(_queue,
                              "metal4-bindless-ios-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  memset(&color, 0, sizeof(color));
  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.045f;
  color.clearColor.float32[3] = 1.0f;

  memset(&passInfo, 0, sizeof(passInfo));
  passInfo.label                = "metal4-bindless-ios-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, _pipeline);
  GPUBindRenderGroup(pass, 0u, _bindlessGroup, 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(_queue, cmdb, frame) != GPU_OK) {
    NSLog(@"GPU: failed to submit Metal 4 bindless frame");
  }
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

  [super viewDidLayoutSubviews];
  width  = (uint32_t)self.view.bounds.size.width;
  height = (uint32_t)self.view.bounds.size.height;
  if (_swapchain && width > 0u && height > 0u &&
      (width != _width || height != _height)) {
    if (GPUResizeSwapchain(_swapchain, width, height) == GPU_OK) {
      _width  = width;
      _height = height;
    }
  }
}

- (void)setRenderingPaused:(BOOL)paused {
  _displayLink.paused = paused;
}

- (void)dealloc {
  [_displayLink invalidate];
  GPUDestroyBindGroup(_bindlessGroup);
  GPUDestroyRenderPipeline(_pipeline);
  GPUDestroyPipelineLayout(_pipelineLayout);
  GPUDestroyBindGroupLayout(_bindlessLayout);
  GPUDestroyBuffer(_selectionBuffer);
  for (uint32_t i = 0u; i < kResourceCount; i++) {
    GPUDestroySampler(_samplers[i]);
    GPUDestroyTextureView(_textureViews[i]);
    GPUDestroyTexture(_textures[i]);
  }
  GPUDestroyShaderLayout(_shaderLayout);
  GPUDestroyShaderLibrary(_library);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);
}

@end

@interface Metal4BindlessAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow                         *window;
@property(nonatomic, strong) Metal4BindlessViewController    *controller;
@end

@implementation Metal4BindlessAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  (void)application;
  (void)launchOptions;

  self.controller = [Metal4BindlessViewController new];
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
    setenv("GPU_METAL_MODE", "metal4", 1);
    return UIApplicationMain(argc,
                             argv,
                             nil,
                             NSStringFromClass(Metal4BindlessAppDelegate.class));
  }
}
