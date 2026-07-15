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

typedef enum GPUCompareOp {
  GPU_COMPARE_NEVER         = 0,
  GPU_COMPARE_LESS          = 1,
  GPU_COMPARE_EQUAL         = 2,
  GPU_COMPARE_LESS_EQUAL    = 3,
  GPU_COMPARE_GREATER       = 4,
  GPU_COMPARE_NOT_EQUAL     = 5,
  GPU_COMPARE_GREATER_EQUAL = 6,
  GPU_COMPARE_ALWAYS        = 7
} GPUCompareOp;

typedef enum GPUStencilOp {
  GPU_STENCIL_OP_KEEP            = 0,
  GPU_STENCIL_OP_ZERO            = 1,
  GPU_STENCIL_OP_REPLACE         = 2,
  GPU_STENCIL_OP_INCREMENT_CLAMP = 3,
  GPU_STENCIL_OP_DECREMENT_CLAMP = 4,
  GPU_STENCIL_OP_INVERT          = 5,
  GPU_STENCIL_OP_INCREMENT_WRAP  = 6,
  GPU_STENCIL_OP_DECREMENT_WRAP  = 7
} GPUStencilOp;

typedef struct GPUStencilFaceState {
  GPUCompareOp compare;
  GPUStencilOp failOp;
  GPUStencilOp depthFailOp;
  GPUStencilOp passOp;
} GPUStencilFaceState;

typedef struct GPUDepthStencilState {
  GPUCompareOp        depthCompare;
  GPUStencilFaceState front;
  GPUStencilFaceState back;
  uint32_t            stencilReadMask;
  uint32_t            stencilWriteMask;
  bool                depthTestEnable;
  bool                depthWriteEnable;
  bool                stencilTestEnable;
} GPUDepthStencilState;

#ifdef __cplusplus
}
#endif
#endif /* gpu_depthstencil_h */
