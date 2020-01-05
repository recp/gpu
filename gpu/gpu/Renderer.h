//
//  Renderer.h
//  gpu
//
//  Created by Recep Aslantas on 3/16/19.
//  Copyright © 2019 Recep Aslantas. All rights reserved.
//

#import <MetalKit/MetalKit.h>
#import <ModelIO/ModelIO.h>
#include <cglm/cglm.h>
#include <cglm/applesimd.h>

// Our platform independent renderer class.   Implements the MTKViewDelegate protocol which
//   allows it to accept per-frame update and drawable resize callbacks.
@interface Renderer : NSObject <MTKViewDelegate>

-(nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view;
- (void) setProj: (matrix_float4x4) proj;

@property (nonatomic, strong) MTKMesh *mesh;

@end

