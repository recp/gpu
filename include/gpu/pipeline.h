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

#ifndef gpu_pipeline_h
#define gpu_pipeline_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "pixelformat.h"
#include "library.h"
#include "cmdqueue.h"
#include "vertex.h"
#include "depthstencil.h"

typedef struct GPUPipelineLayout GPUPipelineLayout;
typedef struct GPUPipelineCache GPUPipelineCache;
typedef struct GPUDevice GPUDevice;

typedef enum GPUFunctionType {
  GPU_FUNCTION_VERT = 1,
  GPU_FUNCTION_FRAG = 2
} GPUFunctionType;

typedef enum GPUPrimitiveTopology {
  GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,
  GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 1,
  GPU_PRIMITIVE_TOPOLOGY_LINE_LIST = 2,
  GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP = 3,
  GPU_PRIMITIVE_TOPOLOGY_POINT_LIST = 4
} GPUPrimitiveTopology;

typedef enum GPUCullMode {
  GPU_CULL_MODE_NONE = 0,
  GPU_CULL_MODE_FRONT = 1,
  GPU_CULL_MODE_BACK = 2
} GPUCullMode;

typedef enum GPUFrontFace {
  GPU_FRONT_FACE_CCW = 0,
  GPU_FRONT_FACE_CW = 1
} GPUFrontFace;

typedef GPUFrontFace GPUWinding;

#define GPUCullModeNone                 GPU_CULL_MODE_NONE
#define GPUCullModeFront                GPU_CULL_MODE_FRONT
#define GPUCullModeBack                 GPU_CULL_MODE_BACK
#define GPUWindingCounterClockwise      GPU_FRONT_FACE_CCW
#define GPUWindingClockwise             GPU_FRONT_FACE_CW

typedef enum GPUBlendFactor {
  GPU_BLEND_FACTOR_ZERO = 0,
  GPU_BLEND_FACTOR_ONE = 1,
  GPU_BLEND_FACTOR_SRC_ALPHA = 2,
  GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 3
} GPUBlendFactor;

typedef enum GPUBlendOp {
  GPU_BLEND_OP_ADD = 0,
  GPU_BLEND_OP_SUBTRACT = 1,
  GPU_BLEND_OP_REVERSE_SUBTRACT = 2,
  GPU_BLEND_OP_MIN = 3,
  GPU_BLEND_OP_MAX = 4
} GPUBlendOp;

typedef uint32_t GPUColorWriteMaskFlags;
enum {
  GPU_COLOR_WRITE_R = 1u << 0,
  GPU_COLOR_WRITE_G = 1u << 1,
  GPU_COLOR_WRITE_B = 1u << 2,
  GPU_COLOR_WRITE_A = 1u << 3,
  GPU_COLOR_WRITE_ALL = GPU_COLOR_WRITE_R | GPU_COLOR_WRITE_G |
                        GPU_COLOR_WRITE_B | GPU_COLOR_WRITE_A
};

typedef struct GPUBlendComponent {
  GPUBlendFactor srcFactor;
  GPUBlendFactor dstFactor;
  GPUBlendOp     op;
} GPUBlendComponent;

typedef struct GPUBlendState {
  bool                   enabled;
  GPUBlendComponent      color;
  GPUBlendComponent      alpha;
  GPUColorWriteMaskFlags writeMask;
} GPUBlendState;

typedef struct GPUColorTargetState {
  GPUFormat     format;
  GPUBlendState blend;
} GPUColorTargetState;

typedef struct GPUMultisampleState {
  uint32_t sampleCount;
  uint32_t sampleMask;
  bool     alphaToCoverageEnable;
} GPUMultisampleState;

typedef struct GPURenderPipeline {
  void                 *_priv;
  void                 *_state;
  GPUPrimitiveTopology  _primitiveTopology;
  GPUCullMode           _cullMode;
  GPUFrontFace          _frontFace;
} GPURenderPipeline;

typedef struct GPURenderPipelineState {
  void *_priv;
} GPURenderPipelineState;

typedef struct GPURenderPipelineCreateInfo {
  GPUChainedStruct             chain;
  const char                  *label;
  GPUPipelineLayout           *layout;
  GPUPipelineCache            *cache;
  GPUShaderLibrary            *library;
  const char                  *vertexEntry;
  const char                  *fragmentEntry;
  GPUVertexState               vertex;
  uint32_t                     colorTargetCount;
  const GPUColorTargetState   *pColorTargets;
  GPUFormat                    depthStencilFormat;
  const GPUDepthStencilState  *pDepthStencilState;
  GPUPrimitiveTopology         primitiveTopology;
  GPUCullMode                  cullMode;
  GPUFrontFace                 frontFace;
  GPUMultisampleState          multisample;
} GPURenderPipelineCreateInfo;

GPU_EXPORT
GPUResult
GPUCreateRenderPipeline(GPUDevice                          * __restrict device,
                        const GPURenderPipelineCreateInfo  * __restrict info,
                        GPURenderPipeline                 ** __restrict outPipeline);

GPU_EXPORT
void
GPUDestroyRenderPipeline(GPURenderPipeline *pipeline);

GPU_EXPORT
GPURenderPipeline*
GPUNewRenderPipeline(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderPipelineState*
GPUNewRenderState(GPUDevice   * __restrict device,
                  GPURenderPipeline * __restrict pipeline);

GPU_EXPORT
void
GPUSetFunction(GPURenderPipeline * __restrict pipline,
               GPUFunction       * __restrict func,
               GPUFunctionType                functype);

GPU_EXPORT
void
GPUColorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUPixelFormat                 pixelFormat);

GPU_EXPORT
void
GPUDepthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
GPUStencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
GPUSampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount);

#ifdef __cplusplus
}
#endif
#endif /* gpu_pipeline_h */
