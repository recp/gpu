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

#ifndef gpu_sample_vrs_compare_h
#define gpu_sample_vrs_compare_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleVRSCompare {
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *finePipeline;
  GPURenderPipeline *coarsePipeline;
  GPUBuffer         *vertexBuffer;
  GPUTexture        *rateTexture;
  GPUTextureView    *rateView;
  GPUExtent2D        attachmentTexelSize;
  GPUVRSModeFlagsEXT mode;
  GPUShadingRateEXT  coarseRate;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} GPUSampleVRSCompare;

GPUResult
GPUSampleChooseVRSRate(const GPUAdapter  *adapter,
                       GPUShadingRateEXT *outRate);

GPUResult
GPUSampleChooseVRSAttachment(const GPUAdapter  *adapter,
                             GPUShadingRateEXT *outRate,
                             GPUExtent2D       *outTexelSize);

GPUResult
GPUSampleVRSCompareInit(GPUSampleVRSCompare *state,
                        GPUDevice           *device,
                        GPUQueue            *queue,
                        GPUSwapchain        *swapchain,
                        GPUShaderLibrary    *library,
                        GPUShaderLayout     *shaderLayout,
                        GPUVRSModeFlagsEXT   mode,
                        GPUShadingRateEXT    coarseRate,
                        GPUExtent2D          attachmentTexelSize,
                        uint32_t             width,
                        uint32_t             height);

GPUResult
GPUSampleVRSCompareResize(GPUSampleVRSCompare *state,
                          uint32_t             width,
                          uint32_t             height);

GPUResult
GPUSampleVRSCompareRender(GPUSampleVRSCompare         *state,
                          void                        *completionSender,
                          GPUCommandBufferCompletionFn completion);

void
GPUSampleVRSCompareDestroy(GPUSampleVRSCompare *state);

#endif
