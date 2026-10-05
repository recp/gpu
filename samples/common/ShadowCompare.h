/*
 * Copyright (C) 2026 Recep Aslantas
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

#ifndef gpu_sample_shadow_compare_h
#define gpu_sample_shadow_compare_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleShadowCompare {
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *depthPipeline;
  GPURenderPipeline *previewPipeline;
  GPURenderPipeline *previewCoolPipeline;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUBindGroup      *shadowGroup;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} GPUSampleShadowCompare;

GPUResult
GPUSampleShadowCompareInit(GPUSampleShadowCompare *state,
                           GPUDevice              *device,
                           GPUQueue               *queue,
                           GPUSwapchain           *swapchain,
                           GPUShaderLibrary       *library,
                           GPUShaderLayout        *shaderLayout,
                           uint32_t                width,
                           uint32_t                height);

GPUResult
GPUSampleShadowCompareResize(GPUSampleShadowCompare *state,
                             uint32_t                width,
                             uint32_t                height);

GPUResult
GPUSampleShadowCompareRender(GPUSampleShadowCompare      *state,
                             void                        *completionSender,
                             GPUCommandBufferCompletionFn completion);

void
GPUSampleShadowCompareDestroy(GPUSampleShadowCompare *state);

#endif
