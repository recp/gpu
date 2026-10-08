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

#ifndef gpu_api_renderpipeline_h
#define gpu_api_renderpipeline_h
#ifdef __cplusplus
extern "C" {
#endif

#include <gpu/common.h>
#include <gpu/gpu.h>
#include "library.h"

typedef struct RenderPipelineState    RenderPipelineState;

typedef enum FunctionType {
  GPU_FUNCTION_VERT = 1,
  GPU_FUNCTION_FRAG = 2,
  GPU_FUNCTION_TASK = 3,
  GPU_FUNCTION_MESH = 4
} FunctionType;

typedef struct ApiRender {
  GPUResult
  (*createPipeline)(GPUDevice                         *__restrict device,
                    const GPURenderPipelineCreateInfo *__restrict info,
                    uint32_t                                      requiredBindGroupMask,
                    GPURenderPipeline                 *__restrict pipeline);

  GPURenderPipeline * (*newRenderPipeline)(GPUFormat pixelFormat, bool mesh);

  RenderPipelineState    * (*newRenderState)(GPUDevice *__restrict device, GPURenderPipeline *__restrict pipeline);

  void (*destroyRenderPipeline)(GPURenderPipeline *pipeline);

  void
  (*setFunction)(GPURenderPipeline *__restrict pipline,
                 ShaderFunction    *__restrict func,
                 FunctionType                  functype);

  GPUResult
  (*setIntersectionFunctions)(GPURenderPipeline         *pipeline,
                              ShaderFunction     *const *functions,
                              const GPUShaderStageFlags *stages,
                              uint32_t                   functionCount);

  void (*colorFormat)(GPURenderPipeline *__restrict pipline, uint32_t index, GPUFormat pixelFormat);

  void (*depthFormat)(GPURenderPipeline *__restrict pipline, GPUFormat pixelFormat);

  void (*stencilFormat)(GPURenderPipeline *__restrict pipline, GPUFormat pixelFormat);

  void (*sampleCount)(GPURenderPipeline *__restrict pipline, uint32_t sampleCount);
} ApiRender;

#ifdef __cplusplus
}
#endif
#endif /* gpu_api_renderpipeline_h */
