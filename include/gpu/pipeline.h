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

#include "common.h"
#include "pixelformat.h"
#include "library.h"

typedef struct GPURenderPipeline {
  void *priv;
} GPURenderPipeline;

typedef struct GPURenderPipelineState {
  void *priv;
} GPURenderPipelineState;

typedef enum GPUFunctionType {
  GPU_FUNCTION_VERT = 1,
  GPU_FUNCTION_FRAG = 2
} GPUFunctionType;

GPU_EXPORT
GPURenderPipeline*
gpuNewPipeline(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderPipelineState*
gpuNewRenderState(GPUDevice   * __restrict device,
                  GPURenderPipeline * __restrict pipeline);

GPU_EXPORT
void
gpuFunction(GPURenderPipeline * __restrict pipline,
            GPUFunction       * __restrict func,
            GPUFunctionType                functype);

GPU_EXPORT
void
gpuColorFormat(GPURenderPipeline * __restrict pipline,
               uint32_t                       index,
               GPUPixelFormat                 pixelFormat);

GPU_EXPORT
void
gpuDepthFormat(GPURenderPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuStencilFormat(GPURenderPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuSampleCount(GPURenderPipeline * __restrict pipline,
               uint32_t                 sampleCount);

#endif /* gpu_pipeline_h */
