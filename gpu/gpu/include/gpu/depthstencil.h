/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef depthstencil_h
#define depthstencil_h

#include "common.h"
#include "pipeline.h"

typedef enum GPUCompareFunction {
  GPUCompareFunctionNever        = 0,
  GPUCompareFunctionLess         = 1,
  GPUCompareFunctionEqual        = 2,
  GPUCompareFunctionLessEqual    = 3,
  GPUCompareFunctionGreater      = 4,
  GPUCompareFunctionNotEqual     = 5,
  GPUCompareFunctionGreaterEqual = 6,
  GPUCompareFunctionAlways       = 7
} GPUCompareFunction;

typedef enum GPUStencilOperation {
  GPUStencilOperationKeep           = 0,
  GPUStencilOperationZero           = 1,
  GPUStencilOperationReplace        = 2,
  GPUStencilOperationIncrementClamp = 3,
  GPUStencilOperationDecrementClamp = 4,
  GPUStencilOperationInvert         = 5,
  GPUStencilOperationIncrementWrap  = 6,
  GPUStencilOperationDecrementWrap  = 7
} GPUStencilOperation;

typedef struct GPUDepthStencil {
  void *priv;
} GPUDepthStencil;

GPU_EXPORT
GPUDepthStencil*
gpuDepthStencilNew(GPUCompareFunction depthCompareFunc,
                     bool               depthWriteEnabled);

#endif /* depthstencil_h */
