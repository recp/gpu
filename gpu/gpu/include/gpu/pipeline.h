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

typedef struct GPUPipeline {
  void *priv;
} GPUPipeline;

typedef struct GPURenderState {
  void *priv;
} GPURenderState;

typedef enum GPUFunctionType {
  GPU_FUNCTION_VERT = 1,
  GPU_FUNCTION_FRAG = 2
} GPUFunctionType;

GPU_EXPORT
GPUPipeline*
gpuNewPipeline(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderState*
gpuNewRenderState(GPUDevice   * __restrict device,
                  GPUPipeline * __restrict pipeline);

GPU_EXPORT
void
gpuFunction(GPUPipeline * __restrict pipline,
            GPUFunction * __restrict func,
            GPUFunctionType          functype);

GPU_EXPORT
void
gpuColorFormat(GPUPipeline * __restrict pipline,
               uint32_t                 index,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuDepthFormat(GPUPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuStencilFormat(GPUPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuSampleCount(GPUPipeline * __restrict pipline,
               uint32_t                 sampleCount);

#endif /* gpu_pipeline_h */
