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

#ifndef gpu_sample_mesh_triangle_h
#define gpu_sample_mesh_triangle_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleMeshTriangle {
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *taskBuffer;
  GPUBindGroup      *taskGroup;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} GPUSampleMeshTriangle;

GPUResult
GPUSampleMeshTriangleInit(GPUSampleMeshTriangle *state,
                          GPUDevice             *device,
                          GPUQueue              *queue,
                          GPUSwapchain          *swapchain,
                          GPUShaderLibrary      *library,
                          GPUShaderLayout       *shaderLayout,
                          uint32_t               width,
                          uint32_t               height);

GPUResult
GPUSampleMeshTriangleResize(GPUSampleMeshTriangle *state,
                            uint32_t               width,
                            uint32_t               height);

GPUResult
GPUSampleMeshTriangleRender(GPUSampleMeshTriangle       *state,
                            void                        *completionSender,
                            GPUCommandBufferCompletionFn completion);

void
GPUSampleMeshTriangleDestroy(GPUSampleMeshTriangle *state);

#endif
