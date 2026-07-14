#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include <stddef.h>
#include <string.h>

#import "../../include/gpu/gpu.h"

typedef struct TriangleVertex {
  float position[2];
} TriangleVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

static const TriangleVertex kTriangleVertices[] = {
  { {  0.0f,  0.65f } },
  { { -0.7f, -0.65f } },
  { {  0.7f, -0.65f } }
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

@interface TriangleViewController : UIViewController {
@private
  CADisplayLink     *_displayLink;
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
  GPUBuffer         *_uniformBuffer;
  GPUBindGroup      *_fragmentGroup;
  uint32_t           _width;
  uint32_t           _height;
}
- (void)setRenderingPaused:(BOOL)paused;
@end

@implementation TriangleViewController

- (BOOL)createGPU {
  NSURL  *artifactURL;
  NSData *artifact;

  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  _adapter = SelectAdapter(_instance);
  _device  = GPUCreateDeviceWithDefaultQueues(_adapter);
  _queue   = GPUGetQueue(_device, GPU_QUEUE_GRAPHICS, 0u);
  if (!_adapter || !_device || !_queue) {
    NSLog(@"GPU: failed to create the default device and queue");
    return NO;
  }

  _surface = GPUCreateSurfaceFromNative(_instance,
                                        _adapter,
                                        (__bridge void *)self.view,
                                        GPU_SURFACE_APPLE_UIVIEW,
                                        UIScreen.mainScreen.scale);
  if (!_surface) {
    NSLog(@"GPU: failed to create the UIKit surface");
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

  artifactURL = [NSBundle.mainBundle URLForResource:@"triangle"
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
      !_shaderLayout ||
      _shaderLayout->bindGroupLayoutCount != 1u ||
      !_shaderLayout->bindGroupLayouts[0]) {
    NSLog(@"GPU: failed to load triangle.us or its reflection");
    return NO;
  }

  GPUVertexAttribute attributes[] = {
    {
      .shaderLocation = 0u,
      .format         = GPU_VERTEX_FORMAT_FLOAT32X2,
      .offset         = offsetof(TriangleVertex, position)
    }
  };
  GPUVertexBufferLayout vertexLayouts[] = {
    {
      .strideBytes    = sizeof(TriangleVertex),
      .stepMode       = GPU_VERTEX_STEP_MODE_VERTEX,
      .attributeCount = 1u,
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
  GPURenderPipelineCreateInfo pipelineInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
      .structSize = sizeof(GPURenderPipelineCreateInfo)
    },
    .label              = "triangle-ios-usl-pipeline",
    .layout             = _shaderLayout->pipelineLayout,
    .library            = _library,
    .vertexEntry        = "tri_vs",
    .fragmentEntry      = "tri_fs",
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
  if (GPUCreateRenderPipeline(_device,
                              &pipelineInfo,
                              &_pipeline) != GPU_OK) {
    NSLog(@"GPU: failed to create render pipeline");
    return NO;
  }

  GPUBufferCreateInfo vertexInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .structSize = sizeof(GPUBufferCreateInfo)
    },
    .label     = "triangle-ios-usl-vertices",
    .sizeBytes = sizeof(kTriangleVertices),
    .usage     = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &vertexInfo, &_vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _vertexBuffer,
                          0u,
                          kTriangleVertices,
                          sizeof(kTriangleVertices)) != GPU_OK) {
    NSLog(@"GPU: failed to create the triangle vertex buffer");
    return NO;
  }

  GPUBufferCreateInfo uniformInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .structSize = sizeof(GPUBufferCreateInfo)
    },
    .label     = "triangle-ios-usl-uniforms",
    .sizeBytes = sizeof(FragmentUniforms),
    .usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST
  };
  FragmentUniforms uniforms = {
    .tint = {0.15f, 0.75f, 1.0f, 1.0f}
  };
  if (GPUCreateBuffer(_device, &uniformInfo, &_uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(_queue,
                          _uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    NSLog(@"GPU: failed to create the fragment uniform buffer");
    return NO;
  }

  GPUBindGroupEntry entries[] = {
    {
      .binding = 0u,
      .buffer  = {
        .buffer = _uniformBuffer,
        .offset = 0u,
        .size   = sizeof(FragmentUniforms)
      }
    }
  };
  GPUBindGroupCreateInfo groupInfo = {
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
      .structSize = sizeof(GPUBindGroupCreateInfo)
    },
    .label      = "triangle-ios-usl-group0",
    .layout     = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1u,
    .pEntries   = entries
  };
  if (GPUCreateBindGroup(_device,
                         &groupInfo,
                         &_fragmentGroup) != GPU_OK) {
    NSLog(@"GPU: failed to create fragment bind group");
    return NO;
  }

  return YES;
}

- (void)drawFrame {
  GPURenderPassColorAttachment color;
  GPURenderPassCreateInfo      passInfo;
  GPURenderPassEncoder        *pass;
  GPUBufferBinding             vertexBinding;
  GPUCommandBuffer            *cmdb;
  GPUFrame                    *frame;
  uint32_t                     dynamicOffset;

  frame = GPUBeginFrame(_swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(_queue,
                              "triangle-ios-usl-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  memset(&color, 0, sizeof(color));
  color.view                    = GPUFrameGetTargetView(frame);
  color.loadOp                  = GPU_LOAD_OP_CLEAR;
  color.storeOp                 = GPU_STORE_OP_STORE;
  color.clearColor.float32[0]   = 0.015f;
  color.clearColor.float32[1]   = 0.025f;
  color.clearColor.float32[2]   = 0.045f;
  color.clearColor.float32[3]   = 1.0f;

  memset(&passInfo, 0, sizeof(passInfo));
  passInfo.label                = "triangle-ios-usl-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  vertexBinding.buffer = _vertexBuffer;
  vertexBinding.offset = 0u;
  dynamicOffset        = 0u;
  GPUBindRenderPipeline(pass, _pipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(pass,
                     0u,
                     _fragmentGroup,
                     1u,
                     &dynamicOffset);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(_queue, cmdb, frame) != GPU_OK) {
    NSLog(@"GPU: failed to submit the frame");
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
  GPUDestroyBindGroup(_fragmentGroup);
  GPUDestroyRenderPipeline(_pipeline);
  GPUDestroyBuffer(_uniformBuffer);
  GPUDestroyBuffer(_vertexBuffer);
  GPUDestroyShaderLayout(_shaderLayout);
  GPUDestroyShaderLibrary(_library);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);
}

@end

@interface TriangleAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow                *window;
@property(nonatomic, strong) TriangleViewController *controller;
@end

@implementation TriangleAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  (void)application;
  (void)launchOptions;

  self.controller = [TriangleViewController new];
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
                             NSStringFromClass(TriangleAppDelegate.class));
  }
}
