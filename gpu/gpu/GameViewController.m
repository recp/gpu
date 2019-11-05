//
//  GameViewController.m
//  gpu
//
//  Created by Recep Aslantas on 3/16/19.
//  Copyright © 2019 Recep Aslantas. All rights reserved.
//

#import "GameViewController.h"
#import "Renderer.h"
#include "include/gpu/gpu.h"

@implementation GameViewController
{
  MTKView  *_view;
  Renderer *_renderer;
  
  GPURenderer    *renderer;
  GPUDevice      *device;
  GPUPipeline    *pipeline;
  GPURenderState *renderState;
  GPULibrary     *library;
  GPUFunction    *vertFunc;
  GPUFunction    *fragFunc;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  
  device      = gpu_device_create();
  pipeline    = gpu_pipeline_create(GPUPixelFormatBGRA8Unorm_sRGB);
  library     = gpu_library_default(device);

  vertFunc    = gpu_func_create(library, "vertexShader");
  fragFunc    = gpu_func_create(library, "fragmentShader");

  gpu_func_set(pipeline, vertFunc, GPU_FUNC_VERT);
  gpu_func_set(pipeline, fragFunc, GPU_FUNC_FRAG);

  renderState = gpu_renderstate_create(device, pipeline);

//  renderer = gpu_renderer_mtkview((MTKView *)self.view);
  
  _view = (MTKView *)self.view;
//   _view.device = MTLCreateSystemDefaultDevice();
  
  _view.device = device->priv;
  if(!_view.device)
  {
    NSLog(@"Metal is not supported on this device");
    self.view = [[NSView alloc] initWithFrame:self.view.frame];
    return;
  }
  
  _renderer = [[Renderer alloc] initWithMetalKitView:_view];
  [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
  _view.delegate = _renderer;
}

@end
