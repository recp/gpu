#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include <stdint.h>

#import "../../include/gpu/gpu.h"

typedef struct TaskParams {
  uint32_t meshGroups[4];
  float    offset[4];
  float    tint[4];
} TaskParams;

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

@interface MeshTriangleViewController : UIViewController {
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
  GPUBuffer         *_taskBuffer;
  GPUBindGroup      *_taskGroup;
  uint32_t           _width;
  uint32_t           _height;
}
- (void)setRenderingPaused:(BOOL)paused;
@end

@implementation MeshTriangleViewController

- (BOOL)createDevice {
  GPUDeviceCreateInfo deviceInfo;
  GPUFeature          requiredFeature;

  if (GPUCreateInstance(NULL, &_instance) != GPU_OK || !_instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  _adapter = SelectAdapter(_instance);
  if (!_adapter ||
      !GPUIsFeatureSupported(_adapter, GPU_FEATURE_MESH_SHADER)) {
    NSLog(@"GPU: mesh shaders are not supported");
    return NO;
  }

  requiredFeature = GPU_FEATURE_MESH_SHADER;
  deviceInfo = (GPUDeviceCreateInfo){
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .structSize = sizeof(GPUDeviceCreateInfo)
    },
    .label = "mesh-triangle-ios-device",
    .required = {
      .featureCount = 1u,
      .pFeatures    = &requiredFeature
    }
  };
  if (GPUCreateDevice(_adapter, &deviceInfo, &_device) != GPU_OK ||
      !_device ||
      !GPUIsFeatureEnabled(_device, GPU_FEATURE_MESH_SHADER)) {
    NSLog(@"GPU: failed to create mesh device");
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
  NSURL                         *artifactURL;
  NSData                        *artifact;
  uint32_t                       entryCount;
  BOOL                           sawTaskUniform;

  artifactURL = [NSBundle.mainBundle URLForResource:@"mesh_triangle"
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
    NSLog(@"GPU: failed to load mesh_triangle.us or its reflection");
    return NO;
  }

  entryCount     = 0u;
  sawTaskUniform = NO;
  entries = GPUGetBindGroupLayoutEntries(_shaderLayout->bindGroupLayouts[0],
                                         &entryCount);
  for (uint32_t i = 0u; entries && i < entryCount; i++) {
    if (entries[i].binding == 0u &&
        entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
        entries[i].visibility == GPU_SHADER_STAGE_TASK_BIT) {
      sawTaskUniform = YES;
      break;
    }
  }
  if (!sawTaskUniform) {
    NSLog(@"GPU: task uniform reflection mismatch");
    return NO;
  }
  return YES;
}

- (BOOL)createPipeline {
  GPUColorTargetState         colorTarget;
  GPUMeshPipelineEXT          meshInfo;
  GPURenderPipelineCreateInfo pipelineInfo;

  colorTarget = (GPUColorTargetState){
    .format = GPUGetSwapchainFormat(_swapchain),
    .blend = {
      .enabled   = false,
      .writeMask = GPU_COLOR_WRITE_ALL
    }
  };
  meshInfo = (GPUMeshPipelineEXT){
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT,
      .structSize = sizeof(GPUMeshPipelineEXT)
    },
    .taskEntry       = "task_main",
    .meshEntry       = "mesh_main",
    .payloadSizeBytes = 0u
  };
  pipelineInfo = (GPURenderPipelineCreateInfo){
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
      .structSize = sizeof(GPURenderPipelineCreateInfo),
      .pNext      = &meshInfo.chain
    },
    .label             = "mesh-triangle-ios-pipeline",
    .layout            = _shaderLayout->pipelineLayout,
    .library           = _library,
    .fragmentEntry     = "fragment_main",
    .colorTargetCount  = 1u,
    .pColorTargets     = &colorTarget,
    .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode          = GPU_CULL_MODE_NONE,
    .frontFace         = GPU_FRONT_FACE_CCW,
    .multisample = {
      .sampleCount = 1u,
      .sampleMask  = UINT32_MAX
    }
  };
  if (GPUCreateRenderPipeline(_device, &pipelineInfo, &_pipeline) != GPU_OK ||
      !_pipeline) {
    NSLog(@"GPU: failed to create mesh pipeline");
    return NO;
  }
  return YES;
}

