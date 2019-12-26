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
#include <cglm/cglm.h>

// Include header shared between C code here, which executes Metal API commands, and .metal files
#import "ShaderTypes.h"

@interface GameViewController()  <MTKViewDelegate>

@end


@implementation GameViewController
{
  MTKView  *_view;
  Renderer *_renderer;
  
  GPURenderer         *renderer;
  GPUDevice           *device;
  GPUPipeline         *pipeline;
  GPURenderState      *renderState;
  GPULibrary          *library;
  GPUFunction         *vertFunc;
  GPUFunction         *fragFunc;
  GPUVertexDescriptor *vert;
  GPUDepthStencil     *depthStencil;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _view    = (MTKView *)self.view;

  device   = gpu_device_new();
  pipeline = gpu_pipeline_new(GPUPixelFormatBGRA8Unorm_sRGB);
  library  = gpu_library_default(device);

  vertFunc = gpu_function_new(library, "vertexShader");
  fragFunc = gpu_function_new(library, "fragmentShader");
  vert     = gpu_vertex_new();

  gpu_attrib(vert, VertexAttributePosition, GPUFloat3, 0, BufferIndexMeshPositions);
  gpu_attrib(vert, VertexAttributeTexcoord, GPUFloat2, 0, BufferIndexMeshGenerics);
  
  gpu_layout(vert, BufferIndexMeshPositions, 12, 1, GPUPerVertex);
  gpu_layout(vert, BufferIndexMeshGenerics,  8,  1, GPUPerVertex);
  
  gpu_function(pipeline, vertFunc, GPU_FUNC_VERT);
  gpu_function(pipeline, fragFunc, GPU_FUNC_FRAG);
  gpu_vertex(pipeline, vert);
  
  gpu_colorfm(pipeline, 0, (GPUPixelFormat)_view.colorPixelFormat);
  gpu_depthfm(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpu_stencfm(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpu_samplco(pipeline, (uint32_t)_view.sampleCount);

  renderState = gpu_renderstate_new(device, pipeline);
  
  depthStencil = gpu_depthstencil_new(GPUCompareFunctionLess, true);
  
  gpu_pass_begin();

  gpu_pass_end();
  
//  gpu_pass_begin();
//
//  gpu_pass_end();
  gpu_commit();
  

//  renderer = gpu_renderer_mtkview((MTKView *)self.view);
//   _view.device = MTLCreateSystemDefaultDevice();
  
  _view.device = device->priv;
  if(!_view.device) {
    NSLog(@"Metal is not supported on this device");
    self.view = [[NSView alloc] initWithFrame:self.view.frame];
    return;
  }
  
  _renderer = [[Renderer alloc] initWithMetalKitView:_view];
  [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
  // _view.delegate = _renderer;
  _view.delegate = self;
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    [_renderer drawInMTKView:view];
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
// [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
  
  float aspect = size.width / (float)size.height;
  
  mat4 proj;
  
  glm_perspective(glm_rad(65.0f), aspect, 0.1f, 100.0f, proj);
  
  [_renderer setProj: glm_mat4_applesimd(proj)];
}

@end
