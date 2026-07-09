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

#ifndef gpu_render_pipeline_internal_h
#define gpu_render_pipeline_internal_h

#include "../../common.h"

struct GPURenderPipelineState {
  void *_priv;
};

struct GPURenderPipeline {
  void                 *_priv;
  void                 *_state;
  GPUPipelineLayout    *_layout;
  uint32_t              _colorTargetCount;
  GPUFormat             _colorTargetFormats[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUFormat             _depthStencilFormat;
  uint32_t              _sampleCount;
  GPUPrimitiveTopology  _primitiveTopology;
  GPUCullMode           _cullMode;
  GPUFrontFace          _frontFace;
  uint32_t              _pushConstantSizeBytes;
  GPUShaderStageFlags   _pushConstantStages;
};

GPU_HIDE
GPURenderPipeline*
gpuCreateRenderPipelineDesc(GPUPixelFormat pixelFormat);

GPU_HIDE
GPURenderPipelineState*
gpuCompileRenderPipelineState(GPUDevice * __restrict device,
                              GPURenderPipeline * __restrict pipeline);

GPU_HIDE
void
gpuPipelineSetFunction(GPURenderPipeline * __restrict pipeline,
                       GPUFunction       * __restrict func,
                       GPUFunctionType                functionType);

GPU_HIDE
void
gpuPipelineSetColorFormat(GPURenderPipeline * __restrict pipeline,
                          uint32_t                       index,
                          GPUPixelFormat                 pixelFormat);

GPU_HIDE
void
gpuPipelineSetDepthFormat(GPURenderPipeline * __restrict pipeline,
                          GPUPixelFormat                 pixelFormat);

GPU_HIDE
void
gpuPipelineSetStencilFormat(GPURenderPipeline * __restrict pipeline,
                            GPUPixelFormat                 pixelFormat);

GPU_HIDE
void
gpuPipelineSetSampleCount(GPURenderPipeline * __restrict pipeline,
                          uint32_t                       sampleCount);

#endif /* gpu_render_pipeline_internal_h */