- (BOOL)createTaskGroup {
  const TaskParams taskParams = {
    .meshGroups = {1u, 1u, 1u, 0u},
    .offset     = {0.12f, 0.0f, 0.0f, 0.0f},
    .tint       = {1.0f, 0.75f, 0.5f, 1.0f}
  };
  GPUBufferCreateInfo    bufferInfo;
  GPUBindGroupEntry      entry;
  GPUBindGroupCreateInfo groupInfo;

  bufferInfo = (GPUBufferCreateInfo){
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .structSize = sizeof(GPUBufferCreateInfo)
    },
    .label     = "mesh-triangle-ios-task-params",
    .sizeBytes = sizeof(taskParams),
    .usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST
  };
  if (GPUCreateBuffer(_device, &bufferInfo, &_taskBuffer) != GPU_OK ||
      !_taskBuffer ||
      GPUQueueWriteBuffer(_queue,
                          _taskBuffer,
                          0u,
                          &taskParams,
                          sizeof(taskParams)) != GPU_OK) {
    NSLog(@"GPU: failed to create task uniform buffer");
    return NO;
  }

  entry = (GPUBindGroupEntry){
    .binding     = 0u,
    .bindingType = GPU_BINDING_UNIFORM_BUFFER,
    .buffer = {
      .buffer = _taskBuffer,
      .size   = sizeof(taskParams)
    }
  };
  groupInfo = (GPUBindGroupCreateInfo){
    .chain = {
      .sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
      .structSize = sizeof(GPUBindGroupCreateInfo)
    },
    .label      = "mesh-triangle-ios-task-group",
    .layout     = _shaderLayout->bindGroupLayouts[0],
    .entryCount = 1u,
    .pEntries   = &entry
  };
  if (GPUCreateBindGroup(_device, &groupInfo, &_taskGroup) != GPU_OK ||
      !_taskGroup) {
    NSLog(@"GPU: failed to create task bind group");
    return NO;
  }
  return YES;
}

- (BOOL)createGPU {
  if (![self createDevice] ||
      ![self createSurface] ||
      ![self createShaderLayout] ||
      ![self createPipeline] ||
      ![self createTaskGroup]) {
    return NO;
  }

  NSLog(@"GPU: iOS mesh/task shader sample ready");
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
                              "mesh-triangle-ios-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color = (GPURenderPassColorAttachment){
    .view    = GPUFrameGetTargetView(frame),
    .loadOp  = GPU_LOAD_OP_CLEAR,
    .storeOp = GPU_STORE_OP_STORE,
    .clearColor.float32 = {0.015f, 0.025f, 0.045f, 1.0f}
  };
  passInfo = (GPURenderPassCreateInfo){
    .label                = "mesh-triangle-ios-pass",
    .colorAttachmentCount = 1u,
    .pColorAttachments    = &color
  };
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, _pipeline);
  GPUBindRenderGroup(pass, 0u, _taskGroup, 0u, NULL);
  GPUDrawMeshEXT(pass, 1u, 1u, 1u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(_queue, cmdb, frame) != GPU_OK) {
    NSLog(@"GPU: failed to submit mesh frame");
  }
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.blackColor;

  if (![self createGPU]) {
    self.view.backgroundColor = UIColor.redColor;
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
      (width != _width || height != _height) &&
      GPUResizeSwapchain(_swapchain, width, height) == GPU_OK) {
    _width  = width;
    _height = height;
  }
}

- (void)setRenderingPaused:(BOOL)paused {
  _displayLink.paused = paused;
}

- (void)dealloc {
  [_displayLink invalidate];
  GPUDestroyBindGroup(_taskGroup);
  GPUDestroyBuffer(_taskBuffer);
  GPUDestroyRenderPipeline(_pipeline);
  GPUDestroyShaderLayout(_shaderLayout);
  GPUDestroyShaderLibrary(_library);
  GPUDestroySwapchain(_swapchain);
  GPUDestroySurface(_surface);
  GPUDestroyDevice(_device);
  GPUDestroyInstance(_instance);
}

@end

@interface MeshTriangleAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow                    *window;
@property(nonatomic, strong) MeshTriangleViewController *controller;
@end

@implementation MeshTriangleAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  (void)application;
  (void)launchOptions;

  self.controller = [MeshTriangleViewController new];
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
                             NSStringFromClass(MeshTriangleAppDelegate.class));
  }
}
