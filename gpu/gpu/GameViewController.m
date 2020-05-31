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

static const NSUInteger kMaxBuffersInFlight = 3;
static const size_t kAlignedUniformsSize = (sizeof(Uniforms) & ~0xFF) + 0x100;
static const NSUInteger uniformBufferSize = kAlignedUniformsSize * kMaxBuffersInFlight;

@interface GameViewController()  <MTKViewDelegate>

@end


void
cmdOnComplete(void *sender, GPUCommandBuffer *cmdb) {
  
}

@implementation GameViewController
{
  MTKView             *_view;
  Renderer            *_renderer;
  
  GPURenderer         *renderer;
  GPUDevice           *device;
  GPUPipeline         *pipeline;
  GPURenderState      *renderState;
  GPULibrary          *library;
  GPUFunction         *vertFunc;
  GPUFunction         *fragFunc;
  GPUVertexDescriptor *vert;
  GPUDepthStencil     *depthStencil;
  GPUBuffer           *dynamicUniformBuffer;
  GPUCommandQueue     *commandQueue;
  GPUCommandBuffer    *cb;
  GPURenderPassDesc   *pass;

  uint32_t             _uniformBufferOffset;
  uint8_t              _uniformBufferIndex;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _view    = (MTKView *)self.view;

  device   = gpuDeviceNew();
  pipeline = gpuPipelineNew(GPUPixelFormatBGRA8Unorm_sRGB);
  library  = gpuDefaultLibrary(device);

  vertFunc = gpuFunctionNew(library, "vertexShader");
  fragFunc = gpuFunctionNew(library, "fragmentShader");
  vert     = gpuVertexDescNew();
  
  gpuAttrib(vert, VertexAttributePosition, GPUFloat3, 0, BufferIndexMeshPositions);
  gpuAttrib(vert, VertexAttributeTexcoord, GPUFloat2, 0, BufferIndexMeshGenerics);
  
  gpuLayout(vert, BufferIndexMeshPositions, 12, 1, GPUPerVertex);
  gpuLayout(vert, BufferIndexMeshGenerics,  8,  1, GPUPerVertex);

  gpuFunction(pipeline, vertFunc, GPU_FUNCTION_VERT);
  gpuFunction(pipeline, fragFunc, GPU_FUNCTION_FRAG);
  gpuVertexDesc(pipeline, vert);

  gpuColorFormat(pipeline, 0, (GPUPixelFormat)_view.colorPixelFormat);
  gpuDepthFormat(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpuStencilFormat(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpuSampleCount(pipeline, (uint32_t)_view.sampleCount);

  renderState          = gpuRenderStateNew(device, pipeline);
  depthStencil         = gpuDepthStencilNew(GPUCompareFunctionLess, true);

  dynamicUniformBuffer = gpuBufferNew(device, uniformBufferSize, GPUResourceStorageModeShared);
  commandQueue         = gpuCmdQueNew(device);

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
  _view.delegate = _renderer;
}

- (void)drawInMTKView:(nonnull MTKView *)view {
  cb = gpuCmdBufNew(commandQueue,  NULL, cmdOnComplete);
  
  if ((pass = gpuPassFromMTKView(view))) {
    GPURenderCommandEncoder *rce;

    rce = gpuRenderCommandEncoder(cb, pass);

    gpuFrontFace(rce, GPUWindingCounterClockwise);
    gpuCullMode(rce, GPUCullModeBack);
    gpuSetRenderPipeline(rce, pipeline);
    gpuSetDepthStencil(rce, depthStencil);
    
    gpuVertexBuffer(rce, dynamicUniformBuffer, _uniformBufferOffset, BufferIndexUniforms);
    gpuFragmentBuffer(rce, dynamicUniformBuffer, _uniformBufferOffset, BufferIndexUniforms);
    
    
    for (NSUInteger bufferIndex = 0; bufferIndex < _renderer.mesh.vertexBuffers.count; bufferIndex++) {
      MTKMeshBuffer *vertexBuffer = _renderer.mesh.vertexBuffers[bufferIndex];
      if ((NSNull*)vertexBuffer != [NSNull null]) {
        gpuVertexBuffer(rce,
                        (GPUBuffer *)vertexBuffer.buffer,
                        vertexBuffer.offset,
                        (uint32_t)bufferIndex);
      }
    }
    
    //        [renderEncoder setFragmentTexture:_colorMap
    //                                       atIndex:TextureIndexColor];

    for(MTKSubmesh *submesh in _renderer.mesh.submeshes) {
      gpuDrawIndexedPrims(rce,
                          (GPUPrimitiveType)submesh.primitiveType,
                          (uint32_t)submesh.indexCount,
                          (GPUIndexType)submesh.indexType,
                          (GPUBuffer *)submesh.indexBuffer.buffer,
                          (uint32_t)submesh.indexBuffer.offset);
    }

    gpuEndEncoding(rce);
    gpuPresent(cb, view.currentDrawable);
  }

  gpuCommit(cb);
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
// [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
  
  float aspect = size.width / (float)size.height;
  
  mat4 proj;
  
  glm_perspective(glm_rad(65.0f), aspect, 0.1f, 100.0f, proj);
  
  [_renderer setProj: glm_mat4_applesimd(proj)];
}

@end
