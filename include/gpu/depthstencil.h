/*
 * Copyright (C) 2020 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef gpu_depthstencil_h
#define gpu_depthstencil_h
#ifdef __cplusplus
extern "C" {
#endif

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

typedef enum GPUDepthStencilFormat {
  GPU_DEPTH_STENCIL_NONE,
  GPU_DEPTH32FLOAT_STENCIL8,
  // ... other formats
} GPUDepthStencilFormat;

typedef struct GPUDepthStencil {
  void *priv;
} GPUDepthStencil;

typedef struct GPUDepthStencilState {
  void *priv;
} GPUDepthStencilState;

GPU_EXPORT
GPUDepthStencil*
GPUNewDepthStencil(GPUCompareFunction depthCompareFunc,
                   bool               depthWriteEnabled);

GPU_EXPORT
GPUDepthStencilState*
GPUNewDepthStencilState(GPUDevice       * __restrict device,
                        GPUDepthStencil * __restrict depthStencil);

#ifdef __cplusplus
}
#endif
#endif /* gpu_depthstencil_h */
