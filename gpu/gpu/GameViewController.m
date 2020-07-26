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
  MTKView                *_view;
  Renderer               *_renderer;
     
  GPURenderer            *renderer;
  GPUDevice              *device;
  GPURenderPipeline      *pipeline;
  GPURenderPipelineState *renderState;
  GPULibrary             *library;
  GPUFunction            *vertFunc;
  GPUFunction            *fragFunc;
  GPUVertexDescriptor    *vert;
  GPUDepthStencil        *depthStencil;
  GPUDepthStencilState   *depthStencilState;
  GPUBuffer              *dynamicUniformBuffer;
  GPUCommandQueue        *commandQueue;
  GPUCommandBuffer       *cb;
  GPURenderPassDesc      *pass;
   
  matrix_float4x4         _projectionMatrix;
  float                   _rotation;
  
  id <MTLBuffer>          _dynamicUniformBuffer;
  uint32_t                _uniformBufferOffset;
  uint8_t                 _uniformBufferIndex;
  void*                   _uniformBufferAddress;
  
  GPUTexture *_colorMap;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  _view    = (MTKView *)self.view;

  _view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  _view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
  _view.sampleCount = 1;
  
  device   = gpuCreateSystemDefaultDevice();
  pipeline = gpuNewPipeline(GPUPixelFormatBGRA8Unorm_sRGB);
  library  = gpuDefaultLibrary(device);

  vertFunc = gpuNewFunction(library, "vertexShader");
  fragFunc = gpuNewFunction(library, "fragmentShader");
  vert     = gpuNewVertexDesc();

  gpuAttrib(vert, AttributePosition, GPUFloat3, 0, BufferIndexMeshPositions);
  gpuAttrib(vert, AttributeTexcoord, GPUFloat2, 0, BufferIndexMeshGenerics);

  gpuLayout(vert, BufferIndexMeshPositions, 12, 1, GPUPerVertex);
  gpuLayout(vert, BufferIndexMeshGenerics,  8,  1, GPUPerVertex);

  gpuFunction(pipeline, vertFunc, GPU_FUNCTION_VERT);
  gpuFunction(pipeline, fragFunc, GPU_FUNCTION_FRAG);
  gpuVertexDesc(pipeline, vert);

  gpuColorFormat(pipeline, 0, (GPUPixelFormat)_view.colorPixelFormat);
  gpuDepthFormat(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpuStencilFormat(pipeline, (GPUPixelFormat)_view.depthStencilPixelFormat);
  gpuSampleCount(pipeline, (uint32_t)_view.sampleCount);

  renderState          = gpuNewRenderState(device, pipeline);
  depthStencil         = gpuNewDepthStencil(GPUCompareFunctionLess, true);
  depthStencilState    = gpuNewDepthStencilState(device, depthStencil);

  dynamicUniformBuffer = gpuNewBuffer(device, uniformBufferSize, GPUResourceStorageModeShared);
  commandQueue         = gpuNewCmdQue(device);
  
//  renderer = gpu_renderer_mtkview((MTKView *)self.view);
//   _view.device = MTLCreateSystemDefaultDevice();
  
  MTKTextureLoader* textureLoader = [[MTKTextureLoader alloc] initWithDevice: device->priv];

  NSDictionary *textureLoaderOptions =
   @{
    MTKTextureLoaderOptionTextureUsage       : @(MTLTextureUsageShaderRead),
    MTKTextureLoaderOptionTextureStorageMode : @(MTLStorageModePrivate)
    };

  NSError *error;
  _colorMap = [textureLoader newTextureWithName: @"ColorMap"
                                    scaleFactor: 1.0
                                         bundle: nil
                                        options: textureLoaderOptions
                                          error: &error];
  
  _view.device = device->priv;
  if(!_view.device) {
    NSLog(@"Metal is not supported on this device");
    self.view = [[NSView alloc] initWithFrame:self.view.frame];
    return;
  }
  
  _renderer = [[Renderer alloc] initWithMetalKitView:_view];
  [_renderer mtkView:_view drawableSizeWillChange:_view.bounds.size];
  [self mtkView:_view drawableSizeWillChange:_view.bounds.size];
   _view.delegate = _renderer;
//  _view.delegate = self;
}

- (void)_updateDynamicBufferState {
  /// Update the state of our uniform buffers before rendering
  
  _uniformBufferIndex   = (_uniformBufferIndex + 1) % kMaxBuffersInFlight;
  _uniformBufferOffset  = kAlignedUniformsSize * _uniformBufferIndex;
  _uniformBufferAddress = ((uint8_t*)gpuBufferContents(dynamicUniformBuffer)) + _uniformBufferOffset;
}

- (void)_updateGameState {
  /// Update any game state before encoding renderint commands to our drawable
  Uniforms * uniforms = (Uniforms*)_uniformBufferAddress;
  
  uniforms->projectionMatrix = _projectionMatrix;
  
  mat4 rot;
  
  glm_translate_make(rot, (vec3){0.0, 0.0, -8.0});
  glm_rotate(rot, _rotation, (vec3){1, 1, 0});

//  glm_rotate_atm(rot, (vec3){0.0, 0.0, -8.0}, _rotation, (vec3){1, 1, 0});
  uniforms->modelViewMatrix = glm_mat4_applesimd(rot);

//    vector_float3 rotationAxis = {1, 1, 0};
//    matrix_float4x4 modelMatrix = matrix4x4_rotation(_rotation, rotationAxis);
//    matrix_float4x4 viewMatrix = matrix4x4_translation(0.0, 0.0, -8.0);
//
//    uniforms->modelViewMatrix = matrix_multiply(viewMatrix, modelMatrix);
    _rotation += .01;
}

- (void)drawInMTKView:(nonnull MTKView *)view {
  cb = gpuNewCmdBuf(commandQueue,  NULL, cmdOnComplete);
  
  [self _updateDynamicBufferState];
  [self _updateGameState];
  
  if ((pass = gpuPassFromMTKView(view))) {
    GPURenderCommandEncoder *rce;

    rce = gpuRenderCommandEncoder(cb, pass);

    gpuFrontFace(rce, GPUWindingCounterClockwise);
    gpuCullMode(rce, GPUCullModeBack);
    gpuSetRenderPipelineState(rce, renderState);
    gpuSetDepthStencil(rce, depthStencilState);

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
    
    gpuRCESetTexture(rce, _colorMap, TextureIndexColor);

//    [renderEncoder setFragmentTexture:_colorMap
//                              atIndex:TextureIndexColor];

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
  
  _projectionMatrix = glm_mat4_applesimd(proj);
}

@end
