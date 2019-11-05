/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/gpu.h"
#include <stdio.h>
#include <cmt/cmt.h>
#import <MetalKit/MetalKit.h>

//GPURenderer*
//gpu_renderer_mtkview(struct MTKView *mtkview) {
//  mtkview.device = gpu_device_create()->priv;
//  
////  NSError *error = NULL;
////
////  _device = mtkView.device;
////
////  // Load all the shader files with a .metal file extension in the project
////  id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];
////  
////  // Load the vertex function from the library
////  id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
////
////  // Load the fragment function from the library
////  id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentShader"];
////  MTLFeatureSet
////  // Configure a pipeline descriptor that is used to create a pipeline state
////  MTLRenderPipelineDescriptor *pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
////  pipelineStateDescriptor.label = @"Simple Pipeline";
////  pipelineStateDescriptor.vertexFunction = vertexFunction;
////  pipelineStateDescriptor.fragmentFunction = fragmentFunction;
////  pipelineStateDescriptor.colorAttachments[0].pixelFormat = mtkView.colorPixelFormat;
////
////  _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor
////                                                           error:&error];
////  if (!_pipelineState)
////  {
////    // Pipeline State creation could fail if we haven't properly set up our pipeline descriptor.
////    //  If the Metal API validation is enabled, we can find out more information about what
////    //  went wrong.  (Metal API validation is enabled by default when a debug build is run
////    //  from Xcode)
////    NSLog(@"Failed to created pipeline state, error %@", error);
////    return nil;
////  }
////
////  // Create the command queue
////  _commandQueue = [_device newCommandQueue];
//  return NULL;
//}
